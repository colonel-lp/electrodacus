#pragma once

#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Small compatibility helpers replacing the handful of Arduino conveniences
// this firmware relied on.

inline uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

inline void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

inline bool str_starts_with(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
