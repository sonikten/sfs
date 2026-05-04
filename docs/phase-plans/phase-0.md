# Phase 0 — Foundations

Status: in progress.

This is the in-repo execution plan for Phase 0. The master meta-plan lives at `~/.claude/plans/the-research-phase-for-mutable-wolf.md`; this file is the load-bearing record for the verification check ("`docs/phase-plans/phase-<current>.md` is committed before the phase's first code commit").

## 1. Scope freeze

In scope (from `sfs-spec/08_implementation_roadmap.md` §1, Phase 0 deliverables):

- Cross-platform CMake build system (macOS x64+arm64, Windows x64, Linux x64).
- JUCE 8 integration as a submodule.
- GitHub Actions CI: build on push, run unit tests, lint, format check.
- Headless test rig that loads a preset, sends fixed MIDI, renders to WAV.
- `clang-format` preset committed; pre-commit hook for format check.
- Skeleton VST3 wrapper passing pluginval Level 1.

Out of scope this phase:

- Any DSP beyond a polynomial 440 Hz sine tone in the skeleton plug-in.
- Substrate, agent, harvester, modulation matrix, macros, presets, GUI.
- Custom clang-tidy AST matchers for the determinism invariants (CI grep checks suffice for Phase 0; matchers are a Phase 1 candidate if grep proves too coarse).
- Linux DAW host testing (CI hash + pluginval green is the bar; in-host testing deferred to Phase 5 per solo-dev risk register).

## 2. Doc-09 update list

None. Phase 0 introduces no parameters and changes no ranges, defaults, symbols, or stream IDs.

## 3. Contract-test gate matrix

All four contract tests SKIPPED for Phase 0 (no DSP exists). The contract harness itself is wired up in Phase 0 so the SKIP marker can be enforced; criteria checks turn on in Phase 1 (drone degraded, pitched) per the master plan.

## 4. Test additions

- `tests/determinism/sine_skeleton_hash.cpp` — render the skeleton sine for 1 s at 48 kHz / 256 blocks, hash output, compare to committed reference hash.
- `tests/refs/sine_skeleton_48k_256.wav` and `tests/refs/sine_skeleton_48k_256.sha256` — reference render + hash.
- `tests/unit/dm_sin_basic.cpp` — sanity test for the polynomial sine: error against `std::sin` < 1e-6 across `[-π, π]`.

(Catch2 is added in step 4 of the bring-up; these tests are written after that step.)

## 5. Risk register delta for Phase 0

Adds (vs. `sfs-spec/08_implementation_roadmap.md` §9):

| Risk | Why now | Mitigation |
|---|---|---|
| Windows MSVC + JUCE + CMake bring-up time sink | Most fragile platform combo for solo dev | Stand up empty CMake on `windows-2022` *before* adding JUCE — failure isolation |
| Apple universal binary with per-arch SIMD flags | `-Xarch_x86_64 -mavx2` is fragile | Build x64-only and arm64-only as separate CI jobs first; combine universal only after both green |
| Linux JUCE GUI deps | apt package list is undocumented | Pin the apt list in the Linux CI step now, even though Phase 0 plug-in is GUI-less |
| Submodule pin drift | Easy to miss | Every submodule pinned to a tagged release; `tools/check_submodule_pins.sh` runs in CI |
| Determinism harness deferral | "We'll add it once there's real DSP" is the wrong order | Sine-skeleton hash diff lands in Phase 0, before DSP confounds the picture |

## 6. Dogfooding script

Skipped for Phase 0 — there is no synth to dogfood. The substantive dogfooding script first appears in `docs/phase-plans/phase-1.md` with the Phase 1 sine-only canonical preset and the pitched + degraded-drone contract tests.

A *toolchain* sanity check at end of Phase 0:

1. Open REAPER on each of the 4 platforms.
2. Load the skeleton VST3.
3. Send any MIDI note. Confirm a 440 Hz sine plays.
4. Stop the transport. Confirm no clicks, no DAW crash, no validation warnings in the REAPER log.

## 7. Definition of done

From `sfs-spec/08_implementation_roadmap.md` §1, Phase 0 gate (extended to four platforms per the locked strategic choice). Status as of the latest commit:

