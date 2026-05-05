# Phase 2 — Polyphony, stereo, full waveforms, macros, mod matrix

Status: not started. Phase 2 code may not land until this file is committed
on `main` and `phase-1-gate` is tagged. Both conditions are met as of
`b65f0dd` / `phase-1-gate`.

This is the biggest single phase by code volume and by user-visible impact.
Phase 1 made the engine work; Phase 2 makes it a synth a user can actually
shape.

Master meta-plan: `~/.claude/plans/the-research-phase-for-mutable-wolf.md`.
Spec authority: `sfs-spec/01_engine_architecture.md` (voice manager,
threading), `sfs-spec/03_agent_dsp.md` (full waveform palette, migration
noise), `sfs-spec/04_harvester_output.md` (stereo + quad), `sfs-spec/05_macros_modulation.md`
(macros + mod matrix), `sfs-spec/09_glossary_and_parameters.md` (canonical
parameter inventory).

## 1. Scope freeze

In scope (from `sfs-spec/08_implementation_roadmap.md` §1, Phase 2 deliverables):

- **8-voice polyphony** (`MAX_VOICES = 8`). Voice manager (`01 §6`) handles
  allocation, voice stealing (5 ms ramp), per-note state.
- **Stereo output**: two harvesters per voice at substrate positions `0`
  and `N/2`. Spatial panning by harvester position is reserved for Phase 3
  (multichannel).
- **Quad output (4.0)**: four harvesters at `(N/4)·k` for `k = 0..3`.
- **All five agent waveforms** (`03 §3`), bandlimited where applicable:
  - `sine` (have it via `dm_sin`)
  - `saw` (naive ramp + PolyBLEP, ~10 ops, 60 dB aliasing)
  - `square` (naive ±1 + PolyBLEP at phase 0 and 0.5)
  - `fmpair` (two-op FM, ratio/index from preset; uses `dm_pow` at ignition)
  - `noise` (sample-and-hold at agent frequency, per-agent Philox stream 5)
- **Six macros** (`05 §2`), each with the fan-out documented in `05 §3`:
  `TENSION`, `DAMPING`, `DENSITY`, `MIGRATION`, `COHERENCE`, `EXCITATION`
- **Per-parameter smoothing** (`05 §4`) — one-pole low-pass per macro target.
- **Modulation matrix** (`05 §5`): 16 slots × 12 sources × 24 destinations.
  Sources: 4 LFOs, 2 envelopes, 2 MIDI key sources, 2 MPE sources, RANDOM,
  MIDI_CC1. Destinations: 6 macros + structural + LFO rates + agent fields
  + envelope times.
- **MIDI** ingest: note-on/off (have it), pitch bend, CC1 (mod wheel),
  CC123 (all-notes-off panic).
- **Sample-accurate parameter automation** (`05 §7`) — host automation
  events get applied at the per-sample boundary, not block-rate.
- **Agent migration with Gaussian noise** per spec `03 §5`. Requires
  `dm_log`, `dm_cos`, `dm_sqrt` (Box-Muller per `06 §1.4`). With Gaussian
  noise the organic and glitch contract tests start to be satisfiable.
- **`dm_sin` coefficient tightening** to 1e-6 budget (deferred from Phase 1).
- **Forbidden-symbol grep activation in CI for `src/engine/`** —
  `tools/check_determinism.sh` already runs; the engine TUs now exist so
  the grep enforces real rules instead of empty matches.

Out of scope this phase:

- 2D substrate (`02 §3`). Phase 3.
- 5.1 / 7.1.4 multichannel. Phase 3.
- First-order ambisonic. Phase 3.
- TOPOLOGY / ASPECT structural selectors. Phase 3.
- MPE Note Expression integration. Phase 3 (mod matrix sources arrive in P2
  to support it; the actual VST3 MPE bindings land in P3).
- GUI (any GUI). Phase 4. Engine remains headless / stock-JUCE-knob only.
- Preset format JSON loader. Phase 4. Phase 2 still uses hard-coded
  defaults + JUCE host parameter automation.
- Substrate downsampling `S = 2`. Phase 3 (only matters for 2D).

## 2. Doc-09 update list

Phase 2 touches a lot of `09`. Each Doc-09 section listed here gets reviewed
for accuracy and amended if the implementation diverged:

