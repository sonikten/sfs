# 07 — GUI and visualization

This document specifies the v1.0 GUI for the SFS plug-in. It is written for a product/GUI designer who will produce a high-fidelity visual design, and for the DSP/integration engineer who will wire the GUI to the engine via the parameter system. It does not prescribe a specific visual style — that's the designer's territory — but it does prescribe layout, hierarchy, panel structure, what data the GUI reads and writes, and the visualisation contract for the substrate.

---

## 1. Design principles

Five principles guide the GUI:

* **Synthesis you can see.** SFS's central novelty is the substrate; the substrate must be visible, animated, and fundamental to the GUI — not a sidebar feature. A user should be able to identify what the engine is doing at a glance.
* **Six knobs that mean something.** The macro panel is the primary user surface. Six knobs is more than analogue-purist instruments and fewer than modular monsters. Each must have an obvious effect.
* **Progressive disclosure.** A new user sees the macro panel and the substrate. An intermediate user opens the modulation matrix. An advanced user accesses the raw parameter table. Default view is uncluttered.
* **No surprises.** Every parameter that affects audio is reachable from the GUI. We don't hide things in JSON-only fields.
* **Animated where animation tells you something; still where it doesn't.** The substrate animates; the macro knobs don't pulse unless they're being modulated.

## 2. Layout overview

The plug-in window is **resizable** within `[800, 1600] × [600, 1200]` pixels, default `1280 × 800`. The internal layout uses a responsive grid; below 1024×640 the modulation matrix collapses into a tab.

```
┌───────────────────────────────────────────────────────────────────────┐
│ HEADER:  [SFS]   [Preset: Slow Glacier ▼]  [<][>][🎲]   [⚙]  [?]      │
├───────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                                                                 │  │
│  │              SUBSTRATE VISUALISATION                            │  │
│  │              (1D strip or 2D heatmap, with agent overlay)       │  │
│  │                                                                 │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │ MACRO PANEL                                                    │   │
│  │  TENSION  DAMPING  DENSITY  MIGRATION  COHERENCE  EXCITATION   │   │
│  │   (knob)   (knob)   (knob)   (knob)     (knob)     (knob)      │   │
│  │  TOPOLOGY [Ring ▼]    ASPECT [════•════]                       │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                                                                       │
│  ┌────────────────────────┐ ┌─────────────────────────────────────┐   │
│  │   MODULATION MATRIX    │ │  ENVELOPES & LFOS                   │   │
│  │  16 slots, list view   │ │  ENV1 ENV2  LFO1 LFO2 LFO3 LFO4     │   │
│  │  src → dst, depth, ▤   │ │  (visual editors for each)          │   │
│  └────────────────────────┘ └─────────────────────────────────────┘   │
│                                                                       │
├───────────────────────────────────────────────────────────────────────┤
│ FOOTER: CPU 24%   Voices 3/8   Latency 0 samples   [Init][Save][Load] │
└───────────────────────────────────────────────────────────────────────┘
```

## 3. Header

A thin header strip across the top, ~48 px tall:

* **SFS logo and product name** (left).
* **Preset selector** (centered, dominant): the current preset name as a clickable element opens the preset browser. Adjacent `<` and `>` arrows step through presets in the current filter. The dice button (`🎲`) randomises within musical bounds (see 06 §7).
* **Settings (`⚙`)** opens preferences (sample rate hint, oversample mode, default channel layout, GUI theme).
* **Help (`?`)** opens the in-app manual.

## 4. The substrate visualiser

This is the GUI's centerpiece. It occupies ~40% of the window's vertical space.

### 4.1 Modes

Two modes, switched by a toggle on the visualiser itself:

#### 4.1.1 1D strip mode (1D substrate)

A horizontal strip representing the substrate's ring topology. The strip wraps — left edge meets right edge, marked with a subtle indicator. Substrate displacement `u[x]` is rendered as a 2D plot: vertical axis shows displacement, horizontal axis shows position around the ring.

