#pragma once

#include <cstddef>

// HTTP server (esp_http_server) replacing ESPAsyncWebServer + AsyncEventSource.
// Serves the config UI and static bundle from SPIFFS, exposes the raw SBMS data,
// streams live data over Server-Sent-Events on /eData, and handles firmware /
// filesystem OTA on /update.

void httpd_start();

// Number of connected /eData (SSE) clients. Mirrors AsyncEventSource::count().
int httpd_sse_count();

// Broadcast a "sbms" SSE event with the given JSON payload to all /eData clients.
void httpd_push_sbms(const char *json, size_t len);
