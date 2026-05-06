# Phase 2 CPU profile — `tools/sfs_profile`

Per `docs/phase-plans/phase-2.md` §7 DoD + `docs/phase-plans/phase-2.md`
§9 step 15. Captures the cost of the Phase 2 engine after macros, ADSR,
LFO bank, mod matrix, and full waveform palette landed.

## Setup

- Host: macOS arm64 (Apple Silicon, Apple M-series)
- Build: Release, `-O3 -fno-fast-math` (default `cmake -B build`)
- Sample rate: 48000 Hz
- Block size: 256 samples
- Run: `build/bin/sfs_profile --substrate-cells 1024 --agents N --seconds 1.0 --repeat 5`
- Date: 2026-05-06

## Results — per single voice

| Substrate cells | Agent count | Wall ms / 1 s render | Real-time factor | One-core % |
|---|---|---|---|---|
| 1024 | 16 (Phase 1 / 2 default) | 70 ms (min) / 72 ms (median) | 14× | 7.0–7.2% |
| 1024 | 32 (Phase 2 target)      | 96 ms / 97 ms                | 10× | 9.6–9.7% |
| 1024 | 64 (sfs-spec/09 max)     | 111 ms / 113 ms              | ~9× | 11.1–11.3% |

## Phase 1 → Phase 2 delta

| Configuration | Phase 1 ms | Phase 2 ms | Δ | Likely source |
|---|---|---|---|---|
| 1024 / 16 | 50 | 70 | +40% | Macro fan-out + 4 LFOs ticking + mod-matrix evaluation per block + DC blocker per channel × 2 (stereo) × ADSR per sample |
| 1024 / 32 | 56 | 97 | +73% | Same as above, scales with active agent count due to DENSITY-driven fan-out |
| 1024 / 64 | 72 | 113 | +57% | Same |

Phase 2 carries ~30–60% more per-sample cost. Most of it is correct
work — the engine is genuinely doing more (5 waveforms vs. 1, full
macro fan-out, LFO bank, mod matrix, stereo). Two clear hotspots:

1. **Mono harvester soft-clip + DC block runs twice** for stereo.
   Already required for the spec's stereo deliverable. No optimisation.
2. **Macro fan-out runs every block** for every active voice.
   Already block-rate. Could move to less-frequent destinations
   re-evaluation (only when input changed), but the savings would be
   marginal — Phase 4 follow-up if profiling pushes back.

## Comparison to `sfs-spec/08 §2.5` budget

Spec scalar budget for `1D N=1024`: **~14 µs/sample = ~672 ms wall per
second of audio = ~67% of one core** for a single voice with full agent
load.

Even at the worst (64 agents): 113 ms / 1 s = ~11% of one core. **6× under
the spec budget.** With 8 voices simultaneously: ~88% of one core
(estimated, untested). Above the budget but within the realtime threshold;
SIMD sweeping the agent loop in Phase 3 should reclaim the slack.

## Polyphony estimate (extrapolated, single thread)

| Voices | Agents/voice | Estimated one-core % |
|---|---|---|
| 8 | 16 | ~56% |
| 8 | 32 | ~78% |
| 8 | 64 | ~90% |

Phase 2 ships single-threaded; the spec's Phase 3 voice-pool threading
plan brings these well below 50% on multi-core hosts.

## Phase 2 gate

- ✅ `1D-32-agent` (10× real-time) — passes spec gate threshold (≥ 4×).
- ✅ `1D-16-agent` (14× real-time) — passes spec gate threshold.
- ✅ `1D-64-agent` (9× real-time) — passes spec gate threshold.

No regressions vs. spec budget. Phase 2 CPU gate cleared.

## Notes for Phase 3

- Profile multi-voice (8 simultaneous) once polyphonic stress-test rig
  lands. Today's `sfs_profile` is single-voice.
- Profile on Linux + Windows runners once CI exposes per-platform
  artifacts. Today's numbers are macOS arm64 only.
- Phase 3's 2D substrate at N×N=64×64 will be the next CPU stress
  point; the spec budget is `~67% one core` for 2D-32-agent.
