#pragma once

#include <cstddef>
#include <string>

// MQTT publisher built on esp-mqtt, replacing PubSubClient. Connection ret/
// reconnect is handled internally by esp-mqtt; mqtt_tick() only starts/stops the
// client to follow g_settings.mq_enabled and configuration changes.

void mqtt_tick();

// Force a stop + reconfigure on the next tick (after a /cfg/mqtt POST).
void mqtt_mark_settings_changed();

bool mqtt_connected();

// Publish to g_settings.mq_prefix + topic. No-op if not connected.
void mqtt_publish(const std::string &topic, const char *payload, size_t len);
