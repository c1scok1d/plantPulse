# On-device diagnosis — 2026-06-11

End-to-end verification of a physical V5 board (`hostname 48CA43BBC5F4`) over the
serial console (115200, native USB `ttyACM0`). Captured the full provision →
boot → upload cycle.

## TL;DR

**The device, WiFi, sensors, and upload path all work. The "no reading in the
app" symptom is caused by the production backend being completely down** —
`athome.rodlandfarms.com` returns HTTP 500 on every endpoint due to a
PHP 8.2 / `nesbot/carbon` incompatibility. Two real (but non-fatal) firmware
bugs were also confirmed; see ROADMAP P1/P2.

## What the serial log proved works

- **WiFi + time sync:** connects, `Time successfully synchronized`.
- **Power is healthy — NOT brownout:** `Battery 3.77 V, SOC 42%`, `USB connected
  and Solar power detected`, reset reason `power-on event`. The earlier brownout
  theory is disproven.
- **Sensors:** `MOISTURE_SENSOR: Raw ADC 3567, Moisture 2%`; MAX17048 battery read OK.
- **Data upload is sent and the server accepts the connection:**
  ```
  MONITOR: Wi-Fi and internet are connected. Uploading data...
  POST http://athome.rodlandfarms.com/api/esp/data?
  Data: api_token=…&hostname=48CA43BBC5F4&sensor=African Violet&location=Livingroom&moisture=2&batt=42.00&charge_status=charging via both USB and Solar
  HTTP_EVENT_ON_CONNECTED → HEADER_SENT → Received header: Server: nginx …
  SLEEP: Entering deep sleep
  ```

The earlier USB `error -71` enumeration loop was a **cable/connector** problem
(another USB device enumerated fine on the same machine); once a good cable/port
was used, the native USB came up as `ttyACM0`.

## Root cause: backend is 500-ing (not the device)

Direct read-only GETs to the backend (mirroring what the app does) both return a
Laravel crash page:

```
GET /api/user/devices?api_token=…        → HTTP 500 (text/html)
GET /api/user/48CA43BBC5F4/latest?...    → HTTP 500 (text/html)

TypeError: Carbon\Carbon::setLastErrors(): Argument #1 ($lastErrors) must be of
type array, bool given
  …/RodlandFarmsAPI/vendor/nesbot/carbon/src/Carbon/Traits/Creator.php:98
  → Carbon::now() → Illuminate\Cache\FileStore->currentTime() → …
```

This is the well-known **PHP 8.2 + outdated `nesbot/carbon`** break: PHP 8.2 made
`DateTime::getLastErrors()` return `false` (not an array) when there are no
warnings; old Carbon declared `setLastErrors(array $lastErrors)` → fatal
TypeError. It fires inside `Carbon::now()`, which Laravel's cache calls on **every
request**, so the *entire* API is down. The host (`/home/customer/www/...`, cPanel
shared hosting) almost certainly auto-upgraded PHP to 8.2.

### How this explains every symptom

