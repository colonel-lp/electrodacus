#include "esp_ota_ops.h"
#include <cassert>
#include <cstdio>
bool validated = false;
bool validImage = false;
int selected = 0;
esp_err_t selectionResult = ESP_OK;
namespace ella {
bool validateBootSelection(const esp_partition_t *partition) {
    validated = validImage && partition;
    return validated;
}
}
int main() {
    esp_partition_t target = {0x150000};
    // Call the public SDK name, as the real Arduino Update library does.
    assert(esp_ota_set_boot_partition(&target) == ESP_ERR_OTA_VALIDATE_FAILED);
    assert(selected == 0); // wrong project/corrupt image cannot select or return OK
    validImage = true;
    assert(esp_ota_set_boot_partition(nullptr) == ESP_ERR_OTA_VALIDATE_FAILED);
    assert(selected == 0);
    selectionResult = -1;
    assert(esp_ota_set_boot_partition(&target) == -1); // failure propagates to Update.end()
    assert(selected == 1);
    validated = false; selectionResult = ESP_OK;
    assert(esp_ota_set_boot_partition(&target) == ESP_OK);
    assert(selected == 2 && validated);
    std::puts("Boot selection: validation precedes SDK selection; failures propagate");
}
