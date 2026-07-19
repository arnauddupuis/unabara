# Parser Smoke Test Checklist

Run this before merging any change to the dive log parser. Each row below is a single import via the **File → Import Dive Log** dialog (use `tests/data/`). For each file:

1. Confirm the dive list dialog (if multiple dives) shows the expected count.
2. Pick a dive and confirm the timeline renders without errors in the log.
3. Spot-check the values listed in the **Expected** column.

## SSRF regression (priority — this section is a pure code move from the old monolithic parser)

| File | Expected |
|---|---|
| `2026-02-28-Monastery_Beach.ssrf` | CCR dive, ~40.6 m max depth, 3 cylinders, gas switches present, CCR sensors (po2Sensors) populated. `diveMode` should resolve to `ClosedCircuit`. CNS ramps to ~55% by the end of the dive (`---` before the first `cns=` sample). Mean depth (AVG cell) shows 19.9 m (from `<depth mean='19.877 m'>`). |
| `CCR_Halcyon_Benoit_cleaned.ssrf` | CCR dive — sensor1/2/3 still populate. |
| `test_multidive.ssrf` | Multiple dives — picker shows them all; opening any one renders correctly. |
| `OC_ScubaproG2_Benoit.ssrf` | Open-circuit, multi-tank pressures present. `diveMode` resolves to `OpenCircuit`. |

## UDDF format

| File | Expected |
|---|---|
| `2026-02-28-Monastery_Beach.uddf` | Same dive as the SSRF version above (CCR, dive #143). ~40.5 m max depth; 3 cylinders with begin/end pressure (Pa→bar conversion correct). Per-sample tank pressure / `<measuredpo2>` absent — Subsurface UDDF export limitation; cells just stay blank. Comma-decimal `<depth>` values (e.g. `2,1`) parse as 2.1. Mean depth (AVG cell) shows 19.8 m (from `<averagedepth>19.832`). |
| `Shearwater.uddf` | Loads; depth/temperature populated; comma decimals parsed. CNS appears late in the dive (`<cns>` elements near the end): 65% → 70% at the last sample, `---` before the first report. Mean depth (AVG cell) shows 22.5 m (from `<averagedepth>22.465`). |
| `Garmin.uddf` | Loads from a different exporter; verifies UDDF dialect handling. |

## Format detection

- Rename a UDDF file to `.xml` and import — the file should still load (sniff-based detection, not extension).
- Rename an SSRF file to `.uddf` — same.

## Error handling

- Try to import a non-XML file (e.g. an image): expect `errorOccurred` with a useful message, no crash.

## What gets verified

| Behaviour | SSRF | UDDF |
|---|---|---|
| Dive metadata (number, datetime, site name) | ✓ | ✓ |
| Cylinders with start/end pressure | ✓ | ✓ |
| Per-sample depth | ✓ | ✓ |
| Per-sample temperature | Celsius | Kelvin → Celsius |
| Per-sample tank pressure | bar | Pa → bar (via `<tankpressure>` when exporter provides it) |
| Per-sample PO2 (CCR) | `sensor1/2/3` | `<measuredpo2>` (when exporter provides it) |
| Per-sample CNS | `cns='NN%'` attribute | `<cns>` percent element (65.0 = 65 %) |
| Mean depth (static, AVG cell) | `<depth mean>` in `<divecomputer>` | `<averagedepth>` in `<informationafterdive>` (fallback: time-weighted average of samples) |
| Gas switches | `<event name="gaschange">` | `<switchmix ref>` resolved via mix → first tank index |
| Dive mode | inferred from CCR cues | `<divemode kind>` or inferred |
