#include "esp_ota_ops.h"

namespace ella {
bool validateBootSelection(const esp_partition_t *partition);
}

// All callers, including the pinned Arduino Update library, must validate the
// project BEFORE the SDK changes otadata. ArduinoOTA sends OK only after this
// returns ESP_OK through Update.end(). Do not replace this with an onEnd check.
extern "C" esp_err_t __real_esp_ota_set_boot_partition(const esp_partition_t *partition);
extern "C" esp_err_t __wrap_esp_ota_set_boot_partition(const esp_partition_t *partition) {
    if (!ella::validateBootSelection(partition)) return ESP_ERR_OTA_VALIDATE_FAILED;
    return __real_esp_ota_set_boot_partition(partition);
}