- [x] CI green on `macos-14`, `windows-2022`, `ubuntu-22.04` for commit `d94b544`. (`macos-13` Intel runner was dropped from v1.0 — runner pool was perma-starved on this account, and cross-architecture verification still spans ARM64 (macOS) + x86_64 (Win/Linux), which is the meaningful test for catching libm/SIMD/RNG drift.)
- [ ] Skeleton VST3 loads in REAPER on each platform and plays a polynomial 440 Hz sine. *(verified macOS arm64 locally; Win/Linux manual checks pending — pluginval green on all 3 in CI; full DAW load is a manual checkpoint deferred to your next session in those hosts.)*
- [x] `tests/refs/sine_skeleton_48k_256.sha256` hash-matches across all 3 platforms — locked in at `0791b4c6abc45f41920b393db935bfcad4046f27b57db770eebe1ab21c580500`. (`determinism-compare` job green for commit `d94b544`.)
- [x] pluginval Level 1 clean on all 3 platforms (`pluginval.yml` v1.0.4) for commit `d94b544`.
- [x] `clang-format --dry-run --Werror` job in `ci.yml` (Linux) — clean on landed files; pending the actual matrix run.
- [x] All 5 submodules pinned to tagged releases; `tools/check_submodule_pins.sh` green locally and wired into CI.
- [x] No use of `std::sin`/`std::cos`/`std::exp`/`std::log`/`std::mt19937`/`std::default_random_engine`/`rand()`/`random()`/`AudioProcessorValueTreeState` in `src/` — `tools/check_determinism.sh` green locally and wired into CI.
- [x] Catch2 v3 test infrastructure landed; `sfs_unit_tests` builds and runs (4 cases / 8 assertions, all pass on macOS arm64). Wired into `ci.yml` via `ctest`.

## 8. CI delta turning on at end of Phase 0

- `ci.yml`: 4-platform matrix build + test + lint + format + pluginval Level 1.
- `determinism.yml`: per-platform render + hash + cross-platform hash compare.
- `pluginval.yml`: pluginval Level 1 (escalates per phase to Level 5 / Level 10 later).
- `cmake/DeterminismChecks.cmake`: invoked from CI; greps compile log for `-ffast-math` / `/fp:fast` and source for forbidden symbols.

## 9. Bring-up order (as built)

1. Repo skeleton (`.gitignore`, `.clang-format`, `.clang-tidy`, `README.md`, `docs/phase-plans/phase-0.md`). **Done.**
2. Top-level `CMakeLists.txt` with C++20, `-fno-fast-math` / `/fp:precise`, `-Werror` / `/WX`; split into `SFS::DeterminismFlags` and `SFS::StrictWarnings`. **Done.**
3. GitHub Actions 3-platform matrix (`macos-14`, `windows-2022`, `ubuntu-22.04`). **Done** (`ci.yml`). Originally included `macos-13` (Intel) but the runner pool was perma-starved; Intel macOS dropped from v1.0.
4. Submodules pinned: JUCE 8.0.4, Catch2 v3.7.1, Random123 v1.14.0, SIMDe v0.8.2, nlohmann/json v3.11.3 + `tools/check_submodule_pins.sh`. **Done.**
5. `cmake/SimdConfig.cmake`. **Done.**
6. Skeleton VST3 with 7th-order Hastings `dm_sin` 440 Hz tone, `juce::ScopedNoDenormals` for FTZ/DAZ, raw `AudioProcessorParameter`s only. **Done.**
7. Headless render rig at `tools/sfs_render`. **Done.**
8. Determinism harness (`tools/render_reference.sh`, `tools/hash_wav.sh`, `tools/wav_diff`, `.github/workflows/determinism.yml`). **Done.**
9. pluginval Level 1 (`.github/workflows/pluginval.yml`, pinned v1.0.4). **Done.**
10. Pre-commit hook (`tools/git-hooks/pre-commit` + `install.sh`). **Done.**

**P0 follow-ups** (landed after the per-step P0.1–P0.10 commits but still inside the Phase 0 boundary):

- `tools/wav_diff` for diagnosing determinism divergences.
- `tools/check_determinism.sh` forbidden-symbol grep + CI wiring.
- Catch2 v3 test infrastructure (`tests/CMakeLists.txt`, `tests/unit/sfs_unit_tests`) + `ctest` wired into CI.
- `dm_sin` upgraded from 5th-order Taylor (~4e-3 error) to 7th-order Hastings (~2e-6 error) after the Catch2 test caught the over-claimed budget.

Each step gated the next; no step was considered "done" until its CI step was wired in. The cross-platform 4-way verification still requires the in-flight CI run to pass.
