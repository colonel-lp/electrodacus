#include "app_mqtt.hpp"

#include "mqtt_client.h"

#include "app_settings.hpp"

static esp_mqtt_client_handle_t s_client = nullptr;
static volatile bool s_connected = false;
static bool s_settings_changed = false;


static void on_mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch(static_cast<esp_mqtt_event_id_t>(event_id))
    {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        break;
    default:
        break;
    }
}


static void mqtt_stop_and_destroy()
{
    if(s_client)
    {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
    }
    s_connected = false;
}


static void mqtt_start()
{
    if(g_settings.mq_host.empty()) return;

    std::string uri = "mqtt://" + g_settings.mq_host + ":" + std::to_string(g_settings.mq_port);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri.c_str();
    cfg.credentials.client_id = g_settings.sta_hostname.c_str();
    cfg.session.keepalive = 60;
    if(!g_settings.mq_user.empty())
    {
        cfg.credentials.username = g_settings.mq_user.c_str();
        cfg.credentials.authentication.password = g_settings.mq_password.c_str();
    }

    s_client = esp_mqtt_client_init(&cfg);
    if(!s_client) return;

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, &on_mqtt_event, nullptr);
    esp_mqtt_client_start(s_client);
}


void mqtt_mark_settings_changed()
{
    s_settings_changed = true;
}


void mqtt_tick()
{
    if(s_settings_changed)
    {
        s_settings_changed = false;
        mqtt_stop_and_destroy();
    }

    if(!g_settings.mq_enabled)
    {
        if(s_client) mqtt_stop_and_destroy();
        return;
    }

    if(!s_client)
    {
        mqtt_start();
    }
}


bool mqtt_connected()
{
    return s_connected;
}


void mqtt_publish(const std::string &topic, const char *payload, size_t len)
{
    if(!s_client || !s_connected) return;

    std::string full = g_settings.mq_prefix + topic;
    esp_mqtt_client_publish(s_client, full.c_str(), payload, static_cast<int>(len), 0, 0);
}
