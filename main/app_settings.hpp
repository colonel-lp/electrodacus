#pragma once

#include <cstdint>
#include <string>

// Runtime configuration, loaded from /spiffs/cfg/* JSON files. Mirrors the flat
// set of globals the original firmware kept in main.cpp.
struct Settings {
    // wifi (/spiffs/cfg/wifi)
    bool sta_enabled = false;
    std::string sta_hostname = "SBMS";
    std::string sta_ssid = "SBMS";
    std::string sta_password;
    std::string ap_ssid = "SBMS";
    std::string ap_password;

    // mqtt (/spiffs/cfg/mqtt)
    bool mq_enabled = false;
    std::string mq_host;
    uint32_t mq_port = 1883;
    std::string mq_prefix = "/";
    std::string mq_user;
    std::string mq_password;

    // data (/spiffs/cfg/data)
    bool data_sbms_enabled = true;
    bool data_sbms_diff = false;
    bool data_s2_enabled = false;

    // system (/spiffs/cfg/sys)
    bool system_ota_limit = true;

    // daily status e-mail (/spiffs/cfg/email)
    bool em_enabled = false;
    uint32_t em_hour = 8;             // hour of day (0-23, SBMS clock) to send the report
    std::string em_host;             // SMTP server hostname
    uint32_t em_port = 465;          // SMTP port (465 = implicit TLS, 25/587 = plain)
    std::string em_user;             // SMTP auth username ("" = no AUTH)
    std::string em_password;         // SMTP auth password
    std::string em_from;             // From address ("" = fall back to em_user)
    std::string em_to;               // recipient address
    bool em_ssl = true;              // use implicit TLS (default on)
    bool em_tls_verify = true;       // verify the server cert against the CA bundle (default on)
};

extern Settings g_settings;

void settings_read_all();
void settings_read_wifi();   // also fills in a default AP SSID/password and rewrites the file
void settings_read_mqtt();
void settings_read_data();
void settings_read_system();
void settings_read_email();

// Read the whole contents of a SPIFFS file into a string. Returns false on error.
bool file_read_all(const char *path, std::string &out);

// Overwrite a SPIFFS file with the given bytes. Returns false on error.
bool file_write_all(const char *path, const char *data, size_t len);
