#pragma once

class AsyncWebServer;

namespace ella {
void begin();                         // first action in setup(), before UART/FS
void filesystemMounted(bool mounted);
bool active();
bool filesystemReady();
bool tick();                          // true: skip all legacy application work
void registerRoutes(AsyncWebServer &server);
void startRecoveryNetwork();
void otaSetup();
void otaTick(bool enabled, bool limited);
} // namespace ella
