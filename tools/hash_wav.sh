#!/usr/bin/env bash
# tools/hash_wav.sh
#
# Print a SHA-256 hash for one or more WAV files. The hash is over the
# whole file, not just the data chunk — this works because the WAV writer
# (JUCE) writes deterministic headers (no timestamps, fixed metadata
# ordering, no padding variation). If a platform ever introduces header
# drift, switch to a data-chunk-only hash via `wav_diff --data-only`.
#
# Output format is identical to `shasum -a 256 <files>` so downstream
# scripts (CI determinism-compare) can `diff` two outputs directly.
#
# Usage:
#   tools/hash_wav.sh <wav> [wav ...]
#   tools/hash_wav.sh --check <hashes.txt>
#
# The `--check` form reads a previously-saved hashes.txt and verifies each
# entry, exiting non-zero on the first mismatch (compatible with shasum -c).

set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: tools/hash_wav.sh <wav> [wav ...]" >&2
    echo "       tools/hash_wav.sh --check <hashes.txt>" >&2
    exit 1
fi

# Pick the right SHA-256 binary across platforms.
if command -v shasum >/dev/null 2>&1; then
    SHA="shasum -a 256"
elif command -v sha256sum >/dev/null 2>&1; then
    SHA="sha256sum"
else
    echo "tools/hash_wav.sh: no shasum / sha256sum on PATH" >&2
    exit 1
fi

if [ "$1" = "--check" ]; then
    shift
    [ "$#" -eq 1 ] || { echo "usage: --check <hashes.txt>" >&2; exit 1; }
    exec $SHA --check "$1"
fi

exec $SHA "$@"
