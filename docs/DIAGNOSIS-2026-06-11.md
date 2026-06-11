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