- Device `POST /api/esp/data` → backend 500 → returns the large Laravel **HTML
  error page** → the reading is **received but never stored** (endpoint crashed
  before saving). That HTML page is also what overflowed the firmware's response
  buffer (firmware bug #2 below).
- App `/user/devices` + `/user/{host}/latest` → 500 → app shows nothing.

### Fix (on the backend host, `RodlandFarmsAPI`)

```bash
cd .../athome.rodlandfarms.com/RodlandFarmsAPI
composer require nesbot/carbon:"^2.72"     # ≥2.62.1 fixes setLastErrors; 2.72+ is PHP 8.2-safe
php artisan cache:clear && php artisan config:clear && php artisan view:clear
```

This backend is **not** on the local fleet box — it's remote shared hosting. See
`docs/BACKEND-MIGRATION.md` for the option of moving it onto the local fleet
(which also permanently removes the shared-host PHP-auto-upgrade risk).

## Confirmed firmware bugs (logged in ROADMAP)

1. **OTA `check_update` fails** — `http://…/firmware.json` → 200 but
   `JSON_HANDLER: Error parsing JSON`; HTTPS fallback → `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED
   (0x8017)` → `create_ssl_handle failed` → `Failed to fetch version information,
   HTTP Status: 0`. Non-fatal (continues to upload) but OTA never works.
2. **Undersized HTTP response buffer** — hundreds of `W HTTP_EVENT: Response
   buffer overflow, truncating response` during the data POST because the server
   returned a large HTML body. Keeps the device awake spamming for ~5 s (wastes
   battery). Relates to the prior commit "removed response.buffer output to log".

---

# Update — deep-sleep reboot loop found & FIXED (later on 2026-06-11)

> Corrects the earlier note above. The line `SLEEP: Entering deep sleep` was
> **printed but the device never actually slept** — it crashed immediately after
> and rebooted, looping every ~10 s. The 8-hour sleep had effectively never worked
> on V5.

## Symptom

A freshly provisioned V5 board POSTed successfully every cycle but then reset
~10 s later instead of deep-sleeping 8 h. The backend showed readings every
~8–12 s. The native USB also kept dropping (no stable `ttyACM` enumeration during
the loop), which made serial capture nearly impossible.

## Dead ends ruled out (in order)

1. **ext0 button wake (IO3 floating).** IO3 (SW1) has only a 100 nF debounce cap
   and **no external pull-up**, so the theory was the pin floats low and `ext0`
   (active-low) fires instantly. Added an RTC pull-up → **no change.** Disabled
   ext0 entirely and set a 60 s timer → **still looped at ~10 s, not 60 s.** So
   ext0 was not the cause.
2. **Brownout / low battery.** Disproven: the loop also happened on USB power and
   on a healthy 89 %-SOC battery. A brownout would also print a distinct
   `Brownout detector was triggered` line; it never did.
3. **Short sleep timer.** `enter_deep_sleep(EIGHT_HOUR_SLEEP)` was correctly
   selected and the µs math was fine.

## Root cause

`enter_deep_sleep()` ran a loop that called `gpio_reset_pin()` on **all 48 GPIOs**
"to reduce leakage" just before `esp_deep_sleep_start()`. Serial capture (once the
ext0-disabled build let USB stay up a bit longer) showed the loop logging
`gpio: GPIO[2] … GPIO[18]` and then the device vanishing — it was resetting the
**USB pins (IO19/20)** (killing the console) and, fatally, the **in-package SPI-flash
pins**. Resetting the flash pins while executing from flash crashes the chip → reset
→ repeat. It never reached `esp_deep_sleep_start()`.

This single bug explains every symptom: the ~10 s loop, the USB dropping each cycle
(so serial/flash were unreliable), and why ext0/brownout/timer fixes all failed.

## Fix (commit `1ffe7da`)

- **Removed the all-GPIO reset loop.** ESP-IDF already isolates GPIOs in deep sleep
  (`sdkconfig: Configure to isolate all GPIO pins in sleep state`), so the loop was
  redundant as well as dangerous.
- **Kept ext0 button-wake** (SW1/IO3) with `rtc_gpio_pullup_en` + `rtc_gpio_hold_en`
  across sleep (and `rtc_gpio_hold_dis` at boot) so the pin holds high and only wakes
  on a real press.

## Verification

- 60 s test build → backend cadence went from ~10 s to a steady **~70 s** (60 s
  sleep + ~10 s active).
- 8 h production build (OTA `1781229549`) → **both bench devices (`48CA43BBC5F4`
  and `48CA43BBC654`) picked up the OTA, posted once, and went silent** (deep
  sleeping) instead of looping. Device 1 fixed itself purely over-the-air.

> Lesson: never `gpio_reset_pin()` the USB or in-package flash/PSRAM pins. If manual
> per-pin isolation is ever reintroduced, allow-list only safe GPIOs.
