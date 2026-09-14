#!/usr/bin/env python3
"""Golden-render regression test for the bundled overlay templates.

Renders every *bundled* template (the set listed in resources.qrc — never the
extra local .utp files that may sit in resources/templates/) with the
render_utp tool at two dive times (mid-dive and deco phase), hashes the PNGs,
and compares against the committed manifest tests/golden/renders.sha256.

Rendered bytes depend on the Qt/FreeType version, so the manifest holds one
section per Qt version ("qt <version>" header lines). The check is ENFORCED
only when the running Qt has a section in the manifest; on any other Qt it is
advisory: differences are reported, the computed section is written next to
the work dir as renders.candidate.sha256 (CI uploads it as an artifact so it
can be committed), and the test passes.

Usage:
  golden_renders.py check  --render-utp BIN --qrc FILE --manifest FILE --work-dir DIR
  golden_renders.py update --render-utp BIN --qrc FILE --manifest FILE --work-dir DIR

`update` regenerates the section for the running Qt version in place,
preserving every other version's section. Intended wrapper:
tools/update_golden_renders.sh
"""

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

# Mid-dive (NDL) and deco phase — together they exercise every data-dependent
# cell state (NDL vs TTS switch, stop depth/time, pressures, PO2).
RENDER_TIMES = (1200, 2400)


def bundled_templates(qrc_path):
    """Template basenames bundled via resources.qrc, in listing order."""
    text = Path(qrc_path).read_text(encoding="utf-8")
    names = re.findall(r'alias="templates/([^"]+\.utp)"', text)
    if not names:
        sys.exit(f"ERROR: no bundled templates found in {qrc_path}")
    return names


def qt_version(render_utp):
    out = subprocess.run([render_utp, "--qt-version"], check=True,
                         capture_output=True, text=True).stdout.strip()
    if not re.fullmatch(r"[0-9.]+", out):
        sys.exit(f"ERROR: unexpected --qt-version output: {out!r}")
    return out


def render_all(render_utp, templates, work_dir):
    """Render every template at every time; return {png_name: sha256hex}."""
    work_dir.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for utp in templates:
        stem = utp[:-len(".utp")]
        for t in RENDER_TIMES:
            png_name = f"{stem}_t{t}.png"
            out_png = work_dir / png_name
            proc = subprocess.run(
                [render_utp, f":/templates/{utp}", str(out_png), "--time", str(t)],
                capture_output=True, text=True)
            if proc.returncode != 0 or not out_png.is_file():
                sys.exit(f"ERROR: render failed for {utp} at t={t}:\n"
                         f"{proc.stdout}{proc.stderr}")
            hashes[png_name] = hashlib.sha256(out_png.read_bytes()).hexdigest()
    return hashes


def parse_manifest(manifest_path):
    """Return (header_comment_lines, {qt_version: {png_name: sha256}})."""
    comments, sections = [], {}
    if not manifest_path.is_file():
        return comments, sections
    current = None
    for line in manifest_path.read_text(encoding="utf-8").splitlines():
        line = line.rstrip()
        if not line:
            continue
        if line.startswith("#"):
            if current is None:
                comments.append(line)
            continue
        m = re.fullmatch(r"qt ([0-9.]+)", line)
        if m:
            current = sections.setdefault(m.group(1), {})
            continue
        m = re.fullmatch(r"([0-9a-f]{64})  (\S+)", line)
        if m and current is not None:
            current[m.group(2)] = m.group(1)
            continue
        sys.exit(f"ERROR: malformed manifest line: {line!r}")
    return comments, sections


DEFAULT_HEADER = [
    "# Golden overlay renders — SHA-256 of render_utp output for every bundled",
    "# template at t=1200 (mid-dive) and t=2400 (deco). One section per Qt",
    "# version (renders depend on Qt/FreeType); the ctest entry enforces only",
    "# the section matching the running Qt and is advisory otherwise.",
    "# Regenerate after an intended renderer/template change:",
    "#   tools/update_golden_renders.sh [build-dir]",
]


def write_manifest(manifest_path, comments, sections):
    lines = list(comments or DEFAULT_HEADER)
    for version in sorted(sections):
        lines.append("")
        lines.append(f"qt {version}")
        for name in sorted(sections[version]):
            lines.append(f"{sections[version][name]}  {name}")
    manifest_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def diff_hashes(golden, computed):
    """Human-readable differences between two {name: hash} maps."""
    problems = []
    for name in sorted(set(golden) | set(computed)):
        if name not in computed:
            problems.append(f"  missing render (template removed?): {name}")
        elif name not in golden:
            problems.append(f"  not in manifest (new template?):    {name}")
        elif golden[name] != computed[name]:
            problems.append(f"  render changed:                     {name}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("check", "update"))
    ap.add_argument("--render-utp", required=True)
    ap.add_argument("--qrc", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--work-dir", required=True)
    args = ap.parse_args()

    manifest_path = Path(args.manifest)
    work_dir = Path(args.work_dir)
    templates = bundled_templates(args.qrc)
    version = qt_version(args.render_utp)

    print(f"Rendering {len(templates)} bundled templates x {len(RENDER_TIMES)} "
          f"times with Qt {version}...")
    computed = render_all(args.render_utp, templates, work_dir)
    comments, sections = parse_manifest(manifest_path)

    if args.mode == "update":
        sections[version] = computed
        write_manifest(manifest_path, comments, sections)
        print(f"Wrote {len(computed)} golden hashes for Qt {version} to {manifest_path}")
        return 0

    # check
    candidate = work_dir / "renders.candidate.sha256"
    write_manifest(candidate, comments, {version: computed})

    if version not in sections:
        print(f"ADVISORY: no golden section for Qt {version} in {manifest_path} "
              f"(sections: {', '.join(sorted(sections)) or 'none'}).")
        print(f"Computed hashes written to {candidate} — commit them into the "
              f"manifest to enforce on this Qt version.")
        return 0

    problems = diff_hashes(sections[version], computed)
    if problems:
        print(f"FAIL: {len(problems)} golden render difference(s) on Qt {version}:")
        print("\n".join(problems))
        print(f"Rendered PNGs: {work_dir}")
        print("If the change is intended, regenerate the manifest with "
              "tools/update_golden_renders.sh and commit the diff.")
        return 1

    print(f"OK: {len(computed)} renders match the Qt {version} goldens.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
