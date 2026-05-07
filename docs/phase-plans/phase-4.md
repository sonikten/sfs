# Phase 4 — GUI, preset system, factory palette

Status: not started. Phase 4 code may not land until this file is
committed on `main` and `phase-3-complete` is tagged. Both conditions
met as of `0499707` / `phase-3-complete`.

Phase 3 made SFS expressive (MPE), spatial (2D + multichannel + FOA),
and threaded. Phase 4 makes it shippable: a curated GUI, a preset
file format with browser, and 128 factory presets that demonstrate
the engine's range. The engine surface is frozen by Phase 4 start;
this phase is mostly content + UX.

Master meta-plan: `~/.claude/plans/the-research-phase-for-mutable-wolf.md`.
Spec authority:
- `sfs-spec/06_rng_presets.md` §3-§5 (preset format, packs, browser)
- `sfs-spec/06_rng_presets.md` §4 + `sfs-spec/09 §4` (128-preset palette)
- `sfs-spec/07_gui_visualization.md` (full GUI spec)
- `sfs-spec/04_harvester_output.md` §4 (LFE LR-2 filter — Phase 4 close-out)
- `sfs-spec/09_glossary_and_parameters.md` (canonical inventory).

## 1. Scope freeze

In scope (from `sfs-spec/08_implementation_roadmap.md` §1, Phase 4
deliverables, plus carry-over Phase 3 items):

**GUI (Doc 07):**
- Curated knob/slider layout for the macro panel (replaces the Phase
  2/3 `GenericAudioProcessorEditor` knob block).
- Substrate visualiser with agent dots + harvester markers overlaid
  on both 1D scope and 2D heatmap (Phase 3's `SubstrateView` shipped
  the field rendering only).
- Modulation matrix editor: 16 slots, source/destination dropdowns,
  depth slider, curve picker, active toggle.
- Envelope editor (ENV1, ENV2): visual ADSR points with drag.
- LFO editor (LFO1-4): rate, depth, shape, sync, phase reset.
- Header: logo, preset name + chooser, prev/next arrows, dice
  randomiser, settings, help.
- Footer: CPU %, active voices, latency, init/save/load buttons.
- Advanced parameter panel (Doc 07 §8a): low-level engine fields
  (ring-queue stats, allocations counter, substrate cell count,
  oversample mode), gated behind a "Show advanced" toggle.
- Preferences pane (Doc 07 §9).
- Help / in-app manual (Doc 07 §10).
- Resizability `[800, 1600] × [600, 1200]` per Doc 07 §2.

**Preset system (Doc 06 §3):**
- `.sfs` JSON file format with `_magic = "SFS-1.00"` first key.
- Round-trip serialiser/deserialiser covering all host-exposed +
  reserved persisted fields per 09 §3.1-3.10.
- `_audio_hash` field — SHA-256 of a 30 s C4 offline render.
- Atomic save (write to `.tmp`, fsync, rename).
- `format_version` migration scaffolding (v1.0 only at v1.0; v1.x
  forward-compat drop-unknown-keys + warn).
- Preset browser (Doc 06 §5): name/author/category/tags index built
  off the user's preset directory + installed packs; search +
  filter + sort.
- `.sfsp` preset pack ZIP loader (Doc 06 §3.5) with optional
  `audio_previews/` and `cover.png`.

**Factory palette (Doc 06 §4 + 09 §4):**
- 128 factory presets distributed 32 Drone + 32 Organic + 24
  Pitched + 16 Glitch + 16 Hybrid + 8 Init.
- Each preset has a verified `_audio_hash`.
- Each preset is tagged for the contract test rig
  (`tags: ["contract:drone"]` etc.) so the build asserts each
  category preset still passes its corner gate.
- The 4 named "contract" presets pinned by Doc 09 §4 (`Init Drone`,
  `Init Organic`, `Init Pitched`, `Init Glitch`) are wired into
  the existing contract harness in addition to the hardcoded 1D/2D
  test cases.

**Phase 3 inherited close-outs:**
- LFE filter for 5.1 + 7.1.4: 1st-order LPF → 2nd-order
  Linkwitz-Riley at 120 Hz (`sfs-spec/04 §3.4`).
- Voice-pool threading dispatch extended to 5.1 / 7.1.4 / FOA
  render paths (Phase 3 shipped only `renderBlockStereo` through
  the pool).
- `sfs_profile` gains `--topology {ring1d|torus2d}` and
  `--threading N` flags so the CPU journal can cover the full
  matrix.
