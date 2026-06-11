# PlantPulse implementation roadmap

Product-wide roadmap covering both halves: the ESP32-S3 firmware
(`github.com/c1scok1d/plantPulse`) and the Flutter app
(`github.com/c1scok1d/Plant_Pulse`). Priorities are severity/sequence-ordered,
not effort-ordered.

## Recommendations (themes)

1. **Provisioning secrets travel in the clear — highest standing risk.** The
   firmware config characteristic `0xFEFA` is `BLE_GATT_CHR_F_WRITE` with **no
   encryption flag**, so the WiFi password and API token are written over the air
   unencrypted during setup. Telemetry upload is also **plain HTTP**
   (`http://athome.rodlandfarms.com/api/esp/data`) with the `api_token` in the
   body. Two cleartext-credential paths.
2. **The BLE protocol change is unverified end to end.** The app↔firmware
   alignment (service `0xFEF3`, JSON → char `0xFEFA`) analyzes clean but has only
   been verified by reading code. A real provision-to-reading round trip is the
   gate.
3. **Latent firmware fragilities.** OTA `check_update` chunked branch is buggy
   (works only because the server returns non-chunked); `current_version_number`
   is a hardcoded string compared by equality; sleep duration is selected by
   commenting code in/out; the JSON parser commits on a trailing `}`.
4. **No tests, no CI in either repo** — the BLE contract especially deserves a
   regression guard so it can't silently diverge again.

## P0 — Prove the current change works

- **End-to-end BLE verification on hardware:** flash firmware → provision from the
  app → confirm a reading reaches the backend.
  - Prereqs (as of this writing): ESP-IDF not configured (`IDF_PATH` unset;
    `~/esp/esp-idf` present), user not in `dialout`, no board enumerated on USB.
  - See **`docs/SETUP.md`** for the IDF toolchain + flash/monitor setup.
- **App provisioning confirmation:** subscribe to the hostname notify
  characteristic `0xFEF9` so the user sees "provisioned ✓" instead of inferring
  success from a BLE disconnect.

## P1 — Close the credential exposure

- Gate `0xFEFA` behind BLE encryption / bonding (`F_WRITE_ENC`) so provisioning is
  not cleartext.
- Move telemetry upload to **HTTPS** (reuse the pinned-cert path already used for
  OTA in `include/cert.h`).
- Harden JSON reassembly (length-prefix or explicit end marker) so a `}` inside a
  password can't trigger an early/failed parse.

## P2 — Robustness & operability

- Fix OTA: repair or delete the buggy chunked branch in `check_update`; replace
  the hardcoded `current_version_number` with the build's real version and compare
  semver, not string equality.
- Make sleep duration an NVS/config value instead of a compile-time comment
  toggle (`SleepDuration` enum in `include/main.h`).
- Resolve the `batteryInserted` TODO from the firmware history.
- Minimal CI: `flutter analyze` + `flutter test` on the app, a compile check on
  the firmware, and one **BLE-contract regression test** asserting the app's
  service/char UUIDs and JSON keys match the firmware's.

## P3 — Maintainability

- Execute the Flutter upgrade (scoped in the app repo's
  `docs/FLUTTER_UPGRADE.md`); fold the `wifi_iot → wifi_scan` swap into it.
- Delete dead `tflite_flutter` classifier files and the stale `install.bat` in the
  app repo.
- Remove `sdkconfig.old` from the firmware repo.

## Sequencing rationale

P0 first because shipping an unverified protocol change is the active risk; P1
next because cleartext credentials are the highest-severity standing issue;
P2/P3 are debt stable enough to schedule.
