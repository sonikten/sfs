# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Repository state

This repository is **specification-only** at the time of writing. There is no source tree, no build system, and no test rig — only the v1.0 technical specification for the Stigmergic Field Synthesis (SFS) VST3 instrument. Treat requests to "build", "test", or "run" as requests to either (a) edit the spec, or (b) bootstrap Phase 0 of the roadmap in `sfs-spec/08_implementation_roadmap.md` (CMake + JUCE 8 submodule + Catch2 + headless render rig). Don't fabricate build commands that the repo can't currently satisfy.

When implementation begins, the planned toolchain is: CMake ≥ 3.22, C++20, JUCE 8, Catch2 v3, Random123 (Philox), SIMDe, nlohmann/json, pluginval. Compile flags `-fno-fast-math` and `-mavx2` (with `/fp:precise /arch:AVX2` on MSVC) are mandatory for the determinism contract — see `08_implementation_roadmap.md` §6.

## Document map and reading order

Ten markdown files under `sfs-spec/` plus three top-level docs. Read in this order when getting up to speed:

1. `stigmergic_field_synthesis.md` (root) — the original research document. Establishes what SFS *is* and the novelty argument.
2. `sfs-spec/00_overview.md` — what v1.0 ships vs. defers, the four sonic-character contract, and the canonical pointer to doc 09.
3. `sfs-spec/01_engine_architecture.md` — subsystem boundaries, threading, voice model, lifecycle.
4. `sfs-spec/02_substrate_dsp.md` through `04_harvester_output.md` — the DSP track.
5. `sfs-spec/05_macros_modulation.md`, `06_rng_presets.md` — control plane, RNG, preset format.
6. `sfs-spec/07_gui_visualization.md` — GUI/product track (parallelisable with DSP track per the roadmap).
7. `sfs-spec/08_implementation_roadmap.md` — phases, gates, CI, dependencies, risk register.
8. `sfs-spec/09_glossary_and_parameters.md` — **canonical reference** for every term, symbol, and parameter ID.

`sfs-spec_overview.md` (root) is a brief hand-off note, not a spec document.

## Canonicality rule

**Document 09 is canonical.** When a definition, range, default, or symbol disagrees between 09 and any other document, 09 wins. When editing the spec to change a parameter's range, default, or definition, update 09 first, then propagate to dependent docs. Don't introduce new terms in 01–08 without adding them to 09's glossary. Don't introduce new parameters without adding a row to 09's parameter inventory.

Notation in the spec: **bold-italic** marks a term defined in 09; `code font` marks a parameter ID that exists 1:1 in 09's parameter table. Preserve this convention when editing.

## Hard invariants (the things that fail review if violated)

These are non-negotiable contracts. Any code, doc edit, or design suggestion that loosens them needs an explicit conversation with the user, not a unilateral change.

- **Stigmergy.** Agents only observe each other through the substrate. Any code path letting agents see each other's state directly is a v1.0 bug (`01_engine_architecture.md` §1).
- **Per-voice instancing.** Each polyphonic voice owns its own substrate, agent pool, and harvester bank. The `shared_substrate` seam is reserved in the preset format but not exposed at v1.0 (`01` §2).
- **Bit-exact reproducibility across platforms.** Same preset + same MIDI + same host conditions ⇒ identical samples on macOS arm64, Windows x64, Linux x64. (Intel macOS was dropped from v1.0 in early Phase 0 — runner-pool issue plus a user decision; cross-architecture verification still spans ARM64 (macOS) + x86_64 (Win/Linux), which is what catches libm/SIMD drift.) This forbids: `std::default_random_engine` or any platform RNG; direct `std::sin`/`std::cos` in the audio loop (use the polynomial approximations / LUTs); platform-specific SIMD math intrinsics with vendor-varying precision; `-ffast-math`. FTZ/DAZ must be set on every audio-thread entry. (`01` §9, `06` §1, `08` §9 risk register.)
- **CFL stability.** Substrate coefficients must satisfy `c² + κ ≤ 0.5` (1D) or `≤ 0.25` (2D); the engine clamps internally to 0.475 / 0.225. Any change to substrate update math must preserve this bound (`02` and `09` §2).
- **Lock-free audio thread.** No allocations, no file I/O, no locks between `setActive(true)` and `setActive(false)`. Control flows in via the typed SPSC ring buffer (`01` §4).
- **Sonic-corner contract.** The four corners (drone / organic / pitched / glitch) are a *release gate*, not a benchmark. Each must have a factory preset that passes the operational test in `00_overview.md` and `08` §2.3 before v1.0 ships.
- **Avoid JUCE's `AudioProcessorValueTreeState` at the audio-thread layer** — its locking model breaks determinism. Use raw `AudioProcessorParameter` + the SPSC queue (`08` §5).

## Architecture in one paragraph

The engine has five subsystems: **plug-in shell** (VST3 + GUI + preset I/O), **control plane** (macros, mod matrix, MIDI/MPE, voice manager), **agent pool** (per-voice oscillators that deposit into and are bent by the substrate), **substrate** (per-voice 1D ring or 2D toroidal field updated by a wave + diffusion + loss equation), and **harvester bank** (per-voice fixed listening points → output stage). Three rate domains: audio rate (substrate, agents, harvester read), block rate (macros, mod matrix, voice allocation — smoothed across the block at audio rate), GUI rate (30/60 Hz). The substrate is the *only* shared-state subsystem with the agent pool — that's the architectural realisation of the stigmergy principle.

Six primary macros: `TENSION`, `DAMPING`, `DENSITY`, `MIGRATION`, `COHERENCE`, `EXCITATION`. Two structural selectors: `TOPOLOGY`, `ASPECT`. 16-slot mod matrix with 12 sources × 24 destinations. Up to 8 voices × 64 agents × 5 waveform types. RNG is Philox-4×32-10 keyed by `(preset_seed, voice_index, agent_index, stream_id)`; counter-based and bit-exact across platforms.

## Editing the spec

- Cross-document consistency matters — parameter ranges, defaults, and stream-ID assignments are duplicated across docs by design (each doc is independently buildable into a milestone). When you change a value in one place, search for it across all ten files and update everywhere it appears, with 09 as the source of truth.
- The spec deliberately defers some features (Möbius/Klein topologies, higher-order ambisonics beyond first, external audio excitation, AU/AAX wrappers, user-extensible agent waveforms). Don't quietly promote a deferred feature into v1.0; if a request seems to require one, surface the deferral.
- Phase 4 (GUI) starts in parallel with Phase 3 once Phase 2 is stable — the DSP track (docs 01–06, 08) and the GUI/product track (07, parts of 05–06) are designed to be worked independently.
