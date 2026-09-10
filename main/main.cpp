#include <cstdio>
#include <string>

#include <ArduinoJson.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_system.h"

#include "app_fs.hpp"
#include "app_settings.hpp"
#include "app_wifi.hpp"
#include "app_serial.hpp"
#include "app_mqtt.hpp"
#include "app_email.hpp"
#include "app_httpd.hpp"
#include "sbms_json.hpp"
#include "sbmsData.hpp"
#include "globals.hpp"
#include "util.hpp"
#include "version.hpp"

// definitions for the globals declared in globals.hpp
JsvarStore varStore;
CellMinMax g_cellMinMax;
volatile bool g_should_reboot = false;
volatile bool g_cell_minmax_reset = false;

static const gpio_num_t LED_GPIO = GPIO_NUM_2; // BUILTIN_LED / LED_BUILTIN on nodemcu-32s


static void updateLed()
{
    const uint32_t t = millis();
    int level;

    if(g_settings.sta_enabled && wifi_sta_connected())
    {
        level = (t % 2000 < 1900);
    }
    else if(g_settings.sta_enabled)
    {
        level = wifi_ap_fallback() ? (t % 500 < 100) : (t % 1500 < 100);
    }
    else
    {
        level = (t % 1500 < 750);
    }

    gpio_set_level(LED_GPIO, level);
}


static void main_loop_task(void *arg)
{
    JsonDocument doc;
    static char jsonBuffer[2000];

    for(;;)
    {
        // reboot if requested from any source, after 1s (let cpu0 finish networking)
        if(g_should_reboot)
        {
            delay_ms(1000);
            esp_restart();
        }

        wifi_tick();
        updateLed();
        mqtt_tick();

        // pop one parsed-variable event per iteration
        std::string ev = uartPopEvent();

        if(ev == "sbms")
        {
            SbmsData sbms(varStore.getVar("sbms"));

            // Per-cell rolling 24 h min/max. A pending GUI reset is honoured
            // here so all history state stays owned by this one task.
            const uint32_t now_ms = millis();
            if(g_cell_minmax_reset)
            {
                g_cell_minmax_reset = false;
                g_cellMinMax.reset(now_ms);
            }
            g_cellMinMax.update(sbms.cellVoltageMV, now_ms);

            // Daily status e-mail: fire once per calendar day, at or after the
            // configured hour. The last-sent date is packed as YYYYMMDD and kept
            // in RAM only, so a reboot after the send hour can produce one extra
            // report that day.
            static uint32_t s_email_last_ymd = 0;
            if(g_settings.em_enabled && sbms.month != 0)
            {
                const uint32_t ymd = static_cast<uint32_t>(sbms.year) * 10000u
                                   + static_cast<uint32_t>(sbms.month) * 100u
                                   + sbms.day;
                if(ymd > s_email_last_ymd && sbms.hour >= g_settings.em_hour)
                {
                    // Mark done for today regardless of the send result so a bad
                    // SMTP config does not retry on every SBMS frame.
                    s_email_last_ymd = ymd;
                    email_send_status(sbms.stateOfChargePercent, sbms.temperatureInternalTenthC);
                }
            }

            const bool toMqtt = g_settings.mq_enabled && g_settings.data_sbms_enabled;

            if(toMqtt || httpd_sse_count() > 0)
            {
                toJsonSBMS(sbms, g_cellMinMax, doc);
                size_t n = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

                if(toMqtt)               mqtt_publish("sbms", jsonBuffer, n);
                if(httpd_sse_count() > 0) httpd_push_sbms(jsonBuffer, n);
            }
        }
        else if(ev == "s2")
        {
            std::string s2 = varStore.getVar("s2");
            if(g_settings.mq_enabled && g_settings.data_s2_enabled)
            {
                mqtt_publish("s2", s2.data(), s2.size());
            }
        }

        delay_ms(10);
    }
}


extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    fs_mount();

    settings_read_all();

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    serial_start();
    wifi_start();
    httpd_start();

    xTaskCreatePinnedToCore(main_loop_task, "mainloop", 8192, nullptr, 5, nullptr, 1);
}
