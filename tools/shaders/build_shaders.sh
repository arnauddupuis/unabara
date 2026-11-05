#!/usr/bin/env bash
# Rebuild the committed .qsb shader packs from their GLSL sources.
#
# The compiled .qsb files are committed (resources.qrc loads them at
# runtime); the .frag sources ship alongside as the human-readable
# reference. CMake deliberately does NOT invoke qsb (the v0.4 plan's build
# decision: the local box and CI lack the Qt6::ShaderTools CMake package;
# .qsb packs are portable across platforms and Qt patch releases).
#
# Run this after editing any shader under resources/shaders/, then commit
# both the source and the regenerated .qsb:
#
#   tools/shaders/build_shaders.sh
#
# qsb comes from the qt6-qtshadertools package (qt6-shadertools on some
# distros). Override the binary with QSB=/path/to/qsb if it is not on PATH.
#
# --qsbversion 64 bakes the Qt 6.4 container format so the pack loads on
# the project's Qt 6.8 floor regardless of the qsb tool's own Qt version;
# --qt6 is the standard Qt Quick target set (GLSL "100 es,120,150",
# HLSL 50, MSL 12).

set -euo pipefail

QSB="${QSB:-qsb}"
if ! command -v "$QSB" >/dev/null 2>&1; then
    for candidate in /usr/lib64/qt6/bin/qsb /usr/lib/qt6/bin/qsb; do
        if [ -x "$candidate" ]; then
            QSB="$candidate"
            break
        fi
    done
fi
if ! command -v "$QSB" >/dev/null 2>&1; then
    echo "error: qsb not found — install qt6-qtshadertools or set QSB=/path/to/qsb" >&2
    exit 1
fi

cd "$(dirname "$0")/../.."

for src in resources/shaders/*.frag; do
    out="${src}.qsb"
    "$QSB" --qt6 --qsbversion 64 -o "$out" "$src"
    echo "built $out"
done
