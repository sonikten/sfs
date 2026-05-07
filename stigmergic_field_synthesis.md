# Stigmergic Field Synthesis

A novel synthesis paradigm for ambient and experimental soundscapes

---

> **Status note (2026-05-01).** This is the **conceptual research document** that established the synthesis paradigm, its novelty argument against prior art, and the four sonic-character corners the engine targets. It is the canonical source for *why* SFS exists and *how it differs from* its closest neighbours.
>
> Implementation-facing readers should consult the v1.0 specification in [`sfs-spec/`](sfs-spec/00_overview.md) for the engine and plug-in build. Where this document and the v1.0 spec disagree on implementation choices — for example, this document discusses shared-substrate as a possibility (§4.1) and lists Möbius/Klein topologies (§4.2.4) and "later phases including ambisonic output" (§7) — **the v1.0 spec is canonical**. Specifically:
>
> * v1.0 uses **per-voice** substrate instancing; the shared-substrate flag is reserved but not exposed (spec 01 §2).
> * v1.0 ships the **ring (1D)** and **torus (2D)** topologies only; Möbius and Klein are deferred (spec 02 §4).
> * v1.0 ships **horizontal-plane first-order ambisonic** (Z = 0 because the substrate is at most 2D); higher-order is deferred (spec 04 §3.6).
> * v1.0's CPU budget for the default polyphony is more conservative than the rough estimates in §7 of this document; see spec 03 §9 and 08 §2.5.
>
> The novelty analysis in §6 of this document, the engine concept, and the macro design philosophy are unchanged.

---

## 1. Executive summary

After surveying the landscape of digital audio synthesis — classical (subtractive, additive, FM, AM, wavetable, phase distortion, vector), physical (waveguide, modal, scanned, FDTD membrane), spectral (phase vocoder, cross-synthesis, additive resynthesis), granular (granular, FOF/VOSIM, waveset, corpus-based concatenative), and the broader family of "non-standard" methods (Xenakis GENDY, cellular automata, reaction-diffusion, coupled-oscillator/Kuramoto networks, swarm/boids, feedback-delay networks, L-systems, scanned chaotic attractors) — there is a clearly underexplored axis in the design space: **a continuous spatial substrate that simultaneously hosts both the audio output and a population of audio-rate oscillator agents that read from and write to that substrate, coupled only indirectly through the field they jointly disturb.**

This document proposes that engine. It is named **Stigmergic Field Synthesis (SFS)** after the entomological notion of stigmergy — coordination via traces left in a shared environment. The core primitive is *neither* an oscillator (additive/FM), *nor* a stored waveform (wavetable/granular), *nor* a physical body (modal/waveguide), *nor* a scanned dynamic surface (Verplank/Mathews). It is the **field-mediated coupling of audio-rate agents through a parameterizable substrate** — a primitive that, as far as a careful prior-art search reveals, no commercial or research synth has been built around.

SFS is purely DSP, deterministic-with-seeded-stochasticity, multichannel-native by construction (each output channel is a different listening point on the substrate), and produces all four target sonic characters — slowly evolving drones, organic breathing textures, pitched playable tones, and emergent glitchy artifacts — from a single coherent engine controlled by a small set of macros that map onto a keyboard interface.

The remainder of this document covers: (2) the synthesis landscape and where SFS sits inside it; (3) the novelty gap and design constraints; (4) the SFS engine in implementation-level detail; (5) why it produces the four target characters; (6) defensibility against the closest prior art; (7) implementation roadmap for a VST3; (8) naming and brand options.

---

## 2. The synthesis landscape

### 2.1 Established families

The following table summarises the methods that define the current solution space, what their *generative primitive* is, and what they cannot easily do.

