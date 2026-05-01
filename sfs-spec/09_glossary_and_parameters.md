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
| `c²` | substrate wave-speed squared | [0.0, 0.49] (1D), [0.0, 0.225] (2D) | dimensionless | 02 |
| `κ` (kappa) | velocity-diffusion coefficient | [0.0, 0.45] | dimensionless | 02 |
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
| `S` | substrate downsample factor | {1, 2, 4} | dimensionless | 01 |
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
| `lfo{i}.shape` | enum | {sine, tri, square, random} | sine | yes | LFO waveform |
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

### 3.9 Engine settings (preferences, not preset)

| ID | Type | Range | Default | Host-visible | Description |
|---|---|---|---|---|---|
| `engine.oversample_mode` | enum | {ECO, STD, PREMIUM} | STD | no | Substrate downsample factor |
| `engine.max_voices` | int | {1, 2, 4, 8} | 8 | no | Polyphony cap |
| `engine.gui_refresh_hz` | int | {30, 60, 120} | 60 | no | Visualiser rate |
| `engine.theme` | enum | {dark, light, hc} | dark | no | GUI theme |

## 4. Default factory preset list

The 128 v1.0 factory presets are catalogued here as a reference. Each line shows category, name, brief description.

Total: **32 Drone + 32 Organic + 24 Pitched + 16 Glitch + 16 Hybrid + 8 Init = 128 presets**. Each must ship with a verified audio hash (06 §3.3) and pass the relevant sonic-corner contract test (08 §2.3).

### Drone (32)

| # | Name | Description |
|---|---|---|
| 1 | Slow Glacier | 60 s static-feel sustain, harmonic stack |
| 2 | Mountain Wind | Drone with slow harmonic centroid drift |
| 3 | Tape Hiss Choir | Choral drone with subtle noise agents |
| 4 | Cathedral Reverb | Long-decay drone, ring topology, low damping |
| 5 | Lighthouse | Slow rotating harvester, single dominant cohort |
| 6–32 | _(defined in build)_ | _(defined in build)_ |

### Organic (32)

| # | Name | Description |
|---|---|---|
| 33 | Forest Breath | High migration, mid coherence; 0.2 Hz LFO on excitation |
| 34 | Coral | Wet, complex; FM agents, slow pitch drift |
| 35 | Tidepool | Random LFO on coherence; sparse density |
| 36 | Skin | Warm low-mid texture; mid-EXCITATION, low MIGRATION |
| 37–64 | _(defined in build)_ | |

### Pitched (24)

| # | Name | Description |
|---|---|---|
| 65 | Bell Choir | Very high coherence, pitched cohort |
| 66 | Soft Lead | Single agent, low density, polyphonic |
| 67 | Glass Pad | Stack of harmonic agents, slow envelope |
| 68 | Sub | Low-frequency single-agent voice |
| 69–88 | _(defined in build)_ | |

### Glitch (16)

| # | Name | Description |
|---|---|---|
| 89 | Static | Maximum excitation, low damping, square LFO on tension |
| 90 | Bones | Sparse density, high migration, FM agents |
| 91 | Cassette | Wow-and-flutter via slow LFO on coherence |
| 92 | Fragment | Random LFO on density (audible spawn/die) |
| 93–104 | _(defined in build)_ | |

### Hybrid (16)

| # | Name | Description |
|---|---|---|
| 105 | Day to Night | 30 s slow ramp from drone to glitch via macro automation |
| 106 | Stage Whisper | Pitched-to-organic via MPE pressure |
| 107 | Erosion | Slow degradation: starts pitched, drifts to noise |
| 108–120 | _(defined in build)_ | |

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
| Maximum harvesters per voice | 16 | accommodates 7.1.4 + ambisonic 1st order |
| Maximum modulation slots | 16 | v1.0 hard cap |
| Maximum LFOs | 4 | v1.0 hard cap |
| Maximum envelopes | 2 | v1.0 hard cap |
| Maximum preset name length | 64 chars | v1.0 |
| Maximum preset description length | 512 chars | v1.0 |
| Maximum preset tag count | 16 | v1.0 |
