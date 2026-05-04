#!/usr/bin/env bash
# tools/hash_pcm.sh
#
# SHA-256 over raw PCM frames produced by sfs_render's --raw option.
# Per sfs-spec/06 §2.2 the determinism contract is on PCM frames, NOT on
# the .wav file — WAV headers can carry platform-specific BWF timestamps
# and INFO-chunk ordering that drift for reasons unrelated to audio.
#
# .raw files are channel-interleaved IEEE 754 float32 (little-endian on
# the v1.0 target platforms, all of which are LE). No header — `shasum`
# directly is correct.
#
# Output format matches `shasum -a 256 <file>` so determinism-compare can
# diff two outputs directly.
#
# Usage:
#   tools/hash_pcm.sh <raw> [raw ...]
#   tools/hash_pcm.sh --check <hashes.txt>

set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: tools/hash_pcm.sh <raw> [raw ...]" >&2
    echo "       tools/hash_pcm.sh --check <hashes.txt>" >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    SHA="shasum -a 256"
elif command -v sha256sum >/dev/null 2>&1; then
    SHA="sha256sum"
else
    echo "tools/hash_pcm.sh: no shasum / sha256sum on PATH" >&2
    exit 1
fi

if [ "$1" = "--check" ]; then
    shift
    [ "$#" -eq 1 ] || { echo "usage: --check <hashes.txt>" >&2; exit 1; }
    exec $SHA --check "$1"
fi

# Normalise the output across platforms. Windows' shasum / sha256sum default
# to binary mode (output: "hash *file"); Unix defaults to text mode
# ("hash  file"). The actual hash is identical either way; we just rewrite
# the separator so downstream diffs (determinism.yml) compare cleanly.
$SHA "$@" | sed 's/ \*/  /'
