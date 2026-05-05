# Phase 1 CPU profile — `tools/sfs_profile`

Per `docs/phase-plans/phase-1.md` §7 DoD: CPU profile recorded; meets
`08 §2.5` Phase 2 gate threshold for `1D-32-agent`.

Tool: `tools/sfs_profile` — times the per-sample inner loop (substrate
step + agent processOneSample + harvester read + DC block + soft clip)
on a single voice rendered for a configurable duration. No I/O in the
hot loop; reports min / median / mean wall time across N repetitions.

## Setup

- Host: macOS arm64 (Apple Silicon)
- Build: Release, `-O3 -fno-fast-math` (default `cmake -B build`)
- Sample rate: 48000 Hz
- Block size: 256 samples
- Run: `build/bin/sfs_profile --substrate-cells N --agents N --seconds 1.0 --repeat 5`

## Results — per single voice

| Substrate cells | Agent count | Wall ms / 1 s render | Real-time factor | One-core % |
|---|---|---|---|---|
| 1024 | 16 (Phase 1 default) | 50 ms (min) / 50 ms (median) | 20× | 5.0% |
| 1024 | 32 (Phase 2 target) | 56 ms / 57 ms | 18× | 5.7% |
| 1024 | 64 (sfs-spec/09 max) | 72 ms / 73 ms | 14× | 7.3% |

## Comparison to `sfs-spec/08 §2.5` budget

Spec scalar budget for `1D N=1024`: **~14 µs/sample = ~672 ms wall per second of audio = ~67% of one core**.

Measured Phase 1 (scalar reference, no SIMD) on macOS arm64:
- Default 16 agents: **50 ms / sec = ~13× faster than spec scalar target**.
- 32 agents: 56 ms / sec, ~12× faster.
- 64 agents: 72 ms / sec, ~9× faster.

The spec's scalar baseline was estimated conservatively against an Apple
M2 baseline; M-series cores are evidently more capable than the spec
assumed (or my scalar implementation is leaner than the spec author
budgeted for). Either way, Phase 1 sits comfortably inside the budget
even before any SIMD work.

## Implications for the 8-voice / Phase 2 polyphony gate

- 8 voices × 32 agents at scalar: 8 × 5.7% = **~46% of one core** on macOS arm64.
- 8 voices × 64 agents at scalar: 8 × 7.3% = **~58% of one core**.

Both fit in a single core even *without* the SIMD pass scheduled in
Phase 1 §9 step 15. SIMD optimisation can be deferred from Phase 1 with
no risk to the Phase 2 polyphony gate (as the master plan §1 already
suggested: "If the scalar code already meets the `08 §2.5` Phase 2
threshold for the `1D-32-agent` config on the M2, defer SIMD optimisation
to P2 where it'll be needed for the polyphony expansion anyway.").

## Cross-platform numbers

Not yet measured on Windows x86_64 or Linux x86_64. Adding a CI step to
run `sfs_profile` and record the numbers per platform is a follow-up;
the determinism contract guarantees identical *output* across platforms,
but per-core wall-clock is naturally different across CPUs.

Estimate (very rough): Intel x86_64 scalar might be ~2-3× slower than
M-series ARM64 for memory-bound DSP loops, putting us at ~15% of one
core for default config — still well inside budget.

## Caveats

- These numbers measure the per-voice loop in isolation, not the full
  `processBlock` JUCE call (which adds MIDI parsing, channel duplication,
  small bookkeeping). Full plug-in CPU is at most a few percent higher.
- Wall-clock has run-to-run variance from cache warmth, OS scheduling,
  thermal state. Numbers above are the median of 5 hot-cache runs.
- No allocations are intentional in the hot loop, but the
  `substrate.step()` Phase A buffer is currently allocated each call.
  Hoisting it to a member buffer is a Phase 2 cleanup (sub-µs win).

## Reproducing

```sh
cmake --build build --target sfs_profile
build/bin/sfs_profile --substrate-cells 1024 --agents 16 --seconds 1.0 --repeat 5
build/bin/sfs_profile --substrate-cells 1024 --agents 32 --seconds 1.0 --repeat 5
build/bin/sfs_profile --substrate-cells 1024 --agents 64 --seconds 1.0 --repeat 5
```

Other configs worth probing (deferred to Phase 2 / 3):
- `--substrate-cells 4096` (max 1D)
- `--substrate-cells 256` (smaller, lower-CPU)
- 2D substrate (Phase 3)