```
   ┌──────────────────────────────────────────────────────┐
   │            ╱╲                                       │
   │   ╲╱╲╱╲   /  ╲    ╲╱╲                              │
   │~~~~~~~~~~/    ╲~~~/   ╲~~╱~╲~~~~~~~~~~~~~~~~~~~~~~  │  ← substrate displacement
   │              ╲╱        ╲╱  ╲                        │
   │                                                      │
   │  ●         ●            ●     ●        ●             │  ← agent positions (dots)
   │                                                      │
   │  ▲                          ▲                        │  ← harvesters L (▲) R (▼)
   │                                              ▼       │
   └──────────────────────────────────────────────────────┘
```

Render rate: 60 Hz from a downsampled snapshot of the substrate. The snapshot is `N` samples updated by the audio thread in a triple-buffered fashion.

Agent positions are dots whose color encodes their waveform shape (sine = blue, saw = orange, square = red, fmpair = purple, noise = gray) and whose size encodes their current amplitude.

Harvesters are persistent triangle markers whose vertical position indicates which output channel they feed (L on top, R on bottom for stereo; more for surround). Their horizontal position is their substrate position.

### 4.1.2 2D heatmap mode (2D substrate)

A 2D heatmap showing `u[x, y]` as colour. Hot colours = positive displacement, cool colours = negative. Black = zero. The torus topology is indicated by mirror tiles at the edges (faint bands of repeated content).

Agent positions are dots overlaid on the heatmap. Harvesters are larger triangle markers.

```
   ┌──────────────────────────────────────────────────────┐
   │ ▒▒▒▓▓▒░░░░░▒▓▓▓▒▒░░░▒▒▓▓▒░░░▒▓▓▓▓▒░░░░░▓▓▓▒▒░       │
   │ ▒▒▓▓▓▒░░  ●░▒▓▓▒▒░ ●░░▒▓▓▒░▒▒▓▓▓▒░░░ ●░▒▓▓▒░       │
   │ ░░▒▓▓▒░░  ░░░▒▒▒░░░░░░▒▒▒▒░▒▒▓▓▒░░░░░░░░▒▒▒░       │
   │ ░░░▒▒░░░  ░░░░░░░░░░░░░░░░░ ▼ ░░░░░ ●  ░░░░░       │
   │ ░░ ▲ ░░░  ░░  ●  ░░░░░░░░░░░░░░░░░░░░░░░░░░░       │
   │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ●  ░░░░░░░       │
   │ ░░░░░░░░░░░░ ●  ░░░░░░░░░░░░░░░░░░░░░░░░░░░       │
   │ ░░░░░░ ▼  ░░░░░░░░░ ●  ░░░░░░░░░░ ▲ ░░░░░░░       │
   └──────────────────────────────────────────────────────┘
```

### 4.2 Visualiser interactivity

* **Click-and-drag a harvester** moves it. The corresponding output channel's spatial position changes in real-time; this is musically meaningful and creative.
* **Hover an agent** shows its frequency, shape, and current envelope value in a tooltip.
* **Right-click anywhere on the substrate** offers context menu: "Place harvester here", "Pin agent here", "Reset to default positions".
* **Scroll wheel on the visualiser** zooms (1D mode only — useful for inspecting fine standing-wave structure on large substrates).

The visualiser is **not** the only way to see what's happening; the macro panel reflects the same state through its modulation indicators (5).

### 4.3 Performance budget

GUI rendering must not exceed 5% CPU. The substrate snapshot pipeline is split across threads to keep the audio thread's contribution bounded:

* **Audio thread** writes a raw snapshot of the substrate displacement buffer into a triple-buffered staging slot at the visualiser refresh rate (default 60 Hz, configurable in preferences). For 1D substrates of any size up to 4096, this is a single `memcpy` of ≤ 16 KB — under 1 µs amortised. For 2D substrates ≤ 128×128, the same `memcpy` (64 KB) is acceptable (~3 µs amortised). For 2D substrates above 128×128, the audio thread writes the **raw** snapshot at the substrate's native size; downsampling to the GUI's display resolution happens on a worker thread.
* **Worker thread** consumes the staging slot and downsamples to the GUI's target render size (256 for 1D strip, 128×128 for 2D heatmap). The worker writes the downsampled buffer to a second triple-buffered slot.
* **GUI thread** reads from the worker's slot and draws.

