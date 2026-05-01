# 08 — Implementation roadmap and tests

This document specifies the build plan from "empty repository" to "v1.0 release candidate." It defines the phases, the milestones that gate them, the test strategy, the CI/CD plan, the third-party dependencies, and the licensing posture.

---

## 1. Phase plan

The build is decomposed into **five phases**, each ending with a demonstrable internal milestone. Phases are roughly time-ordered but several have parallel work; the dependency graph is at the end of this section.

### Phase 0 — Foundations (week 0–2)

Establish repository, build system, third-party dependencies, CI pipeline, code style, and the headless test rig. No DSP code beyond a "hello world" plug-in that loads in REAPER and plays a sine wave.

Deliverables:

* Cross-platform CMake build system (macOS x64+arm64, Windows x64, Linux x64).
* JUCE 8 integration as a submodule.
* GitHub Actions CI: build on push, run unit tests, lint, format check.
* Headless test rig that loads a `.sfs` preset, sends a fixed MIDI sequence, and renders to WAV (used by every later phase).
* Code style: `clang-format` preset committed; pre-commit hook.
* Skeleton VST3 wrapper passing pluginval Level 1.

Gate to Phase 1: a CI green pass on all three platforms with the skeleton plug-in.

### Phase 1 — 1D substrate engine (week 2–6)

Implement the 1D substrate, agent pool, harvester bank, and one-voice mono playback. No GUI beyond a parameter list; no presets beyond hard-coded defaults.

Deliverables:

* 1D substrate (02 §2) with linear deposit kernel and linear read kernel.
* Agent pool (03) with sine waveform only.
* Harvester bank (04) with single-channel mono output.
* Counter-based RNG (06 §1) seeded per voice, per agent.
* Test vectors for substrate (02 §11) and agents (03 §10) all passing.
* CPU profile of the inner loop. Optimization to SSE2 on x86_64, NEON on ARM64.

Gate to Phase 2: a 30-second sustained note plays a recognisable evolving timbre. Substrate stability under all macro positions verified. Bit-exact reproducibility test passes on macOS x64, macOS arm64, Windows x64, Linux x64.

### Phase 2 — Polyphony, stereo, full waveforms (week 6–12)

Expand the engine to multi-voice, multi-channel, multi-waveform agents. Build the macro panel and modulation matrix.

Deliverables:

* `MAX_VOICES = 8` polyphony with voice manager (01 §6).
* Stereo and quad output via two/four harvesters per voice (04 §3).
* All five agent waveforms (03 §3) bandlimited.
* Six macros with fan-out (05 §2) and per-parameter smoothing (05 §4).
* Modulation matrix evaluation (05 §5) with 16 slots and 12 sources.
* MIDI ingest with note-on/off, pitch bend, CC1.
* Sample-accurate parameter automation (05 §7).

Gate to Phase 3: a polyphonic 6-note chord plays cleanly with no clicks, voice stealing works, all four sonic-corner contract tests (00 §"contract") pass. CPU at < 30% on a baseline mid-range CPU (Apple M2, Intel i7-12700) for 8 voices.

### Phase 3 — 2D substrate, multichannel, MPE (week 12–16)

Expand to 2D substrate, surround channel layouts, MPE.

Deliverables:

* 2D substrate (02 §3) with bilinear deposit and read.
* Substrate downsampling (`S = 2`) for the 2D mode.
* 5.1 and 7.1.4 multichannel layouts (04 §3).
* First-order ambisonic output (04 §3.6).
* MPE Note Expression integration (05 §6).
* TOPOLOGY and ASPECT structural selectors (02 §4).

Gate to Phase 4: 2D mode produces audibly distinct timbres from 1D mode. 5.1 / 7.1.4 panning sweeps work. MPE controllers (LinnStrument or Roli) drive per-note expression correctly.

### Phase 4 — GUI, preset system, factory presets (week 14–22, parallel with Phase 3)

Build the GUI per 07. Implement preset format, browser, packs, and the 128-preset factory palette.

Deliverables:

