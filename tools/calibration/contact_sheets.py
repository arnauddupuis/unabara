#!/usr/bin/env python3
"""Render side-by-side contact sheets from the fit results: for each clip,
rows of [original | slider-model grade | editor's edit] at three sample
frames, written to cache/sheets/<clip>_<K>.png. Visual evidence for the
calibration report."""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from calibrate import CLIPS, CACHE, read_png16, slider_predict, K_CURRENT


def write_png(path, img01):
    h, w, _ = img01.shape
    raw = (np.clip(img01, 0, 1) * 65535.0 + 0.5).astype("<u2").tobytes()
    r = subprocess.run(
        ["ffmpeg", "-hide_banner", "-nostats", "-y", "-f", "rawvideo",
         "-pix_fmt", "rgb48le", "-s", f"{w}x{h}", "-i", "-",
         "-frames:v", "1", str(path)],
        input=raw, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"png write failed: {r.stderr[-400:].decode()}")


def main():
    fits_file = sys.argv[1] if len(sys.argv) > 1 else "fits2b.json"
    which = sys.argv[2] if len(sys.argv) > 2 else "fitted"
    fits = json.loads((CACHE / fits_file).read_text())
    K = np.array(fits["K_fitted"] if which == "fitted" else K_CURRENT)
    outdir = CACHE / "sheets"
    outdir.mkdir(exist_ok=True)
    for clip in CLIPS:
        name = clip["name"]
        sliders = np.array(fits["clips"][name][which]["sliders"])
        o_files = sorted((CACHE / name / "orig").glob("*.png"))
        p_files = sorted((CACHE / name / "proc").glob("*.png"))
        picks = [len(o_files) // 4, len(o_files) // 2, 3 * len(o_files) // 4]
        rows = []
        for i in picks:
            o = read_png16(o_files[i])
            p = read_png16(p_files[i])
            g = slider_predict(sliders, K, o.reshape(-1, 3)).reshape(o.shape)
            rows.append(np.concatenate([o, g, p], axis=1))
        sheet = np.concatenate(rows, axis=0)
        out = outdir / f"{name}_{which}.png"
        write_png(out, sheet)
        print(f"wrote {out}  (columns: original | model | editor)")


if __name__ == "__main__":
    main()
