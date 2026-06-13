# PlantPulse — hardware reference

ESP32-S3-MINI soil sensor: solar + battery + USB powered. Last verified against the
EasyEDA project source on 2026-06-11.

> ## Source of truth = the EasyEDA project, not screenshots
> Analyze hardware from **`EasyEDA_Pro/ESP32 Soil/`** (the `.esch` schematic sheets +
> `project.json`), **not** exported PNGs — those are unversioned editor screenshots with
> no title block and can't be reliably attributed to a board rev. The project contains
> every revision as a distinct design, so the source is authoritative.
>
> Map a rev name to its sheet folder via `project.json` (name→docType id), then find the
> sheet under `SHEET/<folder>/1.esch`. Confirmed mapping:
> - **V5 sheet** → `SHEET/1ca46c0a622b4cdb8f7ca57e7c690857/1.esch`
> - **V4 sheet** → `SHEET/f325572339ab43209cde1b0deea9b4b4/1.esch`

## Revisions (all live inside the one EasyEDA project)

| Rev | MCU | Power sensing | Notes |
|---|---|---|---|
| **V5** | ESP32-**S3**-Mini | `USB_DETECT` + `STAT` (+ `SW1` button) | **the board we run** — confirmed in hand |
| V4 | ESP32-S3-Mini | **none** — no `USB_DETECT`/`STAT`/`SW1` nets | intermediate rev; can't do power_source at all |
| V2 / V3 | ESP32-S3-Mini | — | earlier iterations |
| legacy C3-mini | ESP32-**C3**-Mini | — | abandoned earlier design (different MCU + pinout) |

**`legacy_c3mini_production/` belongs to the legacy C3-mini rev** (BOM
`U19 = ESP32-C3-MINI-1-H4`, Apr-2025 fab) — it is **not** the V5 board. Don't design
to it.

### What V5 added over V4
V4 had **no** way to sense power at the MCU. V5 routes two new digital lines —
`USB_DETECT` and `STAT` — which is exactly what the firmware's `power_source` /
`charge_status` feature needs. Neither rev has a dedicated `SOLAR` sense net, so solar
is **inferred** (charging while USB absent), not directly measured.

## Power architecture (V5)
- **Charger:** `U4` = **MCP73831** Li-ion charger. **`STAT`** pin (open-drain:
  **LOW = charging**, high-Z/pulled-up = done/idle) → MCU **GPIO14**.
- **Sources are diode-OR'd:** USB `VBUS` and `Solar+` (Schottky) feed `CHRG5V`.
  **No dedicated solar-sense line** — only **`USB_DETECT`** (a `VBUS` divider) reaches
  the MCU on **GPIO13**.
- **Fuel gauge:** `U14` = **MAX17048** on I²C (SDA=IO16, SCL=IO17; `ALRT` present).
  Gives voltage, SoC, and signed charge rate (CRATE, LSB 0.208 %/hr; + charge / − discharge).
- **Regulator:** 3V3 LDO. Battery + solar + USB; on/off slide switch.

### Deriving power_source / charge_status — **verified on-device 2026-06-11**
| `USB_DETECT` (IO13) | `STAT` (IO14) | power_source | charge_status |
|---|---|---|---|
| HIGH | LOW (charging) | **USB** | charging |
| HIGH | high-Z | **USB** | full/idle |
| LOW | LOW (charging) | **Solar** (inferred) | charging |
| LOW | high-Z | **Battery** | discharging |

Confirmed live: USB plugged → `USB`/`charging`; USB unplugged (on battery) →
`Battery`/`discharging`. Firmware: `data.c` `read_power_state()` reads GPIO13/14; the
MAX17048 CRATE distinguishes idle vs discharging.

## GPIO map (V5)
Net labels are from the V5 `.esch` source; GPIO numbers for `USB_DETECT`/`STAT` are the
firmware's and were **confirmed empirically** (the on-device toggle test above).

