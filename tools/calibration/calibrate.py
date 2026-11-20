#!/usr/bin/env python3
"""C6 calibration: fit GradeTuning constants + bundled presets against
reference edits (original clip, editor's graded export) pairs.

Subcommands:
  extract   decode sparse paired frames from originals + processed clips
            into a 16-bit PNG cache (one pass per file, every Nth frame)
  fit       per-clip free-parameter fits + staged model fits, residual
            report written as JSON next to the cache
  report    render REPORT.md from the fit JSON

The pixel model replicates GradeOps::apply() (src/core/color_grade.cpp):
  matrix (clamp01) -> black point (clamp01) -> red midtone lift
  x + g*4x^2(1-x) (clamp01) -> S-contrast about 0.5.
Ground truth stays the C++ — validate_model cross-checks against a
harness built from the real color_grade.cpp before any fitting.

Everything heavy (frames cache, fit JSON, report) lives under the
calibration footage directory, which is deliberately untracked.
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
CAL = REPO / "tests/videos/calibration_tests"
CACHE = CAL / "cache"

# Rec.709 luma (GradeTuning::kLumaR/G/B).
LUMA = np.array([0.2126, 0.7152, 0.0722])

# ---------------------------------------------------------------------------
# Clip manifest. weight: relative weight in the constant/preset fit stage.
# SportDiver_20260426 (near-irrecoverable dark-lake footage) was dropped
# from the corpus entirely on Arnaud's instruction (2026-11-08).
# ---------------------------------------------------------------------------
CLIPS = [
    dict(
        name="GH010359",
        original="originals/GH010359.MP4",
        orig_args=["-vf", "select='not(mod(n\\,97))',scale=480:270:in_range=pc:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        processed="processed/processed-argonaute/GH010359.mov",
        proc_args=["-vf", "select='not(mod(n\\,97))',scale=480:270:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        frames=1945,
        water="tropical blue ~6 m",
        weight=1.0,
    ),
    dict(
        name="GH010371",
        original="originals/GH010371.MP4",
        orig_args=["-vf", "select='not(mod(n\\,162))',scale=480:270:in_range=pc:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        processed="processed/processed-argonaute/GH010371.mov",
        proc_args=["-vf", "select='not(mod(n\\,162))',scale=480:270:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        frames=3244,
        water="tropical blue, deep/dark",
        weight=1.0,
    ),
    dict(
        name="GX010423",
        original="originals/GX010423.MP4",
        orig_args=["-vf", "select='not(mod(n\\,286))',scale=480:270:in_range=pc:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        processed="processed/processed-argonaute/GX010423.mov",
        proc_args=["-vf", "select='not(mod(n\\,286))',scale=480:270:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        frames=5723,
        water="temperate green ~20 m",
        weight=1.0,
    ),
    dict(
        name="GX010563",
        original="originals/GX010563.MP4",
        orig_args=["-vf", "select='not(mod(n\\,62))',scale=480:270:in_range=pc:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        processed="processed/processed-argonaute/GX010563.mov",
        proc_args=["-vf", "select='not(mod(n\\,62))',scale=480:270:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        frames=1241,
        water="temperate green ~25 m",
        weight=1.0,
    ),
    dict(
        name="SportDiver_20241221_093314",
        original="originals/SportDiver_20241221_093314.MP4",
        # The one smpte170m(bt601)-matrixed source in the corpus.
        orig_args=["-vf", "select='not(mod(n\\,34))',scale=480:270:in_range=pc:out_range=pc:in_color_matrix=smpte170m,format=rgb48be"],
        processed="processed/processed-argonaute/SportDiver_20241221_093314.mov",
        proc_args=["-vf", "select='not(mod(n\\,34))',scale=480:270:in_range=tv:out_range=pc:in_color_matrix=bt709,format=rgb48be"],
        frames=679,
        water="lake 8 m (Léman)",
        weight=1.0,
    ),
]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"FAILED: {' '.join(map(str, cmd))}\n{r.stderr[-2000:]}")
    return r


# ---------------------------------------------------------------------------
# extract
# ---------------------------------------------------------------------------

def cmd_extract(_args):
    for clip in CLIPS:
        for side, src, vf in (
            ("orig", CAL / clip["original"], clip["orig_args"]),
            ("proc", CAL / clip["processed"], clip["proc_args"]),
        ):
            outdir = CACHE / clip["name"] / side
            if outdir.is_dir() and list(outdir.glob("*.png")):
                print(f"skip {clip['name']}/{side} (cached)")
                continue
            outdir.mkdir(parents=True, exist_ok=True)
            print(f"extract {clip['name']}/{side} ...", flush=True)
            run([
                "ffmpeg", "-hide_banner", "-nostats", "-y", "-i", str(src),
                *vf, "-fps_mode", "passthrough",
                str(outdir / "f%04d.png"),
            ])
        no = len(list((CACHE / clip["name"] / "orig").glob("*.png")))
        np_ = len(list((CACHE / clip["name"] / "proc").glob("*.png")))
        print(f"  {clip['name']}: orig {no} frames, proc {np_} frames")
        if no != np_:
            sys.exit(f"frame count mismatch for {clip['name']}: {no} vs {np_}")


# ---------------------------------------------------------------------------
# model: replicates GradeOps::apply() on arrays of shape (N, 3)
# ---------------------------------------------------------------------------

def apply_ops(rgb, matrix, black, lift, contrast):
    v = rgb @ np.asarray(matrix).T
    v = np.clip(v, 0.0, 1.0)
    black = np.asarray(black)
    if np.any(black > 0.0):
        scale = np.where(black > 0.0, 1.0 - black, 1.0)
        v = np.clip((v - np.where(black > 0.0, black, 0.0)) / scale, 0.0, 1.0)
    lift = np.asarray(lift)
    if np.any(lift != 0.0):
        v = np.clip(v + lift * 4.0 * v * v * (1.0 - v), 0.0, 1.0)
    if contrast != 1.0:
        lo = v < 0.5
        v = np.where(lo,
                     0.5 * np.power(2.0 * np.clip(v, 0.0, None), contrast),
                     1.0 - 0.5 * np.power(2.0 * np.clip(1.0 - v, 0.0, None), contrast))
    return v


def build_matrix(kR, kG, kB, borrow, tint_r, tint_g, tint_b, red_k):
    """M = M_red · M_tint · M_wb exactly as resolveWithWb() composes it."""
    m = np.zeros((3, 3))
    m[0][0] = (1.0 - red_k) * tint_r * kR
    m[0][1] = (1.0 - red_k) * tint_r * borrow + red_k * tint_g * kG
    m[1][1] = tint_g * kG
    m[2][2] = tint_b * kB
    return m


def tint_gains(t, strength):
    t = np.clip(t, -1.0, 1.0)
    if t >= 0.0:
        g = np.array([1.0 + strength * t, 1.0 - strength * t, 1.0 + strength * t])
    else:
        g = np.array([1.0 + strength * -t, 1.0, 1.0 - strength * -t])
    return g / float(g @ LUMA)


# ---------------------------------------------------------------------------
# data loading
# ---------------------------------------------------------------------------

def read_png16(path):
    """Minimal 16-bit PNG reader via ffmpeg (no imageio dependency)."""
    r = subprocess.run(
        ["ffmpeg", "-hide_banner", "-nostats", "-i", str(path),
         "-f", "rawvideo", "-pix_fmt", "rgb48le", "-"],
        capture_output=True)
    if r.returncode != 0:
        sys.exit(f"png read failed: {path}\n{r.stderr[-500:].decode()}")
    m = re.search(rb", (\d+)x(\d+)", r.stderr)
    w, h = int(m.group(1)), int(m.group(2))
    return np.frombuffer(r.stdout, dtype="<u2").reshape(h, w, 3) / 65535.0


def load_pairs(clip, per_frame_px=4000, rng_seed=7):
    """Return (orig, proc, frame_idx) float arrays of paired pixels."""
    rng = np.random.default_rng(rng_seed)
    o_dir = CACHE / clip["name"] / "orig"
    p_dir = CACHE / clip["name"] / "proc"
    origs, procs, fidx = [], [], []
    o_files = sorted(o_dir.glob("*.png"))
    p_files = sorted(p_dir.glob("*.png"))
    assert len(o_files) == len(p_files)
    for i, (of, pf) in enumerate(zip(o_files, p_files)):
        o = read_png16(of).reshape(-1, 3)
        p = read_png16(pf).reshape(-1, 3)
        assert o.shape == p.shape, f"{of} vs {pf}: {o.shape} vs {p.shape}"
        sel = rng.choice(o.shape[0], size=min(per_frame_px, o.shape[0]), replace=False)
        origs.append(o[sel])
        procs.append(p[sel])
        fidx.append(np.full(len(sel), i))
    return np.concatenate(origs), np.concatenate(procs), np.concatenate(fidx)


# ---------------------------------------------------------------------------
# fitting
# ---------------------------------------------------------------------------

def huber_loss(res, delta=0.04):
    a = np.abs(res)
    return np.where(a <= delta, 0.5 * res * res, delta * (a - 0.5 * delta))


def free_predict(params, rgb):
    """Free-parameter model: diag WB gains + green->red borrow + red
    recovery borrow/lift + tint diagonal folded into the gains + black +
    contrast + global exposure gain (nuisance).

    Parameterised directly at the ops level:
      p = [m00, m01, m11, m22, black, lift_r, contrast, gain]
    which spans every matrix resolveWithWb() can produce (its matrix has
    exactly these four nonzero entries) without the slider coupling.
    """
    m00, m01, m11, m22, black, lift_r, contrast, gain = params
    m = np.array([[m00, m01, 0.0], [0.0, m11, 0.0], [0.0, 0.0, m22]])
    v = apply_ops(rgb * gain, m, [black] * 3, [lift_r, 0.0, 0.0], contrast)
    return v


FREE_X0 = np.array([1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0])
FREE_LO = np.array([0.3, 0.0, 0.3, 0.3, 0.0, 0.0, 0.6, 0.5])
FREE_HI = np.array([2.0, 1.0, 2.0, 2.0, 0.35, 0.8, 2.5, 3.0])


def fit_free(orig, proc, w=None, x0=None, fixed_gain=None):
    from scipy.optimize import minimize
    if w is None:
        w = np.ones(len(orig))
    w = w / w.sum()

    def objective(p):
        if fixed_gain is not None:
            p = np.append(p, fixed_gain)
        pred = free_predict(p, orig)
        return float((huber_loss(pred - proc).sum(axis=1) * w).sum())

    x0 = np.array(x0 if x0 is not None else FREE_X0)
    lo, hi = FREE_LO.copy(), FREE_HI.copy()
    if fixed_gain is not None:
        x0, lo, hi = x0[:-1], lo[:-1], hi[:-1]
    r = minimize(objective, x0, method="L-BFGS-B", bounds=list(zip(lo, hi)),
                 options=dict(maxiter=400))
    p = np.append(r.x, fixed_gain) if fixed_gain is not None else r.x
    return p, r.fun


def residual_stats(pred, proc):
    d = pred - proc
    rms = float(np.sqrt((d * d).mean()))
    rms8 = rms * 255.0
    dl = (d @ LUMA)
    luma_rms8 = float(np.sqrt((dl * dl).mean())) * 255.0
    chroma = d - dl[:, None]
    chroma_rms8 = float(np.sqrt((chroma * chroma).mean())) * 255.0
    mean8 = tuple(float(x) * 255.0 for x in d.mean(axis=0))
    return dict(rms8=rms8, luma_rms8=luma_rms8, chroma_rms8=chroma_rms8,
                mean8=mean8)


def identity_stats(orig, proc):
    return residual_stats(orig, proc)


def cmd_fit(args):
    results = {}
    for clip in CLIPS:
        name = clip["name"]
        print(f"=== {name} ===", flush=True)
        orig, proc, fidx = load_pairs(clip)

        # Null/identity baseline: how far apart are the pairs before any
        # model (also catches a broken pipeline: orig-vs-orig must be ~0).
        base = identity_stats(orig, proc)

        # Stage 1: full free fit.
        p, loss = fit_free(orig, proc)
        pred = free_predict(p, orig)
        stats = residual_stats(pred, proc)

        # Stage 2: no-exposure fit (gain pinned to 1) — what the shipped
        # model (which has no exposure control) can reach.
        p_ng, _ = fit_free(orig, proc, x0=np.append(FREE_X0[:-1], 1.0),
                           fixed_gain=1.0)
        stats_ng = residual_stats(free_predict(p_ng, orig), proc)

        # Stationarity: independent fit per thirds of the clip.
        drift = []
        for third in range(3):
            m = (fidx * 3 // (fidx.max() + 1)) == third
            pt, _ = fit_free(orig[m], proc[m], x0=p)
            drift.append([round(float(x), 4) for x in pt])

        results[name] = dict(
            water=clip["water"], weight=clip["weight"],
            n_pixels=int(len(orig)), n_frames=int(fidx.max() + 1),
            identity=base,
            free=dict(params=[round(float(x), 4) for x in p],
                      param_names=["m00", "m01(g->r)", "m11", "m22",
                                   "black", "lift_r", "contrast", "gain"],
                      **stats),
            no_gain=dict(params=[round(float(x), 4) for x in p_ng], **stats_ng),
            drift_thirds=drift,
        )
        print(f"  identity rms {base['rms8']:.1f} /255 -> free fit rms "
              f"{stats['rms8']:.1f} (gain {p[-1]:.2f}) | no-gain rms "
              f"{stats_ng['rms8']:.1f}")

    out = CACHE / "fits.json"
    out.write_text(json.dumps(results, indent=1))
    print(f"wrote {out}")


# ---------------------------------------------------------------------------
# stage 2: fit within the real slider + eyedropper parameterization
# ---------------------------------------------------------------------------

# Constants vector K = [kRedBorrow, kRedMidtoneLift, kTintStrength,
#                       kClarityBlack, kClarityContrast]
K_CURRENT = np.array([0.5, 0.35, 0.25, 0.12, 0.6])
K_NAMES = ["kRedBorrow", "kRedMidtoneLift", "kTintStrength",
           "kClarityBlack", "kClarityContrast"]
WB_GAIN_MIN, WB_GAIN_MAX, WB_BORROW_MAX, WB_EPS = 0.5, 2.0, 0.8, 1e-4


def wb_gains_from_sample(sr, sg, sb):
    """numpy clone of ColorGrade::resolveWbGains()."""
    y = float(np.array([sr, sg, sb]) @ LUMA)
    kG = np.clip(y / max(sg, WB_EPS), WB_GAIN_MIN, WB_GAIN_MAX)
    kB = np.clip(y / max(sb, WB_EPS), WB_GAIN_MIN, WB_GAIN_MAX)
    kRraw = y / max(sr, WB_EPS)
    kR = np.clip(kRraw, WB_GAIN_MIN, WB_GAIN_MAX)
    borrow = 0.0
    if kRraw > WB_GAIN_MAX:
        borrow = np.clip((y - WB_GAIN_MAX * sr) / max(sg, WB_EPS),
                         0.0, WB_BORROW_MAX)
    return kR, kG, kB, borrow


def slider_predict(sliders, K, rgb):
    """The shipped model: sliders = (s, t, c, log_sg, log_sb) where the
    neutral sample is (1, exp(log_sg), exp(log_sb)); hasNeutralSample is
    assumed (the eyedropper flow). Returns graded rgb."""
    s, t, c, lsg, lsb = sliders
    kRB, kRL, kTS, kCB, kCC = K
    s = float(np.clip(s, 0.0, 1.0))
    c = float(np.clip(c, 0.0, 1.0))
    kR, kG, kB, borrow = wb_gains_from_sample(1.0, math.exp(lsg), math.exp(lsb))
    tg = tint_gains(float(np.clip(t, -1.0, 1.0)), kTS)
    k = kRB * s
    m = build_matrix(kR, kG, kB, borrow, tg[0], tg[1], tg[2], k)
    m = np.clip(m, -2.0, 2.0)  # colorchannelmixer bound, as resolve() clamps
    return apply_ops(rgb, m, [kCB * c] * 3, [kRL * s, 0.0, 0.0],
                     1.0 + kCC * c)


SLIDER_STARTS = [
    np.array([0.3, 0.0, 0.3, 0.0, 0.0]),
    np.array([0.8, 0.5, 0.5, 0.4, 0.2]),    # strong magenta/green-water
    np.array([0.6, -0.5, 0.3, -0.2, 0.3]),  # warm/blue-water
    np.array([1.0, 0.2, 0.2, 0.9, 0.5]),    # heavy green-dominant sample
]
SLIDER_LO = np.array([0.0, -1.0, 0.0, -1.2, -1.2])
SLIDER_HI = np.array([1.0, 1.0, 1.0, 1.6, 1.6])


def fit_sliders(orig, proc, K, starts=None):
    from scipy.optimize import minimize
    best = None
    for x0 in (starts or SLIDER_STARTS):
        r = minimize(
            lambda p: float(huber_loss(slider_predict(p, K, orig) - proc).sum(axis=1).mean()),
            x0, method="L-BFGS-B",
            bounds=list(zip(SLIDER_LO, SLIDER_HI)), options=dict(maxiter=300))
        if best is None or r.fun < best.fun:
            best = r
    return best.x, best.fun


def cmd_fit2(_args):
    from scipy.optimize import minimize
    data = []
    for clip in CLIPS:
        orig, proc, _ = load_pairs(clip, per_frame_px=1500)
        data.append((clip, orig, proc))

    results = {}

    # Fit A: current constants, per-clip best sliders.
    print("--- Fit A: current constants ---", flush=True)
    slidersA = []
    for clip, orig, proc in data:
        p, loss = fit_sliders(orig, proc, K_CURRENT)
        stats = residual_stats(slider_predict(p, K_CURRENT, orig), proc)
        slidersA.append(p)
        results[clip["name"]] = dict(current=dict(
            sliders=[round(float(x), 4) for x in p], **stats))
        print(f"  {clip['name']}: s={p[0]:.2f} t={p[1]:+.2f} c={p[2]:.2f} "
              f"sample=(1,{math.exp(p[3]):.2f},{math.exp(p[4]):.2f}) "
              f"rms {stats['rms8']:.1f}", flush=True)

    # Fit B: constants free (joint, weighted), sliders refit per candidate.
    print("--- Fit B: joint constants ---", flush=True)
    weights = np.array([c["weight"] for c, _, _ in data])

    def joint_obj(K):
        total = 0.0
        for (clip, orig, proc), w, x0 in zip(data, weights, slidersA):
            p, loss = fit_sliders(orig, proc, K, starts=[x0])
            total += w * loss
        return total / weights.sum()

    K_LO = np.array([0.2, 0.1, 0.1, 0.04, 0.2])
    K_HI = np.array([0.8, 0.8, 0.5, 0.25, 1.6])
    r = minimize(joint_obj, K_CURRENT, method="Nelder-Mead",
                 bounds=list(zip(K_LO, K_HI)),
                 options=dict(maxiter=120, xatol=5e-3, fatol=1e-6))
    K_fit = r.x
    print("  K current:", list(K_CURRENT))
    print("  K fitted :", [round(float(x), 4) for x in K_fit])

    for (clip, orig, proc), x0 in zip(data, slidersA):
        p, _ = fit_sliders(orig, proc, K_fit, starts=[x0] + SLIDER_STARTS)
        stats = residual_stats(slider_predict(p, K_fit, orig), proc)
        results[clip["name"]]["fitted"] = dict(
            sliders=[round(float(x), 4) for x in p], **stats)
        print(f"  {clip['name']}: s={p[0]:.2f} t={p[1]:+.2f} c={p[2]:.2f} "
              f"rms {stats['rms8']:.1f} (was {results[clip['name']]['current']['rms8']:.1f})",
              flush=True)

    out = CACHE / "fits2.json"
    out.write_text(json.dumps(dict(
        K_current=list(K_CURRENT), K_fitted=[float(x) for x in K_fit],
        K_names=K_NAMES, clips=results), indent=1))
    print(f"wrote {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("extract")
    sub.add_parser("fit")
    sub.add_parser("fit2")
    args = ap.parse_args()
    CACHE.mkdir(parents=True, exist_ok=True)
    if args.cmd == "extract":
        cmd_extract(args)
    elif args.cmd == "fit":
        cmd_fit(args)
    elif args.cmd == "fit2":
        cmd_fit2(args)


if __name__ == "__main__":
    main()
