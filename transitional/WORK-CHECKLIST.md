# Ella transition implementation checkpoint

- [x] Confirm installed source `1d8f5ec6a86613d09cdd9cef91cffe9b9a56a0bd`.
- [x] Keep complete helgef ESP-IDF project at repository root.
- [x] Add separately buildable Arduino/ESP-IDF transitional application.
- [x] Preserve ArduinoOTA and existing sensor/MQTT conversion code.
- [x] Add embedded HTTP recovery/update page and settings export.
- [x] Add installed partition/slot and boot diagnostics.
- [x] Add early boot tracking and repeated-failure recovery AP.
- [x] Add verified alternate-slot selection and explicit normal/recovery reboot.
- [x] Disable filesystem uploads, automatic formatting and settings writes.
- [x] Add serialized upload ownership, complete-body checks and abort paths.
- [x] Pass host policy tests with AddressSanitizer/UndefinedBehaviorSanitizer.
- [x] Pass seven binary artifact validation tests.
- [x] Pass full ESP32 cross-build in GitHub Actions.
- [x] Publish final tested source commit to `Ella-Monitoring`.
- [ ] Device installation and recovery tests (not performed in this workspace).

Bootloader/partition table are never uploaded by this transition. Installed
bootloader rollback support remains unverified. Broader ESP-IDF rewrite fixes
from the review remain later work; this transition does not install the rewrite.

## Validation record — 4 November 2026

Source validated: `6727da3ce7e6eb36b09cc6ac8776e75adbe8cc52`.
Later documentation-only commits do not change that application source.

- [Transitional build and host tests](https://github.com/colonel-lp/electrodacus-esp32/actions/runs/41755341011): passed.
- [Retained root ESP-IDF build](https://github.com/colonel-lp/electrodacus-esp32/actions/runs/41755341068): passed.
- Static RAM: 50,180 / 327,680 bytes. Linked application: 1,007,868 / 1,310,720 bytes.
- Application OTA file: 1,007,984 bytes; artifact checksum and appended image SHA-256 verified.
- OTA file SHA-256: `7b182d055e9377741ada8d81a82e50ae70cc42ac1b55fe8928cdca061c36bea2`.
- Sanitized host recovery-policy assertions and all seven Python artifact tests passed.
- Source comparison confirmed unchanged `toJsonSBMS`, MQTT setup/connect/publish,
  MQTT/data settings readers, and the original parser libraries (apart from line endings).
- Root firmware/components/frontend/data source files remain unchanged from `6dc4e04`.
- No device was flashed. Network behavior, settings preservation on the installed
  unit, deliberate crash/power-loss recovery and bootloader rollback are not hardware-verified.
