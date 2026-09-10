#include "app_ota.hpp"

#include <cstring>

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "app_fs.hpp"

namespace {

OtaType s_type = OtaType::Firmware;
bool s_active = false;
bool s_error = false;

// firmware
esp_ota_handle_t s_ota_handle = 0;
const esp_partition_t *s_ota_partition = nullptr;

// filesystem
const esp_partition_t *s_fs_partition = nullptr;
size_t s_fs_offset = 0;
uint8_t s_fs_buf[4096];
size_t s_fs_buf_len = 0;

bool fs_flush_buffer(bool final)
{
    if(s_fs_buf_len == 0) return true;

    size_t write_len = s_fs_buf_len;
    if(final)
    {
        // pad up to a 4-byte boundary for the flash write
        while(write_len % 4 != 0 && write_len < sizeof(s_fs_buf))
        {
            s_fs_buf[write_len++] = 0xFF;
        }
    }

    if(esp_partition_write(s_fs_partition, s_fs_offset, s_fs_buf, write_len) != ESP_OK)
    {
        s_error = true;
        return false;
    }

    s_fs_offset += write_len;
    s_fs_buf_len = 0;
    return true;
}

} // namespace


bool ota_begin(OtaType type)
{
    s_type = type;
    s_error = false;
    s_active = false;
    s_fs_offset = 0;
    s_fs_buf_len = 0;

    if(type == OtaType::Firmware)
    {
        s_ota_partition = esp_ota_get_next_update_partition(nullptr);
        if(!s_ota_partition)
        {
            s_error = true;
            return false;
        }
        if(esp_ota_begin(s_ota_partition, OTA_SIZE_UNKNOWN, &s_ota_handle) != ESP_OK)
        {
            s_error = true;
            return false;
        }
    }
    else
    {
        s_fs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                 ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        if(!s_fs_partition)
        {
            s_error = true;
            return false;
        }
        fs_unmount();
        if(esp_partition_erase_range(s_fs_partition, 0, s_fs_partition->size) != ESP_OK)
        {
            s_error = true;
            return false;
        }
    }

    s_active = true;
    return true;
}


bool ota_write(const uint8_t *data, size_t len)
{
    if(!s_active || s_error) return false;

    if(s_type == OtaType::Firmware)
    {
        if(esp_ota_write(s_ota_handle, data, len) != ESP_OK)
        {
            s_error = true;
            return false;
        }
        return true;
    }

    // filesystem: accumulate into the block buffer
    size_t consumed = 0;
    while(consumed < len)
    {
        size_t space = sizeof(s_fs_buf) - s_fs_buf_len;
        size_t chunk = len - consumed;
        if(chunk > space) chunk = space;

        memcpy(s_fs_buf + s_fs_buf_len, data + consumed, chunk);
        s_fs_buf_len += chunk;
        consumed += chunk;

        if(s_fs_buf_len == sizeof(s_fs_buf))
        {
            if(!fs_flush_buffer(false)) return false;
        }
    }
    return true;
}


bool ota_end()
{
    if(!s_active) return false;
    s_active = false;

    if(s_type == OtaType::Firmware)
    {
        if(s_error)
        {
            esp_ota_abort(s_ota_handle);
            return false;
        }
        if(esp_ota_end(s_ota_handle) != ESP_OK)
        {
            s_error = true;
            return false;
        }
        if(esp_ota_set_boot_partition(s_ota_partition) != ESP_OK)
        {
            s_error = true;
            return false;
        }
        return true;
    }

    // filesystem
    if(!s_error) fs_flush_buffer(true);
    fs_mount(); // bring the (now rewritten, or still-erased on error) filesystem back
    return !s_error;
}


bool ota_has_error()
{
    return s_error;
}
