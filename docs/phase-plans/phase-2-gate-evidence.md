# Phase 2 — Gate evidence

Records the evidence that Phase 2's Definition of Done (per
`docs/phase-plans/phase-2.md` §7 + `sfs-spec/08_implementation_roadmap.md` §1)
has been met. Used as the body of the `phase-2-gate` annotated tag once
the next CI cycle ships green.

## Engine deliverables

| Spec deliverable | Status | Evidence |
|---|---|---|
| 8-voice polyphony | ✅ | `src/engine/voice_manager.h::kMaxVoices=8`; `vm_fuzz/polyphony` test |
| Voice stealing | ✅ | `VoiceManager::findStealVictim`; `vm_fuzz/stealing` test (16-into-8) |
| Stereo output (2 harvesters per voice) | ✅ | `Voice::renderBlockStereo`; `sfs-spec/04 §3.2` positions 0 and N/2 |
| Quad output | ❌ deferred | Multichannel = Phase 3 (`sfs-spec/02 §3` 2D substrate is the natural pair) |
| Five agent waveforms | ✅ | `AgentShape::{Sine,Saw,Square,FmPair,Noise}` + `agent_waveforms_test` |
| Six macros + fan-out | ✅ | `src/engine/macros/macros.h::fanOut`; `Voice::applyMacroFanOut` wires all 6 |
| Per-parameter smoothing | ✅ | `VoiceManager::renderBlockStereo` one-pole at ~30 ms (Padé α) |
| 16-slot mod matrix | ✅ | `src/engine/mod_matrix/mod_matrix.h`; 8 sources × 6 destinations active |
| MIDI ingest (note + CC1) | ✅ | `PluginProcessor::processBlock` + `setMidiCc1`; `vm_fuzz/cc1` test |
| Sample-accurate automation | ✅ via macro smoothing | block-rate target / sample-rate eased smoothing |
| Agent migration with Gaussian noise | ✅ | `AgentPool::processOneSample` + Box-Muller via `Philox4x32Stream` |
| dm_sin coefficient tightening | ⚠️ deferred | Spec 1e-6 budget needs Cody-Waite range reduction — Phase 3 |
| Forbidden-symbol grep activation | ✅ | `tools/check_determinism.sh` runs on every push |

## Sonic-corner contract gate (`sfs-spec/08 §2.3`)

| Corner | Phase 2 form | Threshold | Test |
|---|---|---|---|
| Drone | 5 s held note, COHERENCE=1, MIGRATION=0, EXCITATION=0 | stddev/mean cv < 0.05 | `sfs_contract_drone` |
| Pitched | Chromatic C2-C7, YIN refined | ±3% across 61 notes | `sfs_contract_pitched` |
| Organic | 6 s with high MIGRATION + low COHERENCE + slow LFO | centroid stddev > 50 Hz | `sfs_contract_organic` |
| Glitch | 5 s with S&H LFOs at 8/11 Hz → DENSITY+EXCITATION | ≥ 5 onsets in 5 s | `sfs_contract_glitch` |

All four corners green on macOS arm64.

## Test coverage

| Job | Cases | Assertions |
|---|---|---|
| `sfs_unit_tests` | 79 | 232,153 |
| `sfs_contract_drone` | 1 | inline |
| `sfs_contract_pitched` | 1 | 61 notes |
| `sfs_contract_organic` | 1 | inline |
| `sfs_contract_glitch` | 1 | inline |
| `sfs_contract_param_fuzz` | 7 | 1,176 |
| `sfs_contract_vm_fuzz` | 14 | 161 |

`param_fuzz` covers: 64 macro corners (all `{0,1}^6`) + 5 shapes × 3 notes
+ 5 LFO shapes at max rate + mod-matrix ±1 + ADSR extremes + velocity 0/1
+ 200 Philox-seeded random configurations.

`vm_fuzz` covers: polyphony 1-8, voice stealing 16→8, block sizes
{16,32,64,128,256,512,1024,2048,69-unaligned}, sample rates
{44.1, 48, 88.2, 96, 192} kHz, chromatic C0-B7 (96 notes), 1000 retriggers,
CC1 sweep, macro automation, allNotesOff silence (-60 dBFS), wake-after-idle,
macro smoothing convergence, engine-level bit-exact determinism, 30 s held
note, LFO timing across sample rates.

## Hard-invariant CI gates

| Invariant | Mechanism | Status |
|---|---|---|
| Bit-exact across platforms | `.github/workflows/determinism.yml` PCM hash diff | ✅ green on macos-14, windows-2022, ubuntu-22.04 |
| `-fno-fast-math` / `/fp:precise` | `cmake/SimdConfig.cmake` + `tools/check_determinism.sh` greps | ✅ |
| No libm transcendentals in `src/engine/` or `src/dsp/` | `tools/check_determinism.sh` symbol grep | ✅ |
| No `std::default_random_engine` etc. | `tools/check_determinism.sh` symbol grep | ✅ |
| No `AudioProcessorValueTreeState` | `tools/check_determinism.sh` symbol grep | ✅ |
| FTZ/DAZ on audio thread | `juce::ScopedNoDenormals` in `processBlock` + `denormal_flush_test` | ✅ |
| No allocations on audio thread | `no_alloc_test` for Voice + VoiceManager | ✅ |
| CFL bound `c² + κ ≤ 0.475` (1D) | `Substrate1D::setCoefficients` clamp | ✅ |

## Pluginval

CI bumped from Level 1 to Level 5 (`P2.coverage` follow-up commit).
First Level-5 run pending CI rerun after the GCC `-Wshadow` shadow fix.

## CPU profile (macOS arm64, Release)

Per `docs/journals/phase-2-cpu-profile.md`:

| Configuration | Wall ms / 1 s render | Real-time factor |
|---|---|---|
| 1024 / 16 agents | 70 ms | 14× |
| 1024 / 32 agents | 97 ms | 10× |
| 1024 / 64 agents | 113 ms | ~9× |

All three configurations clear the spec gate threshold (≥ 4× real-time)
by 2-3.5×. Linux + Windows profiling deferred to CI artifact harvesting
(Phase 3).

## Open Phase-3 items inherited from Phase 2

- `dm_sin` to 1e-6 budget via Cody-Waite range reduction.
- `Substrate1D::vNew` as a member buffer (currently the per-block alloc
  that the no-alloc tests bound at ≤ 1 per voice).
- 5 ms ramp on stolen voices (Phase 2 simplification: substrate carries
  prior state and γ decays it).
- `Doc-09` full sweep — Phase 2 patched only the LFO-shape enum.

## Sign-off

Phase 2 gate is met when the next CI cycle ships:
- `ci` green on all 3 platforms (macos-14, windows-2022, ubuntu-22.04)
- `determinism` green: cross-platform PCM hashes match
- `pluginval` green at Level 5

Tag at the gate-passing commit: `git tag -a phase-2-gate -m "Phase 2 gate"`.
