# PlantPulse — cross-layer enhancement roadmap

How hardware → firmware → backend → frontend can evolve together. Grounded in the
code as of 2026-06-11.

## Where the system is today

A **one-way telemetry pipeline**: the device reads soil moisture + battery, POSTs
3 fields, the backend stores them, the app displays them. Solid, and now fully
working end-to-end on the migrated fleet backend.

```
firmware sends (data.c):   moisture, batt, battery_status        (3 fields)
backend device_records:    temperature, humidity, moisture, vpd,
                           batt, charge_status, power_source,
                           battery_status                        (8 columns)
backend → device commands: restart, update_firmware,
                           adjust_power_mode, send_diagnostics    (DeviceController:121-136)
firmware acts on commands: none — it POSTs and ignores the response
```

**Intentional forward-provisioning (NOT gaps):** `temperature`, `humidity`, `vpd`
(and the richer env columns) are placeholders for **future sensor hardware** (e.g.
an I²C temp/humidity sensor on the existing SDA16/SCL17 bus → VPD). Deferred by
design. Everything below is achievable on the **current** V5 hardware.

## The real opportunity

Two things are under-used on hardware we already ship:
1. **The command channel is scaffolded but dead** — the backend emits commands the
   firmware never reads. Closing this turns a passive logger into a controllable device.
2. **Per-plant context exists in the app but is disconnected from telemetry** — the
   "farm" features (plant type, planting/harvesting, growth stage) don't inform the
   "IoT" alerts, which use a hardcoded 20% threshold.

---

## Plays (priority order) — all current-hardware

### 2. Close the loop: activate the command channel
The backend already returns `restart / adjust_power_mode / send_diagnostics /
update_firmware`; the firmware just needs to parse the POST response and act.
- **Firmware:** read the upload response, dispatch on `command`. `adjust_power_mode`
  → change the deep-sleep interval at runtime (today it's a *compile-time* constant
  in `data.c` — `EIGHT_HOUR_SLEEP`); `restart` → `esp_restart()`; `send_diagnostics`
  → POST heap/RSSI/reset-reason/version.
- **Backend:** decide commands per device (cadence, restart) and return them.
- **Frontend:** a device control panel (set sampling interval, request diagnostics).
- **Later / needs HW:** add a relay/valve GPIO + an `irrigate` command → monitoring
  becomes **smart irrigation**.
- *Value:* server-controlled cadence with **no reflash**, remote restart, diagnostics.

### 3. Per-plant intelligence: configurable thresholds + connect the app's two halves
Thresholds are hardcoded (`checkAndSendAlert(...,20,...)`); `level_alerts` only
dedupes (`id, device_id, record_type`). The app's plant/farm data never reaches alerting.
- **Backend:** per-device/per-plant thresholds (new config, or extend `level_alerts`);
  alerts tuned to plant + growth stage.
- **Frontend:** bind "device → plant" so a flowering tomato and a succulent differ;
  unify the **Farm** and **IoT** app sections (today they're separate screens).
- **Firmware (optional):** push thresholds to the edge → wake + alert fast when
  critical instead of waiting for the next 8 h cycle.

### 4. Fleet operations: device health + safe OTA
No fleet view (offline / battery-dying / last-seen); OTA is one version compared by
`strcmp != 0` (downgrade risk — see ROADMAP P2).
- **Backend:** compute device health from `read_at` (last-seen, missed readings,
  battery trend); track per-device firmware version.
- **Frontend:** a device fleet dashboard — *what `qstatus` does for the servers, for
  the sensors.*
- **OTA:** semver + staged rollout + rollback + per-group targeting.

---

## Additional recommendations (found during this work)

- **BUG — data ingest is coupled to push notifications.** `saveEspData` returns
  HTTP 400 "User does not have a valid FCM token" when `$user->fcm_token` is empty —
  which **blocks the reading from being stored at all**. Ingest must always persist;
  alerting is a separate concern. Decouple them (store first, alert best-effort).
  Fits play #3.
- **Power telemetry (current HW, quick win).** The MAX17048 already gives voltage +
  charge rate, and the board knows solar-vs-USB — populate `charge_status` /
  `power_source` / voltage (the *old* firmware sent these) to predict uptime and
  warn before a battery dies.
- **Per-device moisture calibration.** Moisture is mapped with fixed ADC points
  (`map(reading, 3600, 2130, 0, 100)`); store per-device wet/dry points server-side,
  configurable in the app, for accuracy across soils.
- **Fix the BLE provisioning fallback crash** (`hci inits failed` on WiFi-fail →
  reboot loop) so re-provisioning works without a reflash.

## Suggested sequence

1. **#4 device health** (backend + frontend only, no firmware/HW) — fastest, gives
   fleet visibility immediately.
2. **#2 command channel** (firmware + backend) — unlocks runtime cadence control +
   diagnostics; foundational for everything server→device.
3. **Ingest/FCM decoupling** (backend, small) — robustness, do alongside #2/#3.
4. **#3 per-plant thresholds** (backend + frontend) — depends on #2 for edge push.
5. Hardware-dependent items (actuation, env sensors) when the next board revision lands.
