# 09 — Glossary and parameter reference

The single source of truth for terminology, mathematical symbols, and the complete v1.0 parameter inventory. All other documents in this specification refer back here. When a definition or value disagrees between this document and another, this document is canonical.

---

## 1. Glossary

| Term | Definition |
|---|---|
| **Agent** | A single audio-rate oscillator within a voice's agent pool. Has its own position, frequency, phase, amplitude, envelope, and waveform shape. Both deposits into and reads from the substrate. |
| **Agent pool** | The collection of agents owned by a voice. Up to 64 agents per voice in v1.0. Stored as a struct of arrays for SIMD friendliness. |
| **Bend** | The frequency modulation applied to an agent by the substrate value at the agent's position, scaled by the agent's `mod_sensitivity`. |
| **Bit-exact reproducibility** | The contract that an SFS preset, given identical MIDI input and host conditions, produces identical audio output across runs and across platforms. |
| **Block rate** | The rate at which the host calls `process()` — typically `fs / blockSize`, where `blockSize` is 64 to 2048 samples. Macros and the modulation matrix evaluate at block rate. |
| **CFL condition** | The Courant–Friedrichs–Lewy stability bound for the discrete wave equation. For SFS's substrate, the bound is `c² + κ ≤ 0.5` (1D) or `c² + κ ≤ 0.25` (2D). |
| **Cohort** | A subset of agents in a voice that share a harmonic relationship to the played MIDI note, governed by the COHERENCE macro. |
| **Coherence** | The macro controlling how harmonically locked agent frequencies are to the played note. Range [0, 1]. |
| **Critical regime** | The region of parameter space where the substrate is on the edge of self-oscillation: `γ` low, `c²` near maximum. Small inputs trigger blooming responses; the engine sounds most "alive." |
| **Damping** | The macro controlling substrate energy loss rate `γ`. Determines the engine's release feel. |
| **Density** | The macro controlling the number of active agents in a voice. |
| **Deposit** | An agent's contribution to the substrate at its current position. Equals `w_i · a_i · e_i · y_i` per sample. |
| **Deposit kernel** | The interpolation kernel used to spread an agent's deposit across substrate cells when the agent's position is non-integer. v1.0 supports `linear` and `lanczos4`. |
| **Determinism contract** | See "Bit-exact reproducibility." |
| **Excitation** | The macro controlling agent modulation sensitivity to the substrate. At zero, the engine is a parallel additive bank; at maximum, agents are bent strongly by the substrate, producing chaotic regimes. |
| **FDTD** | Finite-difference time-domain. The class of discretisation schemes used for the substrate update. |
| **Harvester** | A fixed (or slowly-moving) listening point on the substrate. Each output channel reads from one harvester. Up to 16 harvesters per voice. |
| **Ignition** | The voice-on initialisation sequence: substrate zeroing, ignition burst injection, agent state initialisation, envelope start. Required to seed the substrate with audible content. |
| **Listening point** | Synonym for harvester position. |
| **Migration** | An agent's continuous movement across the substrate, governed by drift rate `r_i` and Gaussian noise. The MIGRATION macro scales both. |
| **Modulation matrix** | The user-configurable layer between modulation sources (LFOs, envelopes, MIDI/MPE inputs) and modulation destinations (macros and selected low-level parameters). v1.0 has 16 slots. |
| **Multiplicative bend** | The choice that an agent's frequency is modulated as `f_inst = f_i + m_i · u_at · f_i` (proportional to base frequency) rather than additively. Preserves harmonic relationships under modulation. |
| **Per-voice instancing** | The architectural decision that each polyphonic voice owns its own substrate, agent pool, and harvester bank. v1.0 default. |
| **Philox-4×32-10** | The counter-based RNG primitive used by SFS for all stochastic decisions. Bit-exact across platforms; key-splittable for parallel streams. |
| **Sonic-character corner** | One of the four target sound regions (drone, organic, pitched, glitch) that the engine must reach via macro positioning. Each has an operational test in document 08. |
| **Stigmergy** | Coordination via traces left in a shared environment. The architectural principle that gives the engine its name and its key novelty. |
| **Stream ID** | A field of the RNG key that selects a particular use of randomness (ignition burst, agent positions, migration noise, etc.) within a voice. |
| **Substrate** | The continuous spatial field that hosts agent deposits and is read by harvesters. 1D ring or 2D toroidal grid. The medium that makes SFS an instrument rather than an oscillator bank. |
| **Tension** | The macro controlling substrate wave-speed `c²`. High tension produces lively standing-wave behaviour. |
| **Topology** | The boundary structure of the substrate. v1.0 ships `ring` (1D) and `torus_*` (2D); `mobius` and `klein` are reserved. |
| **Voice** | A polyphonic playing instance, comprising one substrate, one agent pool, and one harvester bank. v1.0 has up to 8 voices. |