| Family | Method | Generative primitive | Sonic strength | Limitation for ambient |
|---|---|---|---|---|
| Classical | Additive | Sum of sinusoids | Precise spectral control | Static unless heavily automated |
| Classical | Subtractive | Filtered rich source | Punchy, expressive | Depends entirely on filter motion |
| Classical | FM / PM | Carrier × modulator phase | Metallic, complex | Brittle; lacks continuous "life" |
| Classical | AM / RM | Amplitude product | Bell-like sidebands | Narrow timbral range |
| Classical | Wavetable | Indexed lookup of stored cycles | Strong morphing | Tied to authored content |
| Classical | Phase distortion (Casio CZ) | Bent phase index into sine | FM-like without the patent | Same brittleness family |
| Classical | Vector (Prophet VS) | Crossfade of 4 wavetables | 2D timbre joystick | Still bounded by the 4 corner sounds |
| Physical | Waveguide (Karplus–Strong, Smith) | Bidirectional delay line + filter | Plucked / blown realism | Designed around real instruments |
| Physical | Modal | Bank of damped resonators | Bells, plates, sympathetic strings | Excited by external source; static modes |
| Physical | Scanned (Verplank/Mathews) | Sub-audio dynamic mass-spring chain, scanned | Haptic, evolving | One surface, externally driven |
| Physical | FDTD membrane / plate | Discretized 2D wave equation | Realistic drum/plate | Heavy CPU; physical-instrument-shaped |
| Spectral | Phase vocoder | FFT bin manipulation | Time-stretch, freeze | Source-bound; pre-recorded material |
| Spectral | Cross-synthesis / morphing | One spectrum modulates another | Hybrid timbres | Source-bound |
| Spectral | Additive resynthesis | Tracked partials + noise residual | Smooth morphing | Source-bound |
| Granular | Granular | Short windowed grains from sample | Cloud textures | Source-bound |
| Granular | FOF / VOSIM | Pitch-synchronous formant grains | Vocal, formant | Mostly speech-shaped |
| Granular | Waveset (Wishart) | Segments between zero crossings | Aggressive timbral mutation | Source-bound |
| Granular | Concatenative (CataRT) | Database units selected by descriptor | Corpus exploration | Library-bound |
| Non-standard | GENDY (Xenakis) | Random walks on polygonal-line breakpoints | Stochastic, alien | Single waveform; not spatial |
| Non-standard | Cellular Automata (Chaosynth) | CA cells map to grain or spectrum | Emergent, glitchy | Discrete; CA → control, not output |
| Non-standard | Reaction-diffusion | Gray-Scott / BZ field → spectrogram | Pattern-driven | Mostly offline / spectrogram render |
| Non-standard | Coupled oscillators (Kuramoto) | Phase-locked oscillator network | Synchronisation phenomena | Direct coupling; no spatial field |
| Non-standard | Swarm / boids | Agents drive granular params | Living, organic motion | Agents are *controllers*, not *sources* |
| Non-standard | FDN (as synth) | Feedback delay network with mixing matrix | Chaotic limit cycles | Topology is a directed graph, not a field |
| Non-standard | Self-oscillating filter | Filter pushed past unity Q | Tonal "ring" | Single-mode, fragile |
| Non-standard | L-systems / Holtzman | Symbol-rewriting → samples | Fractal sequences | Mostly compositional, not synthesis-core |

### 2.2 What every existing method shares — and the gap that opens

Across this table, two structural patterns dominate:

The first is **summation-based output**: the output is a weighted sum of independent signal contributions (additive, granular, modal, FDN feedback taps, vector crossfade). Coupling between contributors is either zero (additive, granular) or explicit pairwise (FM, Kuramoto, FDN mixing matrix).

The second is **scanned-output**: the output is read from a state that lives in a different rate domain (scanned synthesis reads the mass-spring chain at audio rate; CA-based methods sample CA state to drive grains; FDTD reads a node of the discretized membrane).

**SFS combines and inverts both.** Output is *read from a continuous spatial substrate*, which is *mutually written by many audio-rate agents* whose own behaviour is *modulated by what they read from that same substrate*. The substrate has its own audio-rate (or near-audio-rate) propagation dynamics. No existing method, in commercial or research literature, structures a complete synthesizer engine around this exact primitive.

---

## 3. Novelty gap and design constraints

### 3.1 What you have already tried

