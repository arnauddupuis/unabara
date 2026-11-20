# C6 colour-grade calibration

Fits `GradeTuning` constants (`include/core/color_grade.h`) and the three
bundled presets (`resources/color_presets/*.ucp`) against reference edits:
pairs of (original clip, an editor's graded export) under
`tests/videos/calibration_tests/` (local-only, untracked — the footage is
not in the repo, and neither is the `REPORT.md` referenced below, which
lives alongside it).

## Layout

- `originals/` + `INDEX.md` — source clips with camera/scene metadata.
- `processed/processed-<editor>/` — one graded export per original (same
  name, same resolution/fps/frame count, colour-only edits) plus a notes
  file per clip (PDF or txt).
- `cache/` — generated: sparse paired frames (16-bit PNG, quarter-ish
  res) and fit result JSONs. Delete to regenerate.

## Decode rules (measured, not assumed — see REPORT.md)

- Originals decode per their tags: full range; bt709 matrix except
  `SportDiver_20241221` (smpte170m). `SportDiver_20260426` carries a
  rotation=90 display matrix — ffmpeg autorotation aligns it with the
  processed file's baked-in rotation.
- Argonaute's ProRes exports are tagged `color_range=unknown` but are
  limited range (Premiere never writes full-range ProRes; the measured
  histograms confirm, with sub-black/super-white grading excursions).

## Running

```bash
python3 tools/calibration/calibrate.py extract   # one-time frame cache
python3 tools/calibration/calibrate.py fit       # free op-level fits per clip
python3 tools/calibration/calibrate.py fit2      # slider-space + joint constants fit
```

Needs ffmpeg/ffprobe, numpy, scipy. `fit` bounds what *any* setting of
the grade ops could reach (plus a nuisance exposure gain); `fit2` fits
inside the real slider + eyedropper parameterization, first with the
current constants, then optimizing the constants jointly across clips
(weighted; the near-irrecoverable Neuchâtel clip is held at weight 0.05).

The Python model is validated against the C++ ground truth
(`GradeOps::apply`) by a throwaway harness compiled from the real
`src/core/color_grade.cpp` — re-run that check (see REPORT.md appendix)
after any change to the C++ before trusting new fit numbers.

## Interpreting

- `identity` rms = original vs edit with no model: the size of the edit.
- `free` rms = best any matrix+black+lift+contrast pipeline can do:
  the floor imposed by ops the model lacks (hue-qualified secondaries,
  luma curves...).
- `fit2` rms under current vs fitted constants = what the shipped sliders
  reach today vs after retuning.

Changing `GradeTuning` constants changes the look of every saved grade
and breaks literal expectations in `color_grade_test.cpp` /
`color_export_test.cpp` — move them together, and bump the grade JSON
`version` per the plan if constants ever change post-release.