* GUI per 07: header, substrate visualiser (1D and 2D modes), macro panel, modulation matrix, envelope/LFO editors, footer.
* Preset format per 06 §3, with load/save and migration scaffolding.
* Preset browser with search, filter, and pack support.
* 128 factory presets with audio hashes verified.
* In-app manual (07 §10).

Gate to Phase 5: GUI passes accessibility check (basic keyboard navigation, no information conveyed by colour alone). All 128 factory presets render bit-identically across platforms. Preset load times < 50 ms cold.

### Phase 5 — Stabilisation and release (week 22–26)

Bugs, polish, beta testing, marketing assets.

Deliverables:

* External beta with 30+ users (sound designers, DSP-aware producers, ambient artists).
* Beta feedback triaged into v1.0 fixes vs. v1.1 backlog.
* Localisation: English at v1.0; Japanese, Korean, Spanish, German for v1.1.
* Marketing page with 4 audio demos (one per sonic corner) and 1 video demo.
* Documentation site (the v1.0 spec, lightly rewritten for end users).
* Release candidate signed and notarised on macOS; signed on Windows.

Gate to release: zero P0/P1 bugs from beta, < 5 P2 bugs, audio hash test passes, pluginval clean.

### Dependency graph

```
Phase 0 ──► Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 5
                                ╲
                                 ╲──► Phase 4 ─────►
```

Phase 4 (GUI) starts in parallel with Phase 3 once Phase 2 is stable. The two converge into Phase 5.

## 2. Test strategy

### 2.1 Unit tests

Every public function in the engine has unit tests. Coverage target: 85% line coverage on the engine module. Examples:

* `Substrate1D::process()` with known inputs reproduces test vectors (02 §11).
* `AgentPool` migration with seeded RNG produces deterministic positions (03 §10).
* `ModulationMatrix::evaluate()` correctly sums multiple slots targeting one destination.
* `Philox4x32::next32()` matches the published reference vectors.
* `softSat()` is identity for `|x| < 0.7` and correctly saturates at `|x| > 1.0`.

