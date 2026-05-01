# 05 — Macros and modulation matrix

The macro panel is what a player and a sound designer actually touch. This document specifies the six primary macros, their fan-out into low-level engine parameters, the smoothing curves, and the modulation matrix architecture that wraps everything in a flexible routing layer.

---

## 1. Design philosophy

Three rules govern macro design:

* **Each macro must do one musically meaningful thing.** A macro that simultaneously raises substrate tension, agent density, and envelope decay is doing three things — split it.
* **Every position of every macro must be musical.** No macro can have a "broken" zone; the curves are tuned so the user can land anywhere on the knob and get a usable patch.
* **Macros are smooth.** A macro at maximum, suddenly snapped to zero, must produce a click-free transition. This is achieved by per-parameter audio-rate smoothing inside the engine.

A macro is a value in `[0.0, 1.0]` exposed to the host. Its effect on engine state goes through a **fan-out function**, which expands one macro into multiple low-level parameter changes with individual curves and weights. The fan-out is preset-defined (factory) and user-modifiable (advanced).

## 2. The six primary macros

### 2.1 TENSION

Controls how far and how quickly disturbances propagate through the substrate.

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `substrate.c2` | `0.49 · m²` | 0.49 |
| `substrate.viscosity_floor` (clamps κ from below at high tension) | `0.005 · m` | 0.005 |
| `harvester.spacing_scale` | `0.5 + 0.5 · m` | 1.0 (full ring spacing) |

Sonic effect: at low tension, the substrate is sluggish and harvester listening points hear mostly local agents. At high tension, the substrate rings actively and harvesters hear standing-wave structures that span the whole ring.

### 2.2 DAMPING

Controls how fast substrate energy decays, and the global "release feel."

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `substrate.gamma` | `0.05 · m²` | 0.05 |
| `agent.envelope.release_rate_scale` | `0.5 + 1.5 · m` | 2.0 (faster release) |
| `voice.gate_threshold` | constant | -72 dBFS |

Sonic effect: low DAMPING gives long sustains and ambient tails. High DAMPING gives plucky, short notes where the substrate's contribution is brief.

### 2.3 DENSITY

Controls how many agents are active and how strongly they deposit.

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `agent.active_count` | `round(8 + 56 · m)` | 64 |
| `agent.deposit_weight_scale` | `0.5 + 0.5 · m` | 1.0 (full deposit) |

Sonic effect: low DENSITY produces sparse, almost solo-instrument timbres (a single agent ringing on the substrate). High DENSITY produces dense, choral textures where individual agents are inaudible and the colony reads as a single complex voice.

The `active_count` change is gracefully handled: agents fade in or out via amplitude ramp over 50 ms when DENSITY is automated mid-note.

### 2.4 MIGRATION

Controls how much agents move across the substrate.

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `agent.drift_scale` | `m²` | 1.0 |
| `agent.migration_noise_scale` | `m` | 1.0 |
| `harvester.orbit_depth` | `0.3 · m` | 0.3 |

Sonic effect: low MIGRATION → static (stable) timbre. High MIGRATION → constantly evolving texture where the colony walks across the substrate, producing slow filter-sweep-like motion as different agents arrive at different harvesters.

### 2.5 COHERENCE

Controls whether agent frequencies are harmonically locked to the played note or detuned.

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `agent.harmonic_lock` | `m` | 1.0 (fully locked) |
| `agent.detune_scale` | `(1 - m)²` | 0.0 (no detune) |
| `agent.shape_param_drift` | `(1 - m) · 0.3` | 0.0 (no shape drift) |

Sonic effect: at high COHERENCE, the colony is a chord — a stack of harmonics of the played note. At low COHERENCE, agents detune freely and the colony is a noise band centered on the note. Mid values give "wide unison" character.

### 2.6 EXCITATION

Controls how strongly the substrate modulates the agents (the magnitude of the closed-loop coupling).

| Destination | Curve | Range at macro = 1 |
|---|---|---|
| `agent.mod_sensitivity_scale` | `m²` | 1.0 |
| `substrate.viscosity_floor_offset` | `-0.01 · m` | -0.01 (lower κ at high m) |
| `agent.frequency_clamp_softness` | `1.0 - 0.5 · m` | 0.5 (less hard clamping at high m) |

Sonic effect: at zero EXCITATION, the engine is a parallel additive bank — agents ignore the substrate. At maximum, agents are bent significantly by the substrate, producing chaotic, blooming, glitch-prone behaviour. This is the "experimental" macro.

EXCITATION's joint curve with TENSION is what creates the most exciting regions of the parameter space — high TENSION with high EXCITATION puts the substrate near its critical point and small inputs create dramatic, evolving, almost biological responses.

## 3. Two structural selectors

### 3.1 TOPOLOGY

Enum: `{RING, TORUS_64, TORUS_128, TORUS_256}`. Default `RING`.

