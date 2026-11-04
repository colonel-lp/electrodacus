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
- [ ] Pass full ESP32 cross-build in GitHub Actions.
- [ ] Publish final tested source commit to `Ella-Monitoring`.
- [ ] Device installation and recovery tests (not performed in this workspace).

Bootloader/partition table are never uploaded by this transition. Installed
bootloader rollback support remains unverified. Broader ESP-IDF rewrite fixes
from the review remain later work; this transition does not install the rewrite.
