# Phase 3 — 2D substrate, multichannel + ambisonic, MPE, threading

Status: not started. Phase 3 code may not land until this file is
committed on `main` and `phase-2-gate` is tagged. Both conditions met
as of `b46ae82` / `phase-2-gate`.

Phase 2 made SFS a complete-feeling synth in stereo. Phase 3 expands
the spatial story (2D substrate, multichannel, ambisonic), the
expressive story (MPE), the precision story (Cody-Waite dm_sin), and
the throughput story (voice-pool threading).

Master meta-plan: `~/.claude/plans/the-research-phase-for-mutable-wolf.md`.
Spec authority: `sfs-spec/02_substrate_dsp.md` §3 (2D substrate),
`sfs-spec/04_harvester_output.md` §3.6 (multichannel + ambisonic),
`sfs-spec/05_macros_modulation.md` §6 (MPE expression sources),
`sfs-spec/01_engine_architecture.md` §6 (voice-pool threading),
`sfs-spec/06_rng_presets.md` §1.3 (dm_sin precision target),
`sfs-spec/09_glossary_and_parameters.md` (canonical inventory).

## 1. Scope freeze

In scope (from `sfs-spec/08_implementation_roadmap.md` §1, Phase 3
deliverables):

- **2D substrate** with torus topology. Sizes 32×32, 64×64, 128×128.
  CFL bound `c² + κ ≤ 0.225` for 2D (engine clamps 0.225). Macro fan-out
  rebinds c²/κ to TENSION/EXCITATION as in Phase 2 with the tighter bound.
- **Structural selector parameters**: `TOPOLOGY` (ring | torus_64 |
  torus_128 | torus_256), `ASPECT` (0.5..2.0, 2D-only).
- **Multichannel output**: 5.1 (six channels) and 7.1.4 (twelve
  channels). Per-channel harvester position derived from a regular
  polygon on the torus surface.
- **First-order ambisonic** (W/X/Y/Z, 4 channels, AmbiX/SN3D).
  Harvester sums into spherical-harmonic basis weighted by direction.
- **MPE Note Expression** — pitch bend per note, channel pressure,
  brightness (Y), slide (X). Maps to `MPE_PITCHBEND`, `MPE_PRESSURE`,
  `MPE_TIMBRE`, `MPE_RANDOM` mod-matrix sources from spec §5.
- **Voice-pool threading**: render the 8 voices in parallel on a
  worker pool. Per-voice render is already isolated (per-voice
  substrate + agents); the bus mix is the only barrier.
- **Cody-Waite range reduction** in `dm_sin` to hit the spec's 1e-6
  precision budget — Phase 2 deferred, see `docs(dm_sin)` commit notes.
- **5 ms ramp on stolen voices** — Phase 2 simplification removed.
  Stolen voice's substrate gain ramps from 1 to 0 over 5 ms; new note's
  agents start depositing immediately. Eliminates click on voice steal.
- **Substrate energy probe** — instrumented decay-vector unit test
  loading the JSON test vectors from `sfs-spec/02 §11`.
- **Linux + Windows CPU profile** alongside macOS, via CI artifact
  harvesting (Phase 2 only profiled macOS arm64).

Out of scope this phase:

- Preset format + browser → Phase 4
- GUI layout (curated knobs) → Phase 4
- 128 factory presets → Phase 4
- AU / AAX wrappers → Phase 5+
- Higher-order ambisonics (>FOA) → deferred indefinitely (sfs-spec/00
  reservation list)

## 2. Doc-09 update list

Sections this phase will touch in `sfs-spec/09_glossary_and_parameters.md`:

- §3.2 Structural — `topology` enum gains `torus_32` / `torus_64` /
  `torus_128` (already listed for `torus_256`); `aspect` becomes
  audible (no-op in Phase 2).
- §3.7 Harvester — multichannel positions for 5.1 / 7.1.4 / FOA.
- §3.10 Internal engine fields — 2D substrate state (u, v as
  [Nx × Ny] arrays).
- §3.11 Mod destinations — adds `STRUCTURAL.TOPOLOGY` and
  `STRUCTURAL.ASPECT` if they become destinations (likely not — they're
  preset-only structural selectors).
- §6 Numerical limits — 2D CFL bound `c² + κ ≤ 0.225` (separate from
  the 1D bound).

Doc-09 sweep extends beyond Phase 3 deliverables: catch up on every
field that drifted between Phase 1 and Phase 2 (envelope time ranges,
LFO rate range, etc. — see `doc(09)` commit for the start).

## 3. Contract-test gate matrix

| Phase | drone | organic | pitched | glitch |
|---|---|---|---|---|
| 3 | ENABLED ×2 (1D + 2D presets) | ENABLED ×2 | ENABLED ×2 | ENABLED ×2 |

Each corner gets a 1D variant (Phase 2 form) and a 2D variant. The 2D
preset must satisfy the same threshold (cv < 0.05 for drone, etc.).

`vm_fuzz` extends with:
- `topology` switching (ring → torus_64 → torus_128) under load.
- 5.1 + 7.1.4 + FOA bus configurations × the existing scenarios.
- MPE note expression sweeps (per-note pitch bend, pressure).

## 4. Test additions

New unit-test files:
- `tests/unit/substrate_2d_test.cpp` — 2D propagation, CFL clamp,
  energy decay, AC content decay regression.