Changes the substrate type. v1.0 implements the change at voice-allocation time, not mid-note: changing TOPOLOGY while a note is held is queued and applied to the next voice ignition. This avoids the "swap the substrate beneath an active voice" problem.

### 3.2 ASPECT

Continuous, `[0.5, 2.0]`, default `1.0`. Only meaningful in 2D mode (no effect in 1D mode). Changes the aspect ratio of the toroidal grid. Applied at next voice ignition.

## 4. Macro smoothing

Each macro's destination parameter is smoothed at audio rate using a one-pole low-pass:

```
smooth_value[n] = α · target_value + (1 - α) · smooth_value[n-1]
```

with per-parameter time constants:

| Parameter class | Smoothing time | Why |
|---|---|---|
| `substrate.c2`, `gamma` | 50 ms | Audible discontinuities if faster |
| `agent.frequency_*` | 30 ms | Agent pitch needs to track macro automation |
| `agent.deposit_weight_scale` | 10 ms | Fast click suppression |
| `agent.harmonic_lock` | 200 ms | Discrete decision; long smoothing avoids zipper |
| `harvester.position_offset` | 100 ms | Spatial wandering should feel slow |
| `agent.active_count` | n/a | Step change with 50 ms agent fade-in/out |

`α = 1 - exp(-1 / (τ · fs))` where `τ` is the time constant in seconds.

## 5. The modulation matrix

The modulation matrix is the layer between **modulation sources** (LFOs, envelopes, MIDI controllers, MPE expressions, randomisers) and **modulation destinations** (any macro, any low-level parameter).

### 5.1 Slots

A modulation slot:

```c++
struct ModulationSlot {
    SourceId   source;
    Destination destination;
    float       depth;      // [-1.0, 1.0]
    Curve       curve;      // LINEAR, EXPONENTIAL, S_CURVE, INVERTED, BIPOLAR
    bool        active;
};
```

v1.0 has **16 slots**. Each slot is independent.

### 5.2 Sources (12 in v1.0)

| Source | Description |
|---|---|
| `LFO1` ... `LFO4` | Four free-running LFOs, sine/triangle/square/random, syncable to host tempo |
| `ENV1` ... `ENV2` | Two ADSR envelopes triggered on note-on |
| `KEY_NOTE` | MIDI note number, mapped to [0, 1] over the 0–127 range |
| `KEY_VELOCITY` | MIDI note-on velocity |
| `MPE_PRESSURE` | Per-note pressure (channel pressure on monophonic) |
| `MPE_SLIDE` | CC74 (per-note slide) |
| `RANDOM` | Per-note random value, seeded |
| `MIDI_CC1` | Mod wheel by default; user-remappable |

Each LFO has its own rate, depth, shape, and reset behaviour (`free`, `note_reset`, `host_sync`).

### 5.3 Destinations (24 in v1.0)

The destinations mirror the macros plus selected low-level parameters:

```
TENSION, DAMPING, DENSITY, MIGRATION, COHERENCE, EXCITATION,    // 6 macros
HARVESTER_SPACING, HARVESTER_ORBIT_DEPTH,                        // 2 layout
LFO1_RATE, LFO2_RATE, LFO3_RATE, LFO4_RATE,                       // LFO modulating LFO
AGENT_DETUNE, AGENT_SHAPE_PARAM,                                   // agent-level
SUBSTRATE_NONLINEAR (reserved),                                    // substrate-level
PITCH_FINE, PITCH_COARSE,                                          // global pitch
MASTER_GAIN, OUTPUT_PAN,                                           // output
ENV1_ATTACK, ENV1_RELEASE,                                         // env modulation
ENV2_ATTACK, ENV2_RELEASE,
SUBSTRATE_SIZE_OFFSET (reserved)
```

User-extensible destinations (e.g., per-LFO depth) are deferred to v2.0.

### 5.4 Evaluation

At block rate, the modulation matrix runs:

```c++
for each modulation slot:
    if !slot.active: continue
    src_value = sources[slot.source].read()
    src_value = applyCurve(src_value, slot.curve)
    contribution = slot.depth * src_value
    destinations[slot.destination].accumulate(contribution)

// After all slots: per-destination, sum contributions and add to base value
for each destination:
    final_value = base_value[destination] + sum_of_contributions[destination]
    final_value = clamp(final_value, destination.min, destination.max)
    setSmoothedTarget(destination, final_value)
```

`base_value[destination]` is the **current value of the destination parameter as set by the user's GUI knob and/or VST3 host automation**. It is the resolved parameter value after the host's sample-accurate parameter-change events have been applied for this block (or sub-block, when the block has been subdivided per §7). The modulation matrix's contributions are then **added** to this base value, and the sum is clamped to the destination's defined range.

Multiple slots writing to the same destination **add** their contributions before clamping. This is the standard subtractive-synth modulation matrix behaviour and matches user expectation. A modulation slot cannot drive a destination outside its defined range; deeply-modulated destinations clip at their bounds rather than wrap.

