# Electrodacus ESP32 code review and improvement list

Reviewed: 4 November 2026  
Repository: colonel-lp/electrodacus-esp32  
Branch: ella-monitoring
Commit: 6dc4e045095f28d347e3754c60b86aa6ba145b71

This is a source review, with small host-side reproductions. No production firmware was edited, built, flashed or tested on the SBMS. These findings do not establish the cause of the earlier real-world disconnections. Some defects are inherited from the original implementation.

## Recommended order

1. Protect configuration and reject invalid SBMS frames.
2. Make task communication safe.
3. Correct Wi-Fi reconnection, fallback timing and initial provisioning.
4. Add bounded diagnostics, then improve MQTT and browser delivery.
5. Make updates recoverable before considering installation.
6. Run compatibility and failure-recovery tests, followed by a separate device stability test.

## Fixes before installing a modified build

### 1. Reject malformed SBMS frames before decoding — high priority; reproduced

[Decoder, lines 22–75](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/components/sbmsdata/sbmsData.cpp#L22)

The decoder reads fixed offsets through a raw pointer without checking string length, framing or the allowed compressed character range. A syntactically quoted but short value can reach it through the variable parser. An empty value can also be returned on a store lookup/lock failure.

A host build of the unchanged decoder, using AddressSanitizer, reports a heap-buffer-overflow at line 71 for a 20-character quoted input. On the device, malformed input could therefore produce invalid readings or a crash; that outcome has not been reproduced on hardware.

**Fix:** validate framing, escaped data, decoded length and field ranges; return an explicit parse result; publish only complete valid frames. Count rejected frames without replacing the last valid sample. Test the complete UART-parser-to-decoder path.

### 2. Make configuration saves validated and recoverable — high priority; source-confirmed

[HTTP settings handler, line 336](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L336) · [file writes, line 28](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_settings.cpp#L28)

The fork correctly assembles the complete HTTP body, but accepts arbitrary bytes, truncates the live settings file before writing, ignores the write result and replies “saved” regardless. Flush/close errors are not checked. Request bodies have no application-level size cap. Missing JSON fields can overwrite runtime defaults with empty/zero values.

**Fix:** cap request size; validate object shape, types, required fields and ranges before mutation. Save through a recoverable mechanism with last-known-good recovery, check every storage result, and report HTTP failure accurately. Account for SPIFFS power-loss behaviour rather than assuming rename alone guarantees safety. Preserve the existing settings if any stage fails.

### 3. Stop unsynchronised cross-task settings changes — high priority; source-confirmed

[Settings reload from HTTP, line 364](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L364) · [Wi-Fi settings use, line 55](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_wifi.cpp#L55) · [MQTT settings use, line 40](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_mqtt.cpp#L40)

The HTTP task mutates global Settings strings while the main loop, Wi-Fi event task and email task can read them. Some flags are ordinary booleans, others are volatile; neither provides the required task synchronisation. In particular, concurrent std::string replacement and reads are unsafe.

**Fix:** transfer validated settings through a queue to one owning task, or use protected immutable snapshots. Use atomics/queues/locks for shared control flags, SSE counts and email status. Take one coherent email configuration snapshot per send.

### 4. Correct Wi-Fi retry and fallback timing — high priority; one defect reproduced

[Wi-Fi events, line 28](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_wifi.cpp#L28) · [Wi-Fi tick, line 146](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_wifi.cpp#L146)

There are two reconnect paths: immediate reconnect in the disconnect event and reapplying mode, credentials and connect every two seconds in the main loop. Return values from those operations are ignored. This is difficult to diagnose and is not evidence that retries themselves caused the previous outages.

A separate definite error is setting the unsigned last-connected timestamp to t + 5000. At t = 100000, subtraction produces 4294962296 ms, so the fallback test can pass immediately after changing settings.

**Fix:** one event-driven connection state machine; distinguish deliberate disconnects, association, DHCP and retry states; apply credentials only when needed; schedule bounded retries/backoff; handle lost-IP and disconnect reasons. Use elapsed-time comparisons or an explicit deadline without future-timestamp subtraction. Keep fallback AP usable during STA retries. Test router reboot, wrong password, unavailable SSID and transitions to a future Pi USB AP.

### 5. Fix first-boot and missing-configuration provisioning — high priority; source-confirmed

[Default settings, line 8](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_settings.hpp#L8) · [Wi-Fi configuration loading, line 40](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_settings.cpp#L40)

The new source tree contains no data/cfg/wifi. When that file is absent, loading returns before generating the documented unique SSID and password. Runtime defaults therefore create an open AP named SBMS, rather than the documented SBMS-[identifier] with the electrodacus password. Invalid JSON takes a similar default path on boot.

**Fix:** construct and validate provisioning defaults even with no readable settings file. Make recovery predictable, give the device a unique identity, and define how its setup credentials are obtained. Test an entirely fresh filesystem and a corrupt configuration.

### 6. Make OTA and filesystem updates safe — high priority; source-confirmed

[Upload handling, line 427](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L427) · [Filesystem erase, line 76](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_ota.cpp#L76) · [Filesystem completion, line 158](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_ota.cpp#L158)

Filesystem uploads erase the partition holding both the web interface and all /cfg settings. The supplied image resets MQTT settings and lacks the Wi-Fi configuration. Update type is chosen from the filename. The HTTP handler does not require a complete multipart closing boundary before ota_end(), and filesystem completion ignores the mount result. An incomplete/corrupt filesystem upload can consequently be reported successful if its flash writes succeeded.

**Fix:** separate user settings from replaceable web assets, or provide a tested settings-preservation migration. Explicitly select and validate the image type, size and integrity; require a complete upload; abort on receive/write errors; verify mount success; coordinate filesystem access. Define and test rollback before installation.

The original repository's partition CSV and this branch's partition CSV match. That removes one source-level migration difference, but does not verify the partition table actually installed on the device. Legacy web assets also live under a different path, so the first migration needs a deliberate firmware/UI update sequence.

### 7. Enforce update restrictions and protect administration — high priority; source-confirmed

[OTA GET check, line 391](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L391) · [Registered routes, line 618](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L618)

The five-minute restriction is checked only by GET /update; POST /update accepts uploads without it. The main frontend directly posts uploads, so it bypasses that check too. Packaged cfg/sys also explicitly disables the restriction, contrary to the README's default claim.

Configuration reads expose stored Wi-Fi, MQTT and email passwords without authentication. Configuration changes, firmware uploads and reboot are likewise unauthenticated. Reboot is available as a GET request.

**Fix:** enforce the chosen update policy on the actual upload endpoint. Add an appropriate authenticated setup/admin flow, protect secrets and state-changing requests, and remove GET-triggered reboot. Document the real defaults and preserve intentionally configured settings during migration.

## Stability and diagnostic improvements

### 8. Repair SSE/browser connection lifecycle — medium priority; source-confirmed

[SSE bookkeeping and broadcast, line 25](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L25)

Client entries are removed only after a send failure; no session-close cleanup is installed. The HTTP server can close/purge sessions and reuse a descriptor before the next broadcast. The saved descriptor can then refer to a different request. An eighth-plus stream is also accepted without a clear rejection path. Slow sends run serially in the HTTP task, potentially delaying settings and updates.

**Fix:** tie each stream to the HTTP session lifetime, remove it on every close/purge, reject excess clients explicitly, and set bounded send/queue budgets. Coalesce live samples rather than accumulating stale frames. Stress-test repeated browser reconnects and slow clients.

### 9. Decouple MQTT publishing from the control loop — medium priority; documented API risk

[Publish call, line 97](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_mqtt.cpp#L97) · [Main loop, line 71](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/main.cpp#L71)

esp_mqtt_client_publish is called from the same loop that processes Wi-Fi state and parsed data. Espressif documents that this API may block for seconds. The return value, MQTT error events, and client-start failures are not recorded.

**Fix:** use bounded asynchronous publishing or a dedicated publisher with a latest-sample policy; check return values and record broker/TCP errors and reconnect counts. Avoid replaying an unbounded backlog of old live readings. Preserve the existing QoS 0/non-retained contract unless a separate change is agreed. Consider a separate availability/LWT topic and configurable unique client ID.

### 10. Record serial loss and publish coherent samples — medium priority; source-confirmed

[UART result queue, line 59](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_serial.cpp#L59) · [Variable commit, line 118](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/components/jsvarstore/jsvarStore.cpp#L118)

The 14-entry queue silently drops events when full. Events carry only names, so later reads can fetch a newer value than the event represents. The parser reports a successful variable event even when its store mutex acquisition failed. Non-data UART events all receive the same parser reset.

**Fix:** distinguish FIFO overflow, buffer-full, framing and other events; count drops/lock failures; hand off valid data with a timestamp/sequence or explicitly coalesce latest values. Check queue/task/driver creation results. Bound UART reads explicitly to sizeof(rxBuf) as defensive hardening.

The current driver ring buffer and local receive buffer are both configured as 1024 bytes. I have not established a UART receive-buffer overflow in this configuration; the reproduced memory error is in the SBMS decoder, not the UART read.

### 11. Add bounded, useful fault evidence — medium priority; missing capability

[MQTT event handler, line 12](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_mqtt.cpp#L12) · [Current debug endpoint, line 272](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_httpd.cpp#L272)

Add a diagnostic response that excludes credentials and contains uptime, reset reason, Wi-Fi association/IP state, RSSI, last disconnect reason, reconnect count, MQTT connection/error state, last valid UART-frame age, dropped/invalid frames, minimum free heap, largest free block and task stack margins.

Keep a small RAM event ring with monotonic timestamps and bounded response size; avoid continuously writing flash. Make this readable after reconnection so the Pi monitor can correlate faults. Keep routine logging off UART0 because it shares the SBMS connection. No sensor payload recording is needed.

### 12. Distinguish network connection from fresh sensor data — medium priority; source-confirmed

[Browser stream status, line 72](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/frontend/app.js#L72) · [Variable reads, line 185](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/components/jsvarstore/jsvarStore.cpp#L185)

The dashboard marks itself live as soon as SSE opens, before receiving valid SBMS data. There is no sample-age timeout; old readings can remain looking live. getVar also returns cached values without testing their age.

**Fix:** track last valid sample time, show waiting/live/stale distinctly, and retain stale values only with explicit age/status. Keep diagnostics separate from the existing MQTT payload unless compatibility is established.

## Smaller corrections and development safeguards

### 13. Correct edge cases and settings UI feedback — medium/low priority

- [Cell delta, line 62](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/sbms_json.cpp#L62): if all cells are zero, the calculation produces -65535 mV. Reproduced using its arithmetic. Return a defined no-valid-cells result without changing normal active-cell calculations.
- [JSON output, line 115](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/main.cpp#L115): check document allocation/overflow and output length before publishing the fixed 2000-byte buffer.
- [Settings form, line 177](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/frontend/app.js#L177): failed loads are hidden and the blank form remains saveable. Show a load error and prevent accidental replacement until a successful load or deliberate reset. Add port/hour/SSID validation and clear save outcomes.
- [Cell history, line 31](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/cell_minmax.cpp#L31): history intentionally covers roughly 23–24 hours, resets on reboot, and also clears at 32-bit millisecond rollover (about 49.7 days). Preserve this lightweight feature, document its limits, and use 64-bit uptime if continuous history across rollover is wanted.

### 14. Keep email optional and harden it later — lower priority for Ella

[Email writes, line 180](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/app_email.cpp#L180) · [Daily scheduling, line 96](https://github.com/colonel-lp/electrodacus-esp32/blob/6dc4e045095f28d347e3754c60b86aa6ba145b71/main/main.cpp#L96)

A zero-byte write can make conn_write loop without progress; TLS WANT_READ/WANT_WRITE retries have no overall deadline. Daily scheduling marks the day done before dispatch/success, so a temporary failure can suppress that day's report. Parallel test/daily requests use an unsynchronised sending flag.

**Fix:** enforce send deadlines and zero-progress handling, synchronise single-send ownership, distinguish queued/sent/failed, and allow a bounded retry policy. Ensure valid system time before certificate validation; this source contains no clock synchronisation setup. Validate SMTP fields and keep certificate verification on. STARTTLS is not implemented; label supported email modes accurately. Leave email disabled unless wanted.

### 15. Add regression tests, reproducible builds and migration documentation — medium priority

The repository CI builds firmware but contains no functional regression suite. ESP-IDF CI selects v5.3.2 while the component manifest broadly accepts >=5.0; dependencies are not fully locked in this snapshot. The README still describes Vue, links cloning the original project and contains outdated update instructions.

**Fix:** pin a tested SDK/dependency set, record firmware provenance, correct installation/provisioning/update instructions and document the two branches. Add meaningful tests for:
- valid/corrupt/truncated serial frames and parser recovery;
- settings validation, failed writes and recovery after interrupted saves;
- Wi-Fi fallback/retry timing, deliberate changes and uptime rollover;
- interrupted, wrong-type and oversized updates;
- repeated SSE connections and slow clients;
- broker outage/recovery, duplicate IDs and queue limits;
- identical existing MQTT fields/units/flags on recorded valid frames.

## Compatibility requirements for Ella

Preserve /Ella/sbms and /Ella/s2 when that prefix is configured. Preserve existing field names, current sign and units: cellsMV in mV, currentMA in mA, temperatures in °C, negative battery current for discharge, and all current status flags. The source retains those core JSON names; this is a source comparison, not an end-to-end compatibility test. The fork additionally supplies cellsMinMV and cellsMaxMV.

Keep PV1 and PV2 in mA at the firmware boundary; the Pi's conversion to amperes remains separate. Do not change battery protections or introduce MQTT control commands. These files implement the ESP32 communication module, not the SBMS protection firmware.

Do not hardcode the Netgear or a future Pi hotspot: retain configurable SSID/broker settings and test reconnection to either network. Finish the current Pi/Netgear baseline before flashing a modified ESP32 build.

## Verification completed and limits

- Inspected the firmware modules, parser/store, frontend, default settings, build configuration and CI at the pinned commit.
- Compared original main-branch partition CSV and relevant original MQTT/JSON handling.
- Compiled the unchanged decoder with host AddressSanitizer/UndefinedBehaviorSanitizer and reproduced the short-frame out-of-bounds read. Leak detection was disabled because this environment does not support LeakSanitizer under its process tracing.
- Reproduced unsigned Wi-Fi timing underflow and all-zero cell-delta arithmetic using the same expressions.
- Checked the ESP-IDF v5.3.2 Wi-Fi/HTTP APIs and the exact ESP-MQTT submodule header used by that SDK.

Primary API references:
- [ESP-IDF v5.3.2 Wi-Fi API](https://github.com/espressif/esp-idf/blob/v5.3.2/components/esp_wifi/include/esp_wifi.h)
- [ESP-IDF v5.3.2 HTTP session API](https://github.com/espressif/esp-idf/blob/v5.3.2/components/esp_http_server/include/esp_http_server.h)
- [ESP-MQTT header pinned by ESP-IDF v5.3.2](https://github.com/espressif/esp-mqtt/blob/cac1552e62b0474c162547b7cce345d7cd1aecfe/include/mqtt_client.h)

A complete ESP32 firmware build and hardware fault/stability tests have not been run. Fixes remain proposals; neither GitHub branch nor the installed firmware has been changed during this review.