## 2. Mathematical symbol table

| Symbol | Definition | Range | Units | Document |
|---|---|---|---|---|
| `c²` | substrate wave-speed squared | [0.0, 0.49] (1D, user-facing), [0.0, 0.225] (2D, user-facing); jointly clamped with `κ` to `c² + κ ≤ 0.475` (1D) and `c² + κ ≤ 0.225` (2D) | dimensionless | 02 |
| `κ` (kappa) | velocity-diffusion coefficient | [0.0, 0.45] (1D, user-facing soft range), [0.0, 0.20] (2D, user-facing soft range); the **joint clamp** above is the hard limit. When user-facing settings would violate the joint clamp, both `c²` and `κ` are softly compressed proportionally so the sum stays within bounds (02 §2.2). | dimensionless | 02 |
| `γ` (gamma) | substrate per-step loss | [0.0, 0.05] | per sample | 02 |
| `N` | number of substrate cells (1D) | {256, 512, 1024, 2048, 4096} | cells | 02 |
| `(W, H)` | substrate dimensions (2D) | {(64,64), (128,128), (256,256)} | cells | 02 |
| `u[x]` | substrate displacement at cell x | [-100, 100] (guard limit; typical \|u\| < 1) | normalised | 02 |
| `v[x]` | substrate velocity at cell x | unbounded; clamped via stability | per sample | 02 |
| `p_i` | agent i position | [0, N) | cells (continuous) | 03 |
| `f_i` | agent i base frequency | [20, 20000] | Hz | 03 |
| `φ_i` (phi) | agent i phase | [0, 1) | normalised cycles | 03 |
| `a_i` | agent i amplitude | [0, 1] | normalised | 03 |
| `e_i` | agent i envelope value | [0, 1] | normalised | 03 |
| `w_i` | agent i deposit weight | typically 0.05 / sqrt(activeCount) | normalised | 03 |
| `m_i` | agent i modulation sensitivity to substrate | [0, 1] | normalised | 03 |
| `r_i` | agent i drift rate | [-0.001 N, +0.001 N] | cells/sample | 03 |
| `ε_i[n]` (epsilon) | agent i migration noise | mean 0, stddev migrationNoise | cells | 03 |
| `y_i[n]` | agent i waveform output at sample n | [-1, 1] | normalised | 03 |
| `u_inject[x]` | total agent injection at cell x at sample n | bounded | normalised | 02, 03 |
| `K` | DC-block update interval | 256 | samples | 02 |
| `S` | substrate downsample factor | {1, 2, 4} | dimensionless | 01 — determined by lookup over (topology, `engine.oversample_mode`); see 01 §5.2. Not a direct user parameter. |
| `fs` | host audio sample rate | 44100, 48000, 88200, 96000, 176400, 192000 | Hz | 01 |
| `fb` | block rate | `fs / blockSize` | Hz | 01 |
| `α` (alpha) | smoothing one-pole coefficient | [0, 1] | per sample | 05 |
| `τ` (tau) | smoothing time constant | per-parameter (10–200 ms) | seconds | 05 |
| `β` (beta) | substrate cubic nonlinearity (reserved) | typical 0–0.01 | dimensionless | 02 §9 |

