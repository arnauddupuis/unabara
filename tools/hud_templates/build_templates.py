#!/usr/bin/env python3
"""Generator for the bundled HUD / Social template family.

Single source of truth for the HUD template designs: each design defines its
data-cell slots (center coordinates in background pixels) once, and this
script derives BOTH sides from it:

  * the background art  -> resources/images/HUD/<name>.svg + .png (via inkscape)
  * the cell layout     -> resources/templates/<Name>.utp

Cell positions are computed with the exact font math of
OverlayGenerator::renderCellBasedOverlay (pixelSize = int(pt*1.33*1.8),
QFontMetrics bounds + 8px padding, top-left anchored) by shelling out to
`render_utp --measure` (build with -DUNABARA_BUILD_TOOLS=ON first).

Usage:  python3 tools/hud_templates/build_templates.py [--svg-only|--utp-only]
"""

import json
import math
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IMG_DIR = os.path.join(ROOT, "resources", "images", "HUD")
TPL_DIR = os.path.join(ROOT, "resources", "templates")
FONTS_DIR = os.path.join(ROOT, "resources", "fonts")
RENDER_UTP = os.path.join(ROOT, "build", "bin", "render_utp")

ORBITRON = "Orbitron"
MONO = "Share Tech Mono"

# ---------------------------------------------------------------------------
# Text measurement (mirrors the C++ renderer via render_utp --measure)
# ---------------------------------------------------------------------------

_measure_cache = {}


def measure(family, pt, text):
    """Return (cell_w, cell_h): the on-canvas footprint of a text block."""
    key = (family, pt, text)
    if key not in _measure_cache:
        out = subprocess.run(
            [RENDER_UTP, "--measure", family, str(pt), text.replace("\n", "\\n")],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        fields = dict(p.split("=") for p in out.split() if "=" in p)
        _measure_cache[key] = (int(fields["cell_w"]), int(fields["cell_h"]))
    return _measure_cache[key]


# ---------------------------------------------------------------------------
# .utp construction
# ---------------------------------------------------------------------------


def argb(hex6, alpha=255):
    return "#%02x%s" % (alpha, hex6.lstrip("#").lower())


SHADOW_OFF = {
    "enabled": False,
    "type": "offset",
    "color": "#ff000000",
    "size": 2,
    "opacity": 0.7,
}


def font_json(family, pt, weight=400):
    return {
        "family": family,
        "pointSize": pt,
        "weight": weight,
        "bold": weight >= 700,
        "italic": False,
    }


def cell(
    cell_id,
    cell_type,
    cx,
    cy,
    family,
    pt,
    sample,
    canvas,
    label_color,
    value_color,
    weight=400,
    show_label=True,
    visible=True,
    tank_index=-1,
    shadow=None,
):
    """Build one .utp cell dict, positioned so the rendered text block for
    `sample` is centered on (cx, cy)."""
    w, h = measure(family, pt, sample if show_label else sample.split("\n")[-1])
    cw, ch = canvas
    x = max(0.0, min(1.0, (cx - w / 2.0) / cw))
    y = max(0.0, min(1.0, (cy - h / 2.0) / ch))
    sh = shadow if shadow is not None else SHADOW_OFF
    c = {
        "cellId": cell_id,
        "cellType": cell_type,
        "position": {"x": round(x, 6), "y": round(y, 6)},
        "visible": visible,
        "calculatedSize": {"width": w, "height": h},
        "font": font_json(family, pt, weight),
        "hasCustomFont": True,
        "labelColor": label_color,
        "valueColor": value_color,
        "textColor": value_color,
        "hasCustomLabelColor": True,
        "hasCustomValueColor": True,
        "hasCustomColor": True,
        "showLabel": show_label,
        "hasCustomShowLabel": not show_label,
        "shadowEnabled": sh["enabled"],
        "shadowType": sh["type"],
        "shadowColor": sh["color"],
        "shadowSize": sh["size"],
        "shadowOpacity": sh["opacity"],
        "hasCustomShadow": shadow is not None,
    }
    if tank_index >= 0:
        c["tankIndex"] = tank_index
    return c


def template_json(
    name,
    image_alias,
    default_font,
    label_color,
    value_color,
    cells,
    shadow=None,
    primary_color=None,
    secondary_color=None,
):
    # v1.1: defaultPrimaryColor/defaultSecondaryColor let the app offer to
    # theme the dive profile with the template's color scheme
    sh = shadow if shadow is not None else SHADOW_OFF
    return {
        "version": "1.1",
        "defaultPrimaryColor": primary_color or label_color,
        "defaultSecondaryColor": secondary_color or value_color,
        "templateName": name,
        "backgroundImage": ":/images/HUD/%s" % image_alias,
        "backgroundOpacity": 1.0,
        "defaultFont": default_font,
        "defaultLabelColor": label_color,
        "defaultValueColor": value_color,
        "defaultTextColor": value_color,
        "defaultShadowEnabled": sh["enabled"],
        "defaultShadowType": sh["type"],
        "defaultShadowColor": sh["color"],
        "defaultShadowSize": sh["size"],
        "defaultShadowOpacity": sh["opacity"],
        "cells": cells,
    }


# Representative display strings (matching OverlayGenerator::generateCellDisplayText
# with metric units) used to center each cell in its slot.
S = {
    "depth": "DEPTH\n18.4 m",
    "temperature": "TEMP\n24.0°C",
    "time": "DIVE TIME\n20:00",
    "ndl": "NDL\n12 min",
    "tank1": "TANK 1 (32%)\n165 bar",
    "tank_pair0": "T 1 (32%)\n165 bar",
    "tank_pair1": "T 2 (21/35)\n200 bar",
    "po2": "CELL 1\n1.21",
    "po2c": "PO2\n1.23",
    "cns": "CNS\n18%",
    "max": "MAX\n32.6 m",
    "avg": "AVG\n14.2 m",
    "gas": "GAS\nEAN32",
    "stop_depth": "STOP\n12.0 m",
    "stop_time": "TIME\n4 min",
}

# ---------------------------------------------------------------------------
# SVG helpers
# ---------------------------------------------------------------------------

GLOW = (
    '<filter id="glow" x="-60%" y="-60%" width="220%" height="220%">'
    '<feGaussianBlur stdDeviation="4" result="b"/>'
    '<feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>'
    "</filter>"
)


def svg_doc(w, h, body, defs=""):
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        'width="%d" height="%d" viewBox="0 0 %d %d">\n<defs>%s</defs>\n%s\n</svg>\n'
        % (w, h, w, h, defs, body)
    )


