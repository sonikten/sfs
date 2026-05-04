# Stigmergic Field Synthesis (SFS)

A VST3 instrument plug-in built around a stigmergic synthesis paradigm: per-voice agent oscillators interact only through a shared substrate (1D ring or 2D toroidal field), producing emergent timbres that range from sustained drones to glitched textures.

## Status

Pre-implementation. The v1.0 specification lives under `sfs-spec/`; the code tree is being bootstrapped (Phase 0 of the implementation roadmap). See:

- `stigmergic_field_synthesis.md` — research background and novelty argument.
- `sfs-spec/00_overview.md` — what v1.0 ships vs. defers.
- `sfs-spec/08_implementation_roadmap.md` — phase plan and CI strategy.
- `sfs-spec/09_glossary_and_parameters.md` — **canonical** glossary and parameter inventory. When in doubt, this document wins.
- `docs/phase-plans/` — per-phase execution plans (written before each phase begins).

## Toolchain

Locked in `sfs-spec/08_implementation_roadmap.md`:

| | |
|---|---|
| Build | CMake ≥ 3.22 |
| Language | C++20 (no extensions) |
| Plug-in framework | JUCE 8 |
| Tests | Catch2 v3 |
| RNG | Random123 (Philox-4×32-10) |
| SIMD portability | SIMDe |
| JSON | nlohmann/json |
| Validation | pluginval |
| Compile flags | `-fno-fast-math` / `/fp:precise`, `-Werror` / `/WX`; AVX2 on x86_64, NEON on ARM64 |

## Determinism contract

The same preset and MIDI input produce **bit-identical** audio across macOS x64, macOS arm64, Windows x64, and Linux x64. This is enforced in CI on every push. See `sfs-spec/01_engine_architecture.md` §9 and `sfs-spec/06_rng_presets.md` §1 for the contract; see `cmake/DeterminismChecks.cmake` (added during Phase 0) for the enforcement mechanism.

## Licensing

This repository contains the engine source. JUCE 8 is included as a submodule under its own license; a commercial JUCE license is required before public release. Patent search and prior-art documentation are tracked separately and must complete before public announcement.
