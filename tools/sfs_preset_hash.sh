#!/usr/bin/env bash
# tools/sfs_preset_hash.sh
#
# Phase 4 §A3 — compute the canonical _audio_hash for a `.sfs` preset
# and (optionally) update the file's `_audio_hash` field in place.
#
# Pipeline: sfs_preset_render → shasum -a 256 → optional jq write-back.
# All three are tools we already vendor / require for the build.
#
# Usage:
#   tools/sfs_preset_hash.sh <preset.sfs> [--update]
#
# Without --update: prints the SHA-256 hash to stdout.
# With --update:    writes "sha256:<hex>" into the preset's _audio_hash
#                   field, atomically (jq → tmp file → mv).
#
# Requires:
#   build/bin/sfs_preset_render   (built via cmake --build build)
#   shasum                        (macOS / Linux preinstalled)
#   jq                            (required for --update only)

set -euo pipefail

PRESET="${1:-}"
UPDATE="${2:-}"

if [ -z "$PRESET" ]; then
  echo "usage: $0 <preset.sfs> [--update]" >&2
  exit 1
fi

if [ ! -f "$PRESET" ]; then
  echo "error: preset not found: $PRESET" >&2
  exit 1
fi

# Locate the renderer. CI builds into build/; developers might use Release/.
RENDER=""
for cand in build/bin/sfs_preset_render build/bin/Release/sfs_preset_render \
            build/bin/sfs_preset_render.exe build/bin/Release/sfs_preset_render.exe; do
  if [ -x "$cand" ]; then
    RENDER="$cand"
    break
  fi
done
if [ -z "$RENDER" ]; then
  echo "error: sfs_preset_render binary not found; build it first (cmake --build build --target sfs_preset_render)" >&2
  exit 1
fi

TMPDIR_OUT="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_OUT"' EXIT
RAW="$TMPDIR_OUT/render.raw"

"$RENDER" "$PRESET" "$RAW" --seconds 30.0 --note 60 --velocity 1.0 --sr 48000 --block 256 >/dev/null

# shasum format: "<hash>  <path>". Strip the path, prepend "sha256:".
HASH=$(shasum -a 256 "$RAW" | awk '{print $1}')
TAGGED="sha256:$HASH"

if [ "$UPDATE" = "--update" ]; then
  if ! command -v jq >/dev/null 2>&1; then
    echo "error: --update requires jq (brew install jq / apt install jq / choco install jq)" >&2
    exit 1
  fi
  TMP_PRESET="${PRESET}.tmp"
  jq --arg h "$TAGGED" '. + {_audio_hash: $h}' "$PRESET" > "$TMP_PRESET"
  mv "$TMP_PRESET" "$PRESET"
  echo "updated $PRESET: _audio_hash = $TAGGED"
else
  echo "$TAGGED"
fi