def brackets(x, y, w, h, arm, color, sw=3, op=0.9):
    """Four corner brackets around rect (x,y,w,h)."""
    p = []
    for cx, cy, dx, dy in (
        (x, y, 1, 1),
        (x + w, y, -1, 1),
        (x, y + h, 1, -1),
        (x + w, y + h, -1, -1),
    ):
        p.append("M %g %g h %g M %g %g v %g" % (cx, cy, dx * arm, cx, cy, dy * arm))
    return (
        '<path d="%s" stroke="%s" stroke-width="%g" fill="none" '
        'opacity="%g" stroke-linecap="square"/>' % (" ".join(p), color, sw, op)
    )


def ticks_h(x1, x2, y, step, color, sw=1.5, op=0.45, ln=7, long_every=5, long_ln=12):
    p, i = [], 0
    x = x1
    while x <= x2:
        l = long_ln if (long_every and i % long_every == 0) else ln
        p.append("M %g %g v %g" % (x, y, l))
        x += step
        i += 1
    return '<path d="%s" stroke="%s" stroke-width="%g" opacity="%g"/>' % (
        " ".join(p),
        color,
        sw,
        op,
    )


def tick_ring(cx, cy, r, n, color, sw=1.5, op=0.7, ln=8, long_every=6, long_ln=18):
    p = []
    for i in range(n):
        a = 2 * math.pi * i / n
        l = long_ln if i % long_every == 0 else ln
        x1, y1 = cx + r * math.cos(a), cy + r * math.sin(a)
        x2, y2 = cx + (r - l) * math.cos(a), cy + (r - l) * math.sin(a)
        p.append("M %.2f %.2f L %.2f %.2f" % (x1, y1, x2, y2))
    return '<path d="%s" stroke="%s" stroke-width="%g" opacity="%g"/>' % (
        " ".join(p),
        color,
        sw,
        op,
    )


def arc(cx, cy, r, a1_deg, a2_deg, color, sw, op=0.9, glow=False):
    a1, a2 = math.radians(a1_deg), math.radians(a2_deg)
    x1, y1 = cx + r * math.cos(a1), cy + r * math.sin(a1)
    x2, y2 = cx + r * math.cos(a2), cy + r * math.sin(a2)
    large = 1 if (a2_deg - a1_deg) % 360 > 180 else 0
    g = ' filter="url(#glow)"' if glow else ""
    return (
        '<path d="M %.2f %.2f A %g %g 0 %d 1 %.2f %.2f" stroke="%s" '
        'stroke-width="%g" fill="none" opacity="%g" stroke-linecap="round"%s/>'
        % (x1, y1, r, r, large, x2, y2, color, sw, op, g)
    )


def hexagon(cx, cy, r, color, sw=3, op=0.9, fill="none", fill_op=1.0, glow=False):
    pts = []
    for i in range(6):
        a = math.pi / 2 + math.pi * i / 3
        pts.append("%.2f,%.2f" % (cx + r * math.cos(a), cy + r * math.sin(a)))
    g = ' filter="url(#glow)"' if glow else ""
    return (
        '<polygon points="%s" stroke="%s" stroke-width="%g" fill="%s" '
        'fill-opacity="%g" opacity="%g"%s/>'
        % (" ".join(pts), color, sw, fill, fill_op, op, g)
    )


