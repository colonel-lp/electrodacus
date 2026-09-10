#pragma once

// WiFi lifecycle built on esp_wifi / esp_netif / esp_event, reproducing the
// station-with-AP-fallback behaviour of the original Arduino updateWifiState() /
// handleWiFi() pair.

void wifi_start();

// Re-read g_settings and (re)apply mode, AP and STA configuration.
// Equivalent of the old updateWifiState().
void wifi_apply_settings();

// Call periodically from the main loop. Drives retry / AP-fallback timing and
// returns true while the station interface holds an IP. Equivalent of handleWiFi().
bool wifi_tick();

// Ask the next wifi_tick() to fully re-init from freshly loaded settings
// (used after a /cfg/wifi POST).
void wifi_mark_settings_changed();

bool wifi_sta_connected();
bool wifi_ap_fallback();