- Linux + Windows CPU profile harvested via CI artefact upload;
  `docs/journals/phase-4-cpu-profile.md` records the cross-platform
  multi-voice reality.

Out of scope this phase:
- Localisation (English only at v1.0) → Phase 5.
- AU / AAX wrappers → Phase 5+.
- Notarisation + signing → Phase 5.
- External audio excitation → v2.0.
- Möbius / Klein topologies, higher-order ambisonics → deferred.
- User preset cloud sync → reserved for a future service (Doc 06 §7).

## 2. Doc-09 update list

Sections this phase will touch in `sfs-spec/09_glossary_and_parameters.md`:

- §3.6 Modulation matrix — `mod{i}.curve` enum verified against
  what the editor lets the user pick (Doc 07 §6.2 lists 5 curves,
  09 currently lists 5 — confirm match before shipping).
- §3.7 Harvester — `orbit_depth`, `orbit_speed_hz`, `orbit_shape`
  become audible (Phase 3 was no-op default 0). Range / default
  re-confirmed against the actual implementation.
- §3.8 Output — `output.master_gain_db`, `output.pan`,
  `output.limiter_active` get their actual ranges + GUI bindings.
- §4 Factory palette — preset names finalised. Phase 4 sound design
  may rename presets relative to the Doc 09 §4 placeholders; the
  spec is updated to match the build at gate time, not the other
  way round.
- §3.10 Internal engine fields — `engine.queue.dropped` exposed
  for the Advanced panel readout.

Doc-09 sweep at gate time: re-verify every persisted field
(09 §3.1-3.10 + reserved) round-trips through the preset format.

## 3. Contract-test gate matrix

| Phase | drone | organic | pitched | glitch |
|---|---|---|---|---|
| 4 | ENABLED ×N — every Drone preset's contract test must pass. Likewise for the other 3 corners. | same | same | same |

Mechanism (per meta-plan §3, Phase 4 row): the contract test rig
loads each `.sfs` whose `tags` contain `contract:<corner>` and
asserts the corner's threshold. A failing own-claimed contract
blocks the build.

The Phase 3 hardcoded 1D + 2D contract presets (currently inline
in `tests/contract/*_test.cpp`) stay green as a regression hedge;
Phase 4 adds the preset-driven loop on top.

`vm_fuzz` extends with:
- Preset round-trip: save → load → re-render → assert PCM equality
  to the original render (catch loader regressions).
- Pack load: extract `.sfsp`, enumerate presets, render each, no
  crash + bounded peak.

## 4. Test additions

New unit-test files:
- `tests/unit/preset_format_test.cpp` — JSON schema validation,
  magic check, version negotiation, round-trip every persisted
  field, atomic save behaviour, malformed-input refusal.
- `tests/unit/preset_browser_test.cpp` — index build over a
  fixture tree, search, filter, tag aggregation, sort.
- `tests/unit/preset_pack_test.cpp` — `.sfsp` ZIP enumeration,
  pack metadata validation, handler for missing audio previews.
- `tests/unit/audio_hash_test.cpp` — 30 s C4 render harness emits
  reproducible SHA-256 per (preset, sample rate, block size).
- `tests/unit/lfe_lr2_test.cpp` — 2nd-order Linkwitz-Riley impulse
  response peak at DC, -6 dB at 120 Hz, > -24 dB at 240 Hz.
- `tests/unit/threading_multichannel_test.cpp` — parallel render
  is bit-exact with serial for 5.1 / 7.1.4 / FOA paths.

New integration assets:
- `presets/factory/` — 128 `.sfs` files authored across the phase.
- `presets/factory/pack.json` for the bundled "Factory Pack" pack.
- `tests/refs/preset_hashes/` — SHA-256 manifest per preset, used
  by the `presets-render` CI job.

## 5. Risk register delta

