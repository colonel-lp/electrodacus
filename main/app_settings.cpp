#include "app_settings.hpp"

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "esp_mac.h"

Settings g_settings;

bool file_read_all(const char *path, std::string &out)
{
    FILE *f = fopen(path, "rb");
    if(!f) return false;

    out.clear();
    char buf[256];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        out.append(buf, n);
    }
    fclose(f);
    return true;
}

bool file_write_all(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if(!f) return false;

    size_t w = fwrite(data, 1, len, f);
    fflush(f);
    fclose(f);
    return w == len;
}


void settings_read_wifi()
{
    std::string raw;
    if(!file_read_all("/spiffs/cfg/wifi", raw)) return;

    JsonDocument doc;
    if(deserializeJson(doc, raw) != DeserializationError::Ok) return;

    g_settings.sta_enabled  = doc["sta_enable"].as<bool>();
    g_settings.sta_hostname = doc["hostname"].as<std::string>();
    g_settings.sta_ssid     = doc["sta_ssid"].as<std::string>();
    g_settings.sta_password = doc["sta_pw"].as<std::string>();
    g_settings.ap_ssid      = doc["ap_ssid"].as<std::string>();
    g_settings.ap_password  = doc["ap_pw"].as<std::string>();

    bool rewrite = false;

    if(g_settings.ap_ssid.empty())
    {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        uint64_t uid = 0;
        for(int i = 0; i < 6; i++) uid |= static_cast<uint64_t>(mac[i]) << (8 * i);

        char ssid[24];
        snprintf(ssid, sizeof(ssid), "SBMS-%04X%08X",
                 static_cast<unsigned>((uid >> 32) & 0xFFFF),
                 static_cast<unsigned>(uid & 0xFFFFFFFFu));
        g_settings.ap_ssid = ssid;
        doc["ap_ssid"] = g_settings.ap_ssid;
        rewrite = true;
    }

    if(g_settings.ap_password.empty())
    {
        g_settings.ap_password = "electrodacus";
        doc["ap_pw"] = g_settings.ap_password;
        rewrite = true;
    }

    if(rewrite)
    {
        std::string out;
        serializeJson(doc, out);
        file_write_all("/spiffs/cfg/wifi", out.data(), out.size());
    }
}

void settings_read_mqtt()
{
    std::string raw;
    if(!file_read_all("/spiffs/cfg/mqtt", raw)) return;

    JsonDocument doc;
    if(deserializeJson(doc, raw) != DeserializationError::Ok) return;

    g_settings.mq_enabled  = doc["mq_enabled"].as<bool>();
    g_settings.mq_host     = doc["mq_host"].as<std::string>();
    g_settings.mq_port     = doc["mq_port"].as<uint32_t>();
    g_settings.mq_prefix   = doc["mq_prefix"].as<std::string>();
    g_settings.mq_user     = doc["mq_user"].as<std::string>();
    g_settings.mq_password = doc["mq_password"].as<std::string>();
}

void settings_read_data()
{
    std::string raw;
    if(!file_read_all("/spiffs/cfg/data", raw)) return;

    JsonDocument doc;
    if(deserializeJson(doc, raw) != DeserializationError::Ok) return;

    g_settings.data_sbms_enabled = doc["sbms_enabled"].as<bool>();
    g_settings.data_sbms_diff    = doc["sbms_diff"].as<bool>();
    g_settings.data_s2_enabled   = doc["s2_enabled"].as<bool>();
}

void settings_read_system()
{
    std::string raw;
    if(!file_read_all("/spiffs/cfg/sys", raw)) return;

    JsonDocument doc;
    if(deserializeJson(doc, raw) != DeserializationError::Ok) return;

    g_settings.system_ota_limit = doc["ota_limit"].as<bool>();
}

void settings_read_email()
{
    std::string raw;
    if(!file_read_all("/spiffs/cfg/email", raw)) return;

    JsonDocument doc;
    if(deserializeJson(doc, raw) != DeserializationError::Ok) return;

    g_settings.em_enabled  = doc["em_enabled"].as<bool>();
    g_settings.em_hour     = doc["em_hour"] | 8u;
    g_settings.em_host     = doc["em_host"].as<std::string>();
    g_settings.em_port     = doc["em_port"] | 465u;
    g_settings.em_user     = doc["em_user"].as<std::string>();
    g_settings.em_password = doc["em_password"].as<std::string>();
    g_settings.em_from       = doc["em_from"].as<std::string>();
    g_settings.em_to         = doc["em_to"].as<std::string>();
    g_settings.em_ssl        = doc["em_ssl"] | true;
    g_settings.em_tls_verify = doc["em_tls_verify"] | true;
}

void settings_read_all()
{
    settings_read_system();
    settings_read_wifi();
    settings_read_mqtt();
    settings_read_data();
    settings_read_email();
}
