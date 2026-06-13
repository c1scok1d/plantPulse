# V6 PCB revision — contractor brief

A hand-off spec for a PCB designer to turn the existing **V5** PlantPulse soil-sensor board
into **V6** in EasyEDA Pro and deliver JLCPCB-ready fabrication files. Everything needed is in
this repo; no prior project knowledge required.

## Scope of work
Modify the existing V5 design by adding **2 required** sub-circuits (and, if the client opts in,
**2 optional** ones). Do **not** redesign the board — keep the V5 outline, connectors, mounting,
stackup, and all existing nets/pins unchanged except where listed.

**Required**
1. **Soil-probe load switch** — a high-side PMOS that powers the existing TLC555 soil-sensor
   sub-circuit from a new *switched* rail `SENSOR_3V3`, gated by MCU `IO21`. Re-route the
   TLC555 VDD + its timing network + the soil SIG divider from `+3V3` to `SENSOR_3V3`.
2. **SHT40** temperature/humidity sensor on the **existing I²C bus** (IO16/IO17), powered from
   **always-on +3V3** (not the switched rail), I²C address `0x44`.

**Optional (include only if the client says so)**
3. **SOLAR_SENSE** — resistor divider from the panel node to `IO4` (ADC) to measure panel voltage.
4. **MAX17048 ALRT** — route the (currently unconnected) fuel-gauge ALRT pin to `IO18` for a
   low-battery wake.

The exact components, values, and **net-by-net connections** are in `NETLIST_v6_additions.md`.
Part numbers + LCSC stock numbers are in `BOM_v6_additions.csv`. Schematic diagrams + rationale
are in `../README.md` (sections: "gate the soil-probe power rail", "SHT40", "SOLAR_SENSE/ALRT").

## What you're given (files to work from)
- **The EasyEDA Pro project**: `../EasyEDA_Pro/ESP32 Soil/` (open in EasyEDA Pro). The current
  board is the **V5** sheet, `SHEET/1ca46c0a622b4cdb8f7ca57e7c690857/1.esch`. *(Alternatively the
  client can share the EasyEDA cloud project link, which is the live source of truth.)*
- `NETLIST_v6_additions.md` — the wiring spec (authoritative).
- `BOM_v6_additions.csv` — new parts + LCSC #s.
- `../README.md` — schematics + design notes (incl. why SHT40 is on always-on 3V3, layout do's/don'ts).
- `MANUFACTURING.md` — the expected export/order steps.

## How to do it (summary — detail in MANUFACTURING.md)
1. Duplicate the V5 schematic sheet → `V6`. Add the parts and wire them per `NETLIST_v6_additions.md`. Run **ERC**.
2. Update PCB from schematic; place + route the new parts. Placement constraints:
   - **SHT40 away from heat** (LDO/charger/ESP module), ideally board edge/vent — it's a temp sensor.
   - Keep `SENSOR_3V3` short; keep the high-impedance nodes (TLC555 1 MΩ timing, SOLAR_SENSE
     divider) short and **off the antenna keep-out**.
3. **DRC** against JLCPCB capabilities (≥6/6 mil trace/space, ≥0.2 mm drill is a safe target).
4. Export **Gerber + drill (zip)**, **BOM.csv**, **Pick-and-Place/CPL.csv** (JLCPCB format).

## Deliverables
1. The **updated EasyEDA project** (V6 schematic + PCB) returned to the client.
2. **JLCPCB-ready files**: Gerber/drill `.zip`, `BOM.csv`, `CPL.csv` — *or* place the JLCPCB
   order directly if the client requests (PCB, qty TBD; PCBA if requested).
3. A **schematic PDF** + a **board/3D preview** image for client review before ordering.

## Acceptance criteria
- ERC **and** DRC pass clean against JLCPCB rules.
- All agreed sub-circuits present and wired exactly per `NETLIST_v6_additions.md`.
- **V5 functionality preserved**: same board outline, connectors, mounting holes, stackup; no
  changes to existing nets/pins beyond moving the TLC555 sub-circuit onto `SENSOR_3V3`.
- New MCU pins used exactly: **IO21** (SOIL_PWR_EN, output), **IO16/17** (I²C, existing), and —
  if optionals included — **IO4** (SOLAR_SENSE/ADC) and **IO18** (ALRT). No strapping pins
  (IO0/45/46) or USB pins (IO19/20).
- BOM uses the specified LCSC parts (or equal value/footprint substitutes if a part is out of
  stock); placement preview shows correct **SOT-23 (Q1)** and **DFN-4 (SHT40)** orientation.

## Decisions the client must confirm before starting
- Which **optional** sub-circuits to include (SOLAR_SENSE and/or ALRT), or required-only.
- **Quantity**, whether to do **JLCPCB assembly (PCBA)** or bare boards, and any deadline/budget.
- That the board outline / connectors stay as-is (no enclosure change).

---

### Copy-paste message to the contractor
> Hi — I need a small revision to an existing EasyEDA Pro PCB (an ESP32-S3 soil sensor),
> turned into JLCPCB-ready fab files. It's well-specified: a repo folder has the exact net-by-net
> wiring (`NETLIST_v6_additions.md`), the new-parts BOM with LCSC numbers (`BOM_v6_additions.csv`),
> schematic diagrams + notes (`README.md`), the manufacturing steps (`MANUFACTURING.md`), and a
> full brief (`CONTRACTOR_BRIEF.md`). The work is: add a high-side load switch + an SHT40 I²C
> sensor (and optionally a solar-sense divider + a fuel-gauge alert line) to the existing V5
> design, route them, and export Gerber/BOM/CPL — without changing the board outline or existing
> nets. I'll send the EasyEDA project (or share the cloud link). Deliverables: updated EasyEDA
> project + Gerber/BOM/CPL zip (or place the JLC order), plus a schematic PDF and board preview
> for my review. Can you quote it?