| Risk | Severity | Mitigation |
|---|---|---|
| Curated GUI is a much larger UX/UI scope than Phase 1-3 GUI scaffolding | High / High | Build the macro panel as the first GUI commit and pause to listen + dogfood before the modulation matrix editor lands. Each panel is a focused commit. |
| Preset format ergonomics drift from spec under engineer pressure | Medium / High | Doc 06 §3 is the single source of truth; loader rejects unknown `_magic`. Preset format test ships before the first `.sfs` file is written. |
| Author 128 musical presets in the Phase 4 timebox (8-week parallel-with-Phase-3 estimate, but solo dev with Phase 3 already done means full 6-8 weeks) | High / High | Init 8 + contract 4 first (= 12 musical presets that are also test fixtures); then iterate by category. Macro-feel journal documents drift across batches. |
| Audio hash for 128 presets across 3 platforms is a long CI run | Medium / Medium | Render in parallel matrix jobs; cache submodule build between presets-render and the existing determinism job. |
| `juce::AudioProcessorValueTreeState` ban (CLAUDE.md) makes binding GUI knobs to params noisier | Medium / Low | Phase 2/3 already use raw `AudioProcessorParameter` + the SPSC queue model; Phase 4 just wraps that with `juce::Slider::onValueChange` callbacks. No new pattern. |
| 2nd-order LR LFE filter introduces NaN if cutoff/sample-rate ratio gets unusual | Low / Low | Clamp coefficients defensively; unit test with extreme sample rates {44.1, 48, 88.2, 96, 192} kHz. |
| Preset browser indexer is on the audio thread by accident | Medium / Low | Indexer runs on a `std::jthread` worker (already the pattern from VoicePool); GUI shows placeholder until indexing completes. |

## 6. Dogfooding script

Concrete REAPER session for Phase 4 gate:

1. Open Live → fresh project → load SFS on a stereo track.
2. Click through every category in the preset browser; pick 3
   presets per category at random; play sustained chords for 30 s.
3. Tweak the macro panel knobs without losing place visually
   (smoothness, value readout legibility).
4. Open the modulation matrix; route LFO3 → MIGRATION at depth
   0.5 on an Organic preset; verify the audible motion matches
   the visualiser.
5. Save preset as user preset; re-load; verify identical render
   (sniff test).
6. Drag-drop a `.sfsp` pack into the GUI; verify it shows in the
   browser; load 2 presets from it.
7. Switch session sample rate from 48 kHz to 96 kHz; verify all
   presets still sound right (no aliasing artefacts; same
   character).
8. Open Advanced panel; verify CPU%, voice count, queue stats
   update live.
9. CPU meter: 8-voice 2D-32 with 4 worker threads under 30% on a
   typical 4-core host.
10. Run pluginval Level 5 on the final build.

## 7. Definition of done

Phase 4 gate is met when:

- `phase-4-gate` annotated tag exists on `main`.
- All 128 factory presets exist in `presets/factory/`, each with a
  verified `_audio_hash` and at least one `tags:contract:*` entry
  for its category.
- `presets-render` CI job renders all 128 presets on every push
  and asserts cross-platform PCM hash match.
- All 4 contract corners pass against AT LEAST 4 distinct factory
  presets per corner (16 contract gates total, on top of the
  Phase 3 hardcoded 1D + 2D test cases).
- Preset round-trip test: save → reload → re-render produces
  bit-exact identical PCM to the original render.
- Preset load < 50 ms cold (Doc 06 §6).
- pluginval Level 5 still green.
- GUI passes a basic accessibility check (keyboard navigation
  reaches every control; no information conveyed by colour alone;
  text contrast ≥ 4.5:1).
- LFE filter is 2nd-order Linkwitz-Riley (regression test in
  `lfe_lr2_test.cpp`).
- Voice-pool threading dispatches all four render paths (stereo /
  FOA / 5.1 / 7.1.4) — `threading_multichannel_test.cpp`.
- `docs/journals/phase-4-cpu-profile.md` recorded for macOS arm64,
  Linux x86_64, Windows x86_64, with multi-voice + threading data.
- `docs/journals/phase-4-macro-feel.md` recorded.
- `docs/journals/phase-4-gui-walkthrough.md` recorded — screen
  captures + commentary of every GUI panel from a fresh launch
  through saving a user preset.
- Doc-09 amendments propagated to dependent docs.

## 8. CI delta

New CI gates that turn on at phase end:

- `presets-render` job (matrix per platform): build sfs_render +
  walk `presets/factory/`, render each `.sfs` for 30 s, hash PCM,
  upload artefact.
- `compare` job extends to compare 128-preset hashes across
  platforms (currently only the 22-preset hardcoded corpus is
  cross-checked).
- `contract-presets` job: run the 4 contract binaries with each
  category's authored preset (replaces Phase 3's hardcoded inline
  test cases as the primary gate; the inline ones stay as
  regression hedges).
- pluginval Level 5 against the GUI-enabled VST3 (Phase 3 was
  diagnostic-GUI; Phase 4 ships the curated GUI).