def rrect(x, y, w, h, r, fill, op, stroke="none", sw=0, sop=1.0):
    s = (
        '<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" '
        'fill-opacity="%g"' % (x, y, w, h, r, fill, op)
    )
    if stroke != "none":
        s += ' stroke="%s" stroke-width="%g" stroke-opacity="%g"' % (stroke, sw, sop)
    return s + "/>"


def line(x1, y1, x2, y2, color, sw=1.5, op=0.5, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    return (
        '<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g" '
        'opacity="%g"%s/>' % (x1, y1, x2, y2, color, sw, op, d)
    )


def text(
    x, y, s, size, color, op=1.0, ls=3, weight=600, anchor="middle", family=ORBITRON
):
    return (
        '<text x="%g" y="%g" font-family="%s" font-size="%g" font-weight="%d" '
        'letter-spacing="%g" fill="%s" fill-opacity="%g" text-anchor="%s">%s</text>'
        % (x, y, family, size, weight, ls, color, op, anchor, s)
    )


def scan_pattern(color, pid="scan"):
    return (
        '<pattern id="%s" width="4" height="8" patternUnits="userSpaceOnUse">'
        '<rect width="4" height="2" fill="%s" opacity="0.10"/></pattern>' % (pid, color)
    )


def chevrons(x, y, size, color, n=2, op=0.6, sw=3):
    p = []
    for i in range(n):
        ox = x + i * (size * 0.9)
        p.append("M %g %g l %g %g l %g %g" % (ox, y, size, size, -size, size))
    return (
        '<path d="%s" stroke="%s" stroke-width="%g" fill="none" opacity="%g" '
        'stroke-linecap="square"/>' % (" ".join(p), color, sw, op)
    )


DARK = "#0A1014"

# ===========================================================================
# Design 1 — HUD Tactical Bar (1920x280) — OC rec lower third
# ===========================================================================

TB_W, TB_H = 1920, 280
TB_SLOTS = [430, 850, 1270, 1690]  # cell centers, cy = 140


def tactical_bar_svg(p):
    a = p["accent"]
    b = [rrect(20, 20, 1880, 240, 8, "#0A1216", 0.55)]
    b.append(line(20, 24, 200, 24, a, 6, 1.0))
    b.append(line(220, 24, 1900, 24, a, 3, 0.9))
    b.append(ticks_h(240, 1880, 32, 40, a, long_ln=10))
    b.append(line(36, 60, 36, 220, a, 5, 0.95))
    b.append(text(54, 254, "UNABARA", 16, a, 0.8, 4, 600, "start"))
    for cx in TB_SLOTS:
        b.append(brackets(cx - 180, 45, 360, 190, 30, a, 3, 0.9))
        b.append(
            '<rect x="%g" y="45" width="360" height="190" fill="%s" '
            'opacity="0.04"/>' % (cx - 180, a)
        )
        b.append(chevrons(cx - 168, 58, 7, a, 2, 0.55, 2.5))
    for x in (640, 1060, 1480):
        b.append(line(x, 60, x, 220, a, 1, 0.18))
    for i, x in enumerate((1810, 1832, 1854)):
        b.append(
            '<rect x="%d" y="238" width="14" height="10" fill="%s" '
            'opacity="%g"/>' % (x, a, (1.0, 0.55, 0.25)[i])
        )
    return svg_doc(TB_W, TB_H, "\n".join(b))


def tactical_bar_cells(p):
    cv = (TB_W, TB_H)
    lc, vc = p["label"], p["value"]
    cs = []
    for cx, (cid, ctype, skey) in zip(
        TB_SLOTS,
        [
            ("depth", "Depth", "depth"),
            ("time", "Time", "time"),
            ("temperature", "Temperature", "temperature"),
            ("ndl", "NDL", "ndl"),
        ],
    ):
        cs.append(cell(cid, ctype, cx, 140, MONO, 26, S[skey], cv, lc, vc))
    # Pre-positioned deco cells (hidden; enable via Display Options)
    cs.append(
        cell(
            "stop_depth",
            "StopDepth",
            120,
            105,
            MONO,
            16,
            S["stop_depth"],
            cv,
            lc,
            vc,
            visible=False,
        )
    )
    cs.append(
        cell(
            "stop_time",
            "StopTime",
            120,
            195,
            MONO,
            16,
            S["stop_time"],
            cv,
            lc,
            vc,
            visible=False,
        )
    )
    return cs


# ===========================================================================
# Design 2 — HUD Sonar Reticle (900x900) — OC 1-tank
# ===========================================================================

SR_W = SR_H = 900


def sonar_svg(p):
    a = p["accent"]
    c = 450
    b = ['<circle cx="450" cy="450" r="425" fill="#06090C" opacity="0.38"/>']
    b.append(
        '<circle cx="450" cy="450" r="428" fill="none" stroke="%s" '
        'stroke-width="2.5" opacity="0.95" filter="url(#glow)"/>' % a
    )
    b.append(tick_ring(c, c, 424, 72, a))
    b.append(
        '<circle cx="450" cy="450" r="392" fill="none" stroke="%s" '
        'stroke-width="1.5" opacity="0.35" stroke-dasharray="4 10"/>' % a
    )
    b.append(
        '<circle cx="450" cy="450" r="260" fill="none" stroke="%s" '
        'stroke-width="1.5" opacity="0.10"/>' % a
    )
    b.append(
        '<circle cx="450" cy="450" r="175" fill="none" stroke="%s" '
        'stroke-width="1.5" opacity="0.14"/>' % a
    )
    b.append(arc(c, c, 410, -125, -35, a, 7, 0.9, glow=True))
    b.append(arc(c, c, 410, 40, 75, a, 4, 0.5))
    # crosshair notches N/E/S/W
    n = []
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        n.append(
            "M %g %g L %g %g" % (c + dx * 440, c + dy * 440, c + dx * 414, c + dy * 414)
        )
    b.append(
        '<path d="%s" stroke="%s" stroke-width="3.5" opacity="1"/>' % (" ".join(n), a)
    )
    b.append(text(450, 862, "UNABARA", 14, a, 0.6, 5, 600))
    return svg_doc(SR_W, SR_H, "\n".join(b), GLOW)


def sonar_cells(p):
    # Cell centers hand-tuned in the template editor (Aug 2026) — they sit
    # slightly off the geometric slot centers to compensate for glyph bearings.
    cv = (SR_W, SR_H)
    lc, vc = p["label"], p["value"]
    return [
        cell(
            "depth",
            "Depth",
            422,
            449,
            ORBITRON,
            40,
            S["depth"],
            cv,
            lc,
            vc,
            weight=700,
            show_label=False,
        ),
        cell("time", "Time", 450, 195, MONO, 17, S["time"], cv, lc, vc),
        cell(
            "temperature",
            "Temperature",
            139,
            450,
            MONO,
            17,
            S["temperature"],
            cv,
            lc,
            vc,
        ),
        cell("ndl", "NDL", 754, 450, MONO, 17, S["ndl"], cv, lc, vc),
        cell(
            "tank_0",
            "Pressure",
            464,
            700,
            MONO,
            17,
            S["tank1"],
            cv,
            lc,
            vc,
            tank_index=0,
        ),
    ]


# ===========================================================================
# Design 3 — HUD Neon Deck (1100x700) — cyberpunk chamfered panel, OC 1-tank
# ===========================================================================

ND_W, ND_H = 1100, 700
ND_PANEL = "100,60 960,60 1040,140 1040,600 1000,640 160,640 60,540 60,100"


def neon_deck_svg(p):
    a, g2 = p["accent"], p["glitch"]
    defs = (
        GLOW
        + scan_pattern(a)
        + ('<clipPath id="panelclip"><polygon points="%s"/></clipPath>' % ND_PANEL)
    )
    b = [
        '<polygon points="%s" fill="#0B0F14" fill-opacity="0.58" stroke="%s" '
        'stroke-width="3" filter="url(#glow)"/>' % (ND_PANEL, a)
    ]
    b.append(
        '<rect x="60" y="60" width="980" height="580" fill="url(#scan)" '
        'clip-path="url(#panelclip)"/>'
    )
    b.append(
        '<polygon points="114,74 950,74 1026,148 1026,594 992,626 172,626 '
        '74,530 74,112" fill="none" stroke="%s" stroke-width="1" '
        'opacity="0.30" stroke-dasharray="12 6"/>' % a
    )
    # glitch bars on the top edge
    b.append('<rect x="830" y="52" width="90" height="6" fill="%s"/>' % a)
    b.append(
        '<rect x="838" y="60" width="90" height="6" fill="%s" opacity="0.6"/>' % g2
    )
    # depth sub-panel
    b.append(
        '<polygon points="140,150 470,150 470,530 440,560 110,560 110,180" '
        'fill="%s" fill-opacity="0.05" stroke="%s" stroke-width="2" '
        'opacity="0.85"/>' % (a, a)
    )
    b.append(text(290, 212, "DEPTH", 22, a, 0.9, 6, 700))
    b.append(line(180, 232, 400, 232, a, 1.5, 0.4))
    # right 2x2 grid separators
    b.append(line(785, 170, 785, 550, a, 1, 0.2))
    b.append(line(540, 360, 1010, 360, a, 1, 0.2))
    b.append(text(986, 620, "UNABARA", 13, "#FFFFFF", 0.35, 5, 500, "end"))
    return svg_doc(ND_W, ND_H, "\n".join(b), defs)


def neon_deck_cells(p):
    # Depth center and right-column nudges hand-tuned in the template editor
    # (Aug 2026). Note: the editor's font controls silently reset Orbitron
    # weight 600 -> 400 when a cell's text settings are touched — keep 600.
    cv = (ND_W, ND_H)
    lc, vc = p["label"], p["value"]
    return [
        cell(
            "depth",
            "Depth",
            263,
            355,
            ORBITRON,
            34,
            S["depth"],
            cv,
            lc,
            vc,
            weight=600,
            show_label=False,
        ),
        cell("time", "Time", 655, 260, MONO, 19, S["time"], cv, lc, vc),
        cell(
            "temperature",
            "Temperature",
            915,
            260,
            MONO,
            19,
            S["temperature"],
            cv,
            lc,
            vc,
        ),
        cell("ndl", "NDL", 662, 455, MONO, 19, S["ndl"], cv, lc, vc),
        cell(
            "tank_0",
            "Pressure",
            935,
            455,
            MONO,
            19,
            S["tank1"],
            cv,
            lc,
            vc,
            tank_index=0,
        ),
    ]


# ===========================================================================
# Design 4 — HUD Data Rail (380x1080) — vertical tek side panel
# ===========================================================================

DR_W, DR_H = 380, 1080
DR_SEPS = (250, 360, 470, 580, 690, 800, 910)


def data_rail_svg(p):
    a = p["accent"]
    b = [rrect(14, 26, 352, 1030, 10, DARK, 0.55)]
    b.append(line(30, 46, 30, 1036, a, 3, 0.9))
    b.append(text(205, 74, "UNABARA", 13, a, 0.8, 4, 600))
    b.append(line(52, 88, 352, 88, a, 1.5, 0.5))
    for y in DR_SEPS:
        b.append(line(52, y, 352, y, a, 1, 0.18))
        b.append(
            '<rect x="26" y="%g" width="9" height="9" fill="%s" '
            'opacity="0.9"/>' % (y - 4.5, a)
        )
    b.append(brackets(48, 94, 312, 148, 18, a, 2, 0.6))  # depth row highlight
    return svg_doc(DR_W, DR_H, "\n".join(b))


def data_rail_cells(p):
    cv = (DR_W, DR_H)
    lc, vc = p["label"], p["value"]
    # Tank pair x-centers hand-tuned in the template editor (Aug 2026: T2's
    # wider trimix label needed room).
    cx, lft, rgt = 205, 118, 292
    tank_lft, tank_rgt = 115, 300
    return [
        cell(
            "depth", "Depth", cx, 165, ORBITRON, 24, S["depth"], cv, lc, vc, weight=600
        ),
        cell("time", "Time", cx, 305, MONO, 16, S["time"], cv, lc, vc),
        cell(
            "temperature",
            "Temperature",
            cx,
            415,
            MONO,
            16,
            S["temperature"],
            cv,
            lc,
            vc,
        ),
        cell("ndl", "NDL", cx, 525, MONO, 16, S["ndl"], cv, lc, vc),
        cell(
            "stop_depth", "StopDepth", lft, 635, MONO, 13, S["stop_depth"], cv, lc, vc
        ),
        cell("stop_time", "StopTime", rgt, 635, MONO, 13, S["stop_time"], cv, lc, vc),
        cell("max_depth", "MaxDepth", lft, 745, MONO, 13, S["max"], cv, lc, vc),
        cell("mean_depth", "MeanDepth", rgt, 745, MONO, 13, S["avg"], cv, lc, vc),
        cell("gas", "Gas", lft, 855, MONO, 13, S["gas"], cv, lc, vc),
        cell("cns", "CNS", rgt, 855, MONO, 13, S["cns"], cv, lc, vc),
        cell(
            "tank_0",
            "Pressure",
            tank_lft,
            965,
            MONO,
            12,
            S["tank_pair0"],
            cv,
            lc,
            vc,
            tank_index=0,
        ),
        cell(
            "tank_1",
            "Pressure",
            tank_rgt,
            965,
            MONO,
            12,
            S["tank_pair1"],
            cv,
            lc,
            vc,
            tank_index=1,
        ),
    ]


# ===========================================================================
# Design 5 — HUD CCR Console (1600x400)
# ===========================================================================

CC_W, CC_H = 1600, 400
CC_HEX = [170, 390, 610]  # PO2 cells 1-3, cy 175; composite at 830


def ccr_console_svg(p):
    a = p["accent"]
    b = [rrect(28, 36, 1544, 328, 10, DARK, 0.55)]
    b.append(brackets(28, 36, 1544, 328, 130, a, 4, 0.9))
    for cx in CC_HEX:
        b.append(hexagon(cx, 175, 92, a, 3, 0.9, glow=True))
    b.append(hexagon(830, 175, 98, a, 3, 0.95, glow=True))
    b.append(hexagon(830, 175, 84, a, 1, 0.5))
    # Caption sizes user-tuned Aug 2026: 16pt in inkscape terms (~21.3px)
    b.append(text(390, 72, "PO2 MONITOR", 21.3, a, 0.6, 4, 600))
    b.append(line(950, 70, 950, 330, a, 1.5, 0.25))
    # segmented status bar bottom right
    seg = []
    for i in range(20):
        op = 0.9 if i < 7 else 0.18
        seg.append(
            '<rect x="%g" y="346" width="18" height="8" fill="%s" '
            'opacity="%g"/>' % (990 + i * 26, a, op)
        )
    b.append("".join(seg))
    b.append(text(60, 352, "UNABARA CCR", 21.3, a, 0.55, 3, 600, "start"))
    return svg_doc(CC_W, CC_H, "\n".join(b), GLOW)


def ccr_console_cells(p):
    cv = (CC_W, CC_H)
    lc, vc, pc = p["label"], p["value"], p["po2"]
    cs = []
    for i, cx in enumerate(CC_HEX):
        cs.append(
            cell(
                "po2_cell%d" % (i + 1),
                "PO2Cell%d" % (i + 1),
                cx,
                175,
                MONO,
                17,
                S["po2"],
                cv,
                pc,
                pc,
            )
        )
    cs.append(
        cell("composite_po2", "CompositePO2", 830, 175, MONO, 19, S["po2c"], cv, pc, pc)
    )
    cs.append(cell("depth", "Depth", 1090, 135, MONO, 21, S["depth"], cv, lc, vc))
    cs.append(cell("time", "Time", 1390, 135, MONO, 19, S["time"], cv, lc, vc))
    cs.append(
        cell(
            "temperature",
            "Temperature",
            1090,
            290,
            MONO,
            19,
            S["temperature"],
            cv,
            lc,
            vc,
        )
    )
    cs.append(cell("ndl", "NDL", 1390, 290, MONO, 19, S["ndl"], cv, lc, vc))
    return cs


# ===========================================================================
# Design 6 — Broadcast Lower Third (1920x220) — clean, no sci-fi ornament
# ===========================================================================

BC_W, BC_H = 1920, 220
BC_SLOTS = [320, 800, 1250, 1650]
BC_SHADOW = {
    "enabled": True,
    "type": "outline",
    "color": "#ff000000",
    "size": 2,
    "opacity": 0.75,
}


def broadcast_svg(p):
    a = p["accent"]
    b = [rrect(30, 28, 1860, 164, 12, "#07090B", 0.55)]
    b.append(line(30, 196, 320, 196, a, 5, 1.0))
    b.append(line(320, 196, 1890, 196, a, 2, 0.4))
    b.append('<rect x="44" y="46" width="6" height="128" fill="%s"/>' % a)
    for x in (560, 1025, 1450):
        b.append('<circle cx="%d" cy="110" r="3" fill="#FFFFFF" opacity="0.25"/>' % x)
    return svg_doc(BC_W, BC_H, "\n".join(b))


def broadcast_cells(p):
    cv = (BC_W, BC_H)
    lc, vc = p["label"], p["value"]
    cs = []
    for cx, (cid, ctype, skey) in zip(
        BC_SLOTS,
        [
            ("depth", "Depth", "depth"),
            ("time", "Time", "time"),
            ("temperature", "Temperature", "temperature"),
            ("ndl", "NDL", "ndl"),
        ],
    ):
        cs.append(
            cell(cid, ctype, cx, 110, ORBITRON, 20, S[skey], cv, lc, vc, weight=500)
        )
    cs.append(
        cell(
            "stop_depth",
            "StopDepth",
            1840,
            70,
            ORBITRON,
            14,
            S["stop_depth"],
            cv,
            lc,
            vc,
            weight=500,
            visible=False,
        )
    )
    cs.append(
        cell(
            "stop_time",
            "StopTime",
            1840,
            155,
            ORBITRON,
            14,
            S["stop_time"],
            cv,
            lc,
            vc,
            weight=500,
            visible=False,
        )
    )
    return cs


# ===========================================================================
# Design 7 — Social Stack HUD (520x1120) — depth/time/temp widget
# ===========================================================================

SS_W, SS_H = 520, 1120
SS_SHADOW = {
    "enabled": True,
    "type": "outline",
    "color": "#ff000000",
    "size": 2,
    "opacity": 0.7,
}


def social_stack_svg(p):
    a = p["accent"]
    b = [text(260, 84, "UNABARA", 15, a, 0.7, 6, 600)]
    slots = (
        (60, 120, 400, 350, 40, 4),
        (60, 520, 400, 270, 32, 3.5),
        (60, 830, 400, 270, 32, 3.5),
    )
    for x, y, w, h, arm, sw in slots:
        b.append(rrect(x + 8, y + 8, w - 16, h - 16, 4, DARK, 0.45))
        b.append(brackets(x, y, w, h, arm, a, sw, 0.92))
        b.append(chevrons(x + w - 40, y + 16, 7, a, 2, 0.55, 2.5))
        b.append(ticks_h(x + 20, x + 90, y + h - 18, 14, a, 1.5, 0.5, 6, 0, 6))
    return svg_doc(SS_W, SS_H, "\n".join(b))


def social_stack_cells(p):
    cv = (SS_W, SS_H)
    lc, vc = p["label"], p["value"]
    return [
        cell(
            "depth", "Depth", 260, 295, ORBITRON, 38, S["depth"], cv, lc, vc, weight=600
        ),
        cell("time", "Time", 260, 655, ORBITRON, 22, S["time"], cv, lc, vc, weight=500),
        cell(
            "temperature",
            "Temperature",
            260,
            965,
            ORBITRON,
            24,
            S["temperature"],
            cv,
            lc,
            vc,
            weight=500,
        ),
    ]


# ===========================================================================
# Design 8 — Social Glass (520x1120) — frosted minimal widget
# ===========================================================================

SG_W, SG_H = 520, 1120


def social_glass_svg(p):
    light = p["glass_light"]
    defs = (
        '<linearGradient id="hl" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#FFFFFF" stop-opacity="0.35"/>'
        '<stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/>'
        "</linearGradient>"
    )
    pills = ((70, 150, 380, 330, 44), (70, 540, 380, 240, 40), (70, 840, 380, 240, 40))
    b = []
    for x, y, w, h, r in pills:
        if light:
            b.append(rrect(x, y, w, h, r, "#FFFFFF", 0.18, "#FFFFFF", 2, 0.65))
            b.append(rrect(x + 6, y + 6, w - 12, 70, r - 8, "url(#hl)", 1.0))
        else:
            b.append(rrect(x, y, w, h, r, DARK, 0.55, "#FFFFFF", 1.5, 0.22))
    return svg_doc(SG_W, SG_H, "\n".join(b), defs)


def social_glass_cells(p):
    cv = (SG_W, SG_H)
    lc, vc = p["label"], p["value"]
    sh = {
        "enabled": True,
        "type": "outline",
        "color": "#ff000000",
        "size": 2,
        "opacity": p["shadow_op"],
    }
    return [
        cell(
            "depth",
            "Depth",
            260,
            315,
            ORBITRON,
            36,
            S["depth"],
            cv,
            lc,
            vc,
            weight=500,
            shadow=sh,
        ),
        cell("time", "Time", 260, 660, MONO, 26, S["time"], cv, lc, vc, shadow=sh),
        cell(
            "temperature",
            "Temperature",
            260,
            960,
            MONO,
            26,
            S["temperature"],
            cv,
            lc,
            vc,
            shadow=sh,
        ),
    ]


# ===========================================================================
# Palettes and design registry
# ===========================================================================


def pal(accent, label, value="#FFFFFF", **extra):
    d = {"accent": accent, "label": argb(label), "value": argb(value)}
    d.update(extra)
    return d


AMBER = "#FFB300"
GREEN = "#39FF64"
MAGENTA = "#FF2D95"
CYAN = "#3AC9FF"
WHITE_ART = "#DCE7EE"

DESIGNS = [
    {
        "base": "HUD_Tactical_Bar",
        "png": "hud_tactical_bar",
        "svg": tactical_bar_svg,
        "cells": tactical_bar_cells,
        "size": (TB_W, TB_H),
        "default_font": (MONO, 26),
        "shadow": None,
        "variants": [
            ("Amber", pal(AMBER, "#FFC24D")),
            ("White", pal(WHITE_ART, "#B9C6CE")),
        ],
    },
    {
        "base": "HUD_Sonar_Reticle",
        "png": "hud_sonar_reticle",
        "svg": sonar_svg,
        "cells": sonar_cells,
        "size": (SR_W, SR_H),
        "default_font": (MONO, 17),
        "shadow": None,
        "variants": [
            ("Green", pal(GREEN, "#5CFF82")),
            ("Amber", pal(AMBER, "#FFC24D")),
        ],
    },
    {
        "base": "HUD_Neon_Deck",
        "png": "hud_neon_deck",
        "svg": neon_deck_svg,
        "cells": neon_deck_cells,
        "size": (ND_W, ND_H),
        "default_font": (MONO, 19),
        "shadow": None,
        "variants": [
            ("Magenta", pal(MAGENTA, "#FF5FAE", glitch=CYAN)),
            ("Cyan", pal(CYAN, "#6BD6FF", glitch=MAGENTA)),
        ],
    },
    {
        "base": "HUD_Data_Rail",
        "png": "hud_data_rail",
        "svg": data_rail_svg,
        "cells": data_rail_cells,
        "size": (DR_W, DR_H),
        "default_font": (MONO, 14),
        "shadow": None,
        "variants": [
            ("Amber", pal(AMBER, "#FFC24D")),
            ("Green", pal(GREEN, "#5CFF82")),
        ],
    },
    {
        "base": "HUD_CCR_Console",
        "png": "hud_ccr_console",
        "svg": ccr_console_svg,
        "cells": ccr_console_cells,
        "size": (CC_W, CC_H),
        "default_font": (MONO, 19),
        "shadow": None,
        "variants": [
            ("Green", pal(GREEN, "#5CFF82", po2=argb(GREEN))),
            ("White", pal(WHITE_ART, "#B9C6CE", po2=argb("#F2F7FA"))),
        ],
    },
    {
        "base": "Broadcast_Lower_Third",
        "png": "broadcast_lower_third",
        "svg": broadcast_svg,
        "cells": broadcast_cells,
        "size": (BC_W, BC_H),
        "default_font": (ORBITRON, 20),
        "shadow": BC_SHADOW,
        "variants": [
            ("Cool", pal(CYAN, "#CFE0EA")),
            ("Amber", pal(AMBER, "#FFC24D")),
        ],
    },
    {
        "base": "Social_Stack_HUD",
        "png": "social_stack_hud",
        "svg": social_stack_svg,
        "cells": social_stack_cells,
        "size": (SS_W, SS_H),
        "default_font": (ORBITRON, 24),
        "shadow": SS_SHADOW,
        "variants": [
            ("Amber", pal(AMBER, "#FFC24D")),
            ("White", pal(WHITE_ART, "#B9C6CE")),
        ],
    },
    {
        "base": "Social_Glass",
        "png": "social_glass",
        "svg": social_glass_svg,
        "cells": social_glass_cells,
        "size": (SG_W, SG_H),
        "default_font": (ORBITRON, 26),
        "shadow": None,
        "variants": [
            ("Light", pal("#FFFFFF", "#E3EDF4", glass_light=True, shadow_op=0.6)),
            ("Dark", pal("#FFFFFF", "#C7D3DA", glass_light=False, shadow_op=0.5)),
        ],
    },
]


# ---------------------------------------------------------------------------
# Rendering / writing
# ---------------------------------------------------------------------------


def fontconfig_file():
    """A fontconfig config that adds resources/fonts, so inkscape can render
    the baked Orbitron wordmarks without installing fonts system-wide."""
    conf = tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False)
    conf.write(
        '<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n'
        "<fontconfig>\n<dir>%s</dir>\n"
        '<include ignore_missing="yes">/etc/fonts/fonts.conf</include>\n'
        "</fontconfig>\n" % FONTS_DIR
    )
    conf.close()
    return conf.name


