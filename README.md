# Stigmergic Field Synthesis (SFS)

A VST3 instrument plug-in built around a stigmergic synthesis paradigm: per-voice agent oscillators interact only through a shared substrate (1D ring or 2D toroidal field), producing emergent timbres that range from sustained drones to glitched textures.

## Status

Phase 3 of 5 (per `docs/phase-plans/phase-3.md`). The DSP engine, polyphony, MPE, ambisonic / surround output, and a working VST3 host wrapper are all on `main`; the curated GUI, preset format, and 128-preset factory palette are Phase 4 work. Released as open source — see [Licensing](#licensing).

What's shipping today:

- **Substrate**: 1D ring (1024 cells) and 2D toroidal grid (32×32) FDTD wave equation, CFL-bounded, cross-platform bit-exact.
- **Agents**: up to 64 per voice; five waveforms (sine, polyBLEP saw, polyBLEP square, FM pair, sample-and-hold noise); per-agent migration, detune, deposit weight; multiplicative bend by substrate value.
- **Polyphony**: 8 voices with anti-click voice stealing (5 ms output ramp + substrate reset).
- **Macros**: TENSION, DAMPING, DENSITY, MIGRATION, COHERENCE, EXCITATION + topology selector, all host-automated and block-rate smoothed.
- **Modulation matrix**: 16 slots, 10 sources (LFOs, envelopes, MIDI CC1, key velocity, MPE pressure / timbre, per-note random), 6 destinations.
- **MPE Note Expression**: per-channel pitch bend (±48 st), pressure, and timbre (CC74).
- **Output layouts**: mono, stereo, 4-channel ambisonic (W, X, Y, Z=0), and 5.1 surround (ITU-R BS.775 angles on the 2D torus). 7.1.4 is Phase 4.
- **GUI** (Phase 2/3 diagnostic form): real-time substrate visualiser (1D scope and 2D heatmap) plus auto-generated knob panel for every host parameter.

What's deferred:

- Curated GUI layout + preset browser (Phase 4).
- 128 factory presets (Phase 4 sound design).
- Voice-pool threading for high-polyphony 2D loads (Phase 4 — single-voice CPU is currently 4.5× under spec budget; threading is a perf optimisation, not a correctness gap).
- 7.1.4 surround, Möbius / Klein topologies, higher-order ambisonics, AU / AAX wrappers.

See `docs/phase-plans/phase-3-gate-evidence.md` for the full Phase 3 deliverable matrix.

## Document map

The v1.0 specification under `sfs-spec/` is the single source of truth. Read in this order when getting up to speed:

1. `stigmergic_field_synthesis.md` — research background and novelty argument.
2. `sfs-spec/00_overview.md` — what v1.0 ships vs. defers.
3. `sfs-spec/01_engine_architecture.md` through `sfs-spec/08_implementation_roadmap.md` — DSP, control plane, GUI, roadmap.
4. `sfs-spec/09_glossary_and_parameters.md` — **canonical** glossary and parameter inventory. When in doubt, this document wins.
5. `docs/phase-plans/` — per-phase execution plans (each written before its phase begins).
6. `docs/journals/` — CPU profiles + macro-feel logs.

## Building

Cross-platform (macOS arm64, Linux x86_64, Windows x64). Submodules under `third_party/` are required.

```bash
git clone --recursive https://github.com/sonikten/sfs.git
cd sfs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Build flags: `-fno-fast-math` / `/fp:precise` and `-Werror` / `/WX` are enabled by default per the determinism contract; AVX2 attaches on x86_64 and NEON is implicit on ARM64. SIMDe handles cross-architecture portability.

Linux additionally needs JUCE's audio + GUI dev packages — see `.github/workflows/ci.yml` for the pinned `apt-get install` list.

The VST3 plug-in is built into `build/src/plugin/sfs_plugin_artefacts/Release/VST3/SFS.vst3` and (on macOS) auto-installed under `~/Library/Audio/Plug-Ins/VST3/`.

## Testing

```bash
# Unit tests (DSP, engine, RNG, no-alloc invariant, etc.)
build/bin/sfs_unit_tests

# Sonic-corner contract tests (drone / organic / pitched / glitch in 1D + 2D)
build/bin/sfs_contract_drone
build/bin/sfs_contract_organic
build/bin/sfs_contract_pitched
build/bin/sfs_contract_glitch

# Cross-platform fuzz harnesses
build/bin/sfs_contract_param_fuzz
build/bin/sfs_contract_vm_fuzz

# Multi-preset regression render (22 PCM hashes — drives the determinism CI)
build/bin/sfs_render_presets ./build/preset_renders

# Single-preset render rig
build/bin/sfs_render

# CPU profiler
build/bin/sfs_profile --substrate-cells 1024 --agents 16 --seconds 1.0 --repeat 5
```

## Toolchain

| | |
|---|---|
| Build | CMake ≥ 3.22 |
| Language | C++20 (no extensions) |
| Plug-in framework | JUCE 8 |
| Tests | Catch2 v3 |
| RNG | Random123 (Philox-4×32-10) |
| SIMD portability | SIMDe |
| JSON | nlohmann/json |
| Validation | pluginval Level 5 |

## Determinism contract

The same preset and MIDI input produce **bit-identical** audio across macOS arm64, Windows x64, and Linux x64. CI's `determinism` job renders a 22-preset corpus (every named preset × {1D ring, 2D torus}), hashes the PCM, and asserts cross-platform equality on every push. A mismatch fails the build with the divergent platform's WAV attached for diagnosis.

Hard invariants the CI enforces (see `tools/check_determinism.sh` and `cmake/DeterminismChecks.cmake`):

- `-fno-fast-math` / `/fp:precise` — non-negotiable; greps the compile log.
- No libm transcendentals (`std::sin`, `std::exp`, …) inside `src/engine/` or `src/dsp/` — agent oscillators use the polynomial `dm_*` primitives in `src/dsp/`.
- No `std::default_random_engine` / `std::mt19937` / `rand()` — Philox-4×32-10 only.
- No `juce::AudioProcessorValueTreeState` — its locking model breaks the audio-thread determinism contract; raw `AudioProcessorParameter` + an SPSC ring buffer are used instead.
- FTZ/DAZ on every audio-thread entry (`juce::ScopedNoDenormals`).
- No allocations on the audio thread; `no_alloc_test` enforces it.
- CFL bound `c² + κ ≤ 0.475` (1D) / `≤ 0.225` (2D) on the substrate.

(Intel macOS was dropped from v1.0 in early Phase 0; the contract still spans two architectures — ARM64 on macOS and x86_64 on Windows + Linux — which is the test that matters for catching libm/SIMD drift.)

See `sfs-spec/01_engine_architecture.md` §9 and `sfs-spec/06_rng_presets.md` §1 for the full contract.

## CI

Three workflows run on every push:

| Workflow | Coverage |
|---|---|
| `ci` | Compile + unit + contract + fuzz tests on macos-14 / ubuntu-22.04 / windows-2022, all with `-Werror` / `/WX`. |
| `determinism` | 22-preset PCM hash compare across the three platforms. |
| `pluginval` | JUCE's pluginval at Level 5 against the built VST3. |

All three must be green for a Phase gate tag to land.

## Licensing

Source code in this repository: open source — pick the license file at the repository root for terms.

Third-party dependencies under `third_party/` are vendored as git submodules under their own licenses:

- **JUCE 8** — JUCE License (free for GPL-3 / open-source projects; commercial license required for closed-source distribution).
- **Catch2 v3** — Boost Software License 1.0.
- **Random123** — BSD 3-Clause.
- **SIMDe** — MIT.
- **nlohmann/json** — MIT.

Contributors: please open issues / PRs on GitHub. The phase-plan workflow (a `docs/phase-plans/phase-N.md` file is committed before phase-N code lands) is the lightweight design-review checkpoint and applies to substantive engine work.