The user has tested and rejected:

- **Pure mathematical-dynamics modelling (e.g., torus dynamics).** The dynamics-as-oscillator approach gave middling results — likely because dynamical-system trajectories on simple manifolds, by themselves, lack the *spatial* and *additive* dimensions that human ears parse as "rich."
- **Pure analog modelling.** Computationally infeasible at scale; redundant given existing virtual-analog products.

These are both exclusions of the *dynamics-only* and *circuit-emulation* corners of the space. Both share the limitation that they treat the engine as a single self-contained system whose timbre is a function of internal state alone. SFS deliberately exits that corner by introducing **populations** (many simple agents) and **a shared field** (continuous space).

### 3.2 The constraints SFS is engineered to satisfy

| Constraint | How SFS honours it |
|---|---|
| Pure DSP, no ML | Agents and substrate are deterministic difference equations |
| Generous CPU budget | Substrate is FFT-friendly; can run at audio rate or downsampled |
| Stereo / multichannel native | Each output channel is a different listening point on the substrate |
| Seeded stochasticity | A 64-bit seed initialises agent positions, phases, perturbations; reload is bit-exact |
| Keyboard + macros | MIDI note pitches the dominant agent cohort; ~6 macros control substrate physics |
| Drones, organic textures, pitched, glitchy | All four emerge from different points in the same parameter space (Section 5) |
| Patentable / defensibly novel | The "field-mediated mutual coupling at audio rate" claim is, on the searches conducted, unclaimed |

---

## 4. The SFS engine

### 4.1 Architectural overview

The engine consists of three layers:

1. **The substrate** — a 1D ring or 2D toroidal grid of sample-valued cells, governed by a parameterised wave-plus-diffusion equation. The substrate is updated every audio sample (or every K samples, with K=1 the default).
2. **The agent population** — a small swarm of audio-rate "voice agents," each of which occupies a continuously-valued position on the substrate, has its own oscillator state, deposits its instantaneous output into the substrate at its position, and is in turn modulated by the value of the substrate at its position.
3. **The harvesters** — fixed or slowly-moving "listening points" on the substrate. Each output channel of the plug-in (L, R, surround channels, ambisonic feeds) is the substrate value sampled at one harvester position.

Crucially, none of the three layers, taken alone, is the "instrument." The instrument is the *coupled* system. Pull out any one layer and you get either a flat field, a parallel additive oscillator bank, or silence.

### 4.2 The substrate

#### 4.2.1 State

For the 1D variant (most CPU-efficient, default for stereo):

> u[n+1, x] = update of substrate value u at cell x and step n+1

The substrate carries two real-valued buffers: displacement `u` and velocity `v`, of length `N` (a power of two, typically 256–4096). Cells wrap (ring topology).

#### 4.2.2 Update rule

The substrate evolves under a discrete wave-plus-diffusion-plus-loss equation:

```
v[n+1, x] = v[n, x]
          + c² · ( u[n, x-1] - 2·u[n, x] + u[n, x+1] )      // wave
          + κ  · ( v[n, x-1] - 2·v[n, x] + v[n, x+1] )      // diffusion
          - γ  · v[n, x]                                    // loss
          + Σ_i δ(x - p_i) · w_i · y_i[n]                   // agent injection

u[n+1, x] = u[n, x] + v[n+1, x]
```

where:
- `c²` is the squared wave-speed (substrate **TENSION**)
- `κ` is the velocity-diffusion coefficient (substrate **VISCOSITY**)
- `γ` is the energy-loss coefficient (substrate **DAMPING**)
- `p_i`, `w_i`, `y_i[n]` are agent *i*'s position, deposit weight, and current oscillator output
- `δ(·)` is replaced in practice by a 2- or 4-tap fractional-position spread (Lagrange or windowed-sinc), so agents at non-integer positions deposit smoothly

The 2D variant replaces the 1D Laplacian with the standard 5-point or 9-point 2D Laplacian on a torus.