- §2 mathematical symbol table — review for any new symbols introduced by
  the macro fan-out (probably none beyond what's already there).
- §3.1 macros — verify ranges, defaults, and curves match the implemented
  fan-out functions exactly.
- §3.2 structural — `structural.deposit_kernel = lanczos4` becomes
  selectable when the lanczos4 deposit lands (optional Phase 2 stretch).
- §3.3 agents — full host-visible inventory: `max_active`, `harmonic_set`,
  `shape_distribution.*`, `shape_param.*`, `detune_scale`. Phase 1 only
  wired `max_active` and the static-defaults; Phase 2 wires everything.
- §3.4 envelopes — `env1` + `env2` ADSR exposed as host parameters.
- §3.5 LFOs — 4 LFOs × 6 fields = 24 parameters.
- §3.6 mod matrix — 16 slots × 5 fields = 80 internal parameters (only
  `depth` is host-visible; the rest are internal).
- §3.7 internal substrate fields — verify the macro fan-out matches the
  internal field IDs exactly.
- §3.8 internal agent fields — same review.
- §"RNG stream IDs" / `06 §2` — confirm IDs 8-11 (mod RANDOM, MPE_RANDOM,
  harvester orbit, dice button) are correctly attributed.
- **Amend `02 §6`** with the in-state-DC-blocker design lesson from Phase 1
  (the spec's `u[x] = u_blocked[x]` overwrite breaks energy conservation;
  the blocker now lives at the harvester output). User has unstaged spec
  edits; coordinate the amendment.

## 3. Contract-test gate matrix

Phase 2 is when ALL FOUR sonic corners must pass on canonical presets
(`sfs-spec/08 §1` Phase 2 gate text: "all four sonic-corner contract tests
pass").

| Corner | Phase 2 status | Driving feature |
|---|---|---|
| **drone** | Already passing in degraded form; tighten with full ENVELOPE + COHERENCE | `agent.harmonic_lock` from COHERENCE macro |
| **organic** | NEW — ENABLED. Centroid wander > 50 Hz from band-passed centroid. | Migration noise (Gaussian) + multiple waveforms |
| **pitched** | Tighten — extend to full C2-C7 chromatic (61 notes) when full pitch tracker lands | YIN port or refined autocorrelation |
| **glitch** | NEW — ENABLED. Onset density ≥ 50 in 50 s @ high EXCITATION + low DAMPING | Substrate driven into critical regime by macros |

`tests/contract/` gets two new files:
- `organic_test.cpp` — band-passed centroid RMS analyser
- `glitch_test.cpp` — spectral-flux onset detector
Plus expanded `pitched_test.cpp` (full chromatic) and `drone_test.cpp`
(60-second analysis window).

## 4. Test additions

New first-party test files (with the spec section that defines them):

- `tests/unit/voice_manager_test.cpp` — voice stealing, note routing,
  per-voice independence.
- `tests/unit/macro_fan_out_test.cpp` — each macro's fan-out function
  matches the curve documented in `05 §3` (e.g.
  `TENSION ↦ substrate.c2 = 0.49 · m²`).
- `tests/unit/mod_matrix_test.cpp` — slot evaluation, multiple-slot
  summation per destination, curve types (LINEAR, EXPONENTIAL, S_CURVE,
  INVERTED, BIPOLAR).
- `tests/unit/lfo_test.cpp` + `tests/unit/envelope_test.cpp`.
- `tests/unit/dm_log_test.cpp` + `dm_cos_test.cpp` + `dm_sqrt_test.cpp` —
  accuracy bounds per `06 §1.6`.
- `tests/unit/agent_waveforms_test.cpp` — saw / square aliasing < -60 dB,
  fmpair output bounded, noise white-spectrum.
- `tests/integration/polyphony_clean.cpp` — 6-note chord plays without
  clicks, voice-stealing transitions are smooth.
- `tests/contract/{organic,glitch}_test.cpp` — see §3.

## 5. Risk register delta

Adds (vs. master plan §5 + Phase 1 risks):

| Risk | Why now | Mitigation |
|---|---|---|
| Macro fan-out feels arbitrary in user testing | Phase 2 is when users can finally tune; if the curves feel wrong, the synth feels wrong | Maintain `docs/journals/phase-2-macro-feel.md` updated per macro after each curve adjustment; A/B test specific curve forms |
| Voice stealing causes audible artifacts | Hard-coded 5 ms ramp may be too short / too long | Test with rapid-fire MIDI sequences (32nd-note runs); compare to JUCE Synthesiser stealing behaviour |
| Mod matrix evaluation cost spikes with many active slots | 16 slots × per-sample evaluation could measurably hit CPU | Block-rate evaluation per spec `05 §5`, sample-rate smoothing only on the macro outputs; profile with all 16 slots active |
| Gaussian noise breaks determinism if Box-Muller cache state leaks | Gauss draws are interleaved across agents within a sample | No-cache Box-Muller per `06 §1.4` (4× more Philox calls but order-independent) |
| `dm_log` / `dm_cos` / `dm_sqrt` cross-platform drift | New transcendental approximations on the audio path | Polynomial coefficients chosen for float-32 minimax; per-platform unit tests asserting < 1e-3 absolute error vs `std::log` etc. |
| Macro→internal-field locking races on parameter automation | Block-rate macro evaluation means automation events between blocks miss the cutoff | Sample-accurate path per `05 §7`; automation events get applied at the sample boundary they land on |
| Polyphony × 8 × 32 agents exceeds per-core CPU on Intel x86_64 scalar | We've only profiled macOS arm64; Intel may be 2-3× slower | Profile on Linux/Win CI runners as part of the gate; fall back to SIMD pass (Phase 1 §15) if budget exceeded |
| `dm_sin` 2e-6 → 1e-6 tightening may shift the locked Phase 1 hash | Coefficient refit changes bytes | Re-render canonical, update `tests/refs/phase1_canonical_c4_*.sha256`, document in commit |

## 6. Dogfooding script

Phase 2 dogfooding (manual, ~60 minutes total, one session per macro group):

**Session 1 — substrate macros** (TENSION, DAMPING, EXCITATION)
- Hold C4 in Ableton Live. Sweep TENSION 0 → 1 over 10 s. Listen for the
  predicted progression from "local agents only" to "ring-spanning standing
  waves." Note in `docs/journals/phase-2-macro-feel.md`.
- Same for DAMPING (release-time control) and EXCITATION (substrate→agent
  coupling intensity).

**Session 2 — agent macros** (DENSITY, MIGRATION, COHERENCE)
- DENSITY 0 → 1 should add agents with a 50 ms fade-in, not click.
- MIGRATION 0 → 1 should produce audibly more wandering timbre.
- COHERENCE 0 → 1 should detune → harmonic lock the agent set.

**Session 3 — full set + polyphony**
- Hold a 6-note chord. Sweep each macro. Listen for per-voice independence
  (the chord stays in tune).
- Stress: rapid 32nd-note arpeggios. Listen for click-free voice stealing.

**Session 4 — sonic-corner presets**
- Build (in code) a "drone" preset config. Render 60 s. Listen.
- Build "organic," "pitched," "glitch." Each should sound like its name.
- These are NOT the contract tests (those are automated); these are the
  sound-designer's ear validating the engine reaches each corner.

Subjective failures recorded in the macro-feel journal block the phase tag.

## 7. Definition of done

From `08 §1` Phase 2 gate, extended for our 3-platform / solo-dev posture:

- [ ] 6-note polyphonic chord plays cleanly (no clicks, no NaN, no voice
      bleed). Tested at 32nd-note arpeggio rate.
- [ ] Voice stealing works under rapid-fire MIDI; transitions are
      bounded-amplitude (< -3 dBFS on the steal click).
- [ ] All four sonic-corner contract tests pass on the canonical Phase 2
      preset for each: drone, organic, pitched, glitch.
- [ ] All six macros wired as host-automatable parameters; each fan-out
      matches the curves in `05 §3`.
- [ ] Modulation matrix evaluates correctly: per-slot source × curve ×
      depth → destination, multiple slots add per destination.
- [ ] Stereo + quad output layouts validated in Live + REAPER.
- [ ] Sample-accurate parameter automation: a host-automated TENSION
      sweep across one block transitions smoothly, not in block-boundary
      stair-steps.
- [ ] All 5 agent waveforms produce sane spectra: sine pure, saw/square
      aliasing < -60 dB above fundamental·N for some N, fmpair bounded,
      noise approximately white.
- [ ] `dm_sin` worst-case error ≤ 1e-6 on principal range; `dm_log`,
      `dm_cos`, `dm_sqrt` within their spec budgets (`06 §1.6`).
- [ ] CPU profile: 8 voices × 32 agents on macOS arm64 ≤ 50% one core
      scalar; cross-platform numbers recorded in
      `docs/journals/phase-2-cpu-profile.md`.
- [ ] `cmake/DeterminismChecks.cmake` activated: forbidden-symbol grep
      enforced for `src/engine/`.
- [ ] Doc-09 diff: every parameter inventory section reviewed; amend
      `02 §6` (DC-block design correction).
- [ ] `docs/journals/phase-2-macro-feel.md` and
      `docs/journals/phase-2-cpu-profile.md` committed.
- [ ] Annotated `git tag phase-2-gate` with gate-evidence summary.

## 8. CI delta turning on at end of Phase 2

- `ci.yml`: builds the new mod-matrix / voice-manager / waveform code,
  runs unit tests for each.
- New workflow `contract.yml` (or extend `ci.yml`): runs all four
  contract tests on every push, gates on green.
- pluginval Level 1 → **Level 5** (`08 §3` schedule). Level 5 stress-tests
  parameter automation, MIDI edge cases, multi-block render consistency.
- `tools/check_determinism.sh` actively enforces against `src/engine/`
  (was empty in P1; now non-trivial).
- Per-platform CPU profile run via `sfs_profile`, results uploaded as a
  CI artifact + diff'd against the previous green commit (regression alert
  if > 10% slower).

## 9. Bring-up order

Each step lands as a discrete commit (or small commit cluster). Adjust as
real implementation throws curveballs.

1. **dm_log + dm_cos + dm_sqrt primitives** (`src/dsp/`) + tests.
   Required by Box-Muller, FM-pair init, harmonic ratio derivation.
   Lightweight first step; gates everything that follows.
2. **dm_sin coefficient tightening** to 1e-6. Update reference hash.
3. **Box-Muller Gaussian** on `Philox4x32Stream` (`nextGaussian()`) +
   tests vs `std::normal_distribution` (must match within 1e-3).
4. **Agent migration noise** (`ε_i` per spec §5): wire Gaussian into
   `processOneSample`. Update Phase 1 reference hash again (intentional;
   audible change).
5. **5 agent waveforms**: extend `Agent` with `shape` enum; add saw/square
   PolyBLEP, fmpair, noise paths in `processOneSample`. Tests per
   waveform.
6. **Macro fan-out infrastructure** (`src/engine/macros/`): each macro
   maps `[0, 1] → internal field(s)` per `05 §3` curves. Per-parameter
   smoothing (one-pole). Tests per macro.
7. **Voice manager** (`src/engine/voice_manager.{h,cpp}`): owns 8 Voice
   instances; routes MIDI per channel; voice stealing with 5 ms ramp.
   Replace `PluginProcessor`'s single Voice with VoiceManager.
8. **Modulation matrix** (`src/engine/mod_matrix.{h,cpp}`): 16 slots,
   12 sources, 24 destinations. Block-rate evaluation; per-destination
   sum.
9. **LFOs + envelopes** (`src/engine/lfo.{h,cpp}`, `envelope.{h,cpp}`):
   4 LFOs + 2 ADSR envelopes per voice. Wired into mod matrix as sources.
10. **Stereo output**: two harvesters per voice. Per-voice mix into the
    stereo bus.
11. **Quad output**: optional bus layout switch.
12. **Sample-accurate parameter automation** (per `05 §7`): JUCE
    `processBlock` parameter changes get applied at the right sample.
13. **Forbidden-symbol grep activated** for `src/engine/` (was empty
    until step 1).
14. **Contract test expansion**: organic + glitch criteria implemented;
    pitched extended to 61 notes.
15. **CPU profile per-platform** + journal.
16. **Doc-09 amendment review** + propagation.
17. **Phase 2 gate tag**.

Steps 1-5 are the "expand the engine vocabulary" cluster.
Steps 6-12 are the "make it controllable" cluster.
Steps 13-17 are the "close the gate" cluster.
