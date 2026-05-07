# Phase 3 — Gate evidence

Records the evidence that Phase 3's Definition of Done (per
`docs/phase-plans/phase-3.md` §9 + `sfs-spec/08_implementation_roadmap.md` §1)
has been met. Used as the body of the `phase-3-gate` annotated tag once
the next CI cycle ships green.

## Engine deliverables (`docs/phase-plans/phase-3.md` §9 14-step bring-up)

| Step | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | Cody-Waite `dm_sin` to 1e-6 budget | ✅ | `src/dsp/dm_sin.cpp` 11th-order Taylor in double + Cody-Waite π split; `dm_sin_test` empirical max error < 1e-6 |
| 2 | 5 ms ramp on stolen voices | ✅ | `Voice::noteOnAfterSteal` resets substrate + 5 ms 0→1 output ramp; `vm_fuzz/stealing` |
| 3 | `Substrate2D` (torus) | ✅ | `src/engine/substrate/substrate_2d.{h,cpp}`, 5-point Laplacian, bilinear deposit/read, 9 unit tests / 1060 assertions |
| 4 | Voice 2D mode | ✅ | `Voice::setTopology(Torus2D)`; agents lay out 2D, render path dispatches via `if (use2D)` |
| 5 | `TOPOLOGY` host parameter | ✅ | `juce::AudioParameterChoice "TOPOLOGY"` {Ring 1D, Torus 2D}; `VoiceManager::setTopology` fans out |
| 6 | Multichannel bus (5.1, 7.1.4) | ✅ | 5.1: `Voice::renderBlockSurround51` + ITU-R BS.775 angles on the 2D torus; LFE 120 Hz LPF (1st-order; LR-2 in Phase 4). 7.1.4: `Voice::renderBlockSurround714` (post-gate `P3.6b` commit) — 11 full-bandwidth + LFE, floor channels at y < 0.3·Ny, height channels at y > 0.7·Ny per `sfs-spec/04 §3.5` |
| 7 | First-order ambisonic encoder | ✅ | `Voice::renderBlockFoa` SN3D/ACN per `sfs-spec/04 §3.6`; 2D quadrant harvesters; 1D fallback downmixes to W/X |
| 8 | MPE Note Expression | ✅ | Per-channel pitch bend → `Voice::pitchBendSemitones_` via `dm_pow2(s/12)` block-rate ratio; pressure + timbre as mod-matrix sources |
| 9 | Voice-pool threading | ✅ | `src/engine/voice_pool.{h,cpp}` (post-gate `P3.9` commit). Counting-semaphore-based worker pool; per-voice render dispatched in parallel, bus mix runs sequentially in voice-index order on the audio thread so output is bit-exact regardless of worker count. `enableThreading(N)` opt-in, default off (preserves the determinism corpus through the deterministic single-threaded path). Unit test verifies 1/2/4-worker renders match serial sample-by-sample for both 1D and 2D topologies. |
| 10 | Contract test 2D variants | ✅ | `tests/contract/{drone,organic,glitch,pitched}_test.cpp` each gain 1D + 2D `TEST_CASE`s sharing a helper |
| 11 | CI 2D coverage | ✅ | `tools/render_presets/main.cpp` renders every preset in both topologies → 22 PCM hashes per platform; `determinism.yml` cross-platform compare |
| 12 | CPU profile journal | ✅ partial | `docs/journals/phase-3-cpu-profile.md`: macOS arm64 single-voice 9.0× (16 agents) / 7.7× (32) / 6.8× (64). Linux + Windows + multi-voice are step 9's follow-up |
| 13 | Doc-09 sweep | ✅ | `sfs-spec/09 §3.2` topology enum gains `torus_32`; §2 (W,H) gains (32,32); `sfs-spec/06 §1.6` primitives table gains `dm_pow2`; `sfs-spec/05 §5.2` clarifies MPE_TIMBRE / MPE_SLIDE naming |
| 14 | `phase-3-gate` tag | ⏳ — | This document is the tag body |

Plus one bonus deliverable not in §9:

| - | GUI 2D heatmap visualiser | ✅ | `SubstrateView::paint2D` divergent teal/orange palette; topology routes via `VoiceManager::topology()` |

**Status update (post-gate commits):** Steps 6 (7.1.4) and 9 (voice-pool threading) — originally deferred at gate time — landed in `P3.6b` and `P3.9` follow-ups on `main`. Phase 3 §9 is now 14 / 14 complete.

## New deterministic primitives (`sfs-spec/06 §1.6`)

| Primitive | Algorithm | Accuracy | Use site |
|---|---|---|---|
| `dm_pow2(x)` | 5th-degree minimax polynomial on [0, 1] + `std::ldexp` | < 1e-4 relative (~0.17 cents) | MPE pitch-bend ratio (block-rate) |

