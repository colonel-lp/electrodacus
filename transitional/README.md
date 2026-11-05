# Ella transitional firmware — application only

This is the first, conservative step from the installed Arduino-based
`0.4.1-3-g1d8f5ec`. It is based on upstream commit
`1d8f5ec6a86613d09cdd9cef91cffe9b9a56a0bd`. The complete helgef ESP-IDF rewrite
remains at the repository root, unchanged by this transition. **Build in this
directory for the first update; the root build is the later rewrite.**

## Scope

- Retains ArduinoOTA/espota on UDP port 3232 and the existing MQTT/sensor code.
- Adds firmware-resident recovery HTML and a terminal-compatible HTTP updater.
- Exports the original settings file contents without changing their schema.
- Reports the installed partition table, running/selected/alternate slots,
  reset reason, boot attempts, health state and installed bootloader identification.
- Starts boot tracking before mounting SPIFFS, loading settings or starting the
  SBMS UART task. A missing/unmountable filesystem enters recovery without formatting.
- Three starts that fail to reach the 60-second health check cause recovery on
  the fourth start. Attempts are saved in a separate `ella_recovery` NVS namespace
  and survive ordinary resets and power cycles while NVS is writable. A new image gets its own count.
- Recovery starts an AP `SBMS-Recovery-XXXXXX`, password `electrodacus`, address
  `192.168.4.1`. It skips all legacy settings readers, UART parsing and MQTT.
  Recovery stays selected until an explicit normal-start request or new image.
- The health check requires the main loop and network to remain available for
  60 seconds. It does not depend on the router, MQTT broker or live SBMS data.
  It confirms an existing OTA pending-verification state when present, then
  clears the failed-start count when storage is writable. Storage errors are reported separately
  and do not block firmware repair or previous-slot selection. This is a liveness check,
  not full sensor validation; it checks network availability at the 60-second threshold.
- Allows explicit previous-slot selection only after full image verification.
  This is application-managed selection, not a guarantee of bootloader rollback.

No `/cfg` files, web assets, filesystem layout, partition table, bootloader or
SBMS protection firmware are changed. All settings writes and filesystem
updates are disabled, including ArduinoOTA `U_SPIFFS`. Wi-Fi defaults are
constructed only in RAM. Boot tracking writes a small record in the existing
NVS partition at startup/health confirmation or an explicit recovery action;
the recovery code never erases or migrates that partition. The inherited Arduino
core still initializes NVS before setup and can erase NVS on its NO_FREE_PAGES
initialization error; this transition does not alter that core behaviour. Thus
persistent boot tracking cannot be guaranteed when NVS itself is damaged.

Normal operation honours existing `ota_limit` and `ota_arduino` settings. Missing
keys default to enabled ArduinoOTA and a five-minute update window. Recovery
always enables ArduinoOTA and HTTP firmware uploads. The time-window check is
on the actual upload start; an accepted upload can finish within its bounded
transfer window. The existing LAN administration access remains unauthenticated;
the confirmation header prevents cross-origin browser form submissions, not
access by another LAN client. Settings exports contain passwords.

## Build and artifacts

The framework and network libraries are pinned in `platformio.ini`. The original
mixed Arduino/ESP-IDF build is retained for this transition; it does not replace
the standalone ESP-IDF project at the root. Application version: `ella-transition-2`.

From the repository root:

```sh
python3 -m pip install platformio==6.1.18
pio run --project-dir transitional --environment ota
python3 transitional/tools/check_firmware.py transitional/.pio/build/ota/firmware.bin
```

The `Ella transitional firmware` workflow builds the firmware and publishes only
`firmware.bin`, its SHA-256 and the application ELF in `ella-transitional-app-only`.
The root `ESP-IDF CI` workflow is a separate build of the later rewrite.
Do not confuse its artifacts with this transition. There is no serial upload
environment in this project. `buildfs`, `uploadfs` and erase targets are blocked.

## First upload from the installed firmware

Before uploading, save copies of the current `/cfg/wifi`, `/cfg/mqtt`, `/cfg/data`
and `/cfg/sys` responses using the installed firmware's existing read endpoints.
Keep those backups private. The installed firmware has no new combined-export
endpoint until this application has booted successfully.

Use the existing working terminal method, **application only**:

```sh
pio run --project-dir transitional --environment ota --target upload --upload-port DEVICE_IP
```

Do not run `uploadfs`, erase flash or upload merged/bootloader/partition-table
images. Do not flash the root ESP-IDF rewrite as this first step. No command in
this repository is automatically run against the installed device.

**The first upload cannot be guaranteed recoverable over Wi-Fi.** None of this
application's recovery code can help before it runs. A crash during core startup
or before the boot record is written, an early hang without a reset, a broken
network driver, incompatible flash hardware or an incompatible installed
bootloader can still make it unreachable. USB access to the SBMS does not by
itself establish direct access to the ESP32 ROM bootloader. The installed
bootloader's rollback support remains unverified; status reports this explicitly.
No bootloader change or automatic previous-image rollback is attempted.

## Recovery and terminal endpoints