| GPIO | Net | Function | Firmware status |
|---|---|---|---|
| IO5  | SIG        | soil moisture (ADC1_CH4) | ✅ GPIO5 |
| IO13 | USB_DETECT | USB-present sense | ✅ GPIO13 (verified) |
| IO14 | STAT       | MCP73831 charge status | ✅ GPIO14 (verified) |
| IO16 | I2C_SDA    | I²C (MAX17048) | ✅ SDA=16 |
| IO17 | I2C_SCL    | I²C (MAX17048) | ✅ SCL=17 |
| —    | ALRT       | MAX17048 alert | (unused) |
| IO34 | LED2       | status LED | ✅ GPIO34 (fixed `1e07b93`) |
| IO3  | SW1 / BUTTON | user button (`SW1`→`IO3`, source-confirmed) | ✅ IO3 (fixed `1e07b93`) |
| IO19 / IO20 | D- / D+ | native USB | ✅ |

## Firmware alignment (V5)
All V5 pins now match and are verified on-device: I²C 16/17, soil ADC GPIO5, power sense
GPIO13/14. The two earlier non-telemetry mismatches were **FIXED (commit `1e07b93`)**:
- **LED2 (IO34)** — firmware drives **IO34** now (`main.c` `#define LED_GPIO 34`); status LED works.
- **SW1 / button (IO3)** — firmware reads **IO3** now (`main.c` `#define BUTTON_GPIO 3`); factory-reset long-press works.

Both confirmed on-device (LED blink + long-press factory reset).

## V6 recommendation — add an I²C temperature/humidity sensor

**Why:** the Flutter app already ships temperature/humidity/VPD charts and a disease
catalog keyed on temp/humidity ranges, but **V5 has no RH/T sensor**, so those fields are
always null. The app is now null-safe (it simply hides the temp/humidity/VPD cards when the
data is absent — `IoT_monitoring_page`/`Records`), so adding the sensor lights them up with
**no further app work**.

**Hardware (V6):** add an **SHT40** on the **existing I²C bus** (SDA=IO16, SCL=IO17 — already
pulled up for the MAX17048). Address `0x44` (SHT40-AD1B), distinct from the MAX17048's `0x36`.
No new GPIO, no bus change — a one-part addition.

### V6 sub-circuit — SHT40 (temperature / humidity)
```
   +3V3  ──┬──────────────┬─────────────► (always-on rail — NOT the switched SENSOR_3V3)
           │              │
        [100nF]        ┌──┴──┐ VDD
           │           │ U?  │  SHT40-AD1B  (DFN-4, addr 0x44)
          GND          │     │
                  SDA ─┤     ├─ SCL
                   │   └──┬──┘    │
                   │     GND      │
   I2C_SDA(IO16) ──┘              └── I2C_SCL(IO17)      ← reuse the existing MAX17048 bus
        (bus pull-ups to +3V3 already present: ~4.7 kΩ each — no new pull-ups needed)
```

| Ref | Part (JLCPCB) | Value | Note |
|---|---|---|---|
| U? | **SHT40-AD1B** (Sensirion, C2761795) | I²C, 0x44 | ±0.2 °C / ±1.8 %RH; AHT20 (0x38) / SHTC3 (0x70) are fallbacks |
| C | 0402 | **100 nF** | VDD decoupling, close to the sensor |

**Power it from the always-on +3V3, *not* the switched `SENSOR_3V3`.** The SHT40 idles at
~0.08 µA (single-shot draw ~320 µA for ~10 ms, i.e. sub-µA averaged), so gating saves nothing
— and because its SDA/SCL stay pulled to the always-on bus (shared with the always-on
MAX17048), a powered-down SHT40 would be **back-fed through its I²C ESD diodes** (~0.5 mA via
the pull-ups), wasting more than it saves and risking latch-up. Keep VDD on the same rail as
the bus pull-ups.

**Layout:** place the SHT40 **away from self-heating parts** (the XC6220 LDO, MCP73831 charger,
ESP module) — ideally a board edge / near a vent slot — or it reads warm. A small thermal-relief
slot/cutout around it helps ambient accuracy.

**Firmware (turnkey, ~1 hr once a board exists):**
1. After `i2c_master_init()` in `monitor()` (`data.c`), probe `0x44` (address write, short
   timeout): ACK ⇒ present; NACK ⇒ absent (the V5 path, byte-for-byte unchanged).
2. If present: send measure cmd `0xFD`, wait ~10 ms, read 6 bytes
   (`T_msb T_lsb crc RH_msb RH_lsb crc`). Convert: `T_°C = -45 + 175*raw_t/65535`,
   `RH = -6 + 125*raw_rh/65535` (clamp 0–100).
