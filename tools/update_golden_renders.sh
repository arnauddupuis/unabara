#!/usr/bin/env bash
# Regenerate tests/golden/renders.sha256 for the Qt version render_utp is
# built against (other Qt versions' sections are preserved). Run this after an
# INTENDED change to overlay rendering or a bundled template, review the
# manifest diff, and commit it with the change.
#
# Usage: tools/update_golden_renders.sh [build-dir]   (default: build)
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${1:-$repo_root/build}"
render_utp="$build_dir/bin/render_utp"

if [[ ! -x "$render_utp" ]]; then
    echo "render_utp not found at $render_utp" >&2
    echo "Build it first: cmake -B \"$build_dir\" -DUNABARA_BUILD_TESTS=ON && cmake --build \"$build_dir\" --target render_utp" >&2
    exit 1
fi

# Rebuild render_utp so edited templates/renderer code can't produce stale
# goldens (the qrc-embedded templates are compiled into the binary).
cmake --build "$build_dir" --target render_utp

exec python3 "$repo_root/tests/golden/golden_renders.py" update \
    --render-utp "$render_utp" \
    --qrc "$repo_root/resources.qrc" \
    --manifest "$repo_root/tests/golden/renders.sha256" \
    --work-dir "$build_dir/tests/golden_renders"