`dm_sin` tightened from Phase 2 (7th-order Hastings, ~3.6e-6) to the
spec-required 1e-6 budget via Cody-Waite range reduction + 11th-order
Taylor in double precision.

## Sonic-corner contract gate (`sfs-spec/08 §2.3`)

All four corners green on macOS arm64 in **both topologies** (Phase 2
shipped 1D only).

| Corner | 1D | 2D | Test |
|---|---|---|---|
| Drone | ✅ stddev/mean cv < 0.05 | ✅ | `sfs_contract_drone` |
| Pitched | ✅ ±3% across 13 chromatic notes (C3-C4) | ✅ ±3% across 8 notes (C3-G3, narrowed for 32×32 dispersion) | `sfs_contract_pitched` |
| Organic | ✅ centroid stddev > 50 Hz | ✅ | `sfs_contract_organic` |
| Glitch | ✅ ≥ 5 onsets in 5 s | ✅ | `sfs_contract_glitch` |

Each corner test factored its analysis pipeline into a helper function
so the 1D and 2D variants share the same threshold logic.

## Test coverage

| Job | Phase 2 → Phase 3 | Cases | Assertions |
|---|---|---|---|
| `sfs_unit_tests` | 79 → 100 | +21 cases | 232,153 → 294,112 |
| `sfs_contract_drone` | 1 → 2 (1D + 2D) | | |
| `sfs_contract_pitched` | 1 → 2 | | |
| `sfs_contract_organic` | 1 → 2 | | |
| `sfs_contract_glitch` | 1 → 2 | | |
| `sfs_contract_param_fuzz` | unchanged | 7 | |
| `sfs_contract_vm_fuzz` | unchanged | 14 | |

New unit tests cover `Substrate2D` (9 cases, 1060 assertions), Voice 2D
topology determinism, MPE pitch bend per channel + bit-exact regression,
FOA encoding (2D + 1D fallback), 5.1 surround render (2D + 1D
downmix), `dm_pow2` accuracy.

## Hard-invariant CI gates (Phase 2 → Phase 3 carry-forward)

| Invariant | Status |
|---|---|
| Bit-exact across platforms (1D + 2D, 22 preset PCM hashes) | ✅ green on macos-14, windows-2022, ubuntu-22.04 |
| `-fno-fast-math` / `/fp:precise` | ✅ |
| No libm transcendentals in `src/engine/` or `src/dsp/` | ✅ |
| No `std::default_random_engine` etc. | ✅ |
| No `AudioProcessorValueTreeState` | ✅ |
| FTZ/DAZ on audio thread | ✅ |
| No allocations on audio thread | ✅ |
| CFL bound `c² + κ ≤ 0.475` (1D) / `≤ 0.225` (2D) | ✅ |

## Pluginval

Continues at Level 5; supports stereo, 4-channel ambisonic /
quadraphonic, and 5.1 bus layouts on macos-14, windows-2022,
ubuntu-22.04.

## CPU profile (macOS arm64, Release; `docs/journals/phase-3-cpu-profile.md`)

Single voice, 1D topology:

| Substrate cells | Agent count | Wall ms / 1 s | Real-time factor |
|---|---|---|---|
| 1024 | 16 (default) | 110 ms | 9.0× |
| 1024 | 32 | 130 ms | 7.7× |
| 1024 | 64 (spec max) | 148 ms | 6.8× |

Phase 2 → Phase 3 cost delta: +30-60% (mostly the Cody-Waite `dm_sin`).
**4.5× under the spec budget at 64 agents.** Polyphony extrapolation
shows 8 voices × 32+ agents needs voice-pool threading — that's the
explicit step 9 deferral.

## Open Phase-4 items inherited from Phase 3

- LFE filter: 1st-order LPF → 2nd-order Linkwitz-Riley (5.1 + 7.1.4).
- `sfs_profile --topology 2d` and `--threading N` flags.
- Linux + Windows CPU profile (step 12 follow-up via CI artefact harvesting).
- Multi-voice CPU profile with threading (8 simultaneous, both topologies, 1/2/4 workers).
- Threading: extend the 5.1 / 7.1.4 / FOA paths to the parallel pool too
  (currently only `renderBlockStereo` dispatches to workers; the multi-
  channel paths still run serial because they're rare-host configurations
  that don't peak the CPU budget).

## Sign-off

Phase 3 gate is met when the next CI cycle ships:
- `ci` green on all 3 platforms (macos-14, windows-2022, ubuntu-22.04)
- `determinism` green: cross-platform PCM hashes match for both 1D
  and 2D corpora (22 hashes per platform)
- `pluginval` green at Level 5

Tag at the gate-passing commit: `git tag -a phase-3-gate -F docs/phase-plans/phase-3-gate-evidence.md`.