### 5.5 Curve types

```
LINEAR:      y = x
EXPONENTIAL: y = sign(x) · x²
S_CURVE:     y = x · (3 - 2 · |x|)         (smoothstep on bipolar input)
INVERTED:    y = -x
BIPOLAR:     y = 2x - 1                    (unipolar source → bipolar destination)
```

## 6. MIDI / MPE integration

### 6.1 Standard MIDI

* Note-on triggers a voice with the played note as `KEY_NOTE` modulation source and velocity as `KEY_VELOCITY`.
* Note-off triggers gate-off.
* Pitch bend (channel) bends all active voices on that channel by `±2 semitones` (configurable; reserved as a global preference).
* CC1 (mod wheel) is exposed as `MIDI_CC1` source; user-remappable to other CCs.
* All-notes-off (CC123) panics all voices over 10 ms.

### 6.2 MIDI 2.0 / VST3 Note Expression

VST3 Note Expressions are mapped 1:1 to MPE expressions where applicable. The plug-in wrapper (07) registers the standard expressions:

* `kVst3NoteExpressionTuningTypeID` → per-note pitch bend
* `kVst3NoteExpressionVolumeTypeID` → per-note velocity continuation
* `kVst3NoteExpressionPanTypeID` → per-note pan (mapped to harvester position offset)
* Plus three custom expressions: `MPE_PRESSURE`, `MPE_SLIDE`, `MPE_RANDOM` (so MPE controllers without slide can still drive randomisation as an expression).

### 6.3 Per-note vs. global modulation

Sources that are intrinsically per-note (`KEY_NOTE`, `KEY_VELOCITY`, `MPE_*`, `ENV1`, `ENV2`, `RANDOM`) modulate per-voice. Global sources (`LFO*`, `MIDI_CC1`) modulate all voices identically.

This is how you would, for example, modulate `EXCITATION` with `MPE_PRESSURE`: each note can become more glitchy independently as the player presses harder on its key.

## 7. Sample-accurate automation

VST3 supports per-sample-offset parameter changes within a process block. The shell reads these from `IParameterChanges` and converts them into `ControlEvent` queue entries with timestamps.

The audio thread, at the start of each block, examines event timestamps. If any events exist mid-block, the audio thread subdivides the block:

```c++
size_t segmentStart = 0;
while (segmentStart < blockSize) {
    size_t segmentEnd = blockSize;
    for (auto& evt : events) {
        if (evt.timestamp > segmentStart && evt.timestamp < segmentEnd) {
            segmentEnd = evt.timestamp;
        }
    }
    engine.process(segmentEnd - segmentStart, audioOut + segmentStart);
    applyEventsAt(segmentEnd);
    segmentStart = segmentEnd;
}
```

This is exact sample-accurate automation. The cost is at most a few function-call overheads per parameter change.

## 8. Macro panel mapping (for GUI design)

The default macro panel layout for v1.0 (specified here so 07 has a single source of truth):

```
┌─────────────────────────────────────────────────────────────┐
│  TENSION    DAMPING    DENSITY    MIGRATION   COHERENCE  EXC│
│  [knob]     [knob]     [knob]     [knob]      [knob]    [knob]│
│                                                              │
│  TOPOLOGY [▼ Ring]        ASPECT [════════•════]            │
└─────────────────────────────────────────────────────────────┘
```

Six big knobs, two structural controls. Below this strip, the modulation matrix and preset browser live in their own panels (07).

A small color indicator on each knob shows incoming modulation (a thin arc around the knob, animated to reflect modulator current value).

## 9. Default modulation slots in factory presets

Each factory preset ships with sensible default modulation routings. Examples:

* **Drone** preset: LFO1 → MIGRATION (depth 0.2), LFO1 rate 0.05 Hz, sine. ENV1 → DAMPING, depth -0.3, slow attack.
* **Organic** preset: LFO1 → COHERENCE (depth 0.4), LFO1 rate 0.1 Hz, random. LFO2 → MIGRATION (depth 0.5), LFO2 rate 0.2 Hz, sine. MPE_PRESSURE → EXCITATION (depth 0.6).
* **Pitched** preset: KEY_VELOCITY → DENSITY (depth 0.4). ENV1 → DAMPING, fast attack.
* **Glitch** preset: LFO1 → EXCITATION (depth 0.7), LFO1 rate 4 Hz, square. RANDOM → COHERENCE (depth 0.5).

Each is a starting point — users can rewrite all 16 slots.

## 10. Open questions

* **Macro-level macros.** A "super-macro" mode where one macro modulates several others is reserved. v1.0 uses the modulation matrix for this purpose.
* **Curve editor.** A visual curve editor for modulation curves is deferred to v1.2 — v1.0 ships with the five enum curves only.
* **Modulation visualization.** Live "modulation flow" arrows from sources to destinations (as in Bitwig's "The Grid") are a stretch goal for v1.1.
