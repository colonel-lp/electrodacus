#include "recovery.hpp"
#include "recovery_policy.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/semphr.h"

namespace ella {
namespace {
SemaphoreHandle_t mutex = nullptr;
struct Guard {
    bool locked;
    Guard() : locked(mutex && xSemaphoreTakeRecursive(mutex, portMAX_DELAY) == pdTRUE) {}
    ~Guard() { if (locked) xSemaphoreGiveRecursive(mutex); }
};
nvs_handle storage = 0;
bool storageReady = false;
bool inRecovery = false;
bool fsReady = false;
bool healthy = false;
bool healthTried = false;
bool limitedOta = true;
bool arduinoStarted = false;
bool arduinoBusy = false;
bool rebootPending = false;
uint32_t rebootSince = 0;
uint32_t bootSince = 0;
BootRecord record;
const char *reason = "normal";
const char *lastError = "";
String apName;

struct Upload {
    AsyncWebServerRequest *owner = nullptr;
    const esp_partition_t *target = nullptr;
    esp_ota_handle_t handle = 0;
    bool open = false;
    bool checked = false;
    bool failed = false;
    int status = 400;
    const char *error = "Incomplete firmware upload";
    size_t expected = 0;
    size_t received = 0;
    size_t prefixLength = 0;
    uint8_t prefix[kPrefixSize] = {};
    uint32_t started = 0;
    uint32_t last = 0;
} upload;

bool saveRecord() {
    return storageReady && nvs_set_blob(storage, "boot", &record, sizeof(record)) == ESP_OK &&
           nvs_commit(storage) == ESP_OK;
}

bool layoutMatches() {
    struct Expected { const char *label; uint8_t type; uint8_t subtype; uint32_t address; uint32_t size; };
    const Expected expected[] = {
        {"nvs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x9000, 0x5000},
        {"otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xe000, 0x2000},
        {"app0", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x140000},
        {"app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x150000, 0x140000},
        {"spiffs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0x290000, 0x170000}
    };
    for (const auto &e : expected) {
        const auto *p = esp_partition_find_first(static_cast<esp_partition_type_t>(e.type),
            static_cast<esp_partition_subtype_t>(e.subtype), e.label);
        if (!p || p->address != e.address || p->size != e.size) return false;
    }
    return ESP.getFlashChipSize() >= 0x400000;
}

bool validImage(const esp_partition_t *partition) {
    if (!partition) return false;
    esp_app_desc_t desc = {};
    if (esp_ota_get_partition_description(partition, &desc) != ESP_OK ||
        desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        std::strncmp(desc.project_name, "electrodacus-esp32", sizeof(desc.project_name)) != 0) return false;
    esp_partition_pos_t pos = {};
    pos.offset = partition->address;
    pos.size = partition->size;
    esp_image_metadata_t metadata = {};
    return esp_image_verify(ESP_IMAGE_VERIFY, &pos, &metadata) == ESP_OK;
}

bool busy() { return upload.owner || arduinoBusy || rebootPending; }
bool otaAllowed() { return inRecovery || !limitedOta || millis() < 300000; }

bool confirm(AsyncWebServerRequest *request, const char *action) {
    // A custom header prevents cross-origin browser form submissions. It is
    // deliberately not described as authentication; legacy LAN access remains.
    return request->hasHeader("X-Ella-Confirm") &&
           request->getHeader("X-Ella-Confirm")->value() == action;
}

void respond(AsyncWebServerRequest *request, int status, const String &message,
             const char *type = "text/plain") {
    auto *response = request->beginResponse(status, type, message);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("X-Frame-Options", "DENY");
    request->send(response);
}

void abortUpload(const char *error) {
    // ESP-IDF 4.0 has no esp_ota_abort(). esp_ota_end() releases the handle on
    // every result and does not select a boot partition, even for a valid image.
    if (upload.open) esp_ota_end(upload.handle);
    upload = Upload{};
    lastError = error;
}

void failUpload(int status, const char *error) {
    if (upload.open) esp_ota_end(upload.handle); // free only; never select this slot
    upload.open = false;
    upload.failed = true;
    upload.status = status;
    upload.error = error;
    lastError = error;
}

void scheduleReboot() { rebootSince = millis(); rebootPending = true; }

const char recoveryPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Ella SBMS recovery</title><style>
body{font:16px system-ui;max-width:780px;margin:30px auto;padding:0 18px;background:#17242c;color:#e8eff1}
button,a,input{font:inherit;margin:6px 8px 6px 0}button{padding:9px}a{color:#83d8cf}pre{white-space:pre-wrap;overflow-wrap:anywhere}
</style><h1>Ella SBMS recovery</h1>
<p>This page is built into the application firmware. Settings and web files are read only in this transitional build.</p>
<p><a href="/recovery/settings">Download settings backup (contains passwords)</a></p>
<input id="file" type="file" accept=".bin"><button id="upload">Upload application firmware</button>
<p>Upload only the application .bin. Keep this page open until the response. A successful upload reboots automatically.</p>
<button data-action="enter">Restart in recovery</button><button data-action="resume">Restart normally</button>
<button data-action="previous">Boot verified previous slot</button><p id="result" role="status"></p><pre id="status">Loading status…</pre>
<script>
const status=document.getElementById('status'),result=document.getElementById('result');
async function refresh(){try{const r=await fetch('/recovery/status',{cache:'no-store'});status.textContent=JSON.stringify(await r.json(),null,2)}catch(e){status.textContent=String(e)}}
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=async()=>{const a=b.dataset.action;
if(!window.confirm('Confirm '+a+'?'))return;try{const r=await fetch('/recovery/'+a,{method:'POST',headers:{'X-Ella-Confirm':a}});result.textContent=await r.text()}catch(e){result.textContent=String(e)}});
document.getElementById('upload').onclick=async()=>{const f=document.getElementById('file').files[0];if(!f)return;
if(!window.confirm('Upload '+f.name+' and restart?'))return;result.textContent='Uploading…';
try{const r=await fetch('/recovery/firmware',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-Ella-Confirm':'firmware'},body:f});result.textContent=await r.text()}catch(e){result.textContent=String(e)};refresh()};refresh();
</script></html>)HTML";

void statusResponse(AsyncWebServerRequest *request) {
    Guard guard;
    if (!guard.locked) { respond(request, 503, "Recovery state unavailable"); return; }
    DynamicJsonDocument doc(6144);
    doc["mode"] = inRecovery ? "recovery" : "normal";
    doc["firmware_role"] = "ella-transitional";
    doc["reason"] = reason;
    doc["uptime_ms"] = millis();
    doc["reset_reason"] = static_cast<int>(esp_reset_reason());
    doc["boot_attempts"] = record.attempts;
    doc["healthy"] = healthy;
    doc["boot_record_ready"] = storageReady;
    doc["filesystem_mounted"] = fsReady;
    doc["filesystem_writes_enabled"] = false;
    doc["partition_layout_compatible"] = layoutMatches();
    doc["installed_bootloader_rollback"] = "unverified";
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    doc["application_rollback_config"] = true;
#else
    doc["application_rollback_config"] = false;
#endif
    doc["idf_version"] = esp_get_idf_version();
    const auto *app = esp_ota_get_app_description();
    doc["version"] = app->version;
    doc["project"] = app->project_name;
    doc["recovery_ap"] = apName;
    doc["ota_allowed"] = otaAllowed();
    doc["espota_enabled"] = arduinoStarted;
    doc["upload_active"] = upload.owner != nullptr || arduinoBusy;
    doc["last_error"] = lastError;
    doc["free_heap"] = ESP.getFreeHeap();
    const auto *running = esp_ota_get_running_partition();
    const auto *boot = esp_ota_get_boot_partition();
    const auto *previous = esp_ota_get_next_update_partition(nullptr);
    doc["running_partition"] = running ? running->label : "";
    doc["boot_partition"] = boot ? boot->label : "";
    doc["previous_partition"] = previous ? previous->label : "";
    if (!busy()) doc["previous_image_valid"] = validImage(previous);
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK)
        doc["ota_image_state"] = static_cast<int>(state);
    else doc["ota_image_state"] = nullptr;
    JsonArray partitions = doc.createNestedArray("partitions");
    // The retained SDK predates ESP_PARTITION_TYPE_ANY. Enumerate every valid
    // type, including user-defined types, without assuming the source table.
    for (unsigned type = 0; type < 0xff; ++type) {
        auto it = esp_partition_find(static_cast<esp_partition_type_t>(type), ESP_PARTITION_SUBTYPE_ANY, nullptr);
        while (it) {
            const auto *p = esp_partition_get(it);
            auto out = partitions.createNestedObject();
            out["label"] = p->label; out["type"] = p->type; out["subtype"] = p->subtype;
            out["offset"] = p->address; out["size"] = p->size; out["encrypted"] = p->encrypted;
            it = esp_partition_next(it);
        }
    }
    if (doc.overflowed()) { respond(request, 500, "Status allocation failed"); return; }
    String out;
    if (!serializeJson(doc, out)) { respond(request, 500, "Status serialization failed"); return; }
    respond(request, 200, out, "application/json");
}

void settingsResponse(AsyncWebServerRequest *request) {
    Guard guard;
    if (!guard.locked || busy()) { respond(request, 409, "Update or restart in progress"); return; }
    if (!fsReady) { respond(request, 503, "Filesystem is not mounted; no settings were changed"); return; }
    const char *names[] = {"wifi", "mqtt", "data", "sys", "email"};
    DynamicJsonDocument doc(24576);
    doc["format"] = "ella-settings-raw-v1";
    bool complete = true;
    auto files = doc.createNestedObject("files");
    for (size_t i = 0; i < 5; ++i) {
        const String path = String("/cfg/") + names[i];
        if (!SPIFFS.exists(path)) {
            files[path] = nullptr;
            if (i < 4) complete = false;
            continue;
        }
        auto file = SPIFFS.open(path, "r");
        if (!file || file.size() > 4096) { respond(request, 500, "Settings backup failed: unreadable or oversized file"); return; }
        const size_t size = file.size();
        String raw;
        if (!raw.reserve(size + 1)) { respond(request, 500, "Settings backup allocation failed"); return; }
        while (file.available() && raw.length() < size) raw += static_cast<char>(file.read());
        file.close();
        if (raw.length() != size) { respond(request, 500, "Settings backup read failed"); return; }
        files[path] = raw; // exact original JSON text, not a migration/rewrite
    }
    doc["complete"] = complete;
    if (doc.overflowed()) { respond(request, 500, "Settings backup allocation failed"); return; }
    String out;
    if (!serializeJson(doc, out)) { respond(request, 500, "Settings backup serialization failed"); return; }
    auto *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Content-Disposition", "attachment; filename=ella-sbms-settings.json");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    request->send(response);
}

void uploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Guard guard;
    if (!guard.locked || !confirm(request, "firmware")) return;
    if (index == 0) {
        if (busy() || !otaAllowed()) return;
        upload = Upload{};
        upload.owner = request;
        upload.target = esp_ota_get_next_update_partition(nullptr);
        upload.expected = total;
        upload.started = upload.last = millis();
        request->onDisconnect([request]() {
            Guard guard;
            if (guard.locked && upload.owner == request) abortUpload("Upload disconnected before completion");
        });
        if (!layoutMatches() || !upload.target) { failUpload(409, "Unrecognised installed partition layout"); return; }
        if (request->contentType() != "application/octet-stream" || total != request->contentLength() ||
            total < kPrefixSize || total > upload.target->size) {
            failUpload(400, "Send one application binary with its exact Content-Length"); return;
        }
    }
    if (upload.owner != request || upload.failed) return;
    if (!validChunk(upload.received, upload.expected, index, total, len)) {
        failUpload(400, "Incomplete or out-of-order upload"); return;
    }
    upload.last = millis();
    upload.received += len;
    if (!upload.checked) {
        size_t copy = kPrefixSize - upload.prefixLength;
        if (copy > len) copy = len;
        std::memcpy(upload.prefix + upload.prefixLength, data, copy);
        upload.prefixLength += copy;
        data += copy; len -= copy;
        if (upload.prefixLength < kPrefixSize) return;
        if (!validAppPrefix(upload.prefix, upload.prefixLength)) {
            failUpload(400, "Not an ESP32 electrodacus application image"); return;
        }
        if (esp_ota_begin(upload.target, upload.expected, &upload.handle) != ESP_OK) {
            failUpload(500, "Could not start application update"); return;
        }
        upload.open = true;
        upload.checked = true;
        if (esp_ota_write(upload.handle, upload.prefix, kPrefixSize) != ESP_OK) {
            failUpload(500, "Firmware prefix write failed"); return;
        }
    }
    if (len && esp_ota_write(upload.handle, data, len) != ESP_OK) failUpload(500, "Firmware write failed");
}

void uploadComplete(AsyncWebServerRequest *request) {
    Guard guard;
    if (!guard.locked) { respond(request, 503, "Recovery state unavailable"); return; }
    if (!confirm(request, "firmware")) { respond(request, 403, "Missing X-Ella-Confirm: firmware"); return; }
    if (upload.owner != request) { respond(request, 409, otaAllowed() ? "No active upload, timed out, or updater busy" : "OTA time window has closed"); return; }
    if (!upload.failed && (!upload.open || upload.received != upload.expected)) failUpload(400, "Incomplete firmware upload");
    if (!upload.failed) {
        const esp_err_t result = esp_ota_end(upload.handle); // validates full image; frees handle on every result
        upload.open = false;
        if (result != ESP_OK || !validImage(upload.target)) failUpload(400, "Firmware image validation failed");
    }
    if (!upload.failed) {
        record.forced = 0; record.attempts = 0;
        if (!saveRecord()) failUpload(500, "Could not prepare boot record; firmware not selected");
        else if (esp_ota_set_boot_partition(upload.target) != ESP_OK) failUpload(500, "Could not select new firmware");
    }
    if (upload.failed) {
        const int status = upload.status;
        const char *error = upload.error;
        abortUpload(error);
        respond(request, status, error);
        return;
    }
    upload = Upload{};
    lastError = "";
    scheduleReboot();
    respond(request, 200, "Firmware validated and selected. Restarting.");
}

void command(AsyncWebServerRequest *request, const char *action) {
    Guard guard;
    if (!guard.locked) { respond(request, 503, "Recovery state unavailable"); return; }
    if (!confirm(request, action)) { respond(request, 403, "Missing confirmation header"); return; }
    if (busy()) { respond(request, 409, "Update or restart in progress"); return; }
    if (!healthy) { respond(request, 409, "Wait for the 60-second startup check before restarting"); return; }
    const auto *previous = esp_ota_get_next_update_partition(nullptr);
    if (std::strcmp(action, "previous") == 0 && (!layoutMatches() || !validImage(previous))) {
        respond(request, 409, "Previous slot has no verified electrodacus application"); return;
    }
    const BootRecord old = record;
    record.attempts = 0;
    record.forced = std::strcmp(action, "enter") == 0;
    if (!saveRecord()) { record = old; respond(request, 500, "Boot record write failed"); return; }
    if (std::strcmp(action, "previous") == 0 && esp_ota_set_boot_partition(previous) != ESP_OK) {
        record = old; saveRecord();
        respond(request, 500, "Could not select previous slot"); return;
    }
    scheduleReboot();
    respond(request, 200, "Boot request saved. Restarting.");
}
} // namespace

void begin() {
    bootSince = millis();
    mutex = xSemaphoreCreateRecursiveMutex();
    WiFi.persistent(false); // do not rewrite the SDK's stored Wi-Fi settings
    const auto *app = esp_ota_get_app_description();
    const esp_err_t opened = nvs_open("ella_recovery", NVS_READWRITE, &storage);
    storageReady = opened == ESP_OK;
    if (!mutex || !storageReady) {
        inRecovery = true; reason = "boot-record-unavailable";
    } else {
        size_t length = sizeof(record);
        const esp_err_t read = nvs_get_blob(storage, "boot", &record, &length);
        if (read == ESP_ERR_NVS_NOT_FOUND) record = BootRecord{};
        else if (read != ESP_OK || length != sizeof(record) || !validRecord(record)) {
            record = BootRecord{};
            inRecovery = true; reason = "boot-record-invalid";
        }
        if (startBoot(record, app->app_elf_sha256)) {
            inRecovery = true;
            reason = record.forced ? "requested" : "repeated-unconfirmed-boots";
        }
        if (!saveRecord()) { inRecovery = true; reason = "boot-record-write-failed"; }
    }
    char name[32];
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(name, sizeof(name), "SBMS-Recovery-%06X", static_cast<unsigned>(mac & 0xffffff));
    apName = name;
    otaSetup();
}

void filesystemMounted(bool mounted) {
    fsReady = mounted;
    if (!mounted) { inRecovery = true; reason = "filesystem-mount-failed"; }
}
bool active() { return inRecovery; }
bool filesystemReady() { return fsReady; }

void startRecoveryNetwork() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    if (!WiFi.softAP(apName.c_str(), "electrodacus")) lastError = "Recovery AP failed to start";
    ArduinoOTA.setHostname(apName.c_str());
}

void otaSetup() {
    // In this pinned core Update.begin only allocates RAM; erase/write occurs
    // after onStart. Aborting here therefore rejects U_SPIFFS before any erase.
    ArduinoOTA.setRebootOnSuccess(false)
      .onStart([]() {
        Guard guard;
        if (!guard.locked || ArduinoOTA.getCommand() != U_FLASH || busy() || !layoutMatches() || !otaAllowed()) {
            Update.abort(); lastError = "ArduinoOTA rejected: firmware only, updater busy, or incompatible layout";
            return;
        }
        arduinoBusy = true;
      })
      .onEnd([]() {
        Guard guard;
        const auto *running = esp_ota_get_running_partition();
        const auto *selected = esp_ota_get_boot_partition();
        bool ok = guard.locked && arduinoBusy && selected && running &&
                  selected->address != running->address && validImage(selected);
        if (ok) { record.forced = 0; record.attempts = 0; ok = saveRecord(); }
        if (!ok) {
            if (running) esp_ota_set_boot_partition(running);
            lastError = "ArduinoOTA image or boot record validation failed";
        } else { lastError = ""; scheduleReboot(); }
        arduinoBusy = false;
      })
      .onError([](ota_error_t) {
        Guard guard;
        if (guard.locked) {
            Update.abort(); arduinoBusy = false;
            lastError = "ArduinoOTA aborted; current application remains selected";
        }
      });
}

void otaTick(bool enabled, bool limited) {
    bool service = false;
    {
        Guard guard;
        if (!guard.locked) return;
        limitedOta = limited;
        const bool allow = (inRecovery || enabled) && otaAllowed();
        if (allow && !arduinoStarted) { ArduinoOTA.begin(); arduinoStarted = true; }
        else if (!allow && arduinoStarted) { ArduinoOTA.end(); arduinoStarted = false; }
        service = arduinoStarted && !upload.owner && !rebootPending;
    }
    // Never hold the state mutex while ArduinoOTA's blocking transfer runs:
    // HTTP can still reject concurrent requests and report status.
    if (service) ArduinoOTA.handle();
}

bool tick() {
    bool restart = false;
    {
        Guard guard;
        if (guard.locked) {
            const uint32_t now = millis();
            if (upload.owner && (elapsed(now, upload.last, kUploadIdleMs) || elapsed(now, upload.started, kUploadMaxMs)))
                abortUpload("Firmware upload timed out; current application remains selected");
            const bool networkReady = WiFi.status() == WL_CONNECTED ||
                ((WiFi.getMode() & WIFI_AP) && static_cast<uint32_t>(WiFi.softAPIP()) != 0);
            if (!healthTried && !busy() && networkReady && elapsed(now, bootSince, kHealthyMs)) {
                healthTried = true; // a storage fault must not cause a flash-write loop
                // Confirms an actual pending state if present. This does not
                // establish that the installed bootloader implements rollback.
                esp_ota_img_states_t state;
                const auto *running = esp_ota_get_running_partition();
                bool confirmed = true;
                if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
                    confirmed = esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
                record.attempts = 0;
                if (inRecovery) record.forced = 1;
                healthy = confirmed && saveRecord();
                if (!healthy) { reason = "health-confirmation-failed"; lastError = reason; }
            }
            restart = rebootPending && elapsed(now, rebootSince, 1000);
        }
    }
    if (restart) ESP.restart();
    if (inRecovery) {
        otaTick(true, false);
        digitalWrite(LED_BUILTIN, millis() % 500 < 250);
    }
    return inRecovery;
}

void registerRoutes(AsyncWebServer &server) {
    // The pinned AsyncWebServer also matches child paths. Register the page
    // after its specific GET endpoints so it cannot shadow status/export.
    server.on("/recovery/status", HTTP_GET, statusResponse);
    server.on("/recovery/settings", HTTP_GET, settingsResponse);
    server.on("/recovery/firmware", HTTP_POST, uploadComplete, nullptr, uploadBody);
    for (const char *action : {"enter", "resume", "previous"}) {
        const String path = String("/recovery/") + action;
        server.on(path.c_str(), HTTP_POST, [action](AsyncWebServerRequest *request) { command(request, action); });
    }
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/recovery"); });
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        respond(request, 410, "Filesystem/multipart updates disabled. Use /recovery/firmware for the application only.");
    });
    server.on("/recovery", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto *response = request->beginResponse_P(200, "text/html", recoveryPage);
        response->addHeader("Cache-Control", "no-store");
        response->addHeader("X-Frame-Options", "DENY");
        request->send(response);
    });
}
} // namespace ella
