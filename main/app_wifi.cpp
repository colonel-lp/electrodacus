#include "app_wifi.hpp"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "app_settings.hpp"
#include "util.hpp"

static esp_netif_t *s_sta_netif = nullptr;
static esp_netif_t *s_ap_netif  = nullptr;

static volatile bool s_sta_connected = false;
static bool s_sta_should_connect = false;
static bool s_ap_fallback = false;

// timing state, mirrors the old globals
static uint32_t s_lastWiFiTime = 0;
static uint32_t s_lastWifiRetryTime = 0;
static bool s_settingsChanged = false;


static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if(base == WIFI_EVENT)
    {
        switch(id)
        {
        case WIFI_EVENT_STA_START:
            if(s_sta_should_connect) esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta_connected = false;
            if(s_sta_should_connect) esp_wifi_connect(); //keep retrying
            break;
        default:
            break;
        }
    }
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        s_sta_connected = true;
    }
}


static void apply_ap_config()
{
    wifi_config_t ap = {};
    const std::string &ssid = g_settings.ap_ssid;
    const std::string &pass = g_settings.ap_password;

    strlcpy(reinterpret_cast<char *>(ap.ap.ssid), ssid.c_str(), sizeof(ap.ap.ssid));
    ap.ap.ssid_len = ssid.length();
    strlcpy(reinterpret_cast<char *>(ap.ap.password), pass.c_str(), sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_netif_set_hostname(s_ap_netif, "SBMS");
}

static void apply_sta_config()
{
    wifi_config_t sta = {};
    strlcpy(reinterpret_cast<char *>(sta.sta.ssid), g_settings.sta_ssid.c_str(), sizeof(sta.sta.ssid));
    strlcpy(reinterpret_cast<char *>(sta.sta.password), g_settings.sta_password.c_str(), sizeof(sta.sta.password));

    esp_netif_set_hostname(s_sta_netif, g_settings.sta_hostname.c_str());
    esp_wifi_set_config(WIFI_IF_STA, &sta);
}


void wifi_apply_settings()
{
    const bool sta_on = g_settings.sta_enabled;
    const bool ap_on  = s_ap_fallback || !sta_on;

    wifi_mode_t mode = WIFI_MODE_AP;
    if(sta_on && ap_on)       mode = WIFI_MODE_APSTA;
    else if(sta_on && !ap_on) mode = WIFI_MODE_STA;
    else                      mode = WIFI_MODE_AP;

    esp_wifi_set_mode(mode);

    if(ap_on)
    {
        apply_ap_config();
    }

    if(sta_on)
    {
        s_sta_should_connect = true;
        apply_sta_config();
        esp_wifi_connect();
    }
    else
    {
        s_sta_should_connect = false;
        s_sta_connected = false;
        esp_wifi_disconnect();
    }
}


void wifi_mark_settings_changed()
{
    s_settingsChanged = true;
}


void wifi_start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_wifi_event, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_wifi_event, nullptr, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_lastWiFiTime = millis();
    s_lastWifiRetryTime = s_lastWiFiTime;
    s_ap_fallback = false;

    wifi_apply_settings();
}


bool wifi_tick()
{
    const uint32_t t = millis();

    if(s_settingsChanged)
    {
        s_settingsChanged = false;
        s_lastWiFiTime = t + 5000; //give it a little extra time
        s_lastWifiRetryTime = t;
        s_ap_fallback = false;
        wifi_apply_settings();
    }

    const bool sta_on = g_settings.sta_enabled;

    if(sta_on && s_sta_connected)
    {
        s_lastWiFiTime = t;
        if(s_ap_fallback)
        {
            s_ap_fallback = false;
            wifi_apply_settings();
        }
    }
    else if(sta_on && !s_sta_connected)
    {
        if(t - s_lastWifiRetryTime > 2000) //keep retrying every 2s even while the AP is up
        {
            s_lastWifiRetryTime = t;
            wifi_apply_settings();
        }
        else if(t - s_lastWiFiTime > 20000 && !s_ap_fallback) //enable AP fallback after 20s
        {
            s_lastWifiRetryTime = t;
            s_ap_fallback = true;
            wifi_apply_settings();
        }
    }

    return s_sta_connected;
}


bool wifi_sta_connected() { return s_sta_connected; }
bool wifi_ap_fallback()   { return s_ap_fallback; }
