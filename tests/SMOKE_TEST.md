# Parser Smoke Test Checklist

Run this before merging any change to the dive log parser. Each row below is a single import via the **File → Import Dive Log** dialog (use `tests/data/`). For each file:

1. Confirm the dive list dialog (if multiple dives) shows the expected count.
2. Pick a dive and confirm the timeline renders without errors in the log.
3. Spot-check the values listed in the **Expected** column.

## SSRF regression (priority — this section is a pure code move from the old monolithic parser)

| File | Expected |
|---|---|
| `2026-02-28-Monastery_Beach.ssrf` | CCR dive, ~40.6 m max depth, 3 cylinders, gas switches present, CCR sensors (po2Sensors) populated. `diveMode` should resolve to `ClosedCircuit`. CNS ramps to ~55% by the end of the dive (`---` before the first `cns=` sample). Mean depth (AVG cell) shows 19.9 m (from `<depth mean='19.877 m'>`). Max depth (MAX cell) is a running max: 9.6 m at 12:35 (current depth 8.7 m), pinned at 40.6 m from the deepest point to the end. Gas (GAS cell): `EAN29` for the whole dive — the switch to the diluent cylinder at 0:10 coincides with the first sample, so the O2 cylinder (0) is never the active gas at any sample. |
| `Galileo_G2-TEK_SM_DecoO2-cleaned.ssrf` | Sidemount OC with deco O2. Gas (GAS cell): `Air` from the start — the 0↔1 sidemount switches at 9:41/14:49/27:37 don't change the displayed mix (both cylinders are air) — then `EAN99` from 37:17, `Air` at 48:01, `EAN99` from 50:05 to the end. |
| `CCR_Halcyon_Benoit_cleaned.ssrf` | CCR dive — sensor1/2/3 still populate. |
| `test_multidive.ssrf` | Multiple dives — picker shows them all; opening any one renders correctly. |
| `OC_ScubaproG2_Benoit.ssrf` | Open-circuit, multi-tank pressures present. `diveMode` resolves to `OpenCircuit`. |

## UDDF format

| File | Expected |
|---|---|
| `2026-02-28-Monastery_Beach.uddf` | Same dive as the SSRF version above (CCR, dive #143). ~40.5 m max depth; 3 cylinders with begin/end pressure (Pa→bar conversion correct). Per-sample tank pressure / `<measuredpo2>` absent — Subsurface UDDF export limitation; cells just stay blank. Comma-decimal `<depth>` values (e.g. `2,1`) parse as 2.1. Mean depth (AVG cell) shows 19.8 m (from `<averagedepth>19.832`). |
| `Shearwater.uddf` | Loads; depth/temperature populated; comma decimals parsed. CNS appears late in the dive (`<cns>` elements near the end): 65% → 70% at the last sample, `---` before the first report. Mean depth (AVG cell) shows 22.5 m (from `<averagedepth>22.465`). |
| `Garmin.uddf` | Loads from a different exporter; verifies UDDF dialect handling. |

## Garmin FIT format

| File | Expected |
|---|---|
| `2025-12-14-14-17-24.fit` | Dive #98, 2025-12-14 14:17 local, OC deco dive on Lake Geneva (location `46.480629, 6.461355`). ~24.1 m max depth, ~77 min, min temp 10 °C. 2 cylinders (Air + EAN80); GAS cell switches to `EAN80` during the final deco stop. CNS ramps to ~15%, `--` at the very start. AVG cell 14.8 m (from the FIT dive summary). Deco phase: NDL 0 with TTS/ceiling populated (ceiling 3 m). |
| `2026-01-12-10-40-25.fit` | Dive #104, shallow long dive (~9.6 m max, ~114 min), no GPS → location empty. |
| Any CCR file from `divelogs_medico_fits.zip` (e.g. `2026-01-25-10-41-00.fit`, dive #114) | `diveMode` resolves to `ClosedCircuit`; 3 cylinders incl. `Air (diluent)` and `EAN50` bailout; PO2 cell shows the active setpoint (0.70 bar). |
| Non-dive FIT from the zip (e.g. `2026-01-28-05-58-39.fit`) | Clean error: "FIT file does not contain a dive activity", no crash. |

Tank-pod pressures (`TANK_UPDATE`/`SENSOR_PROFILE`) have no coverage in the real samples — they are exercised by a synthesized FIT file (big-endian records, developer fields, compressed timestamps, 2 pods with pressures and volumes, mid-dive gas switch) during development; re-verify with a real Descent T1/T2 log when one becomes available.

## Format detection

- Rename a UDDF file to `.xml` and import — the file should still load (sniff-based detection, not extension).
- Rename an SSRF file to `.uddf` — same.
- Rename a `.fit` file to `.xml` — same (binary sniff on the `.FIT` header signature).

## Error handling

- Try to import a non-XML file (e.g. an image): expect `errorOccurred` with a useful message, no crash.
- Truncate a `.fit` file (`head -c 50000 x.fit > cut.fit`) and import: the partial dive loads (salvage) with a warning in the log; a file cut before any samples reports a clean error instead.

## What gets verified

| Behaviour | SSRF | UDDF | FIT |
|---|---|---|---|
| Dive metadata (number, datetime, site name) | ✓ | ✓ | number + local datetime; location = GPS coordinates |
| Cylinders with start/end pressure | ✓ | ✓ | gases from `dive_gas`, pressures from `tank_summary` (pod) |
| Per-sample depth | ✓ | ✓ | mm → m |
| Per-sample temperature | Celsius | Kelvin → Celsius | Celsius |
| Per-sample tank pressure | bar | Pa → bar (via `<tankpressure>` when exporter provides it) | `tank_update` bar×100 → bar (T1/T2 pods only) |
| Per-sample PO2 (CCR) | `sensor1/2/3` | `<measuredpo2>` (when exporter provides it) | active setpoint (Descent records no sensor values) |
| Per-sample CNS | `cns='NN%'` attribute | `<cns>` percent element (65.0 = 65 %) | `record` field 97, percent |
| Mean depth (static, AVG cell) | `<depth mean>` in `<divecomputer>` | `<averagedepth>` in `<informationafterdive>` (fallback: time-weighted average of samples) | `dive_summary` avg_depth mm → m |
| Max depth (running, MAX cell) | computed from samples (no parsing) — deepest point reached up to the current time | same | same |
| Gas switches | `<event name="gaschange">` | `<switchmix ref>` resolved via mix → first tank index | event 57, data = gas slot index |
| Gas mix (GAS cell) | computed from gas switches + cylinder O2/He (`Air`/`EANxx`/`O2`/`21/35`) | same | same |
| Dive mode | inferred from CCR cues | `<divemode kind>` or inferred | sub-sport (63 = CCR, 53-57 = OC/gauge/apnea) |
