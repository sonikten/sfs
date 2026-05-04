# Phase 1 — 1D substrate engine

Status: not started. Phase 1 code may not land until this file is committed
on `main` and Phase 0 has tagged `phase-0-gate`.

This is the in-repo execution plan for Phase 1. Master meta-plan:
`~/.claude/plans/the-research-phase-for-mutable-wolf.md`. Spec authority:
`sfs-spec/01_engine_architecture.md`, `sfs-spec/02_substrate_dsp.md`,
`sfs-spec/03_agent_dsp.md`, `sfs-spec/04_harvester_output.md`,
`sfs-spec/06_rng_presets.md` §1, `sfs-spec/09_glossary_and_parameters.md`.

## 1. Scope freeze

In scope (from `sfs-spec/08_implementation_roadmap.md` §1, Phase 1):

- **1D substrate** (`02 §2`): leapfrog displacement/velocity update, linear deposit kernel, linear read kernel, DC block at 5 Hz, periodic-boundary ring topology, internal CFL clamp `c² + κ ≤ 0.475`.
- **Agent pool** (`03`): sine waveform only via `dm_sin` (other 4 waveforms deferred to P2). Per-sample read-bend-deposit-migrate cycle. 1 voice × up to 32 agents (the spec's Phase 1 default).
- **Harvester bank** (`04 §1–2`): single mono harvester at a fixed listening point. Linear interpolation read.
- **Counter-based RNG** (`06 §1`): Philox-4×32-10 keyed by `(preset_seed, voice_index, agent_index, stream_id)` with stream IDs 0–7 assigned per `06 §2`. Box–Muller no-cache.
- **Hard-coded "preset"** (no preset format yet): a single canonical Phase 1 sustain configuration baked into the headless render rig and the AudioProcessor.
- **Test vectors** for substrate (`02 §11`) and agents (`03 §10`) — port and pass them.
- **CPU profile** of the inner per-sample loop. SSE2/NEON tightening only where the profile says it matters; don't pre-optimise.

Out of scope this phase:

- The other 4 agent waveforms (saw, square, FM-pair, noise). All P2.
- Polyphony — single-voice only. `MAX_VOICES = 1` for Phase 1.
- Stereo / quad / multichannel. Mono only. (Stereo lands at P2 alongside the harvester second listening point.)
- Macros and the modulation matrix. Substrate parameters are set directly from the preset JSON (or hard-coded values) at this phase. Macros land at P2.
- LFOs, envelopes (beyond a hard-coded ADSR or simple gate-on/off envelope for note-on).
- MIDI handling beyond note-on / note-off (no pitch bend, no CC, no MPE).
- 2D substrate (`02 §3`). All P3.
- TOPOLOGY / ASPECT structural selectors. P3.
- Preset JSON load/save. P4. (P1 uses a hard-coded preset baked into code.)
- GUI — none, except the **standalone 1D substrate visualiser diagnostic tool** brought forward from P4 per the solo-dev risk register (master plan §5). This is a separate executable / dev tool, NOT a GUI in the plug-in.

## 2. Doc-09 update list

Updates land in `sfs-spec/09_glossary_and_parameters.md` first, then propagate
to dependent docs in the same PR:

- §2 mathematical symbol table — verify ranges for `c²`, `κ`, `γ`, `N`, `u`, `v`, `p_i`, `f_i`, `φ_i`, `a_i`, `e_i`, `w_i`, `m_i`, `r_i`, `ε_i[n]`, `y_i[n]`, `u_inject[x]`, `K`, `α_dc`. No new symbols expected; correct any that don't match the implementation.
- §3.2 structural parameters — verify `structural.substrate_size` and `structural.deposit_kernel` / `read_kernel` defaults match Phase 1 implementation.
- §3.3 agent parameters — `agent.max_active = 32` for Phase 1 (cap the v1.0 max of 64 until P2 polyphony lands so we don't overshoot the CPU budget).
- §3.7 internal substrate fields — confirm `substrate.c2`, `substrate.viscosity`, `substrate.gamma`, `substrate.size` exist and are documented; add if missing.
- §3.8 internal agent fields — confirm `agent.deposit_weight`, `agent.mod_sensitivity_scale`, `agent.drift_scale`, `agent.migration_noise_scale`, `agent.harmonic_lock`, `agent.envelope.*` exist and are documented; add if missing.
- §"RNG stream IDs" (likely under `06 §2` / `09 §"RNG"`) — lock in the assignments for IDs 0–7 used in Phase 1 (init, position, shape, harmonic ratios, migration noise, sample-hold, random-walk LFO if any, envelope randomisation). Reserve 8–11 for P2/P3 expansion as the spec already plans.

If any of the above already match the implementation, the phase-1 commit
records "verified, no change" rather than churning the file.

## 3. Contract-test gate matrix

Reference: `08 §2.3` automated criteria.

| Corner | Status | Notes |
|---|---|---|
| **drone** | ENABLED degraded | Sine + low MIGRATION on 1D can satisfy `stddev(centroid)/mean(centroid) < 0.05`. Use the Phase-1-specific canonical preset, hash-locked to `tests/refs/contract/phase1_drone.wav`. |
| **organic** | SKIP | Needs centroid wander → multiple waveforms / coupling → P2. Test exits 0 with `# SKIPPED Phase < 2` marker. |
| **pitched** | ENABLED | Single-agent sine on 1D substrate with low coupling holds stable f0; YIN passes. Chromatic C2–C7 rendered at 0.8 s/note. |
| **glitch** | SKIP | Needs onset density → full coupling at high EXCITATION → P2. SKIP marker enforced. |

The skip markers themselves must exit non-zero if missing (no silent passes).

## 4. Test additions

New first-party files (with the spec section that defines them):

- `tests/unit/substrate_1d_test.cpp` — leapfrog stability, CFL clamp, periodic boundary, linear deposit and read kernels. Test vectors from `02 §11`.
- `tests/unit/agent_pool_test.cpp` — RNG-seeded position init, migration determinism with seeded Philox, multiplicative bend math. Test vectors from `03 §10`.
- `tests/unit/harvester_test.cpp` — linear-interpolated read at integer and fractional positions, mono output mixing.
- `tests/unit/rng_philox_test.cpp` — Philox-4×32-10 reference vectors per `06 §1` and the published Random123 vectors. Box–Muller stability.
- `tests/unit/dm_sin_refined_test.cpp` — refine `dm_sin` coefficients to hit the 1e-6 worst-case error budget on `[-π, π]`. (Phase 0 uses ~3e-6 placeholder coefficients.)
- `tests/integration/sine_30s_render.cpp` — render the Phase 1 canonical preset for 30 s, verify no NaN, no clip, reasonable spectrum.
- `tests/contract/{drone,pitched}.cpp` — wrappers around the criteria from `08 §2.3`. Drone: STFT centroid stability. Pitched: YIN tracking.
- `tests/contract/skipped/{organic,glitch}.cpp` — minimal stubs that exit 0 with the `# SKIPPED Phase < 2` marker.
- `presets/contract/phase1_drone.json` and `phase1_pitched.json` — the canonical contract presets. Hard-coded JSON form (preset format spec'd at P4 but the JSON is plain `nlohmann/json` data — no engine-side parser yet; the headless render rig reads these directly).
- `tests/refs/contract/phase1_drone.wav` and `phase1_pitched.wav` — committed reference WAVs (post first-CI cross-platform agreement).

CPU profile: a `tools/sfs_profile/` micro-benchmark over 60 s of audio at the
spec's reference configurations (M2 / i5-12600). Output as a markdown table
in `docs/journals/phase-1-cpu-profile.md`; gate on the spec's `08 §2.5`
benchmarks.

## 5. Risk register delta for Phase 1

Adds (vs. master plan §5 and `08 §9`):

| Risk | Why now | Mitigation |
|---|---|---|
| Substrate ring boundary effects audible as artefacts | First time agents migrate across the ring boundary in real audio | Test vector for periodic-boundary continuity at sample boundaries; ear test with a sweep across cell index 0 |
| Migration RNG draws drift across platforms | Phase 1 is the first phase that actually uses Philox in the audio loop | Reference vector test in `tests/unit/rng_philox_test.cpp`; cross-platform hash compare via the existing P0.8 harness extended with the Phase 1 canonical preset |
| YIN / pitch tracker dependency choice | Pitched contract needs a pitch tracker; vendor a small one or write our own | Recommended: vendor the public-domain Cython→C YIN port from aubio (BSD), or write a simple AMDF tracker. Decision made and committed before coding the pitched contract. |
| Single-voice scope hides voice-stealing bugs | We only catch them in P2 | Accepted; P2 plan-file must include a voice-stealing stress test before that phase closes |
| Standalone 1D visualiser diagnostic tool conflated with the eventual P4 GUI | Easy to over-build the diagnostic | Constrain the tool to **a single window, single substrate, no controls** other than a "save snapshot to PNG" button. Not packaged with the plug-in. |
| Hard-coded preset diverges from the eventual preset format | When P4 lands the JSON spec, our hard-coded values may not load cleanly | Land the canonical preset as `presets/contract/phase1_*.json` *now* with the field names from `06 §3.x`; the loader stays a hard-coded `if (path == ...) return ...` switch in the headless render rig until P4 |

## 6. Dogfooding script

Phase 1 dogfooding session (manual, ~30 minutes, end of phase, before tagging
`phase-1-gate`):

**Setup**
- Open REAPER on macOS arm64 (primary dev platform).
- Load the skeleton VST3.
- Insert the Phase 1 canonical preset by replacing `PluginProcessor`'s hard-coded init values with the `phase1_drone` preset (or via a temporary "preset selector" CC).

**Script**
1. Hold C4 (note 60) for 30 seconds. Listen for an evolving timbre — not a static sine, not noise, not silence. Write notes in `docs/journals/phase-1-macro-feel.md`.
2. Release. Listen for the substrate decay tail. Should die down naturally, no abrupt cut.
3. Hold C4 again immediately. Listen for re-ignition behaviour — does it sound like the same instrument or a different one?
4. Try C2, C5, C7. Listen for whether each pitch is recognisable (pitched contract test should already pass automatically).
5. Mute the channel. Watch the standalone visualiser diagnostic tool for 30 s. Does the substrate field "look like" what you'd expect?

**Acceptance** (subjective, recorded in the journal):
- Timbre changes audibly over 30 s of sustain. ✓ / ✗
- No clicks, dropouts, or crashes. ✓ / ✗
- Re-ignition is perceptually consistent. ✓ / ✗
- Visualiser shows wave-like activity, not silence or runaway saturation. ✓ / ✗

Subjective failures are blockers; document them and triage before tagging.

## 7. Definition of done

From `08 §1` Phase 1 gate, extended for the master plan's 4-platform / solo-dev posture:

- [ ] 30-second sustained note plays a recognisable evolving timbre (subjective; recorded in the macro-feel journal).
- [ ] Substrate stability verified across the macro-corner grid + 8 random points (CFL clamp test).
- [ ] Bit-exact reproducibility on all 4 platforms for the Phase 1 canonical render (extends the P0 determinism harness with `phase1_drone.wav` and `phase1_pitched.wav`).
- [ ] Pitched contract test passes (YIN ±3% on chromatic C2–C7).
- [ ] Drone-degraded contract test passes (`stddev(centroid)/mean(centroid) < 0.05`).
- [ ] Organic and glitch contract tests SKIP cleanly (marker present, non-zero on absence).
- [ ] All unit test vectors from `02 §11` and `03 §10` pass.
- [ ] CPU profile recorded; meets `08 §2.5` Phase 2 gate threshold for `1D-32-agent` (early indicator; the actual P2 gate is the budget enforcer).
- [ ] No allocations on the audio thread (Catch2 fixture override) — per the master-plan §4 invariant.
- [ ] FTZ/DAZ verified at audio-thread entry (per-platform unit test).
- [ ] Doc-09 diff non-empty (or "verified, no change") and propagated.
- [ ] `docs/journals/phase-1-cpu-profile.md` and `docs/journals/phase-1-macro-feel.md` committed.
- [ ] Annotated `git tag phase-1-gate` with gate-evidence summary.

## 8. CI delta turning on at end of Phase 1

- `ci.yml` builds the new substrate / agent / harvester / RNG targets and runs unit tests.
- `determinism.yml` extends to render the Phase 1 canonical preset on each platform; same hash-compare flow.
- New workflow `contract.yml` — runs `pitched` and `drone` contract criteria against the freshly-rendered canonical WAVs; SKIP-marker enforcement for organic/glitch.
- pluginval level stays at 1 for Phase 1 (escalates to 5 at P2 when polyphony + macros land).
- `cmake/DeterminismChecks.cmake` activated: greps `src/engine/` for `std::sin`/`std::cos`/`std::exp`/`std::log`/`sinf`/`cosf` and fails on hit; greps `src/` for `std::default_random_engine`/`std::mt19937`/`rand()`/`random()`/`AudioProcessorValueTreeState`. (Was deferred from P0 because there was no `src/engine/` to enforce against.)

## 9. Bring-up order (proposed)

Each step should land as a discrete commit, like Phase 0's per-step pattern,
unless multiple steps are tightly coupled. Adjust during execution.

1. RNG: `src/engine/rng/philox.{h,cpp}` + reference vector tests + Box–Muller helper. (Determinism foundation; everything else seeds from this.)
2. `dm_sin` coefficient refinement to 1e-6 + unit test.
3. Substrate 1D scalar reference: `src/engine/substrate/substrate_1d.{h,cpp}` — pure scalar; no SIMD yet. CFL clamp, leapfrog, DC block.
4. Linear deposit kernel + linear read kernel (`src/engine/substrate/kernels.{h,cpp}`).
5. Substrate test vectors from `02 §11` ported.
6. Agent pool scalar reference: `src/engine/agents/agent_pool.{h,cpp}` — sine only, multiplicative bend, migration via Philox stream 4.
7. Agent test vectors from `03 §10` ported.
8. Harvester bank: `src/engine/harvester/harvester.{h,cpp}` — single mono listening point.
9. Engine glue (`src/engine/engine.{h,cpp}`): substrate + agent pool + harvester per-sample interleave. Keep the AudioProcessor wrapper thin.
10. Plug-in integration: `PluginProcessor.cpp` swaps the 440 Hz test tone for the Phase 1 engine. Hard-coded `phase1_drone` config.
11. Headless render rig extension: render the Phase 1 canonical presets.
12. Contract tests for `pitched` and `drone`.
13. Standalone 1D visualiser diagnostic tool.
14. CPU profile micro-benchmark.
15. SSE2/NEON optimisation pass *only* on hot-path TUs identified by the profile.

Step 15 is the only optional/scope-flexible step — if the scalar code already
meets the `08 §2.5` Phase 2 threshold for the `1D-32-agent` config on the M2,
defer SIMD optimisation to P2 where it'll be needed for the polyphony
expansion anyway. Don't burn budget on premature SIMD.