- `preset-roundtrip` job: load every `.sfs`, save to a temp dir,
  reload, render, assert hash unchanged.

## 9. Bring-up order

Each step lands as a discrete commit (or small commit cluster).

**Track A — preset format + content (lower risk, can run in parallel
with Track B):**
1. **Preset JSON schema + load/save** — `.sfs` format with magic
   header, all persisted fields per 09 §3, atomic save.
2. **Audio-hash render harness** — offline 30 s C4 render →
   SHA-256, exposed via `tools/sfs_preset_hash`.
3. **Preset format unit tests** — round-trip every persisted field,
   reject malformed, version negotiation.
4. **Init presets (8)** — first batch authored, hashed, contract-
   tagged. Forms the in-tree ground truth for everything else.
5. **Contract-driven test rig** — load `.sfs` whose `tags` claim
   `contract:<corner>` and run the corner's threshold check.
6. **Drone presets (32)** — sound design batch.
7. **Organic presets (32)**.
8. **Pitched presets (24)**.
9. **Glitch presets (16)**.
10. **Hybrid presets (16)**.
11. **`.sfsp` pack loader** — ZIP enumeration, pack metadata.
12. **Factory pack bundle** — 128 presets shipped as one
    `Factory.sfsp` in the build artefact.

**Track B — GUI (higher risk, dogfood at every panel):**
13. **Macro panel** — replaces the GenericAudioProcessorEditor
    knob block with a curated 6-knob + 2-selector layout per
    Doc 07 §5.
14. **Substrate visualiser overlay** — extend `SubstrateView`
    with agent dots + harvester markers per Doc 07 §4.
15. **Footer** — CPU %, voice count, latency, init/save/load.
16. **Header** — logo, preset name display, prev/next, dice.
17. **Modulation matrix editor** — list view, source/dest pickers,
    depth, curve, active toggle.
18. **Envelope editor** — visual ADSR drag for ENV1, ENV2.
19. **LFO editor** — rate, depth, shape, sync, phase reset.
20. **Preset browser** — search, filter, sort, drag-drop install.
21. **Advanced panel** — Doc 07 §8a engine instrumentation.
22. **Preferences pane**.
23. **Help / in-app manual**.
24. **Window resizability + responsive grid** — collapse modulation
    matrix into a tab below 1024×640.

**Track C — Phase 3 inherited close-outs (one focused commit each):**
25. **LFE → 2nd-order Linkwitz-Riley** for 5.1 + 7.1.4.
26. **Voice-pool threading** for FOA / 5.1 / 7.1.4 render paths.
27. **`sfs_profile --topology` + `--threading` flags** — extend the
    existing profiler.
28. **Linux + Windows CPU profile harvesting** — CI artefact upload
    + journal commit.

**Gate work:**
29. **Doc-09 sweep + propagation** — match spec to shipped reality.
30. **GUI accessibility audit** — keyboard nav, contrast, no-colour-
    only; record findings in `docs/journals/phase-4-gui-walkthrough.md`.
31. **`phase-4-gate` annotated tag** with evidence summary.

Tracks A, B, C are independent; expect interleaving. The plan-file
will be revised with progress notes if the order shifts (the
meta-plan permits track reshuffling, not deliverable substitution).

## 10. Open questions (to resolve before each step kicks off)

- **Preset directory location.** macOS: `~/Music/SFS Presets/`?
  Windows: `%USERPROFILE%\Documents\SFS Presets\`? Linux:
  `$XDG_DATA_HOME/sfs/presets/`? Resolve in step 1; document in
  Doc 06 once decided.
- **Audio hash sample rate.** Doc 06 says 30 s C4 — at 48 kHz?
  Or include 44.1 / 48 / 96 in the hash manifest? Decide step 2.
  Single canonical rate (48 kHz) is simpler; multi-rate is more
  defensive.
- **Curated knob art.** SFS-Component-style vector knobs from
  scratch, or a permissive open-source library (foleys, Gin)?
  Decide step 13 after a layout sketch.
- **Modulation curve picker UX.** A dropdown with 5 named curves,
  or a draggable curve display? Decide step 17 after dogfooding
  the 5-named-curves form.
- **Preset randomiser.** Doc 06 §7 calls for "random preset within
  musical bounds" — what does "musical bounds" mean concretely?
  Curated parameter ranges per category? Decide step 16 after the
  first 32 Drone presets are authored.
