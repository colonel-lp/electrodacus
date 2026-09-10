#pragma once

#include <cstddef>
#include <cstdint>

// Streamed firmware / filesystem update, replacing the Arduino Update library.
//  - Firmware  -> esp_ota_* against the inactive OTA app slot.
//  - Filesystem-> raw write of the "spiffs" data partition (SPIFFS is unmounted
//    for the duration and remounted by ota_end()).

enum class OtaType { Firmware, Filesystem };

bool ota_begin(OtaType type);
bool ota_write(const uint8_t *data, size_t len);
bool ota_end();          // finalize; firmware: set boot partition, filesystem: remount
bool ota_has_error();
