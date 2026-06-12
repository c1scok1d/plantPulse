# PlantPulse implementation roadmap

Product-wide roadmap covering both halves: the ESP32-S3 firmware
(`github.com/c1scok1d/plantPulse`) and the Flutter app
(`github.com/c1scok1d/Plant_Pulse`). Priorities are severity/sequence-ordered,
not effort-ordered.

## Recommendations (themes)

1. ~~**Provisioning secrets travel in the clear — highest standing risk.**~~
   **✅ RESOLVED (2026-06-12).** Both cleartext-credential paths are closed:
   telemetry upload is now HTTPS (P1, commit `35cc0c3`), and BLE provisioning is
   now encrypted/bonded (`0xFEFA` is `F_WRITE_ENC`, verified on hardware — see P1).
   The WiFi password and api_token no longer travel in the clear on either path.
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

## P0 — Prove the current change works — ✅ DONE (2026-06-11)

- **End-to-end BLE verification on hardware — ✅ DONE.** Two V5 boards
  (`48CA43BBC5F4`, `48CA43BBC654`) were provisioned from the app over the new BLE
  contract (`0xFEF3` / JSON → `0xFEFA`) and readings reached the backend. The
  protocol realignment is confirmed on real hardware, not just by code review.
- **`power_source` / `charge_status` — ✅ verified end-to-end.** USB↔Battery toggle
  flips the reported value correctly (firmware GPIO13/14 → POST → backend → API).
- **Deep-sleep reboot loop — ✅ FOUND & FIXED** (commit `1ffe7da`; see P2 and
  `docs/DIAGNOSIS-2026-06-11.md`). The 8 h sleep had never actually worked on V5.
- **App provisioning confirmation (still open):** subscribe to the hostname notify
  characteristic `0xFEF9` so the user sees "provisioned ✓" instead of inferring
  success from a BLE disconnect.

## P1 — Close the credential exposure

- **Telemetry upload → HTTPS — DONE (commit `35cc0c3`).** The data POST now uses
  `https://` with `esp_crt_bundle_attach` (same root-CA path as OTA); server confirmed
  accepting it. Was plain HTTP with the api_token in the body.
- **JSON reassembly hardened — DONE (commit `35cc0c3`).** BLE config reassembly now
  commits on `cJSON_Parse` success (string-aware) instead of a trailing `}`, so a `}`
  inside a value at a chunk boundary can't trigger an early/failed parse.