This split keeps the audio-thread snapshot cost bounded by a fixed `memcpy` regardless of substrate size, with all signal processing (downsampling, normalisation) on the worker. For offline-render configurations (1024×1024), no live visualisation is provided; a snapshot button captures a single frame on demand.

GPU acceleration (OpenGL via JUCE's Component or a custom GL backend) is recommended for the 2D heatmap. CPU rasterisation is acceptable for the 1D strip.

## 5. The macro panel

Six large knobs and two structural controls. The knobs occupy ~120 px diameter each.

### 5.1 Knob design

Each knob shows:

* **Label** above the knob (TENSION, DAMPING, ...).
* **Value** centered (numeric, optionally hidden after first interaction to reduce clutter).
* **Modulation arc**: a thin secondary arc around the knob, animated to reflect the *current modulation contribution* from all active modulation slots. A static arc means no live modulation; a moving arc means modulation is active.
* **Modulation depth indicators**: small triangle marks around the knob's edge for every modulation slot targeting this knob, color-coded by source.

Hover-state shows the destination's mapping curve and the current effective value.

### 5.2 Click behaviour

* **Click-drag** vertically: change the macro value.
* **Double-click**: reset to the preset's default for this macro.
* **Right-click**: context menu — "Add modulation slot", "Reset", "Map to MIDI CC...", "Copy value", "Paste value".
* **Cmd/Ctrl-click-drag**: fine adjustment (10× slower).
* **Shift-click-drag**: snap to typical preset positions (8 detents per knob).

### 5.3 TOPOLOGY and ASPECT

`TOPOLOGY` is a small dropdown showing the active topology and offering the four enum values; the Möbius and Klein options are visually present but disabled in v1.0 with a tooltip "v1.1".

`ASPECT` is a thin horizontal slider. Greyed out when `TOPOLOGY = ring` (1D mode); active in 2D modes.

## 6. The modulation matrix panel

A list of 16 slots, scrollable if the panel can't show all 16 at once.

```
┌─ MODULATION MATRIX ───────────────────────────────────────┐
│                                                            │
│  1 [✓] LFO1  → MIGRATION   ▒▒▒▒▒░░░░░ 0.20  [LIN]   [✕]   │
│  2 [✓] ENV1  → DAMPING     ▒░░░░░░░░░ -0.30 [LIN]   [✕]   │
│  3 [ ] LFO2  → TENSION     ░░░░░░░░░░ 0.00  [LIN]   [✕]   │
│  4 [✓] MPE_P → EXCITATION  ▒▒▒▒▒▒░░░░ 0.60  [EXP]   [✕]   │
│  5 [+] add slot                                            │
│  ...                                                       │
└────────────────────────────────────────────────────────────┘
```

Each row shows: active checkbox; source dropdown; arrow; destination dropdown; depth slider with bar visualisation; curve dropdown; remove button.

Adding a slot: click `[+]`. Slot becomes editable in place. Save state to the preset.

Removing: `[✕]` deactivates and clears. Removed slots shift up.

### 6.1 Source selection

Source dropdown shows the 12 sources grouped:

```
LFOs:
  LFO1
  LFO2
  LFO3
  LFO4
Envelopes:
  ENV1
  ENV2
Performance:
  KEY_NOTE
  KEY_VELOCITY
  MPE_PRESSURE
  MPE_SLIDE
  RANDOM
MIDI:
  MIDI_CC1
```

### 6.2 Destination selection

Destination dropdown shows the 24 destinations grouped:

```
Macros:
  TENSION, DAMPING, DENSITY, MIGRATION, COHERENCE, EXCITATION
Layout:
  HARVESTER_SPACING, HARVESTER_ORBIT_DEPTH
Per-LFO:
  LFO1_RATE, LFO2_RATE, LFO3_RATE, LFO4_RATE
Per-agent:
  AGENT_DETUNE, AGENT_SHAPE_PARAM
Output:
  PITCH_FINE, PITCH_COARSE, MASTER_GAIN, OUTPUT_PAN
Envelopes:
  ENV1_ATTACK, ENV1_RELEASE
  ENV2_ATTACK, ENV2_RELEASE
Reserved (greyed):
  SUBSTRATE_NONLINEAR, SUBSTRATE_SIZE_OFFSET
```

## 7. Envelopes and LFOs panel

A horizontal strip showing six small editors, one per envelope/LFO source.

### 7.1 Envelope editor

For ENV1, ENV2:

```
┌─ ENV1 ──────────────┐
│         ▲          │
│        ╱  ╲╲       │
│       ╱    ▔▔▔╲    │
│      ╱        ╲    │
│ ────/──────────╲──  │
│                    │
│ A  D  S  R         │
│ [▤][▤][▤][▤]       │  ← four small sliders below
└────────────────────┘
```

The envelope shape is drawn as a curve. ADSR sliders are below; click-drag any of them, or click-drag the curve handles directly.

### 7.2 LFO editor

For each LFO:

```
┌─ LFO1 ──────────────┐
│  ∿                  │
│       SINE   ▼      │  ← shape selector
│  RATE  0.05 Hz      │
│  ▤▤▤▤▤▤░░░░░░░░     │
│  SYNC  [free ▼]     │
└─────────────────────┘
```

`Sync` allows: `free`, `tempo_quarter`, `tempo_eighth`, `tempo_dotted_eighth`, etc.

## 8. Footer

Status strip across the bottom, ~32 px:

* **CPU usage** (engine only, not host).
* **Voice count** (active / max).
* **Reported latency** in samples and milliseconds.
* **Init / Save / Load** buttons (tertiary; the preset selector in the header is primary).
* **Advanced (`•••`) link** opens the Advanced parameter panel (§8a).

## 8a. Advanced parameter panel

The "no surprises" principle requires that every audio-affecting parameter is reachable from the GUI. Many such parameters (e.g., per-LFO `reset_on_note`, `agent.shape_distribution.*`, `harvester.orbit_shape`, `structural.deposit_kernel`) are not promoted onto the macro panel because they are configuration choices, not performance controls. They are reached via the **Advanced parameter panel**, opened from the footer:

```
┌─ ADVANCED PARAMETERS ───────────────────────────────────────────┐
│  Filter:  [search…]                          Show:  ☑ All   ▼   │
│                                                                  │
│  ▼ Substrate                                                     │
│      structural.deposit_kernel  [linear   ▼]                     │
│      structural.read_kernel     [linear   ▼]                     │
│      structural.substrate_size  [1024     ▼]                     │
│      substrate.laplacian_order  [5        ▼]   (2D only)         │
│                                                                  │
│  ▼ Agents                                                        │
│      agent.max_active                  [—————•———] 32            │
│      agent.harmonic_set                [{1, 2, 3, 4, 5, 6, 7, 8}]│
│      agent.shape_distribution.sine     [—————————•] 0.60         │
│      agent.shape_distribution.saw      [•———————————] 0.00       │
│      agent.shape_distribution.square   [•———————————] 0.00       │
│      agent.shape_distribution.fmpair   [——————•—————] 0.30       │
│      agent.shape_distribution.noise    [——•—————————] 0.10       │
│      agent.detune_scale                [•———————————] 0.00       │
│                                                                  │
│  ▼ LFOs                                                          │
│      lfo1.reset_on_note   [☐]   lfo2.reset_on_note   [☑]         │
│      lfo3.reset_on_note   [☐]   lfo4.reset_on_note   [☐]         │
│                                                                  │
│  ▼ Harvesters                                                    │
│      harvester.orbit_shape  [circle ▼]                           │
│                                                                  │
│  ▶ Reserved (read-only)                                          │
│  ▶ Engine preferences (see ⚙)                                    │
│                                                                  │
│  [Reset section]   [Reset all]                  [Close]          │
└──────────────────────────────────────────────────────────────────┘
```

Every parameter row in 09 §3 (the parameter inventory) appears here, with edit controls appropriate to its type (slider for floats, dropdown for enums, checkbox for booleans, multi-select for arrays). The reserved-fields fold-out shows fields that are persisted but not user-editable in v1.0 (e.g., `shared_substrate`, `substrate.nonlinear_beta`); these are read-only in v1.0 and become editable in later versions.

The Advanced panel is reached by a discreet "•••" link in the footer. New users never need it; advanced users use it routinely.

## 9. Preferences (`⚙`)

A modal dialog:

* **Sample rate**: read-only, shows host rate.
* **Oversample mode**: ECO (S=4), Standard (S=2), Premium (S=1). Default Standard.
* **Default channel layout**: `Stereo`, `Quad`, `5.1`, `7.1.4`, `Ambisonic 1st`. Used when host opens with no prior layout.
* **Visualiser refresh rate**: 30 Hz / 60 Hz / 120 Hz.
* **Theme**: `Dark`, `Light`, `High contrast`.
* **Polyphony cap**: 1, 2, 4, 8 voices.
* **Reset all preferences**.

## 10. Help (`?`)

In-app manual with three views:

* **Quick start**: 3 minutes from open to first sound.
* **Macro reference**: each macro with audio examples and explanation.
* **Theory**: plain-language explanation of the substrate, agents, harvesters — material from the original research doc, simplified.

The help view is reachable offline; it ships as static HTML embedded in the binary.

## 11. Visualisation contract (for engineering)

The substrate visualiser reads a snapshot from the engine via this interface:

```c++
struct SubstrateSnapshot {
    int     dimensions;            // 1 or 2
    int     width;                 // for 1D, length; for 2D, x dimension
    int     height;                // 1 for 1D
    float   data[MAX_SNAPSHOT];    // [-1, +1] normalised displacement
    uint64_t snapshotIndex;        // monotonically increasing
    int     activeAgentCount;
    AgentSnapshot agents[MAX_AGENTS_FOR_VIS];   // simplified, vis-friendly
    int     activeHarvesterCount;
    HarvesterSnapshot harvesters[MAX_HARVESTERS];
};

struct AgentSnapshot {
    float   position[2];   // 1D: only [0] meaningful
    float   amplitude;
    uint8_t shape;
};

struct HarvesterSnapshot {
    float   position[2];
    int     channelIndex;
};
```

The audio thread updates the snapshot at most 60 times per second (every 800 samples at 48 kHz, gated). It writes to one of three buffers (triple buffering); the GUI thread reads the most recent complete one. No locks; release-acquire memory ordering.

The data array is normalised so the GUI doesn't need to know the substrate's actual displacement scale — `[-1, +1]` suffices for visualisation.

## 12. GUI framework

Recommend [JUCE 8](https://juce.com/) for v1.0 GUI. Reasons:

* Mature, widely-supported on macOS/Windows/Linux.
* Built-in Component framework with hardware-accelerated OpenGL backend (necessary for the 2D heatmap at 60 Hz).
* Ships with a parameter system that maps cleanly to VST3 sample-accurate automation.
* Has a healthy ecosystem of UI component libraries (e.g., [foleys_gui_magic](https://github.com/ffAudio/foleys_gui_magic)) for designers who prefer visual layout tools.

Trade-offs vs. iPlug2 are discussed in 08.

## 13. Open questions

* **Touch/iPad version.** Conceptually possible — JUCE supports iOS — but not in v1.0 scope.
* **Hardware controller integration.** Native [Komplete Kontrol](https://www.native-instruments.com/en/products/komplete/keyboards/komplete-kontrol-s-series-mk3/) and [Push 3](https://www.ableton.com/en/push/) integrations are reserved.
* **Sound preview on hover.** Hovering a preset in the browser plays a 4-second snippet. Reserved for v1.1.
* **Theme system.** v1.0 ships dark by default with a light alternative. A full theme API for users to skin the synth is reserved.