This is **not** physical modelling of an instrument. The substrate has no acoustic-impedance interpretation — `c²` can range freely from "very slow" (the substrate behaves like a lazy molasses) to "explosive" (signal reaches the other side of the substrate inside one audio sample). It is a **synthesis substrate**, tunable beyond what any real material allows.

#### 4.2.4 Stability and aliasing

The wave term of the substrate update is a standard explicit second-order finite-difference scheme and is bound by the Courant–Friedrichs–Lewy condition: `c² · Δt² / Δx² ≤ 1`, where `Δt = 1/fs` and `Δx` is the cell spacing (taken as 1 in the equations above). Because `c²` is user-exposed via the `TENSION` macro, the macro's range must be clamped internally so the worst-case product stays below the Courant bound; this is a fixed engineering constraint, not a runtime check. Going beyond the bound causes the substrate to diverge audibly within a few samples — a useful informal signal during development that the constraint is active.

The diffusion term `κ` is unconditionally stable under explicit Euler for `κ ≤ 0.5`. In practice `κ` lives well below this and provides free high-frequency rolloff at the substrate update step, suppressing imaging from agent waveforms at moderate-to-high settings. At low `κ`, agents must contribute bandlimited signals — sine waves are safe by construction; saw, square, and noise contributions must be antialiased (BLEP, polyBLEP, or oversampled generation) before being deposited into the substrate. The fractional-position deposit kernel (a 4-tap windowed-sinc or a simple linear-interpolation kernel) further softens deposit imaging.

A unit-norm DC blocker on the substrate displacement buffer prevents long-term drift accumulating from asymmetric deposit weights. The loss term `γ` provides additional protection: at any non-zero `γ` the system is dissipative and cannot accumulate energy unboundedly, which together with the Courant-bounded wave term gives provable BIBO stability of the substrate against any bounded agent input.

#### 4.2.3 Why this substrate equation specifically

The wave term gives the substrate the ability to *propagate* an injected signal — agents far apart still influence one another, with a delay set by `c²`. The diffusion term softens hard edges and prevents aliasing under sharp injection. The loss term keeps the substrate stable under arbitrarily many agents and provides natural decay. Because `γ`, `κ`, `c²` are independent macros, the user can drive the substrate continuously between four limit behaviours:

- **Wave-dominant (`c²` high, `γ` low):** the substrate rings. Agent contributions echo around the ring as standing waves.
- **Diffusive (`κ` high):** agent contributions blur into each other; the substrate behaves like a smearing reverb.
- **Lossy (`γ` high):** the substrate damps out almost instantly. Output approaches a direct sum of agents with light spatial-aware weighting.
- **Critical (`c²` ≈ critical, `γ` low, `κ` low):** the substrate is on the edge of self-oscillation. Tiny agent injections trigger blooming responses. This is where the engine is most alive and most chaotic.

### 4.3 The agent population

Each agent *i* in the population is a small state machine with:

| Field | Role |
|---|---|
| `p_i` | Position on the substrate (continuous, wraps) |
| `f_i` | Base frequency (Hz) |
| `φ_i` | Phase accumulator |
| `a_i` | Amplitude (envelope-modulated) |
| `m_i` | Modulation sensitivity (how strongly substrate value bends the agent) |
| `s_i` | Self-shape (sine, sawtooth, square, FM-pair, noise — chosen at seed) |
| `r_i` | Drift rate (how fast `p_i` migrates) |
| `e_i` | Envelope state (attack-decay-sustain-release per agent) |

At each audio sample the agent computes:

```
y_i[n] = a_i · e_i[n] · waveform( s_i, φ_i )

Δφ_i   = 2π · ( f_i + m_i · u[n, p_i] ) / fs

φ_i    += Δφ_i
p_i    += r_i + ε_i[n]               // ε_i is seeded Gaussian noise, optional
```

Two crucial features:

- The agent's **frequency** is bent by the substrate value at its position. This is what indirectly couples agents: if agent A's deposit raises the substrate near agent B, B's pitch warps. With many agents this creates the "breathing" character — the colony settles into transient consensus pitches and then drifts.
- The agent **migrates** across the substrate at rate `r_i`. Migration combined with substrate wave-propagation creates Doppler-like inflections and spatial counterpoint that no fixed-position oscillator bank produces.

