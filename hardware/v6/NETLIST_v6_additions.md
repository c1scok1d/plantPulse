# V6 net additions — exact wiring to enter in EasyEDA

These are the connections to add on top of the **V5 schematic** (sheet
`SHEET/1ca46c0a622b4cdb8f7ca57e7c690857/1.esch`). Designators below assume the next-free
numbers after the V5 census (V5 has no `Q`; highest `R`=R30, `C`=C17, `U`=U18) — adjust if
EasyEDA auto-numbers differently. Schematic rationale + diagrams: `../README.md`.

New nets introduced: `SENSOR_3V3` (switched), `SOIL_PWR_EN`, `SOLAR_SENSE`, `ALRT_INT`.

## 1. Soil-probe load switch  (REQUIRED — the battery win)
Insert a high-side PMOS between the always-on `+3V3` and a new switched rail `SENSOR_3V3`,
and **move the TLC555 sensing sub-circuit onto `SENSOR_3V3`**.

| Ref | Pin | Net |
|---|---|---|
| Q1 (AO3401A) | S (source) | `+3V3` |
| Q1 | D (drain) | `SENSOR_3V3` |
| Q1 | G (gate) | `Q1_GATE` |
| R31 (100k) | a / b | `+3V3` / `Q1_GATE`  (gate pull-up → OFF when Hi-Z) |
| R32 (1k) | a / b | `IO21` (`SOIL_PWR_EN`) / `Q1_GATE` |
| C18 (100n) | a / b | `SENSOR_3V3` / `GND` |

Then **re-net the TLC555 sensor block** that is currently on `+3V3`:
- TLC555 `VDD` : `+3V3` → **`SENSOR_3V3`**
- TLC555 timing network (the 1 MΩ + cap) top rail : `+3V3` → **`SENSOR_3V3`**
- Any soil SIG/ADC divider top : `+3V3` → **`SENSOR_3V3`**
- TLC555 `SIG`/output → **unchanged** (still to `IO5` / ADC1_CH4)
- TLC555 `GND` → unchanged

> MCU pin: `IO21` becomes `SOIL_PWR_EN` (output, active-LOW). Firmware: set
> `SOIL_PWR_GPIO = GPIO_NUM_21`, `SOIL_PWR_ACTIVE_LEVEL = 0` in `main/sensor_data/data.c`.

## 2. SHT40 temp/humidity  (REQUIRED for the app's T/RH/VPD features)
On the **existing I²C bus** and the **always-on +3V3** (NOT `SENSOR_3V3` — see README).

| Ref | Pin | Net |
|---|---|---|
| U19 (SHT40-AD1B) | VDD | `+3V3` |
| U19 | GND | `GND` |
| U19 | SDA | `I2C_SDA` (IO16) |
| U19 | SCL | `I2C_SCL` (IO17) |
| C19 (100n) | a / b | `+3V3` / `GND`  (close to U19) |

> Reuse the existing MAX17048 bus pull-ups — **no new pull-ups**. Addr `0x44` (≠ MAX17048 `0x36`).
> Place U19 away from the LDO/charger/MCU (self-heating). Firmware auto-detects it at `0x44`.

## 3. SOLAR_SENSE divider  (OPTIONAL — measure solar vs infer it)
Divider from the **panel side of the OR Schottky** to an ADC1 pin.

| Ref | Pin | Net |
|---|---|---|
| R33 (1M) | a / b | `Solar+` / `SOLAR_SENSE` |
| R34 (820k) | a / b | `SOLAR_SENSE` / `GND` |
| C20 (100n) | a / b | `SOLAR_SENSE` / `GND` |

> MCU pin: `SOLAR_SENSE` → `IO4` (ADC1_CH3). Tap `Solar+` on the panel side of its Schottky.

## 4. MAX17048 ALRT wake  (OPTIONAL — low-battery deep-sleep wake)
| Ref | Pin | Net |
|---|---|---|
| R35 (100k) | a / b | `+3V3` / `ALRT_INT` |
| U14 (MAX17048) | ALRT | `ALRT_INT`  (currently unconnected on V5) |

> MCU pin: `ALRT_INT` → `IO18` (RTC-capable). Firmware: move button(IO3)+ALRT to `ext1 ANY_LOW`.

## V6 MCU pin additions (sanity-check against the module pinout)
| GPIO | Net | Dir | Notes |
|---|---|---|---|
| IO21 | SOIL_PWR_EN | out | PMOS gate, active-low |
| IO4  | SOLAR_SENSE | in (ADC1_CH3) | optional |
| IO18 | ALRT_INT | in (RTC, ext1) | optional |

All three are free on V5. Avoid the strapping pins (IO0/IO45/IO46) and USB (IO19/20).