Use `http://DEVICE_IP/recovery` in normal mode, or connect to the recovery AP and
open `http://192.168.4.1/recovery`. The HTML, status and updater do not use SPIFFS.
If SPIFFS fails to mount, settings cannot be exported but firmware recovery still
runs. In recovery mode, MQTT and live sensor pages intentionally stop.

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/recovery` | Embedded recovery page |
| GET | `/recovery/status` | Partitions, boot state and update diagnostics; no credentials |
| GET | `/recovery/settings` | Download exact settings text in `ella-settings-raw-v1` JSON |
| POST | `/recovery/firmware` | Raw `application/octet-stream`, exact Content-Length |
| POST | `/recovery/enter` | Save recovery request and restart |
| POST | `/recovery/resume` | Clear recovery request and restart normally |
| POST | `/recovery/previous` | Verify and select the alternate app slot, then restart |
| GET | `/update` | Redirect to the embedded recovery page |
| POST | `/update` | Reject old multipart/filesystem updater |

Examples, replacing `DEVICE_IP` with the real address:

```sh
curl --fail http://DEVICE_IP/recovery/status
curl --fail --output ella-sbms-settings.json http://DEVICE_IP/recovery/settings
curl --fail -X POST -H 'X-Ella-Confirm: enter' http://DEVICE_IP/recovery/enter
curl --fail -X POST -H 'X-Ella-Confirm: resume' http://192.168.4.1/recovery/resume
curl --fail -X POST -H 'X-Ella-Confirm: previous' http://192.168.4.1/recovery/previous
curl --fail -X POST -H 'X-Ella-Confirm: firmware' \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @transitional/.pio/build/ota/firmware.bin \
  http://192.168.4.1/recovery/firmware
```

Wait for `healthy: true` before uploads, manual restarts or previous-slot requests.
Use the page’s **Refresh status** button after the first 60 seconds. A
pending bootloader verification state could otherwise cause an unexpected
rollback on reboot. Both update transports use the inactive application slot.
HTTP checks the complete body length, image prefix, project/chip, SDK full-image
verification and boot-selection result. It aborts on disconnect, a 15-second
idle timeout, a five-minute total timeout, incorrect chunk sequence or a flash
error. Only one upload/slot-selection action can run at a time. ArduinoOTA keeps
its existing MD5 transfer check. A linker-wrapped SDK boot selector checks the
application/project and full image **before** changing the boot selection and
before ArduinoOTA sends its success response. The CI checks the actual linked
ELF to confirm Arduino Update calls this validator. A failed selection is
reported as a failed transfer; no select-then-restore workaround is used.

`/recovery/previous` means the **alternate current slot**, not a permanent rescue
image. The next attempted OTA writes that slot, so an interrupted upload can
destroy its old contents even though the running application remains intact.
No filesystem backup is automatically restored. The export is a backup only;
this transitional build deliberately provides no import/settings-write route.

## Degraded boot storage and bootloader information

`boot_record_writable` and `boot_record_warning` report persistent-storage health
independently of `healthy`. When boot storage fails, the recovery service still
allows validated firmware updates and selection of a verified previous image.
Explicit normal/recovery restart requests also carry an image-bound, one-shot
request in RTC memory. It is accepted only after a software reset, never after a
crash, watchdog reset or power loss. A power cycle with failed NVS can therefore
return to recovery. RTC is not a replacement for persistent failed-boot tracking.
No storage error prompts an automatic NVS erase in the recovery service.

The page and `/recovery/status` display `installed_bootloader.idf_version`, separate
from `application_idf_version` (the older `idf_version` alias is retained).
Identification reads only the original ESP32 bootloader region at 0x1000–0x7fff.
It validates image boundaries/checksum and decodes a recognised bootloader
descriptor where available, including its independent numeric project version.
For older images, one unambiguous version string plus a bootloader log marker is
reported as **inferred from embedded text**. Multiple candidates, stripped logs,
read failures, encryption or unrecognised images produce **Unknown**. This never
substitutes the application's SDK version and never proves rollback support.
There is no bootloader download or write endpoint.

## Tests and installation gate

Run host tests without a device:

```sh
bash transitional/tests/run_host_tests.sh
```

Before installing the later ESP-IDF rewrite, record a deliberate hardware test:

1. Check normal MQTT topics, units/signs and sensor readings against the Pi.
   Check the original dashboard still loads from the retained filesystem.
2. Download the combined settings backup; compare each string in `files` with
   the pre-update original. Check `complete: true`, partition offsets/sizes and
   `healthy: true`. Keep the bootloader rollback result as unverified until
   separately established.
3. Request recovery; join its AP and verify the embedded page, status, ArduinoOTA
   availability and export. Request a normal start and check MQTT returns.
4. Verify that a filesystem upload and settings write are rejected without
   changing the retained files. Test corrupt/wrong/oversized and interrupted
   firmware uploads on spare hardware before attempting them in the installation.
5. On spare hardware, simulate three failed starts, a missing/unmountable
   filesystem and power interruptions; verify the fourth boot enters recovery.
   Also inject boot-record open/write/commit failures: recovery uploads, previous-slot
   selection and one-shot normal restart must remain usable after the health check.
   Test a valid ESP32 image with a different project through espota: it must fail
   without changing the selected boot slot. Host tests cover policy and call order,
   not actual flash/network hardware behavior.
6. Inspect `previous_image_valid` before any further upload. Previous-slot
   selection reboots into the old firmware, which may not contain this recovery
   service. Test that deliberately only when its original updater is available.

Device flashing and deliberate crash/power-loss tests require separate access
to the installation and have not been performed by the source-edit workflow.
Do not treat a successful build or host test as proof of Wi-Fi recovery.
