#include "app_fs.hpp"

#include "esp_spiffs.h"

static const char *PARTITION_LABEL = "spiffs";
static bool s_mounted = false;

bool fs_mount()
{
    if(s_mounted) return true;

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = PARTITION_LABEL;
    conf.max_files = 8;
    conf.format_if_mount_failed = false;

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    s_mounted = (err == ESP_OK);
    return s_mounted;
}

void fs_unmount()
{
    if(!s_mounted) return;
    esp_vfs_spiffs_unregister(PARTITION_LABEL);
    s_mounted = false;
}
