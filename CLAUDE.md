# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

PlantPulse is **ESP32-S3 firmware** (ESP-IDF, C) for a battery-powered soil-moisture sensor. The device provisions over **BLE**, joins WiFi, reads soil moisture (ADC) + battery telemetry (MAX17048 fuel gauge over I2C), POSTs readings to `athome.rodlandfarms.com`, checks for OTA firmware updates, then **deep-sleeps for 8 hours** and repeats. It is a standalone repo (`github.com/c1scok1d/plantPulse`), unrelated to the web apps described in the parent `~/Desktop/CLAUDE.md`.

## Build / flash / monitor

Requires the ESP-IDF toolchain on PATH (`. $IDF_PATH/export.sh` first). Target chip is **esp32s3**.

```bash
idf.py set-target esp32s3      # only if build/ is fresh; sdkconfig is gitignored
idf.py build                   # compile
idf.py -p /dev/ttyACM0 flash   # flash (adjust serial port)
idf.py -p /dev/ttyACM0 monitor # serial console; Ctrl-] to exit
idf.py -p /dev/ttyACM0 flash monitor   # common combo
idf.py fullclean               # wipe build/ when CMake/config gets stuck
```

There is **no test suite** and no lint config — this is bare ESP-IDF firmware. Verification is done on-device over the serial monitor (logs are heavily `ESP_LOGI`-instrumented per subsystem TAG).

Partition layout is **custom** (`partitions.csv`, set via `PARTITION_TABLE_FILENAME` in the root `CMakeLists.txt`): factory app + `ota_0` + `ota_1`, each 2 MB, to support OTA. `sdkconfig.defaults` enables NimBLE (BLE) and the single-app-large partition base. `sdkconfig` itself is gitignored — it is regenerated from defaults + `set-target`.

## Architecture & control flow

Source lives under `main/` (one IDF component, registered in `main/CMakeLists.txt`); public headers in `include/`. Everything shares one global `main_struct` (`include/main.h`) holding provisioning state, WiFi creds, sensor name/location, API token, and hostname.

**Boot decision (`app_main`, `main/main.c`):**
1. Configure button GPIO (GPIO6) + ADC1_CHANNEL_4 (GPIO5, soil probe); start `monitor_button_press` task.
2. `nvs_init()` then `read_from_nvs(...)` to load saved credentials.
3. **If no credentials** → `ble_advert()`: start NimBLE provisioning (blinking LED on GPIO2 signals "waiting for config").
4. **If credentials exist** → skip BLE, spawn `check_credentials` task → `wifi_init()`.

**BLE provisioning path:** advertises as `Plant Pulse <last-4-of-MAC>` with custom 16-bit GATT service `0xFEF3`. Phone reads the hostname characteristic (`0xFEF9`) and writes a JSON blob (`{ssid, password, sensor_name, sensor_location, api_token}`) to the config characteristic (`0xFEFA`). `device_write` accumulates BLE write chunks into `json_buffer` until it sees a closing `}`, then `parse_json()` persists everything to NVS. **MTU is bumped to 256** so the api_token fits. A BLE disconnect triggers `esp_restart()` (so the device re-reads NVS and proceeds to WiFi).

**WiFi + measurement path (`main/wifi_driver/wifi_drv.c`):** STA mode, 8 retries. On `IP_EVENT_STA_GOT_IP` it runs SNTP time sync (blocks in `wait_for_time_sync`) then calls `monitor()`. **On final WiFi failure it falls back to `ble_advert()`** for re-provisioning. There's a compile-time `STATIC_PASSWORD`/`STATIC_SSID` escape hatch (commented out) for hardcoding creds.

**`monitor()` (`main/sensor_data/data.c`) is the per-wake cycle:** `check_update()` (OTA) → init I2C → `getBattery()` (MAX17048 at I2C addr 0x36, pins SDA=16/SCL=17) → `readMoisture()` (ADC raw mapped 3600→0%, 2130→100% via `map()`) → `uploadReadings()` → 3s delay → `enter_deep_sleep(EIGHT_HOUR_SLEEP)`. **Sleep duration is selected by uncommenting one of the `enter_deep_sleep(...)` calls** (1 min / 1 hr / 8 hr / 12 hr — see `SleepDuration` enum in `main.h`). Deep sleep resets all GPIOs except the button (GPIO6, ext0 wake) for power saving; the RTC timer also wakes it.

**Upload (`uploadReadings` → `POST` in `main/rest_methods/rest_methods.c`):** spawns a FreeRTOS task that builds an `application/x-www-form-urlencoded` body (`api_token, hostname, sensor, location, moisture, batt, battery_status`) and POSTs to `http://athome.rodlandfarms.com/api/esp/data?`. Moisture and battery are clamped to ≤100.

**OTA (`check_update`/`perform_ota_update` in `main.c`):** GETs `firmware.json`, compares its `version` string against the hardcoded `current_version_number`, and on mismatch runs `esp_https_ota` against `firmware.bin`. TLS uses the pinned root cert in `include/cert.h` (`cert_pem2`).

**Button (`monitor_button_press`):** long press (≥3 s) → `erase_nvs_data()` + reboot (factory-reset provisioning). Short press is currently a no-op (action commented out).

## Conventions & gotchas

- **Per-subsystem log TAGs**: most functions shadow `TAG` locally (e.g. `"BLE_WRITE"`, `"OTA_CHECK"`, `"DATA"`). The global `char *TAG = "-"` is the fallback. Keep this pattern when adding code so serial logs stay greppable.
- **Provisioning JSON keys are a hard contract** with the companion mobile app: `ssid`, `password`, `sensor_name`, `sensor_location`, `api_token`. Don't rename without updating the app.
- **Hardware pin map is fixed in code** (not Kconfig): button GPIO6, soil ADC GPIO5/ADC1_CH4, LED GPIO2, I2C SDA16/SCL17, MAX17048 @ 0x36. Changing the board means editing these `#define`s.
- The known-rough spot is the **chunked-response branch of `check_update`** (`main.c`) — its buffer realloc/`memcpy` logic is buggy; the non-chunked branch is the working path. The server currently returns non-chunked.
- Endpoints are split: OTA JSON/bin and data upload all point at `athome.rodlandfarms.com` but mix `http`/`https` — the data POST is plain HTTP, OTA is HTTPS with the pinned cert.
- `sdkconfig.old` is a stale backup; `sdkconfig` is gitignored. Don't commit either.