def main():
    svg_only = "--svg-only" in sys.argv
    utp_only = "--utp-only" in sys.argv
    os.makedirs(IMG_DIR, exist_ok=True)
    fc = fontconfig_file()
    env = dict(os.environ, FONTCONFIG_FILE=fc)

    if not utp_only and not os.path.exists(RENDER_UTP) and not svg_only:
        sys.exit(
            "render_utp not found at %s — build with -DUNABARA_BUILD_TOOLS=ON"
            % RENDER_UTP
        )

    qrc_lines = []
    for d in DESIGNS:
        w, h = d["size"]
        for suffix, palette in d["variants"]:
            name = "%s_%s" % (d["base"], suffix)
            png_name = "%s_%s.png" % (d["png"], suffix.lower())
            svg_path = os.path.join(IMG_DIR, "%s_%s.svg" % (d["png"], suffix.lower()))
            png_path = os.path.join(IMG_DIR, png_name)

            if not utp_only:
                with open(svg_path, "w") as f:
                    f.write(d["svg"](palette))
                subprocess.run(
                    [
                        "inkscape",
                        svg_path,
                        "--export-type=png",
                        "--export-filename=%s" % png_path,
                        "--export-width=%d" % w,
                        "--export-height=%d" % h,
                    ],
                    check=True,
                    env=env,
                    capture_output=True,
                )
                print("rendered %s (%dx%d)" % (png_name, w, h))

            if not svg_only:
                fam, pt = d["default_font"]
                tpl = template_json(
                    name,
                    png_name,
                    font_json(fam, pt),
                    palette["label"],
                    palette["value"],
                    d["cells"](palette),
                    d["shadow"],
                    # pal() extras are raw #RRGGBB -> argb-wrap the overrides
                    primary_color=argb(palette["primary"]) if "primary" in palette else None,
                    secondary_color=argb(palette["secondary"]) if "secondary" in palette else None,
                )
                utp_path = os.path.join(TPL_DIR, "%s.utp" % name)
                with open(utp_path, "w") as f:
                    json.dump(tpl, f, indent=4, sort_keys=True)
                    f.write("\n")
                print("wrote   %s.utp (%d cells)" % (name, len(tpl["cells"])))

            qrc_lines.append(
                '        <file alias="templates/%s.utp">resources/templates/%s.utp</file>'
                % (name, name)
            )
            qrc_lines.append(
                '        <file alias="images/HUD/%s">resources/images/HUD/%s</file>'
                % (png_name, png_name)
            )

    os.unlink(fc)
    print("\nqrc entries:\n" + "\n".join(qrc_lines))


if __name__ == "__main__":
    main()