- **BLE provisioning encryption — ⚠️ ENCRYPTION HANDSHAKE VERIFIED; FULL PROVISIONING
  NOT YET CONFIRMED (2026-06-12).** The crypto layer is in and demonstrably works:
  `0xFEFA` (config write) is `F_WRITE_ENC` and `0xFEF9` (hostname read) is `F_READ_ENC`,
  with LE Secure Connections + bonding (`sm_sc=1`, `sm_bonding=1`,
  `sm_io_cap=NO_INPUT_OUTPUT` → Just Works). The app pairs by **reading `0xFEF9` first** —
  the encrypted read forces the link up — then writes the config over the now-encrypted
  link (firmware commits `08c225f`/`b0cb113`/`f70e254`; app `c27cdac`). On a V5 board
  (`48:CA:43:BB:C6:9E`) + Galaxy S23 (Android 16) the encrypted read returned the hostname,
  the config write was accepted, and the **phone formed a bond** (confirmed via
  `dumpsys bluetooth_manager` — bonded under `com.rodlandfarms.plantpulse`). So secrets no
  longer travel in the clear over BLE. Firmware build `1781232142`.
  - **BUT the end-to-end cycle is still failing (2026-06-12, evening).** After the
    encrypted config write the device does **not** complete provisioning as expected, and a
    **deep-sleep boot loop may have regressed** (cf. the P2 `1ffe7da` fix — possibly
    reintroduced by the encryption-path reboot changes in `b0cb113` "don't reboot until
    provisioned"). **Next session: attach the serial monitor to the bench device** during a
    provision and watch the post-write path (config save → `esp_restart` → WiFi join →
    upload) to find where it stalls/loops. Do NOT treat this item as closed until a real
    reading from a freshly-provisioned device lands in the backend.
  - Residual (separate from the above): Just Works gives no MITM protection (encryption vs.
    passive eavesdropping only) — acceptable for an IO-less sensor; revisit if a
    display/keypad is ever added.

## P2 — Robustness & operability

### Confirmed firmware bugs (observed on-device 2026-06-11 — see `docs/DIAGNOSIS-2026-06-11.md`)

- **Deep-sleep reboot loop — FIXED (commit `1ffe7da`).** `enter_deep_sleep()` reset
  *all 48 GPIOs* (incl. USB IO19/20 and the in-package SPI-flash pins) right before
  sleeping, crashing the chip before `esp_deep_sleep_start()` → reset every ~10 s; the
  8 h sleep never actually worked on V5. Removed the loop (IDF auto-isolates GPIOs in
  sleep already); kept ext0 button-wake with an RTC pull-up + hold on IO3. Verified on
  two boards via OTA `1781229549` (they now sleep instead of looping). **Rule: never
  `gpio_reset_pin()` USB/flash/PSRAM pins.**
- **V5 pin map — FIXED (commit `1e07b93`).** Button moved GPIO6→**IO3** (`SW1`), status
  LED GPIO2→**IO34** (`LED2`), per the EasyEDA V5 source. LED blink and factory-reset
  long-press both confirmed on-device.
- **OTA TLS — FIXED (commit `c3baf43`).** Was failing with
  `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED (0x8017)` against a stale pinned cert. Now uses
  `esp_crt_bundle_attach` (the ESP-IDF root-CA bundle, already in sdkconfig) — no
  pinning, trusts the new host's Let's Encrypt chain, survives LE rotation. The old
  `cert_pem*` arrays in `include/cert.h` are now unused.
- **OTA hosting — REBUILT on the new host.** `firmware.json`/`firmware.bin` were
  static files in SiteGround's docroot (not part of the Laravel app). Now served
  from `backend/ota/` bind-mounted into the container's `public/`, at
  `https://athome.rodlandfarms.com/firmware.{json,bin}`. **`version` is now a
  STRING** (firmware checks `cJSON_IsString`; the old JSON used a number, so OTA
  never fired). Current published build: **`1781229549`** (V5 deep-sleep fix). Publish
  a new one: drop a new `ota/firmware.bin` + bump the version string in
  `ota/firmware.json` — no rebuild. Versions are **unix timestamps** so a release is
  self-dating. Confirmed OTA works in practice: both bench devices pulled and applied
  `1781229549` over the air.
- **OTA version compare — FIXED (commit `35cc0c3`).** Now monotonic: versions are unix
  timestamps and `check_update` only updates when server > current (no downgrade).
  Replaced the buggy chunked branch (read into an unallocated pointer; self-`memcpy`)
  with one small fixed-buffer read that handles both response types; uncommented
  `esp_http_client_cleanup`.
- **Undersized HTTP response buffer — FIXED (commit `35cc0c3`).** The POST handler no
  longer copies the response body into a fixed 2 KB stack buffer (no bounds check →
  overflow + ~5 s wasted wake on large error pages). The upload only needs the status
  code, so the body is ignored.
- **Provisioned-✓ notify — DONE (commit `061f3a2` fw / `d8eb599` app).** Firmware
  notifies the hostname on `0xFEF9` after saving config; the app subscribes, shows
  "provisioned ✓", and registers the device. (Internet-connectivity confirmation is a
  separate follow-up — see bottom.)

### Other P2

- ~~Fix OTA chunked branch + monotonic version compare.~~ **DONE (`35cc0c3`).**
- ~~Make sleep duration an NVS/config value.~~ **DONE (`35cc0c3`)** — `nvs_get/set_sleep_seconds`
  (default 8 h), settable via an optional `sleep_seconds` key in the provisioning JSON.
- ~~Resolve the `batteryInserted` TODO.~~ No such TODO remained in the code.
- ~~Minimal CI + BLE-contract regression test.~~ **DONE** — `firmware-build.yml`
  (ESP-IDF build) and `app-ci.yml` (`flutter analyze`/`test`); `test/ble_contract_test.dart`
  asserts the UUIDs + JSON keys match the firmware (contract extracted to
  `lib/ui/devices/ble_provisioning_contract.dart`).

## P3 — Maintainability

- **Flutter upgrade — STILL OPEN (needs device).** Phased 3.10→3.16→3.22 per the app
  repo's `docs/FLUTTER_UPGRADE.md`; fold the `wifi_iot → wifi_scan` swap in. Firebase /
  google_sign_in / vendored-formz are the landmines; needs on-device smoke tests each hop.
- ~~Delete dead `tflite_flutter` files + stale `install.bat`; drop unused models.~~
  **DONE (app `67f8cd9`)** — removed the 4 commented `tflite_flutter` files, `install.bat`,
  and 3 unused 23 MB `.tflite` models (~67 MB). Live ML path (`model_v4_3` via ML Kit) kept.
- ~~Remove `sdkconfig.old` from the firmware repo.~~ Done.

## Follow-ups / deferred

- **Verify internet connectivity at provisioning (deferred).** Beyond "config saved",
  confirm the device actually reaches the backend and surface "online ✓" in the app.
  Options: (A) app polls `GET /user/{hostname}/latest` until the first reading [recommended,
  testable]; (B) firmware WiFi/BLE-coex HTTPS check during the BLE session notifying
  ONLINE/OFFLINE on `0xFEF9`; (C) both. Builds on the `0xFEF9` hostname notify.

## Status (2026-06-12)

P0 ✅ done · P1 ⚠️ BLE-encryption **handshake** verified on hardware (bond + encrypted
read/write) but **full provisioning still failing** — possible deep-sleep boot-loop
regression; needs serial-monitor diagnosis on the bench device (see P1 detail) · P2 ✅
done · P3: app cleanup ✅, **Flutter upgrade in progress** — the
upgraded app now lives in a new repo `mobile_app/plantpulse_flutter/`
(`github.com/c1scok1d/plantpulse_flutter`, Flutter 3.24.5 / Dart 3.5.4, `wifi_scan`
swap folded in); installs and runs on the S23. A startup crash from the
`firebase_messaging` upgrade (inline `onBackgroundMessage` closure → null callback
handle) was fixed with a top-level `@pragma('vm:entry-point')` handler.

**Backend note:** `athome.rodlandfarms.com` was 500-ing on 2026-06-11 (PHP 8.2 +
old `nesbot/carbon`; see `docs/DIAGNOSIS-2026-06-11.md`) but is **back up as of
2026-06-12** — all endpoints return clean JSON. The remaining edge is that a device
provisioned during the outage may hold a stale `api_token`; re-login + re-provision
to refresh it. Migrating the backend onto the local fleet (`docs/BACKEND-MIGRATION.md`)
is still the durable fix for the shared-host PHP-auto-upgrade risk.
