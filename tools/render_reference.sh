#!/usr/bin/env bash
# tools/render_reference.sh
#
# Render the canonical Phase 0 reference clip via sfs_render. Used by:
#   * The determinism CI job: each platform renders this clip, uploads the
#     WAV + hash artifact, then determinism-compare asserts equality.
#   * Local developers: re-create tests/refs/sine_skeleton_48k_256.wav after
#     an intentional acceptance change (commit message must explain).
#
# Phase 0 reference clip: 1 s of polynomial 440 Hz sine, 48 kHz, 256-sample
# blocks, stereo, 32-bit float WAV. As later phases land, more reference
# clips will be added (1D-substrate Phase 1 canonical, 2D Phase 3, factory
# presets Phase 4) — each as a separate render invocation.
#
# Usage:
#   tools/render_reference.sh [output_dir]
#       output_dir defaults to ./build/ref_renders
#
# Exit codes:
#   0  success
#   1  invocation problem
#   2  sfs_render binary not found (build it first)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="${1:-${ROOT}/build/ref_renders}"
mkdir -p "$OUT_DIR"

# Locate the sfs_render binary (Release build expected; Debug works too).
RENDER_BIN=""
for candidate in \
    "${ROOT}/build/bin/sfs_render" \
    "${ROOT}/build/bin/Release/sfs_render" \
    "${ROOT}/build/bin/sfs_render.exe" \
    "${ROOT}/build/bin/Release/sfs_render.exe"
do
    if [ -x "$candidate" ]; then
        RENDER_BIN="$candidate"
        break
    fi
done

if [ -z "$RENDER_BIN" ]; then
    echo "tools/render_reference.sh: sfs_render binary not found." >&2
    echo "Build it first:  cmake --build build --target sfs_render" >&2
    exit 2
fi

echo "Using render binary: $RENDER_BIN"

# Render each canonical clip. Add new entries below as later phases land.
"$RENDER_BIN" --sr 48000 --block 256 --seconds 1.0 --channels 2 \
    --out "${OUT_DIR}/sine_skeleton_48k_256.wav"

echo
echo "Renders complete. Hash with:"
echo "  tools/hash_wav.sh ${OUT_DIR}/*.wav"
