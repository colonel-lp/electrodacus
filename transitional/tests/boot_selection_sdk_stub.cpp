#include "esp_ota_ops.h"
#include <cassert>
extern bool validated;
extern int selected;
extern esp_err_t selectionResult;
extern "C" esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition) {
    assert(validated && partition); // no SDK selection before validation
    ++selected;
    return selectionResult;
}