Framework: [Catch2](https://github.com/catchorg/Catch2) v3.

### 2.2 Integration tests

End-to-end render tests using the headless test rig. Each test:

1. Loads a fixed preset.
2. Sends a fixed MIDI sequence.
3. Renders to WAV at fixed sample rate and block size.
4. Compares against a stored reference WAV (bit-exact for v1.0; allows ±1 LSB tolerance for v1.1+).

The factory preset palette is the integration test corpus: 128 presets × multiple sample rates × multiple block sizes = ~2000 WAV comparisons per CI run.

### 2.3 Sonic-corner contract tests

For each of the four sonic-character corners, an automated listening test (00 §"contract"):

* **Drone**: render a 60-second sustain. Compute spectral centroid every 100 ms. Variance must be < 5% of mean.
* **Organic**: render a 60-second sustain. Compute spectral centroid; band-pass filter at 0.1–0.5 Hz; the filtered signal's RMS must exceed a threshold (i.e., real motion exists in this band).
* **Pitched**: render a chromatic scale C2–C7 at velocity 100. Run YIN (or pYIN) pitch tracking. Voicing confidence must be ≥ 0.95 at all notes.
* **Glitch**: render a 60-second sustain. Run an onset detection (e.g., spectral flux) and count onsets. Must have ≥ 60 onsets in 60 s. RMS must stay within −12 dBFS bound.

Test runs on every CI pass for the four "canonical" presets (one per corner). Failure blocks the build.

### 2.4 Stability tests

A fuzz test sweeps every macro at every position with random modulation matrices for 60 seconds, with the infinity guard (02 §6) instrumented. Any infinity-guard fire is a P0 bug.

Runs nightly, not per-commit.

### 2.5 CPU benchmarks

A benchmark suite measures CPU usage at fixed configurations:

| Config | Target on Apple M2 | Target on Intel i5-12600 |
|---|---|---|
| 1 voice, 1D, 1024 cells, 32 agents, default macros | < 4% one core | < 6% one core |
| 8 voices, 1D, 1024 cells, 32 agents, default macros | < 30% one core | < 50% one core |
| 1 voice, 2D, 64×64, 32 agents, default macros | < 8% one core | < 12% one core |
| 8 voices, 2D, 64×64, 32 agents, default macros | < 60% one core | < 100% one core (i.e., needs S=2) |

Regression alerts trigger if any benchmark exceeds its target by 10%.

### 2.6 Pluginval

[pluginval](https://github.com/Tracktion/pluginval) at strictness Level 10 must pass on every release candidate. This catches a wide class of host-compatibility bugs (parameter range violations, threading issues, malformed VST3 metadata).

### 2.7 Listening tests (manual)

Two formal listening sessions per phase:

* **Internal**: 4 listeners (sound designer + 3 engineers), structured comparison against reference plug-ins (e.g., Madrona Aalto/Kaivo, Mutable Rings, U-He Bazille) for the same musical task.
* **External beta**: 30+ users in Phase 5, structured feedback survey.

Listening tests do **not** gate CI but inform release decisions.

## 3. CI/CD

GitHub Actions matrix:

* `macos-13` (Intel) and `macos-14` (Apple Silicon)
* `windows-2022`
* `ubuntu-22.04`

Per push:

* Build all platforms.
* Run unit tests.
* Run integration tests at 48 kHz, 44.1 kHz; block sizes 64, 256, 1024.
* Run pluginval Level 5 (Level 10 only on release candidate tags).
* Lint check (`clang-format --dry-run --Werror`).

Per nightly:

* Run stability fuzz tests for 30 minutes.
* Run full integration test matrix (all sample rates × all block sizes × all factory presets).

Per release tag:

* Build signed binaries (macOS notarised; Windows code-signed).
* Run pluginval Level 10.
* Generate audio demos (one per factory preset for the marketing site).
* Publish artifacts to a private staging bucket; manual promotion to public.

## 4. Third-party dependencies

| Dependency | Purpose | License | Notes |
|---|---|---|---|
| [JUCE 8](https://juce.com/) | Plug-in framework, GUI | GPL/Commercial | Need commercial license at release |
| [Catch2 v3](https://github.com/catchorg/Catch2) | Unit testing | BSL-1.0 | OK for distribution |
| [Random123](https://github.com/DEShawResearch/random123) | Philox RNG | BSD-3 | OK for distribution |
| [SIMDe](https://github.com/simd-everywhere/simde) | SIMD abstraction | MIT | OK |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON for presets | MIT | OK |
| [pluginval](https://github.com/Tracktion/pluginval) | Plug-in validator | GPL | Build-time only |
| [REAPER](https://www.reaper.fm/) | Test host | Commercial | Dev license per engineer |

No GPL-licensed code ships in the binary except JUCE under its commercial license. All other dependencies are permissive.

## 5. Framework choice: JUCE 8 vs. iPlug2

We choose JUCE 8. Reasons:

* JUCE 8 has a mature, well-documented GUI framework with hardware-accelerated OpenGL backends — necessary for the 2D heatmap visualiser at 60 Hz.
* Larger ecosystem of design-time tools (e.g., [foleys_gui_magic](https://github.com/ffAudio/foleys_gui_magic) for visual layout).
* Better support for sample-accurate VST3 parameter automation than is widely advertised; we can extend JUCE's `AudioProcessorParameter` system with custom listener pathways.
* Pluginval is JUCE-aware.

iPlug2 advantages we forgo: smaller binaries (~1 MB vs. JUCE's ~5 MB), permissive license. We accept the binary size and pay for the JUCE commercial license.

We avoid JUCE's `AudioProcessorValueTreeState` parameter abstraction at the audio-thread layer — its locking model is incompatible with our determinism requirements. Instead, we use raw `AudioProcessorParameter` objects and route through our own SPSC queue (01 §4.4).

## 6. Build configuration

```
cmake_minimum_required(VERSION 3.22)
project(SFS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX /fp:precise /arch:AVX2)
else()
    add_compile_options(-Wall -Wextra -Werror -fno-fast-math -mavx2)
endif()

# Subprojects:
add_subdirectory(third_party/JUCE)
add_subdirectory(third_party/Catch2)
add_subdirectory(third_party/SIMDe)
add_subdirectory(third_party/Random123)

add_subdirectory(engine)        # the DSP engine
add_subdirectory(plugin)        # the VST3 wrapper
add_subdirectory(tests)         # unit + integration tests
```

`-fno-fast-math` is mandatory for determinism. `-mavx2` is the default; runtime SSE2/NEON fallback paths are dispatched in code, but the compile target requires AVX2 for the desktop binary.

A separate "ECO" build with `-mavx -mno-avx2` is offered for older CPUs.

## 7. Licensing

* **SFS engine and plugin**: proprietary, sold through online distribution.
* **The reference test vectors** (JSON files): public domain (CC0). This lets third-party implementations validate compatibility.
* **The .sfs and .sfsp formats**: openly documented (this spec). Anyone can write a reader.
* **The factory preset audio under default settings**: bundled with the plug-in license. Users may use them in commercial productions without further license obligations.

## 8. Beta program

Beta opens at end of Phase 4. Recruitment:

* 30 testers across roles: 10 ambient/experimental artists, 10 sound designers (game audio, film), 10 DSP-aware producers.
* Mix of platforms: 15 macOS, 12 Windows, 3 Linux.
* Mix of DAWs: Live, Logic, REAPER, Bitwig, Cubase, FL Studio, Studio One, Reason.

Beta builds expire 30 days after issue. Feedback collected via:

* Structured form per session.
* Open Discord channel.
* Bi-weekly beta call.

Triage:

* P0: crashes, hangs, audio dropouts, host incompatibility — fix in next beta.
* P1: wrong output, missing macros, GUI bugs that block use — fix before v1.0.
* P2: nice-to-have, polish — schedule for v1.0 or v1.1 based on capacity.

## 9. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Bit-exact reproducibility fails across platforms | Medium | High | Compile with `-fno-fast-math`; ban platform-specific intrinsics; use polynomial approximations; CI matrix tests on every push |
| 2D substrate too CPU-heavy on user machines | High | Medium | Ship `S = 2` as default for 2D; add `S = 4` "ECO" mode; document per-voice CPU profile |
| Macro fan-out feels arbitrary to sound designers | Medium | High | Beta listening sessions specifically test macro feel; iterate per beta feedback; ship multiple "macro profiles" if needed |
| GUI doesn't communicate substrate well | High | High | Prototype the visualiser early in Phase 1 (not Phase 4) and test informally with sound designers |
| Patent search reveals collision | Low | High | Run formal patent search before public announcement; have fallback narrative ("novel implementation of well-understood class") |
| Voice stealing produces audible artifacts | Medium | Medium | Hard-coded 5 ms ramp; tested in CI; user-facing setting for "graceful steal" |

## 10. Milestones with calendar dates (placeholder)

| Milestone | Target |
|---|---|
| Phase 0 complete | Week 2 |
| Phase 1 complete | Week 6 |
| Phase 2 complete | Week 12 |
| Phase 3 complete | Week 16 |
| Phase 4 complete | Week 22 |
| Beta open | Week 22 |
| RC1 | Week 25 |
| v1.0 release | Week 27 |

Total: ~6 months from kickoff. This assumes 1 senior DSP engineer + 0.5 GUI/product engineer, full-time. Half that staffing roughly doubles the timeline; double staffing reduces it modestly (Phase 1–3 is mostly serial).

## 11. Open questions

* **Cloud preset sync.** A subscription product around preset-sharing is plausible but out of scope for v1.0.
* **MPE+ / MIDI 2.0 native.** v1.0 supports MPE via VST3 Note Expression. Native MIDI 2.0 is reserved for v1.1.
* **Plugin formats beyond VST3.** AU and AAX are reserved for v1.3.
* **Hardware port.** The engine is portable C++ with no host-specific dependencies. A hardware product (Eurorack, standalone) is conceivable post-v1.x.