A typical patch uses 8–64 agents per voice. Each agent is cheap (< 30 multiply-adds per sample), so the dominant cost is the substrate update.

### 4.4 The harvesters

Output is harvested by sampling the substrate at one or more fractional positions:

```
out_ch[n] = u[n, h_ch]    for each output channel ch
```

For stereo, two harvesters at distinct positions on the substrate produce a correlated-but-distinct L/R pair *as a direct consequence of the synthesis*, not as a downstream stereo effect. For surround / ambisonics, additional harvesters are placed around the substrate. Because the substrate has wave-propagation, harvesters at different positions hear different mixes of the same agents at different micro-delays — i.e., natural inter-channel decorrelation that already encodes spatial cues.

A small `harvester orbit` LFO can move harvester positions slowly on the substrate, producing slow stereo motion that even fixed agents cannot produce on their own.

### 4.5 The macro control surface

The engine exposes a small set of macro controls. Each macro is a smoothed continuous value that fans out internally to several engine parameters according to a designer-tuned curve. The proposed macro set (six is the sweet spot for a hardware-feeling soft synth):

| Macro | Internal mapping |
|---|---|
| **TENSION** | `c²` of the substrate (wave-speed) |
| **DAMPING** | `γ` (and partly `κ`) — substrate energy dissipation |
| **DENSITY** | Number of active agents and their per-agent injection weight |
| **MIGRATION** | Agents' drift rate `r_i` and migration-noise scale |
| **COHERENCE** | How tightly agent frequencies `f_i` are locked to integer / just-intonation ratios of the played note. At max, the colony is harmonically locked; at min, agents detune freely. |
| **EXCITATION** | Agents' modulation sensitivity `m_i` to substrate. At zero the engine is a parallel additive bank. At max the agents are highly bent by the substrate, producing chaotic / glitchy regimes. |

Two further macros that round out the front panel:

- **TOPOLOGY** (selector, not continuous) — 1D ring, 2D torus, 2D Möbius, 2D Klein. Higher-genus surfaces produce subtly different mode spectra.
- **ASPECT** (continuous) — substrate length / aspect ratio. Effectively the "size of the room" the agents are in. Independent of `TENSION`, this changes the ring's natural mode set.

A keyboard player's interaction model is then: play notes (the played note pitches the dominant cohort of agents according to `COHERENCE`); ride a few macros to morph the substrate physics. Modulation can be assigned to macros from a built-in slow-LFO bank (random-walk LFOs that themselves use the seeded RNG, so a recalled preset is bit-exact reproducible).

### 4.6 Seeded stochasticity

The engine carries a single 64-bit `seed`. From it, every stochastic decision deterministically derives:

