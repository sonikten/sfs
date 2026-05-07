# Phase 4 — Gate evidence

Records the evidence that Phase 4's Definition of Done (per
`docs/phase-plans/phase-4.md` §7 + `sfs-spec/08_implementation_roadmap.md` §1)
has been met. Used as the body of the `phase-4-gate` annotated tag.

## Engine deliverables

Phase 4 inherited two close-out items from Phase 3 (per
`docs/phase-plans/phase-3-gate-evidence.md`):

| Step | Deliverable | Status | Evidence |
|---|---|---|---|
| C25 | LFE filter 1st-order LPF → 2nd-order Linkwitz-Riley | ✅ | `Voice::lfeLpfStage1_/lfeLpfStage2_` cascade; `tests/unit/lfe_lr2_test.cpp` (-6 dB at 120 Hz, < -13 dB at 240 Hz, < -25 dB at 1 kHz) |
| C26 | Voice-pool threading for FOA / 5.1 / 7.1.4 | ✅ | All four render paths dispatch through `VoicePool` when `enableThreading(N)` is on; bit-exact regression test for each |

## Preset system (Doc 06 §3)

| Step | Deliverable | Status | Evidence |
|---|---|---|---|
| A1 | `.sfs` JSON format | ✅ | `src/preset/preset.{h,cpp}` mirrors Doc 06 §3.2; round-trip every persisted field; magic check; format-version negotiation; atomic save (.tmp + rename) |
| A2 | Preset → engine binding | ✅ | `src/preset/preset_engine.{h,cpp}` `applyToEngine`; bit-exact regression test confirms preset-driven render matches direct-setter render sample-by-sample |
| A3 | Audio-hash render harness | ✅ | `tools/sfs_preset_render` (JUCE-free) + `tools/sfs_preset_hash.sh` wrapper; 30 s C4 SHA-256 → `_audio_hash` field |
| A4 | Contract-driven test rig | ✅ | `tests/contract/preset_contract_test.cpp` walks `presets/{contract,factory}/*.sfs`; dispatches by `contract:<corner>` tag |
| B14a | AudioProcessor preset I/O | ✅ | `SfsAudioProcessor::loadPresetFromFile` + `savePresetToFile`; writes into host AudioParameters; processBlock fan-out picks them up |

## Factory palette (Doc 06 §4 + 09 §4)

| Category | Spec count | Shipped | Source |
|---|---|---|---|
| Init | 8 | 8 | Hand-authored (P4.A5), all corner-tagged |
| Drone | 32 | 32 | 5 named (Slow Glacier, Mountain Wind, Tape Hiss Choir, Cathedral Reverb, Lighthouse) + 27 generated fillers |
| Organic | 32 | 32 | 4 named (Forest Breath, Coral, Tidepool, Skin) + 28 fillers |
| Pitched | 24 | 24 | 4 named (Bell Choir, Soft Lead, Glass Pad, Sub) + 20 fillers |
| Glitch | 16 | 16 | 4 named (Static, Bones, Cassette, Fragment) + 12 fillers |
| Hybrid | 16 | 16 | 5 named (Day to Night, Stage Whisper, Erosion, Tide & Stone, Skin Drone) + 11 fillers |
| **Total** | **128** | **128** | |

Each preset has a verified `_audio_hash` from the 30 s C4 canonical
render. The fillers are deterministically generated from
`tools/generate_filler_presets.py` (PRNG seed = slot ID) — re-running
the script produces byte-identical output. Phase 5 sound-design pass
will replace fillers with hand-tuned named variations as beta
feedback comes in.

## Sonic-corner contract gate (`sfs-spec/08 §2.3`)

The Phase 3 hardcoded inline contract tests stay green as regression
hedges. The new preset-driven `sfs_contract_presets` rig walks the
factory palette and asserts each tagged preset meets its corner
threshold:

| Corner | Tagged presets | Threshold | Worst observed |
|---|---|---|---|
| Drone | 35 | cv < 0.05 | 0.0366 (Mountain Wind, slow LFO drift) |
| Organic | 30 | stddev > 50 Hz | 78 Hz (typical 80-140 Hz) |
| Pitched | 22 | err < 3% | 0.256% (autocorrelation pitch) |
| Glitch | 14 | onsets ≥ 5 | 6 (boundary fillers) |

The "≥4 distinct presets per corner" gate criterion is satisfied
many times over — at least 14 contract-tagged presets per corner.

## Curated GUI (Doc 07)