3. VPD (kPa): `SVP = 0.6108*exp(17.27*Tc/(Tc+237.3))`, `VPD = SVP*(1 - RH/100)`.
4. Add `temperature`/`humidity`/`vpd` to the `upload_data_t` structs (both inline copies in
   `data.c`) + the `uploadReadings()` signature, and **append** to the POST body only when
   present: `&temp=%.1f&humid=%.1f&vpd=%.2f`. (Backend already has the `temperature/
   humidity/vpd` columns and renames `temp→temperature`, `humid→humidity` on ingest.)

**Units decision:** the app labels temperature **°F** and the disease catalog uses °F
ranges → send temperature in **°F** (`Tf = Tc*9/5 + 32`) but compute **VPD from °C**.
(Alternatively standardize the whole stack on °C and relabel the app — pick one before
shipping V6.)

The probe-gate guarantees V5 stays identical on the wire, so this is safe to ship before any
V6 exists and activates automatically when a sensor is present.

## V5 BOM — key parts (from the EasyEDA source, 2026-06-12)

| Block | Part | Notes |
|---|---|---|
| MCU | **ESP32-S3-MINI-1-N8** | 8 MB flash, no PSRAM, integrated antenna (A1) |
| Charger | **MCP73831T-2ACI/OT** | 4.2 V linear Li-ion charger; `STAT`→IO14 |
| Regulator | **XC6220B331MR-G** (Torex) | 3.3 V/1 A LDO, **Iq ~10 µA**, has CE — good low-power part |
| Fuel gauge | **MAX17048G+T10** | I²C 0x36; V/SoC/CRATE; `ALRT` **unused** |
| Soil sensor | **TLC555CDR** + 1 MΩ timing net | **capacitive astable oscillator, powered 24/7** ← main drain |
| Source OR | **VS-10BQ015** Schottky | `Solar+` + USB `VBUS` diode-OR → `CHRG5V` |
| Sense | `USB_DETECT` (IO13 divider), `STAT` (IO14) | solar **inferred**, not measured |
| UI | tactile `SW1`→IO3, blue LED `XL-1608UBC`→IO34 | button = RTC wake / factory reset |
| Conn | USB-C, battery/probe PH2.0 (`WAFER-PH2.0-2P`), slide switch `SSSS811101` | |

## V6 recommendation — gate the soil-probe power rail (battery life)

**Finding (V5, confirmed against the EasyEDA source 2026-06-12):** the soil sensor is a
**TLC555 capacitive oscillator** whose VDD ties **straight to the always-on +3V3 rail** — there
is **no MOSFET / load switch / enable line** anywhere on the board (census `A1 C17 D3 R14 U6
LED1 SW1`, **zero `Q`**). So it oscillates **continuously, 24/7**, between the 8-hourly reads.
On an 8 h duty cycle this idle drain dominates battery life.

**Estimated idle current budget** (bench-measure to confirm the TLC555 figure):

| Consumer | Idle current |
|---|---|
| ESP32-S3 deep sleep | ~8–10 µA |
| XC6220 LDO Iq | ~10 µA |
| MAX17048 | ~23 µA |
| **TLC555 (continuous)** | **~200–700 µA** |
| **Total — V5 today** | **~250–750 µA** |
| Total — probe **gated** (V6) | **~45 µA** |

On the 1100 mAh / 4.1 Wh cell (80% usable, 3 wakes/day, ~1.1 mAh/day active):

| Config | Daily draw | Life per charge (usable) |
|---|---|---|
| **V5 today** (TLC555 always on) | ~7–18 mAh/day | **~1.5–4 months** |
| **V6** (probe gated) | ~2.2 mAh/day | **~12–14 months** |

⇒ Gating the TLC555 is a **~4–6× battery-life win** and likely removes any need for indoor
solar. It can't be done in firmware on V5 (nothing to switch) — it needs the load switch below.

### V6 sub-circuit — high-side PMOS load switch (recommended)
Power the TLC555 + 1 MΩ timing net + SIG divider from a **switched**
rail `SENSOR_3V3`, gated by GPIO21, **failsafe-off** in deep sleep:

