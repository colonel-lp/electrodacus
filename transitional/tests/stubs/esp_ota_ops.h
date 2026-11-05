#pragma once
typedef int esp_err_t;
struct esp_partition_t { unsigned address; };
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_OTA_VALIDATE_FAILED = 0x1503;
extern "C" esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
