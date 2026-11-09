#!/usr/bin/env python3
"""Depth-split calibration round: extract Arnaud's Unabara renders
(processed-unabara) + the new GX010567 pair, verify app-vs-model parity,
recover the slider values behind each render, and fit Shallow/Deep preset
variants per water type. Builds on calibrate.py's cache and model."""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from calibrate import (CAL, CACHE, run, read_png16, slider_predict,
                       residual_stats, huber_loss, K_CURRENT, load_pairs)

# name -> (every-Nth, scale, smpte170m original?)  — matches calibrate.CLIPS
RENDERS = {
    "GH010359": (97, "480:270", False),
    "GH010371": (162, "480:270", False),
    "GX010423": (286, "480:270", False),
    "GX010563": (62, "480:270", False),
    "GX010567": (144, "480:270", False),
    "SportDiver_20241221_093314": (34, "480:270", True),
}


def extract():
    # New original: GX010567 (full range bt709, like the other GoPros).
    n, size, _ = RENDERS["GX010567"]
    outdir = CACHE / "GX010567/orig"
    if not (outdir.is_dir() and list(outdir.glob("*.png"))):
        outdir.mkdir(parents=True, exist_ok=True)
        print("extract GX010567/orig ...", flush=True)
        run(["ffmpeg", "-hide_banner", "-nostats", "-y",
             "-i", str(CAL / "originals/GX010567.MP4"),
             "-vf", f"select='not(mod(n\\,{n}))',scale={size}:in_range=pc:out_range=pc:in_color_matrix=bt709,format=rgb48be",
             "-fps_mode", "passthrough", str(outdir / "f%04d.png")])
    # Arnaud's renders: limited-range bt709 h264 (measured from probe tags).
    for name, (n, size, _) in RENDERS.items():
        outdir = CACHE / name / "una"
        if outdir.is_dir() and list(outdir.glob("*.png")):
            print(f"skip {name}/una (cached)")
            continue
        outdir.mkdir(parents=True, exist_ok=True)
        print(f"extract {name}/una ...", flush=True)
        run(["ffmpeg", "-hide_banner", "-nostats", "-y",
             "-i", str(CAL / f"processed/processed-unabara/{name}.mp4"),
             "-vf", f"select='not(mod(n\\,{n}))',scale={size}:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be",
             "-fps_mode", "passthrough", str(outdir / "f%04d.png")])
        no = len(list((CACHE / name / "orig").glob("*.png")))
        nu = len(list(outdir.glob("*.png")))
        print(f"  {name}: orig {no}, una {nu}")


def load_side(name, side, per_frame_px=3000, seed=7):
    rng = np.random.default_rng(seed)
    o_files = sorted((CACHE / name / "orig").glob("*.png"))
    s_files = sorted((CACHE / name / side).glob("*.png"))
    k = min(len(o_files), len(s_files))
    origs, sides = [], []
    for of, sf in zip(o_files[:k], s_files[:k]):
        o = read_png16(of).reshape(-1, 3)
        p = read_png16(sf).reshape(-1, 3)
        sel = rng.choice(o.shape[0], size=min(per_frame_px, o.shape[0]), replace=False)
        origs.append(o[sel])
        sides.append(p[sel])
    return np.concatenate(origs), np.concatenate(sides)


def recover_sliders(orig, target):
    """Best (s,t,c) with no WB sample explaining orig->target."""
    from scipy.optimize import minimize
    starts = [np.array(x) for x in
              ([0.3, 0, 0.3], [0.7, 0.45, 0.6], [1.0, 0.45, 0.0],
               [0.2, -0.4, 0.7], [0.95, 0.0, 0.0], [0.75, 0.25, 0.0])]
    best = None
    for x0 in starts:
        r = minimize(lambda q: float(huber_loss(
            slider_predict(np.array([q[0], q[1], q[2], 0, 0]), K_CURRENT, orig)
            - target).sum(axis=1).mean()),
            x0, method="L-BFGS-B", bounds=[(0, 1), (-1, 1), (0, 1)],
            options=dict(maxiter=300))
        if best is None or r.fun < best.fun:
            best = r
    return best.x


def main():
    extract()
    print("\n--- parity: GX010567 render vs Monastery-Shallow simulation ---")
    orig, una = load_side("GX010567", "una")
    ms = np.array([0.7030050712719298, 0.45, 0.5960286458333334, 0.0, 0.0])
    pred = slider_predict(ms, K_CURRENT, orig)
    print("  model-vs-render:", {k: round(v, 2) if not isinstance(v, tuple) else v
                                 for k, v in residual_stats(pred, una).items()})

    print("\n--- recovered sliders per Unabara render ---")
    out = {}
    for name in RENDERS:
        orig, una = load_side(name, "una")
        q = recover_sliders(orig, una)
        stats = residual_stats(slider_predict(np.array([*q, 0, 0]), K_CURRENT, orig), una)
        out[name] = dict(sliders=[round(float(x), 3) for x in q],
                         rms8=round(stats["rms8"], 2))
        print(f"  {name}: s={q[0]:.2f} t={q[1]:+.2f} c={q[2]:.2f} "
              f"(model-vs-render rms {stats['rms8']:.1f})", flush=True)
    (CACHE / "unabara_recovered.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