```
        +3V3 (always-on, from XC6220)
          │
     ┌────┴────┐ S
 R_PU│        ┌┴┐
100kΩ│   Q1   │ │  AO3401A (or DMG2305UX) — P-ch logic-level, SOT-23
     ├────────┤ │
     │      G └┬┘
IO21─┴─[1kΩ]───┘ D        IO21 LOW → ON (Vgs=-3.3V); HIGH/Hi-Z → OFF (R_PU holds gate=source)
                ├──────────► SENSOR_3V3 ── TLC555 VDD, 1 MΩ timing, SIG divider
                                            (the SHT40 stays on always-on +3V3 — see its section)
              [100nF]
                │
               GND
```

| Ref | Part (JLCPCB) | Value | Note |
|---|---|---|---|
| Q1 | **AO3401A** (C15127, basic) or **DMG2305UX** (C107489) | P-MOSFET SOT-23 | sub-mA load → Rds(on) irrelevant; want low Vgs(th) |
| R_PU | 0402 | **100 kΩ** | gate pull-up → OFF when Hi-Z (deep sleep) |
| R_G | 0402 | **1 kΩ** | gate series (slew/ESD); optional |
| C_BYP | 0402 | **100 nF** | TLC555 decoupling, **on `SENSOR_3V3`** |

**Zero-BOM alternative:** the TLC555 Icc is < 1 mA, so you may instead drive `SENSOR_3V3`
directly from GPIO21 (skip Q1), **active-HIGH** — fine for low volume, slightly less clean for
EMI. (This rail carries only the TLC555 sensing network; the SHT40 lives on always-on +3V3.)

**Layout:** keep `SENSOR_3V3`/SIG off the antenna keep-out; route the high-impedance 1 MΩ
timing node short.

**Firmware (in place):** `data.c` `readMoisture()` gates on read (power → settle → sample →
cut), `SOIL_PWR_GPIO` defaults `-1` (V5 no-op, safe over OTA) with polarity defaulting to
**active-low** for this PMOS topology. On a V6 build set `SOIL_PWR_GPIO = GPIO_NUM_21`
(firmware commits `9388903`, `9ab5f4f`).

## Solar charging (V5 already supports it)

V5 **already has a solar input**: `Solar+` is **Schottky diode-OR'd with USB `VBUS`** into
`CHRG5V`, which feeds the **MCP73831** Li-ion charger — so charging is hardware-autonomous
(works during deep sleep, no firmware needed). There is **no dedicated solar-sense line**;
solar is *inferred* (`USB_DETECT` low + `STAT` charging ⇒ "Solar"). **Intended use is
indoor**, where ambient light is 100–1000× weaker than sun: a small panel is marginal-to-
insufficient (a palm-size indoor-grade amorphous/DSSC cell only nets positive at a genuinely
bright window, and needs an MPPT harvester like TI **BQ25570** to extract usable energy at
low panel V/I — the linear MCP73831 won't cold-start or track MPP at indoor power levels).
**Recommendation for indoor:** prioritize cutting sleep current (gate the probe, low-Iq LDO)
and/or a larger cell to reach multi-year life on USB top-ups, rather than relying on indoor
solar. Reserve a panel for a bright-window SKU with an MPPT front-end. Keep the panel `Voc`
within the diode-OR/charger input limits and the Schottky reverse-block in place.

## V6 — optional: measure solar (SOLAR_SENSE) + low-battery wake (ALRT)

### SOLAR_SENSE — measure the panel instead of inferring it
Today solar is *inferred* (`USB_DETECT` low + `STAT` charging) — you can't tell "solar
charging" from "panel dark / dead." A divider from the panel node to a spare **ADC1** pin lets
firmware read the panel voltage directly.

```
  Solar+ ──[ R_T 1MΩ ]──┬──► SOLAR_SENSE  (IO4 / ADC1_CH3)
 (panel side of         │
  the OR Schottky)   [R_B 820kΩ] ‖ [C 100nF]
                        │
                       GND
```
`Vadc = Vsolar · Rb/(Rt+Rb) ≈ Vsolar · 0.45` → a 6.5 V-Voc panel maps to ~2.9 V (within the
S3 ADC range at 12 dB atten; keep panel Voc ≤ ~6 V for the MCP73831 input anyway). Tap the
**panel side of the Schottky** so you read the panel itself, not the OR'd 5 V bus (USB would
otherwise mask it). 1 MΩ+820 kΩ holds the continuous divider leak to ~2.7 µA @ 5 V; the 100 nF
gives the ADC sample-and-hold a low-impedance source. **Use ADC1** — ADC2 is unusable with WiFi up.

