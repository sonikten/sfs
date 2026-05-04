#!/usr/bin/env bash
# tools/check_determinism.sh
#
# Static enforcement of the determinism contract (CLAUDE.md "Hard invariants",
# sfs-spec/01 §9, sfs-spec/06 §1). Greps src/ for symbols that would silently
# break bit-exact reproducibility across platforms. Used by CI on every push
# (per master plan §4.2) so the rules are enforced, not just documented.
#
# What this script forbids and why:
#
#   1. Standard-library transcendentals in src/engine/ and src/dsp/:
#        std::sin, std::cos, std::exp, std::log, std::tan,
#        std::asin, std::acos, std::atan, std::atan2,
#        std::sinh, std::cosh, std::tanh, std::pow,
#        sinf, cosf, expf, logf, tanf, etc.
#      libm differs at the ULP level across platforms; hash drift is
#      guaranteed if these reach the audio loop. Use sfs::dsp::dm_*
#      polynomial / LUT replacements. Allowed in tools/ and tests/.
#
#   2. Platform RNGs anywhere in src/:
#        std::default_random_engine, std::mt19937, std::mt19937_64,
#        std::random_device, std::minstd_rand, std::ranlux*,
#        rand(), random(), srand(), srandom(), drand48().
#      Use Random123 Philox-4×32-10 (sfs-spec/06 §1).
#
#   3. juce::AudioProcessorValueTreeState anywhere in src/:
#      Its locking model breaks the audio-thread determinism contract
#      (sfs-spec/08 §5). Use raw juce::AudioProcessorParameter + the
#      SPSC ring buffer (sfs-spec/01 §4.4).
#
# Exit codes:
#   0  clean
#   1  one or more forbidden symbols found

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

red()    { printf "\033[31m%s\033[0m\n" "$*"; }
green()  { printf "\033[32m%s\033[0m\n" "$*"; }
yellow() { printf "\033[33m%s\033[0m\n" "$*"; }

# Helper: grep that excludes comments and string literals (best-effort).
# We use `grep -E` with a leading negative lookahead approximation —
# require the symbol NOT be preceded by `//` on the line.
forbidden_check() {
    local label="$1"
    local where="$2"
    local pattern="$3"
    local hint="$4"

    if [ ! -d "$where" ]; then
        return 0
    fi

    # Scan .cpp/.h/.hpp/.cxx/.hxx/.cc/.c. Skip comment lines starting with //.
    matches=$(grep -RnE \
        --include='*.cpp' --include='*.h' --include='*.hpp' \
        --include='*.cxx' --include='*.hxx' --include='*.cc' --include='*.c' \
        "$pattern" "$where" 2>/dev/null \
        | grep -vE '^[^:]+:[0-9]+:\s*//' \
        | grep -vE '^[^:]+:[0-9]+:\s*\*' \
        || true)

    if [ -n "$matches" ]; then
        red "FORBIDDEN: $label in $where/"
        echo "$matches" | sed 's/^/    /' >&2
        echo "  Hint: $hint" >&2
        echo
        return 1
    fi
    return 0
}

failures=0

# --- 1. libm transcendentals in src/engine/ and src/dsp/ -----------------
# Match e.g. `std::sin(`, `std::cos<float>(`, `sinf(`, but not `dm_sin(`,
# `asin`, `cosa`, etc. — anchor on word boundary before the call.
TRANSCENDENTAL_PATTERN='(\bstd::(sin|cos|tan|exp|log|log2|log10|asin|acos|atan|atan2|sinh|cosh|tanh|pow)\s*[<(]|\b(sinf|cosf|tanf|expf|logf|sinhf|coshf|tanhf|powf|asinf|acosf|atanf|atan2f)\s*\()'

for dir in src/engine src/dsp; do
    forbidden_check "libm transcendentals" "$dir" \
        "$TRANSCENDENTAL_PATTERN" \
        "Use sfs::dsp::dm_sin / dm_cos / dm_exp / dm_log / dm_tanh / dm_pow." \
        || failures=$((failures + 1))
done

# --- 2. Platform RNGs in src/ ---------------------------------------------
RNG_PATTERN='(\bstd::(default_random_engine|mt19937|mt19937_64|random_device|minstd_rand|ranlux24|ranlux48)\b|\b(rand|random|srand|srandom|drand48)\s*\()'

forbidden_check "platform RNGs" "src" \
    "$RNG_PATTERN" \
    "Use Random123 Philox-4×32-10 keyed by (preset_seed, voice, agent, stream_id)." \
    || failures=$((failures + 1))

# --- 3. AudioProcessorValueTreeState in src/ ------------------------------
APVTS_PATTERN='\bAudioProcessorValueTreeState\b'

forbidden_check "AudioProcessorValueTreeState" "src" \
    "$APVTS_PATTERN" \
    "Use raw juce::AudioProcessorParameter + the SPSC queue (sfs-spec/01 §4.4)." \
    || failures=$((failures + 1))

if [ "$failures" -gt 0 ]; then
    echo
    red "$failures determinism check(s) failed."
    exit 1
fi

green "All determinism checks passed."
exit 0