| Step | Panel | Status | Notes |
|---|---|---|---|
| B13 | Macro panel | ✅ | 6 rotary knobs (TENSION/DAMPING/DENSITY/MIGRATION/COHERENCE/EXCITATION) + TOPOLOGY + SHAPE selectors. JUCE `SliderParameterAttachment` / `ComboBoxParameterAttachment`, no APVTS. |
| B14 | Substrate visualiser overlay | ✅ | `SubstrateView` paints agent dots (color = waveform) + harvester markers on both the 1D scope and 2D heatmap |
| B15 | Footer | ✅ | Voice count / latency / topology readouts at 8 Hz. CPU% deferred to Phase 5 (needs render-block timing infra). |
| B16 | Header bar | ✅ | Logo + clickable preset name + Load.../Save As... buttons |
| B20 | Preset browser popup | ✅ | Click preset name → categorised popup of all `presets/factory/**/*.sfs`, Init first then alphabetical |
| B17 | Curated mod-matrix editor | ⚠️ deferred | The 4 default mod-matrix slots remain accessible via the `GenericAudioProcessorEditor` "advanced" panel below the macro panel. Full 16-slot configurable editor with source / dest / curve / depth pickers is a Phase 5 polish commit. |
| B18 | Curated envelope editor | ⚠️ deferred | ADSR knobs accessible via the advanced panel. Visual ADSR drag-points are Phase 5 polish. |
| B19 | Curated LFO editor | ⚠️ deferred | LFO rate + shape accessible via the advanced panel. Per-LFO visual editors are Phase 5 polish. |
| B21 | Advanced parameter panel | ✅ partial | The `GenericAudioProcessorEditor` instance fills this role today; the spec's Doc 07 §8a panel with engine-internal readouts (queue stats, alloc counter) is Phase 5. |
| B22 | Preferences pane | ⚠️ deferred | No user-facing preferences in v1.0 ship; per-host settings live in DAW project state. |
| B23 | In-app manual | ⚠️ deferred | The in-tree spec docs serve as documentation for v1.0 ship. |
| B24 | Responsive resize | ✅ partial | Window is resizable in [640×620, 1600×1400]; the responsive collapse-to-tab below 1024×640 is Phase 5. |

## Test coverage

| Job | Phase 3 → Phase 4 | Cases | Assertions |
|---|---|---|---|
| `sfs_unit_tests` | 100 → 121 | +21 | 386k → 518k |
| `sfs_contract_drone` (1D + 2D) | unchanged | 2 | inline |
| `sfs_contract_pitched` (1D + 2D) | unchanged | 2 | 13 + 8 notes |
| `sfs_contract_organic` (1D + 2D) | unchanged | 2 | inline |
| `sfs_contract_glitch` (1D + 2D) | unchanged | 2 | inline |
| `sfs_contract_param_fuzz` | unchanged | 7 | |
| `sfs_contract_vm_fuzz` | unchanged | 14 | |
| `sfs_contract_presets` (Phase 4 new) | new | 5 | 140 |

New unit tests added in Phase 4 cover: preset format round-trip
(7 cases / 157 assertions), preset → engine binding bit-exactness
(3 cases), LFE LR-2 frequency response (4 cases), threading
multichannel determinism (3 new cases — FOA / 5.1 / 7.1.4).

## Hard-invariant CI gates

All Phase 0/1/2/3 invariants carry forward:

| Invariant | Status |
|---|---|
| Bit-exact across platforms (22 hardcoded preset PCM hashes, 1D + 2D) | ✅ green on macos-14 / ubuntu-22.04 / windows-2022 |
| `-fno-fast-math` / `/fp:precise` | ✅ |
| No libm transcendentals in `src/engine/` or `src/dsp/` | ✅ |
| No `std::default_random_engine` etc. | ✅ |
| No `juce::AudioProcessorValueTreeState` | ✅ |
| FTZ/DAZ on audio thread | ✅ |
| No allocations on audio thread | ✅ |
| CFL bound `c² + κ ≤ 0.475` (1D) / `≤ 0.225` (2D) | ✅ |

## Pluginval

Continues at Level 5; supports stereo / 4-channel ambisonic /
quadraphonic / 5.1 / 7.1.4 bus layouts on macos-14, windows-2022,
ubuntu-22.04. The Phase 4 GUI panels (HeaderBar, MacroPanel, Footer,
SubstrateView with overlay) all pass pluginval's editor lifecycle
test — open / resize / close cycles produce no leaks or hangs.

## CPU profile (macOS arm64; `docs/journals/phase-4-cpu-profile.md`)

| Configuration | Wall ms / 1 s | Real-time factor |
|---|---|---|
| 1D 1024 cells / 16 agents | 109 ms | 9.15× |

Phase 3 → Phase 4 delta: ~0 ms. The Phase 4 work (GUI shell, preset
I/O, threading-dispatch wiring, LR-2 LFE filter) is entirely outside
the audio inner loop OR gated behind opt-in flags / channel layouts.

**6.2× under the spec budget.**

## Open Phase 5 items inherited from Phase 4

- Curated mod-matrix / envelope / LFO editors (B17-B19) — the four
  active mod-matrix slots are accessible via the advanced panel
  today; the spec's full 16-slot editor with source / destination
  pickers + draggable ADSR points + per-LFO shape visualisers are
  polish work.
- Advanced parameter panel proper (B21) — engine-internal instrumentation
  surface (queue dropped counters, alloc counter, oversample mode toggle).
- Preferences pane (B22) + In-app manual (B23) — user-facing
  documentation surface.
- Responsive layout collapse (B24) — modulation matrix collapses to
  a tab below 1024×640.
- `.sfsp` pack loader (A11) + factory pack bundle (A12) — third-party
  preset distribution. Factory presets ship as a folder for v1.0.
- `sfs_profile --topology / --threading` flags (C27) + Linux + Windows
  CPU profile harvesting (C28) — cross-platform performance journals.
- GUI accessibility audit — keyboard navigation, contrast verification,
  no-colour-only check.
- Doc 09 sweep — verify every persisted field round-trips through the
  preset format end-to-end.

## Sign-off

Phase 4 gate is met when this commit's CI cycle ships:
- `ci` green on all 3 platforms (macos-14, windows-2022, ubuntu-22.04)
- `determinism` green: cross-platform PCM hashes match for the
  22-preset 1D+2D corpus + the 128 factory PCM hashes
- `pluginval` green at Level 5

Tag at the gate-passing commit: `git tag -a phase-4-gate -F docs/phase-plans/phase-4-gate-evidence.md`.
