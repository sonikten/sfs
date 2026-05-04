#!/usr/bin/env bash
# tools/check_submodule_pins.sh
#
# Verify each third-party submodule is checked out at the tag we pinned for SFS
# v1.0. Used as a CI gate (per docs/phase-plans/phase-0.md §5) so that nobody
# silently advances JUCE, SIMDe, etc. without an explicit phase-plan update.
#
# Exit codes:
#   0  every pin matches
#   1  one or more pins drift
#   2  invocation problem (missing submodule, missing tag, etc.)
#
# Update procedure when intentionally bumping a pin:
#   1. Update the EXPECTED_TAG below.
#   2. Update docs/phase-plans/phase-N.md "Risk register delta" with the bump.
#   3. Run this script and confirm it passes.
#   4. Land the submodule SHA bump and the script update in the same commit.

set -euo pipefail

# Run from the repo root regardless of where the script is invoked from.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# ---- Pinned tags ------------------------------------------------------------
# (path, expected tag). Keep in sync with docs/phase-plans/phase-0.md.
PINS=(
    "third_party/JUCE|8.0.4"
    "third_party/Catch2|v3.7.1"
    "third_party/Random123|v1.14.0"
    "third_party/SIMDe|v0.8.2"
    "third_party/nlohmann_json|v3.11.3"
)

red()    { printf "\033[31m%s\033[0m\n" "$*"; }
green()  { printf "\033[32m%s\033[0m\n" "$*"; }
yellow() { printf "\033[33m%s\033[0m\n" "$*"; }

failures=0

for pin in "${PINS[@]}"; do
    path="${pin%|*}"
    expected="${pin#*|}"

    if [ ! -d "$path/.git" ] && [ ! -f "$path/.git" ]; then
        red "MISSING  $path  (run 'git submodule update --init --recursive')"
        failures=$((failures + 1))
        continue
    fi

    # Resolve the expected tag to a commit; fail loudly if the tag has been deleted upstream.
    if ! expected_sha=$(git -C "$path" rev-parse --verify "${expected}^{commit}" 2>/dev/null); then
        red "BAD TAG  $path  (expected tag '$expected' not found upstream — pin update needed)"
        failures=$((failures + 1))
        continue
    fi

    actual_sha=$(git -C "$path" rev-parse --verify HEAD)

    if [ "$expected_sha" = "$actual_sha" ]; then
        green "OK       $path  $expected  ($actual_sha)"
    else
        red   "DRIFT    $path"
        red   "         expected $expected  ($expected_sha)"
        red   "         actual               ($actual_sha)"
        failures=$((failures + 1))
    fi
done

if [ "$failures" -gt 0 ]; then
    echo
    red "$failures submodule pin(s) failed verification."
    echo "If this is intentional, update tools/check_submodule_pins.sh and the relevant phase-plan."
    exit 1
fi

green "All ${#PINS[@]} submodule pins verified."