- `tests/unit/ambisonic_test.cpp` — W/X/Y/Z basis verification at
  cardinal directions.
- `tests/unit/voice_pool_threading_test.cpp` — parallel render
  determinism + alloc-free.
- `tests/unit/cody_waite_test.cpp` — dm_sin precision <1e-6 across
  [-1000π, 1000π].

New integration presets (named):
- `presets/contract/drone_2d.json`
- `presets/contract/glitch_2d.json`
- `presets/contract/foa_test.json`

Reference WAVs added to `tests/refs/`:
- `phase3_canonical_2d_c4_48k_256.{raw,sha256}`
- `phase3_foa_canonical_48k_256.{raw,sha256}`

## 5. Risk register delta

| Risk | Severity | Mitigation |
|---|---|---|
| 2D substrate CPU ≫ 1D | High / Medium | Profile early; SIMD the inner loop if needed; keep N≤128 |
| Voice-pool threading breaks bit-exact determinism | High / Low | Per-voice render is already isolated; only worry is bus mix order — fixed sum order solves it |
| MPE on JUCE has rough edges | Medium / Medium | Use juce::AudioProcessor::supportsMPE = true + per-note state in VoiceManager |
| Cody-Waite reduction hard to get exactly right | Medium / Low | Reference implementation in cephes; deterministic via float-only ops |
| Multichannel output on Linux (ALSA limitations) | Low / Medium | Defer to Phase 5; CI verifies the rendered buffer correctness, not playback |

## 6. Dogfooding script

Concrete REAPER session for Phase 3 gate:

1. Load SFS as a 7.1.4 instrument on a track with ATMOS bus.
2. Switch `TOPOLOGY` between ring and torus_64; verify the spatialisation
   changes audibly (torus has more diffuse / smeared movement).
3. Sweep `ASPECT` slowly; verify the smearing dimension follows.
4. Play a sustained chord and use the MPE keyboard's per-note slide /
   pressure; verify each note responds independently.
5. CPU meter under 50% for 8-voice 2D-128 render at 48 kHz / 256 blocks.

## 7. Definition of done

Phase 3 gate is met when:

- `phase-3-gate` annotated tag exists on `main`.
- All 8 contract tests pass (4 corners × 2 topologies).
- Cross-platform PCM hashes match for the 1D canonical AND the 2D
  canonical AND the FOA canonical.
- Voice-pool threading test asserts identical PCM regardless of worker
  count (1, 2, 4, 8).
- pluginval Level 5 still green (regression check; would catch MPE
  binding issues).
- `docs/journals/phase-3-cpu-profile.md` recorded for macOS arm64,
  Linux x86_64, Windows x86_64 (via CI artifact harvesting).
- `docs/journals/phase-3-macro-feel.md` recorded.
- Doc-09 amendments propagated to dependent docs.

## 8. CI delta

New CI gates that turn on at phase end:

- Cross-platform hash for the 2D canonical render.
- Voice-pool threading determinism (same input → same output regardless
  of worker count).
- Multichannel buses (5.1, 7.1.4, FOA) validated by pluginval Level 5.
- Linux + Windows CPU profile artifacts uploaded each push (used to
  populate the Phase 3 CPU profile journal).

## 9. Bring-up order

Each step lands as a discrete commit (or small commit cluster).

1. **Cody-Waite range reduction in dm_sin** — closes Phase 2's 1e-6
   precision deferral. Standalone, low-risk first step.
2. **5 ms ramp on stolen voices** — small VoiceManager tweak with a
   targeted unit test.
3. **`Substrate2D` class** alongside `Substrate1D`. Same wave equation
   in 2D, leapfrog, CFL bound 0.225. Standalone unit tests.
4. **Voice gains 2D mode** behind a per-voice topology selector;
   Phase 2 1D is the default, 2D opt-in.
5. **TOPOLOGY / ASPECT host parameters** — adds two more host params
   to PluginProcessor.
6. **Multichannel bus support** — VoiceManager renders to N output
   channels; PluginProcessor's BusesProperties exposes 2 / 6 / 12 / 4
   (FOA) layouts.
7. **First-order ambisonic encoder** — per-harvester direction → SH
   weights → W/X/Y/Z bus.
8. **MPE Note Expression** — VoiceManager grows per-MPE-channel state;
   mod matrix gains MPE_* sources.
9. **Voice-pool threading** — parallel render via std::jthread or
   juce::ThreadPool. Bit-exact determinism preserved by fixed sum order.
10. **Contract test expansion** — 8 corners (4 × {1D, 2D}).
11. **CI workflow update** — render 2D canonical, FOA canonical, hash
    + cross-platform compare.
12. **CPU profile per platform**, journaled.
13. **Doc-09 sweep + propagation**.
14. **Phase 3 gate tag**.

Steps 1-2 are "close Phase 2 deferrals".
Steps 3-9 are the spatial + threading + MPE feature push.
Steps 10-14 are gate work.

## 10. Open questions (to resolve before each step kicks off)

- 2D substrate cell layout: row-major or Z-order curve for cache
  locality? Profile-driven decision in step 3.
- MPE: support per-note polyphony directly via JUCE's MPE class, or
  hand-roll the channel routing? Decide in step 8 after surveying JUCE 8's
  MPE support.
- Voice-pool threading: thread-per-voice (8 threads) or work-stealing
  pool? 8-voice fixed pool argues for the former; profile in step 9.