- Initial agent positions, phases, and waveform-shape choices
- Agent migration noise (a per-agent counter-based RNG stream — splittable, parallel-friendly)
- Substrate "ignition" perturbation at voice-on (a tiny noise burst into the substrate so it doesn't start at exact zero)
- LFO random-walk paths

Because the RNG is counter-based and split per-agent, multi-core voice rendering produces bit-identical output regardless of how many threads run. Recall a preset → recall the seed → the patch sounds identical.

The user can audition different seeds with a single dice button, and lock one in. This converts "happy accidents" into shareable presets without sacrificing reproducibility.

---

## 5. Why SFS produces the four target sonic characters

The user requested an engine equally good at slow drones, organic breathing textures, pitched playable tones, and glitchy artifacts. Each comes out of a different region of the same parameter space.

### 5.1 Slow evolving drones

Set `DAMPING` low, `TENSION` moderate-high, `DENSITY` moderate, `MIGRATION` low, `COHERENCE` high, `EXCITATION` low-to-moderate. The substrate rings continuously, agents are harmonically locked, and the few that migrate slowly cause harmonic emphasis to drift around the ring. Harvester orbit at 0.05 Hz creates barely-perceptible stereo wandering. The resulting drone is harmonically stable but never repeats: a quintessential ambient pad with internal motion.

### 5.2 Organic, breathing textures

Raise `MIGRATION` and `EXCITATION`. Agents now migrate noticeably and are bent by the substrate around them. The colony settles into transient pitch consensuses for 1–3 seconds, then breaks and re-forms. Lower `COHERENCE` slightly so agents detune freely. The texture sounds biological — like wind in a forest, like breathing, like a colony of small bells — without ever becoming purely random.

### 5.3 Pitched, playable tones

Raise `COHERENCE` to maximum, set `MIGRATION` low, `DAMPING` moderate, `EXCITATION` low. MIDI note → fundamental of the dominant cohort; the harmonics of that note dominate the substrate. The instrument plays as a complex but pitched soft synth — recognisable note-to-note, with sustained notes still gaining motion from substrate ringing. The release tail (`DAMPING` + substrate energy) is long and natural.

### 5.4 Glitchy, unpredictable artifacts

Push `EXCITATION` toward max, drop `DAMPING` to near zero, and put `TENSION` near the critical value where the substrate is on the edge of self-oscillation. The substrate's response to agent injection is now strongly nonlinear — small input causes blooming pattern formation, sudden harmonic shifts, and audible "events." The seeded RNG ensures these events are reproducible; the modulation sensitivity ensures they emerge organically, not as scripted clicks. This is the "experimental" corner of the engine.

A single SFS patch can be modulated *between* these four corners on a slow envelope — the same engine can drone for a minute, breathe organically for thirty seconds, accept a melodic phrase, and erupt into a glitch passage, without changing instruments.

---

## 6. Defensibility against the closest prior art

This is not a legal opinion, but a careful comparison against the closest published or shipping techniques.

### 6.1 Versus scanned synthesis (Verplank, Shaw, Mathews, 1998–1999)

Scanned synthesis uses a *single* sub-audio dynamic system (a mass-spring chain at sub-15-Hz) that is *scanned* periodically at audio rate to produce the output. The performer applies haptic forces to the chain.

SFS differs on three independent axes: (a) the substrate is not scanned — output is read at fixed harvester points, with substrate dynamics running at audio rate; (b) the substrate is acted upon by *many audio-rate agent oscillators* simultaneously, not by external haptic forcing alone; (c) agents are *modulated by the substrate at their own positions*, creating closed-loop coupling that scanned synthesis does not contain. The mechanisms are not subsets of one another.

### 6.2 Versus modal synthesis with sympathetic coupling

Modal synthesis is a parallel bank of damped resonators, optionally cross-coupled to model sympathetic strings. The resonator parameters are static; the coupling is direct (a small mixing matrix between resonator outputs).

SFS does not use a fixed modal decomposition. The substrate's modes emerge from the topology and tension parameters and shift continuously when those macros move. Agents are not resonators — they are forced oscillators with their own frequency clocks, only loosely "owned" by the substrate.

### 6.3 Versus FDTD membrane / plate synthesis

FDTD methods discretise the 2D wave equation to model real instrument bodies (drum heads, plates). The model is *driven by an external strike or excitation* and produces audio by reading membrane displacement at one node.

SFS uses the same family of difference equations but: (a) is driven continuously from within by audio-rate agents, not externally by a strike; (b) the agents are part of the synthesizer, not part of an "instrument being modeled"; (c) agents are themselves modulated by the substrate, which FDTD instrument models do not do. SFS is not "physical modelling of a drum" — the drum metaphor would be a drum whose air molecules each sing.

### 6.4 Versus coupled-oscillator networks (Kuramoto, Kuroscillator)

Kuramoto-style networks couple oscillators *directly* through phase-difference terms. Coupling is pairwise and described by a coupling matrix; there is no spatial substrate.

SFS couples agents *indirectly*, only through what they jointly write into and read from a shared continuous field. This indirection produces propagation delays (because the substrate has wave-speed), spatial structure (the field has locations), and emergent pattern formation (the substrate can support standing waves and reaction-diffusion-like blooms). None of these is present in a Kuramoto coupling matrix.

### 6.5 Versus Madrona Labs Kaivo (the closest commercial neighbour)

Kaivo pairs a granular sampler (the source) with a 2D FDTD physical-model resonator (the body). Sources feed the resonator unidirectionally; the resonator does not modulate the granular sources. The resonator is a body simulation — a virtual instrument-shaped object — and the user picks among modeled bodies (drum, plate, etc.).

SFS differs on three independent claims: (a) sources and substrate are mutually coupled — the substrate value at an agent's position bends that agent's frequency, closing the loop that Kaivo leaves open; (b) the substrate is not a body model — it has no acoustic interpretation, no preset "drum" or "plate," and ranges into regimes (near-critical wave-speed, vanishing damping) that no real material occupies; (c) agents migrate continuously across the substrate, while Kaivo's grain "strike points" are essentially static excitation locations chosen per note. Kaivo is the strongest commercial neighbour and a useful comparable for marketing, but the underlying mechanism is sufficiently different that SFS is not a re-implementation of it.

### 6.6 Versus swarm-controlled granular (Blackwell, ToneCarver Boids)

Swarm-controlled granular synths use boids agents to control granular synthesis *parameters* — agents are controllers, not signal sources. The audio is still granular synthesis of recorded samples.

SFS agents *are* the signal sources. There is no recorded sample. The agents' state writes directly into the audio-domain substrate. The systems are categorically different.

### 6.7 Versus Xenakis GENDY / dynamic stochastic synthesis

GENDY constructs a *single* polygonal waveform whose breakpoints undergo random walks. There is one waveform, no spatial substrate, no agents.

SFS has many agents on a shared field; stochasticity in SFS is per-agent migration noise and an optional substrate ignition burst, both seeded. The two algorithms share only the abstract notion that "controlled randomness can be musical."

### 6.8 Versus reaction-diffusion / cellular automata synthesis

These map a CA or Gray-Scott field's evolution to a spectrogram or to grain triggers. The field is *the only source of state*; there are no oscillators living on it.

SFS does the inverse: the field is a *medium*, and the *oscillators are the source*. The substrate's role is to couple them.

### 6.9 Patent-risk assessment summary

A precise patent claim would centre on: *a digital synthesis method in which a population of audio-rate oscillator agents simultaneously deposit their output into and are frequency-modulated by a continuous spatial substrate that itself evolves under a programmable wave-and-diffusion equation, with audio output read at one or more listening points on said substrate.* That precise compound — agents + substrate + mutual coupling at audio rate + readout at fixed harvesters — appears clear of the prior art reviewed.

A patent attorney should still run a formal prior-art search, particularly across Yamaha, Roland, Korg, Madrona Labs, U&I, and Native Instruments patent portfolios, and examine Stanford CCRMA and IRCAM publications from 2005 onward. The closest hits to be vigilant about are any patent extension of scanned synthesis that adds agent populations, and any Madrona Labs filings that might cover bidirectional grain/resonator coupling.

---

## 7. Implementation roadmap (VST3)

A four-phase build is recommended.

### Phase 1 — Reference engine (1D, mono, single voice)

Implement the substrate as a 1D ring of 1024 cells, agent population of 16 simple sine-wave agents, single harvester. No GUI; render to disk. Validate: drones, basic pitched playback, frequency response, stability under macro sweeps, bit-exact reproducibility under fixed seed. Establish CPU baseline.

This phase confirms the engine works at all and that the macros have the intended sonic effect. It is essentially a test fixture, not a product.

### Phase 2 — Polyphonic, stereo, multi-waveform agents

Add per-voice substrate instances (or a shared substrate with per-voice agent partitions, depending on character). Add waveform variety (sine, saw, square, noise, FM-pair). Add the second harvester for native stereo. Build the seeded RNG pipeline. Wire MIDI note-on to dominant-cohort frequency.

This is the first build where a musician could play it as an instrument.

### Phase 3 — 2D substrate, multichannel output, full macro panel

Implement the 2D toroidal substrate with the 5-point Laplacian. Add topology selector (1D ring, 2D torus, 2D Möbius — Möbius is implemented by sign-flipping at one boundary of the torus). Add multichannel output with 4–8 harvesters laid out around the substrate. Build the macro panel (TENSION / DAMPING / DENSITY / MIGRATION / COHERENCE / EXCITATION / TOPOLOGY / ASPECT).

This is the first build that justifies the brand.

### Phase 4 — Preset system, modulation matrix, polish

Implement the preset format around the seed. Build a small modulation matrix (slow LFO bank, envelope followers, MIDI CC routing) for live performance control of macros. Add visualisation of the substrate (a thin animated strip / 2D heatmap) — this is enormous for product feel; the synth becomes a visual instrument the way Razor or Phase Plant are.

Optional later phases: ambisonic output, host-tempo-synced harvester orbits, an "external excitation" input that lets the user drive the substrate from sidechain audio (turning the synth into a transformer for any input).

### Performance budget

For a 1D substrate of 1024 cells with 32 agents:

- Substrate update: 1024 × 5 ops = ~5K ops/sample → 220 MFLOPS at 44.1 kHz
- Agent update: 32 × ~30 ops = ~1K ops/sample → 44 MFLOPS
- Harvesters: trivial

Total ~265 MFLOPS for one voice. Eight voices on one core is well within a modern desktop CPU. SIMD vectorisation of the substrate update brings this down by 4–8×. The 2D variant (256×256 = 65K cells) is roughly 64× heavier and may want to run at 22 kHz or with a downsampled substrate clock for polyphony, with full audio rate for solo voices.

### Testing

- **Stability**: assert substrate `‖u‖∞` stays bounded under all macro positions and any agent count up to a maximum. Add per-sample DC blocker on the substrate to eliminate long-term drift.
- **Bit-exact reproducibility**: render the same preset twice and `diff` the output buffers.
- **Aliasing**: Bandlimit each agent's waveform; the substrate's diffusion term gives a free LPF at high `κ` but cannot be relied on at low `κ`.
- **Subjective**: build presets in each of the four sonic-character corners and confirm a single patch can morph between them via macro automation.

---

## 8. Naming and brand

A patentable engine deserves a memorable instrument. Stigmergic Field Synthesis is the technical name; the product needs a shorter brand. Some candidates that fit the "field of agents writing into a shared substrate" metaphor:

- **STRATA** — speaks to layering, deposition, traces left in a medium.
- **PALIMPSEST** — successive markings on a shared surface; literary, evocative, niche.
- **ALLUVIUM** — what the agents leave behind in the substrate; geological calm.
- **MURMUR** — short, the evocation of a colony of small voices coordinating without a leader.
- **CONFLUENCE** — many streams meeting; clean, available in many spellings.
- **SEDIMENT** — direct, slightly grim, fits ambient.
- **HALOPHYTE** — plants that thrive in salt; weird-organic.
- **EIDETIC** — vivid memory traces; cerebral.

If a one-word verb-noun is preferred for the user-facing identity, **STRATA** and **MURMUR** are the most marketable; **PALIMPSEST** is the most distinctive but less easy to say. The technical paper title can remain *Stigmergic Field Synthesis*.

---

## 9. Closing notes

The core bet of this design is that *indirect, field-mediated coupling between many simple agents* has been left underexplored not because it doesn't work, but because the existing synthesis traditions — instrument modelling, additive/subtractive, granular reuse of recorded material — each have their own internal momentum. Scanned synthesis came closest, but stopped at one externally-driven surface and did not let the surface itself drive a population. SFS finishes that move.

If the engine works as designed (and the parameter behaviour predicted in Section 5 follows from the equations in Section 4), the result is a single coherent paradigm — not a recombination of three other paradigms — that produces ambient drones, organic textures, pitched tones, and glitch all from one set of macros, with native multichannel output, full preset reproducibility, and a clean novelty story for both patent counsel and the press release.