## 3. Parameter inventory

The complete list of parameters exposed to the host and stored in presets. Each row gives the parameter ID, type, range, default, host visibility (whether it appears in the host's automation list), and a one-line description.

### 3.1 Macro parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `macro.tension` | float | [0, 1] | 0.5 | yes | Substrate wave-speed |
| `macro.damping` | float | [0, 1] | 0.3 | yes | Substrate energy loss |
| `macro.density` | float | [0, 1] | 0.6 | yes | Agent count and deposit |
| `macro.migration` | float | [0, 1] | 0.2 | yes | Agent movement |
| `macro.coherence` | float | [0, 1] | 0.8 | yes | Agent harmonic locking |
| `macro.excitation` | float | [0, 1] | 0.3 | yes | Substrate→agent coupling |

### 3.2 Structural parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `structural.topology` | enum | {ring, torus_64, torus_128, torus_256} | ring | yes | Substrate type |
| `structural.aspect` | float | [0.5, 2.0] | 1.0 | yes | 2D substrate aspect ratio |
| `structural.substrate_size` | enum | {256, 512, 1024, 2048, 4096} | 1024 | no | 1D cell count |
| `structural.deposit_kernel` | enum | {linear, lanczos4} | linear | no | Agent deposit interpolation |
| `structural.read_kernel` | enum | {linear, hermite4} | linear | no | Harvester read interpolation |

### 3.3 Agent parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `agent.max_active` | int | [1, 64] | 32 | no | Cap for active agents |
| `agent.harmonic_set` | float[] | each in [0.25, 8.0] | [1, 2, 3, 4, 5, 6, 7, 8] | no | Harmonic ratios |
| `agent.shape_distribution.sine` | float | [0, 1] | 0.6 | no | Probability of sine shape |
| `agent.shape_distribution.saw` | float | [0, 1] | 0.0 | no | Probability of saw shape |
| `agent.shape_distribution.square` | float | [0, 1] | 0.0 | no | Probability of square shape |
| `agent.shape_distribution.fmpair` | float | [0, 1] | 0.3 | no | Probability of FM-pair shape |
| `agent.shape_distribution.noise` | float | [0, 1] | 0.1 | no | Probability of noise shape |
| `agent.shape_param.fm_ratio` | float | [0, 1] | 0.5 | no | FM ratio index |
| `agent.shape_param.fm_index` | float | [0, 1] | 0.3 | no | FM modulation index |
| `agent.detune_scale` | float | [0, 1] | 0.0 | no | Static detune amount |

**Shape-distribution normalisation rules** (`agent.shape_distribution.*`):

* All five shape probabilities are clamped individually to `[0, 1]` at preset load.
* If the sum after clamping is greater than `0`, the probabilities are normalised to sum to 1.0.
* If the sum after clamping is exactly `0` (or NaN, infinite, or all out-of-range pre-clamp), the engine falls back to the documented default: `sine = 1.0`, all others `0.0`. A warning is logged in development builds; in release builds the fallback is silent.
* Migration from a v1.0 preset to a future format MUST preserve the post-normalisation distribution, not the raw values, so older presets sound the same in newer builds even if normalisation rules change.

### 3.4 Envelope parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `env1.attack` | float (log) | [0.001, 30.0] | 0.5 | yes | Env1 attack time (s) |
| `env1.decay` | float (log) | [0.001, 30.0] | 1.0 | yes | Env1 decay time (s) |
| `env1.sustain` | float | [0, 1] | 0.7 | yes | Env1 sustain level |
| `env1.release` | float (log) | [0.001, 30.0] | 2.0 | yes | Env1 release time (s) |
| `env2.attack` | float (log) | [0.001, 30.0] | 0.05 | yes | Env2 attack time (s) |
| `env2.decay` | float (log) | [0.001, 30.0] | 0.3 | yes | Env2 decay time (s) |
| `env2.sustain` | float | [0, 1] | 1.0 | yes | Env2 sustain level |
| `env2.release` | float (log) | [0.001, 30.0] | 0.5 | yes | Env2 release time (s) |

### 3.5 LFO parameters (per LFO, 1–4)

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `lfo{i}.rate_hz` | float (log) | [0.001, 50.0] | 1.0 | yes | LFO rate (free) |
| `lfo{i}.depth` | float | [0, 1] | 1.0 | yes | LFO depth |
| `lfo{i}.shape` | enum | {sine, triangle, saw, square, sample_hold} | sine | yes | LFO waveform (Phase 2 ships all five; sample_hold uses Philox stream `LfoRandomWalk`) |
| `lfo{i}.sync` | enum | {free, tempo_whole, tempo_half, tempo_quarter, tempo_eighth, tempo_sixteenth, tempo_dotted_quarter, tempo_dotted_eighth, tempo_triplet_quarter, tempo_triplet_eighth} | free | yes | Tempo sync |
| `lfo{i}.phase` | float | [0, 1] | 0.0 | yes | Initial phase |
| `lfo{i}.reset_on_note` | bool | {false, true} | false | yes | Reset phase on note-on |

### 3.6 Modulation matrix parameters (per slot, 1–16)

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `mod{i}.active` | bool | — | false | no | Slot active |
| `mod{i}.source` | enum | (see 05 §5.2) | LFO1 | no | Mod source |
| `mod{i}.destination` | enum | (see 05 §5.3) | TENSION | no | Mod target |
| `mod{i}.depth` | float | [-1, 1] | 0.0 | yes | Mod amount |
| `mod{i}.curve` | enum | {LINEAR, EXPONENTIAL, S_CURVE, INVERTED, BIPOLAR} | LINEAR | no | Mod curve |

### 3.7 Harvester parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `harvester.spacing` | float | [0, 1] | 0.5 | yes | Harvester separation |
| `harvester.orbit_depth` | float | [0, 1] | 0.0 | yes | Orbit motion depth |
| `harvester.orbit_speed_hz` | float (log) | [0.001, 5.0] | 0.05 | yes | Orbit rotation speed |
| `harvester.orbit_shape` | enum | {circle, figure_eight, random_walk, off} | circle | no | Orbit pattern |

### 3.8 Output parameters

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `output.master_gain_db` | float | [-60, +12] | 0.0 | yes | Master gain |
| `output.pan` | float | [-1, +1] | 0.0 | yes | Stereo pan |
| `output.limiter_active` | bool | — | false | yes | Brick-wall limiter |

### 3.9 Reserved persisted fields

Fields that are written to and read from the preset file but are not user-editable in v1.0. They reserve the JSON keys for future-version expansion and load with documented defaults when omitted.

| ID | Type | Default (v1.0) | Description |
|---|---|---|---|
| `shared_substrate` | bool | `false` | Reserved. Will allow voices in the same instance to share a single substrate (architecture seam left open in 01 §2). |
| `substrate.laplacian_order` | enum | `5` (5-point) | 2D Laplacian stencil. `9` is implemented but not exposed in the GUI. |
| `substrate.nonlinear_beta` | float | `0.0` | Reserved cubic-restoring-force coefficient for the substrate (02 §9). |

These fields persist in presets so v1.x and v2.0 builds can recognise older presets that pre-date the field. Migration is described in 06 §3.4.

### 3.10 Internal engine fields (private, not user-controllable)

The following names are internal engine state. They are referenced throughout the technical documents (02–05) but are NOT host parameters and do NOT appear in presets directly. The user reaches them through the macros, modulation matrix, and structural selectors. Listed here for cross-document traceability.

| Internal field | Where set | Reached via |
|---|---|---|
| `substrate.c2` | macro fan-out | `macro.tension` (05 §2.1) |
| `substrate.viscosity` | macro fan-out | `macro.tension` (clamp) and `macro.damping` (05 §2.1, §2.2) |
| `substrate.gamma` | macro fan-out | `macro.damping` (05 §2.2) |
| `substrate.size` | preset load | `structural.substrate_size` (09 §3.2) |
| `agent.active_count` | macro fan-out | `macro.density` (05 §2.3) |
| `agent.deposit_weight_scale` | macro fan-out | `macro.density` (05 §2.3) |
| `agent.drift_scale` | macro fan-out | `macro.migration` (05 §2.4) |
| `agent.migration_noise_scale` | macro fan-out | `macro.migration` (05 §2.4) |
| `agent.harmonic_lock` | macro fan-out | `macro.coherence` (05 §2.5) |
| `agent.detune_scale` (live) | macro fan-out | `macro.coherence` |
| `agent.shape_param_drift` | macro fan-out | `macro.coherence` |
| `agent.mod_sensitivity_scale` | macro fan-out | `macro.excitation` (05 §2.6) |
| `agent.frequency_clamp_softness` | macro fan-out | `macro.excitation` |
| `agent.envelope.release_rate_scale` | macro fan-out | `macro.damping` |
| `voice.gate_threshold` | constant | engine constant (`-72 dBFS`, 01 §6.3) |
| `harvester.position_offset` | macro fan-out | `macro.migration` (orbit depth scaling) |
| `harvester.spacing_scale` | macro fan-out | `macro.tension` |

These names appear in the technical documents because the macro fan-out tables refer to them. They are not directly editable in any v1.0 GUI control or preset file; only the macros, mod-matrix slots, and host-exposed parameters in 09 §3.1–3.10 are user-facing.

### 3.11 Modulation destinations (canonical IDs)

The modulation matrix (05 §5) maps modulation-source values to one of 24 canonical destination IDs. Each destination targets a host-exposed parameter (or, in three reserved cases, an internal field that becomes user-exposed in a future version). Modulation contributions are added to the destination's base parameter value at evaluation time (05 §5.4) and the sum is clamped to the base parameter's range.

| Destination ID | Target base parameter | Effective range of contribution | Notes |
|---|---|---|---|
| `TENSION` | `macro.tension` | [-1, 1], summed with base, clamped to [0, 1] | |
| `DAMPING` | `macro.damping` | [-1, 1], summed, clamped to [0, 1] | |
| `DENSITY` | `macro.density` | [-1, 1], summed, clamped to [0, 1] | |
| `MIGRATION` | `macro.migration` | [-1, 1], summed, clamped to [0, 1] | |
| `COHERENCE` | `macro.coherence` | [-1, 1], summed, clamped to [0, 1] | |
| `EXCITATION` | `macro.excitation` | [-1, 1], summed, clamped to [0, 1] | |
| `HARVESTER_SPACING` | `harvester.spacing` | [-1, 1], summed, clamped to [0, 1] | |
| `HARVESTER_ORBIT_DEPTH` | `harvester.orbit_depth` | [-1, 1], summed, clamped to [0, 1] | |
| `LFO1_RATE` | `lfo1.rate_hz` | bipolar in log-Hz units; ±2 octaves around base | |
| `LFO2_RATE` | `lfo2.rate_hz` | ±2 octaves | |
| `LFO3_RATE` | `lfo3.rate_hz` | ±2 octaves | |
| `LFO4_RATE` | `lfo4.rate_hz` | ±2 octaves | |
| `AGENT_DETUNE` | `agent.detune_scale` | [-1, 1], summed, clamped to [0, 1] | |
| `AGENT_SHAPE_PARAM` | per-agent `shapeParam` | bipolar in normalised units (02 §3.4 mapping) | Affects FM ratio/index drift |
| `SUBSTRATE_NONLINEAR` | `substrate.nonlinear_beta` (reserved) | [-1, 1], scaled to [0, 0.01] | Modulation has no effect in v1.0; reserved |
| `PITCH_FINE` | global pitch offset | bipolar, ±50 cents at depth 1 | |
| `PITCH_COARSE` | global pitch offset | bipolar, ±12 semitones at depth 1 | |
| `MASTER_GAIN` | `output.master_gain_db` | bipolar, ±12 dB at depth 1, clamped to [-60, 12] dB | |
| `OUTPUT_PAN` | `output.pan` | [-1, 1], summed, clamped to [-1, 1] | |
| `ENV1_ATTACK` | `env1.attack` | bipolar in log-time; ±2 octaves | |
| `ENV1_RELEASE` | `env1.release` | bipolar in log-time; ±2 octaves | |
| `ENV2_ATTACK` | `env2.attack` | bipolar in log-time; ±2 octaves | |
| `ENV2_RELEASE` | `env2.release` | bipolar in log-time; ±2 octaves | |
| `SUBSTRATE_SIZE_OFFSET` | `structural.substrate_size` (reserved) | reserved | Not exposed in v1.0; modulation has no effect |

The "Modulation destinations" ID space is distinct from the host parameter ID space (§3.1–3.10): a destination ID like `TENSION` is the **modulation contribution accumulator** for the underlying parameter `macro.tension`, not the parameter itself.

### 3.12 Engine settings (preferences, not preset)

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `engine.oversample_mode` | enum | {ECO, STD, PREMIUM} | STD | no | Substrate downsample factor |
| `engine.max_voices` | int | {1, 2, 4, 8} | 8 | no | Polyphony cap |
| `engine.gui_refresh_hz` | int | {30, 60, 120} | 60 | no | Visualiser rate |
| `engine.theme` | enum | {dark, light, hc} | dark | no | GUI theme |

## 4. Default factory preset list

This section is the **design intent** for the v1.0 factory palette: category counts, naming style, and the few presets whose names are pinned by the contract tests (08 §2.3) and tutorials (the Init series). Individual preset patches (the macro values, modulation slots, and authored seed) are produced during Phase 4 of the roadmap (08 §1) and are not part of this v1.0 specification — they are content, not contract.

What this section commits to:

* **Total preset count: 128.** The release gate (08 §1, Phase 4) requires all 128 to be authored, hashed, and contract-tested.
* **Category counts: 32 Drone + 32 Organic + 24 Pitched + 16 Glitch + 16 Hybrid + 8 Init = 128.** These counts are the contract; individual preset names within categories may shift as Phase 4 sound design proceeds.
* **Each preset must ship with a verified audio hash** (06 §3.3) and pass the relevant sonic-corner contract test (08 §2.3) for its category.
* **The 8 Init presets are pinned by name and intent** because they are referenced by the in-app manual (07 §10) and the test rig.
* **Four canonical "contract" presets** — one per sonic corner — are pinned by name (`Init Drone`, `Init Organic`, `Init Pitched`, `Init Glitch`) and tagged `tags: ["contract:<corner>"]` so the test rig can locate them.

The named-and-described preset rows below are the v1.0 design targets. Rows marked `_(Phase 4)_` are placeholders that Phase 4 sound design will fill — the table specifies the count and category but not the final names. A preset name that appears in both the spec and a shipped build must match the build; if Phase 4 names a Drone preset "Glacial Slow" instead of "Slow Glacier", the spec is updated to match the build, not the other way round.

### Drone (32)

| # | Name | Description |
|---|---|---|
| 1 | Slow Glacier | 60 s static-feel sustain, harmonic stack |
| 2 | Mountain Wind | Drone with slow harmonic centroid drift |
| 3 | Tape Hiss Choir | Choral drone with subtle noise agents |
| 4 | Cathedral Reverb | Long-decay drone, ring topology, low damping |
| 5 | Lighthouse | Slow rotating harvester, single dominant cohort |
| 6–32 | _(Phase 4)_ | _(Phase 4)_ |

### Organic (32)

| # | Name | Description |
|---|---|---|
| 33 | Forest Breath | High migration, mid coherence; 0.2 Hz LFO on excitation |
| 34 | Coral | Wet, complex; FM agents, slow pitch drift |
| 35 | Tidepool | Random LFO on coherence; sparse density |
| 36 | Skin | Warm low-mid texture; mid-EXCITATION, low MIGRATION |
| 37–64 | _(Phase 4)_ | |

### Pitched (24)

| # | Name | Description |
|---|---|---|
| 65 | Bell Choir | Very high coherence, pitched cohort |
| 66 | Soft Lead | Single agent, low density, polyphonic |
| 67 | Glass Pad | Stack of harmonic agents, slow envelope |
| 68 | Sub | Low-frequency single-agent voice |
| 69–88 | _(Phase 4)_ | |

### Glitch (16)

| # | Name | Description |
|---|---|---|
| 89 | Static | Maximum excitation, low damping, square LFO on tension |
| 90 | Bones | Sparse density, high migration, FM agents |
| 91 | Cassette | Wow-and-flutter via slow LFO on coherence |
| 92 | Fragment | Random LFO on density (audible spawn/die) |
| 93–104 | _(Phase 4)_ | |

### Hybrid (16)

| # | Name | Description |
|---|---|---|
| 105 | Day to Night | 30 s slow ramp from drone to glitch via macro automation |
| 106 | Stage Whisper | Pitched-to-organic via MPE pressure |
| 107 | Erosion | Slow degradation: starts pitched, drifts to noise |
| 108–120 | _(Phase 4)_ | |

### Init (8)

| # | Name | Description |
|---|---|---|
| 121 | Init Drone | Bare drone preset, ring topology |
| 122 | Init Organic | Bare organic preset, ring |
| 123 | Init Pitched | Bare pitched preset, ring |
| 124 | Init Glitch | Bare glitch preset, ring |
| 125 | Init Drone 2D | Bare drone, torus_64 |
| 126 | Init Organic 2D | Bare organic, torus_64 |
| 127 | Init Pitched 2D | Bare pitched, torus_64 |
| 128 | Init Glitch 2D | Bare glitch, torus_64 |

The Init presets are documented patches — each demonstrates one engine feature in isolation and is the starting point for tutorials and the manual.

## 5. Configuration file locations

| File | macOS | Windows | Linux |
|---|---|---|---|
| User presets | `~/Library/Audio/Presets/SFS/` | `%APPDATA%\SFS\Presets\` | `~/.config/SFS/presets/` |
| Preferences | `~/Library/Application Support/SFS/preferences.json` | `%APPDATA%\SFS\preferences.json` | `~/.config/SFS/preferences.json` |
| Crash logs | `~/Library/Logs/SFS/` | `%LOCALAPPDATA%\SFS\Logs\` | `~/.local/share/SFS/logs/` |

## 6. Numerical limits

| Limit | Value | Notes |
|---|---|---|
| Maximum voice count | 8 | v1.0 hard cap |
| Maximum agents per voice | 64 | v1.0 hard cap |
| Maximum substrate cells (1D) | 4096 | v1.0 hard cap |
| Maximum substrate cells (2D) | 65536 (256×256) | v1.0 hard cap; offline render allows 1024×1024 |
| Maximum harvesters per voice | 16 | accommodates the largest active v1.0 layout (7.1.4 = 12 harvesters) with headroom for future FOA/HOA layouts; only one layout is active at a time |
| Maximum modulation slots | 16 | v1.0 hard cap |
| Maximum LFOs | 4 | v1.0 hard cap |
| Maximum envelopes | 2 | v1.0 hard cap |
| Maximum preset name length | 64 chars | v1.0 |
| Maximum preset description length | 512 chars | v1.0 |
| Maximum preset tag count | 16 | v1.0 |
