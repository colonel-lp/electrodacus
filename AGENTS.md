# Ella ESP32 firmware — repository instructions

These instructions apply throughout `colonel-lp/electrodacus`. The active working branch is `ESP32-development` (renamed from `Ella-Monitoring`).

## Authorization and communication

- **If unsure, ask questions.** Clarify unclear requirements, scope, branch, firmware base, behavior or delivery before taking dependent action. Do not silently choose an interpretation that changes what the owner requested.
- **Do not start work unless explicitly told to do so.** Questions, discussion, suggestions, investigations and proposed improvements do not authorize implementation, firmware builds, installation or the next stage.
- Carry out only the task authorized in the current conversation. A request to explain or review authorizes that explanation or review; a request to update documentation authorizes the documentation only. Stop at the requested result.
- Within an explicitly authorized task, complete the necessary steps without repeatedly asking for the same permission. GitHub access does not expand the authorized scope.
- Keep replies concise and concrete. Distinguish facts, assumptions and unresolved questions. Report blockers promptly.
- Record proposed changes in the agreed pending-work document when asked; do not implement them merely because they have been listed. Before implementation, read the latest agreed scope, corrections and exclusions.

## Repository and firmware bases

- Work on `ESP32-development` unless the owner explicitly selects another branch. Preserve the other firmware base branches.
- The repository root contains helgef's standalone ESP-IDF rewrite: `main/`, `components/`, `frontend/`, `data/`, root CMake and SDK configuration.
- `transitional/` contains a separate build based on the original Arduino/ESP-IDF firmware. Its ArduinoOTA and recovery additions are not integrated into the root rewrite. Never describe those additions as present in helgef's firmware.
- Read `transitional/README.md` and `transitional/WORK-CHECKLIST.md` when working on the transition. Use the current build files and operating instructions for exact commands.
- Do not switch firmware bases, merge the two implementations, move their source, or port features between them without an explicit request. When carrying agreed behavior over, inspect and preserve its working implementation rather than inventing a replacement.
- Do not modify related repositories without a request. The EQ-DSP and PicoData instruction files informed this workflow; their Android, APK, DSP and Pi-specific rules do not apply here.

## Source, builds and installation

- Source edits, firmware compilation and device installation are distinct actions. A source request does not authorize flashing a device. A build request does not authorize flashing, erasing, filesystem migration or bootloader/partition-table changes.
- Commit and push authorized changes by default unless local-only or download-only delivery is requested. Keep compilation and binary publication within the requested scope; account for existing CI triggered by a push.
- Documentation-only work needs no firmware rebuild, version increment or application test suite. Avoid unnecessary CI builds for documentation-only commits.
- Advance firmware versions for an authorized new firmware release; do not increment them for documentation, recovery of completed work or publication retries.
- The transitional update is application-only. Preserve the existing filesystem, settings files/schema, partition layout and installed bootloader unless a later change is explicitly authorized.
- Do not enable filesystem uploads, automatic formatting, settings migrations or erase targets as a convenience. Preserve updater restrictions, validation, concurrency guards, abort paths and verified previous-slot selection.
- The working installation has Wi-Fi and USB access; rear board access is unavailable. Do not assume the SBMS USB connector provides ESP32 ROM flashing or GPIO0 access.
- Recovery depends on application code running. Preserve the distinction between application recovery, manual alternate-slot selection and installed bootloader rollback. Never claim guaranteed Wi-Fi recovery or verified bootloader support without evidence.
- Keep ArduinoOTA/espota and HTTP update behavior clear for each build. Do not remove an agreed updater or fallback without approval.

## Monitoring compatibility

- This repository implements the ESP32 communications module, not SBMS battery-protection firmware. Do not change battery protections or introduce control commands without a request.
- Preserve configurable Wi-Fi and MQTT settings. Do not hardcode a router, hotspot, broker, device address or private credentials.
- Preserve existing MQTT topic construction, field names, status flags, units, polarity and calculations unless a change is explicitly agreed. With the configured Ella prefix, retain `/Ella/sbms` and `/Ella/s2`.
- Keep `cellsMV` in millivolts, `currentMA` in milliamps, temperatures in Celsius and negative battery current for discharge. Preserve all four current fields. Pi-side unit conversions remain separate.
- Preserve existing valid-frame output and live-data behavior. Missing, malformed or stale data must not silently become fresh zero readings.
- UART0 is the SBMS data link. Keep routine diagnostics and debug output off that link.

## Working and publication procedure

- Read current instructions and only task-relevant files. Search with `rg` and read targeted ranges; do not routinely load entire repositories, histories or successful logs.
- Verify current branch/file identity before edits and check the remote head before publication. Use current file SHAs or guarded branch updates. Preserve concurrent owner changes; never blindly overwrite or force-push over them.
- Batch independent reads and bounded uploads. Keep dependent edits, commits and branch updates sequential. Inspect every result; combine related files in a coherent commit rather than one commit per file.
- For multi-step work, save a local checklist, baseline/destination details and publication receipts. Complete one stage at a time and record actual progress so another session can resume.
- Keep large text, binary/base64 data and logs on disk or inside programmatic execution. Return compact status, paths, hashes and IDs; never print encoded files into chat to construct uploads.
- Prefer an available authenticated Git workflow or the connected GitHub tools. Connector access does not authenticate shell Git; never extract connector credentials.
- Reuse confirmed blobs and uploads. After an ambiguous write, make one compact status read before retrying. Allow at most one retry after a concrete correction or confirmed transient fault; if it fails again, stop that publication stage and report the completed work, remaining files and blocker.
- Resume unfinished work without rebuilding completed firmware or re-uploading confirmed objects merely to recover or retry publication.
- Never create ZIPs or run archive-generation helpers for source, firmware, scripts or recovery. The owner creates archives.
- Do not commit credentials, keys, private captures, installation-specific network/device details, local SDK paths or routine build output. Preserve upstream attribution and applicable license notices.

## Validation and documentation

- Use focused checks appropriate to the authorized change. Protocol, persistence, boot/update and recovery changes need meaningful checks of affected behavior and failure paths. Broaden testing only for a concrete failure or unresolved concern.
- Preserve settings, valid MQTT output and recovery/update invariants when making changes. Do not weaken checks or change SDK targets merely to silence failures or warnings.
- Use current official Espressif/framework/library documentation when API details are needed. Respect the SDK pinned by the particular build; do not assume newer APIs exist in the transitional SDK.
- Distinguish source inspection, host tests, simulations, cross-compilation, artifact verification and real device testing. Report only checks actually completed; identify the tested source revision and material limits.
- Keep one canonical document per topic. Update affected instructions and validation/checkpoint records with actual results; keep proposals and uninstalled changes clearly marked as pending.
- Preserve necessary operating/update/recovery commands, hashes, compatibility requirements, backups and evidence limits. Keep public documents free of private device readings, inventory, addresses and runtime/process snapshots.
- Do not claim firmware was flashed, recovered over Wi-Fi or hardware-tested unless that happened. When a build or transfer cannot complete, say so and preserve the completed work.