| Ref | Part | Value | Note |
|---|---|---|---|
| R_T | 0402 | **1 MΩ** | top, to `Solar+` (panel side of Schottky) |
| R_B | 0402 | **820 kΩ** | bottom, to GND |
| C | 0402 | **100 nF** | across R_B — ADC filter / charge reservoir |

Firmware: read `ADC1_CH3`, scale ×`(Rt+Rb)/Rb` (≈2.22) → `Solar+` volts; >~4.5 V ⇒ panel
producing. Report *measured* solar in `power_source` instead of inferring it.
**New pin: IO4 = ADC1_CH3.**

### ALRT → GPIO — wake on low battery
The MAX17048 `ALRT` (open-drain, active-low) is wired on the chip but unused. Route it to a
free **RTC-capable** GPIO with a pull-up so the device can wake from deep sleep on a low-SOC /
low-voltage alert instead of only noticing at the next 8 h wake.

```
  +3V3 ──[ R 100kΩ ]──┬──► ALRT_INT  (IO18 — RTC-capable, ext1 wake)
                       │
  MAX17048 ALRT ───────┘   (open-drain — pulls LOW on alert)
```
| Ref | Part | Value | Note |
|---|---|---|---|
| R | 0402 | **100 kΩ** | `ALRT` pull-up to always-on +3V3 |

Firmware: set the MAX17048 alert thresholds over I²C (low-SOC %, low-V), then add IO18 as a
deep-sleep wake source. **ext0 takes only one pin and the button already uses it (IO3)** — move
**both to `ext1` `ANY_LOW`** (both are active-low) so the device wakes on button *or* battery
alert. On wake, read the MAX17048 and clear the alert latch. **New pin: IO18 (RTC).**

**Cost/benefit:** both are low-BOM niceties, not essentials. SOLAR_SENSE turns "is the panel
working?" from a guess into a measurement — worth it if you ship a solar SKU. ALRT-wake only
buys *earlier* low-battery notice than the existing 8 h cadence — nice for a prompt low-battery
push, skippable otherwise. Neither touches the V5 fleet: guard the new pins behind defines (like
the soil gate) so firmware uses them only when the board has them.

### V6 new-pin summary
| Pin | Net | Function | Type |
|---|---|---|---|
| IO21 | `SOIL_PWR_EN` | soil load-switch gate (active-low) | output |
| IO4  | `SOLAR_SENSE` | panel-voltage divider | ADC1_CH3 |
| IO18 | `ALRT_INT` | MAX17048 low-batt wake | RTC input (ext1) |
| IO16/17 | I²C | + SHT40 (0x44) on existing bus | — |

## V6 — add / enhance / remove (summary)

What V5 does well (keep): power-source + charge sensing (`USB_DETECT`/`STAT`), a real fuel
gauge (MAX17048), a low-Iq LDO (XC6220 — *not* the battery problem), corrosion-immune
capacitive sensing (TLC555), a working solar input, and OTA + encrypted/bonded BLE.

**ADD**
- **Soil-probe high-side load switch** (sub-circuit above) — the #1 battery win (~4–6×).
- **SHT40** (0x44) on the existing I²C bus → lights up the app's temp/humidity/VPD + disease
  features with no app work. Keep it on **always-on +3V3** (sub-µA idle; gating it would
  back-feed through the shared-bus pull-ups — see the SHT40 sub-circuit).
- *Optional* **`SOLAR` sense** — a divider from `Solar+` to a spare ADC so solar is *measured*,
  not inferred (distinguishes "solar charging" from a dead panel).
- *Optional* **`ALRT`→RTC-capable GPIO** (MAX17048) for low-battery wake/alert between cycles.

**ENHANCE**
- Move TLC555 decoupling + the SIG/ADC network onto the switched rail (nothing leaks when gated).
- For indoor SKUs, offer a **larger cell** (2000–3500 mAh) → multi-year on USB top-ups.

**REMOVE / SIMPLIFY (deployment-dependent)**
- If indoor-only and skipping solar, the `Solar+` Schottky diode-OR + connector *could* be a
  cost-down — but it's cheap and validated, so keep it unless you want the BOM saving. Don't
  remove native USB (IO19/20) or the button. TLC555 can stay (gating makes its draw a non-issue).
