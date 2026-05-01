# SFS v1.0 — Technical Specification

Stigmergic Field Synthesis: complete v1.0 engine specification

---

## How to read this specification

This is a multi-document specification for the v1.0 release of the Stigmergic Field Synthesis (SFS) engine and its corresponding VST3 instrument plug-in. It assumes the reader is familiar with the [original research document](../stigmergic_field_synthesis.md), which establishes the synthesis paradigm itself, its novelty argument, and the four sonic-character corners the engine targets.

The specification is split across nine documents because the v1.0 surface is too large for a single readable file. Each document is independently buildable into a milestone — i.e., a DSP engineer can implement document 02 without depending on the GUI document 07 having stabilised, and vice versa.

| # | Document | Primary audience | Build dependency |
|---|---|---|---|
| 00 | This overview | Everyone | — |
| 01 | [Engine architecture](01_engine_architecture.md) | DSP engineer | Foundation |
| 02 | [Substrate DSP](02_substrate_dsp.md) | DSP engineer | Needs 01 |
| 03 | [Agent DSP](03_agent_dsp.md) | DSP engineer | Needs 01, 02 |
| 04 | [Harvester and output stage](04_harvester_output.md) | DSP engineer | Needs 02 |
| 05 | [Macros and modulation matrix](05_macros_modulation.md) | DSP + GUI | Needs 01–04 |
| 06 | [RNG and preset format](06_rng_presets.md) | DSP + GUI | Needs 01, 05 |
| 07 | [GUI and visualization](07_gui_visualization.md) | GUI/product | Needs 05, 06 |
| 08 | [Implementation roadmap and tests](08_implementation_roadmap.md) | DSP + project lead | Needs all above |
| 09 | [Glossary and parameter reference](09_glossary_and_parameters.md) | Everyone | Reference |

A symbol or term highlighted in **bold-italic** is defined in document 09. A parameter named in `code font` corresponds 1:1 to a parameter ID in document 09's parameter table.

## What v1.0 is, and is not

v1.0 ships with:

* A 1D ring substrate (default) and a 2D toroidal substrate (high-CPU mode).
* Up to 8-voice polyphony with per-voice substrate instancing.
* Up to 64 agents per voice, with five agent waveform types.
* Six primary macros (TENSION, DAMPING, DENSITY, MIGRATION, COHERENCE, EXCITATION) plus two structural selectors (TOPOLOGY, ASPECT).
* A 16-slot modulation matrix with 12 source types and 24 destination targets.
* Native stereo, quad, 5.1, and 7.1.4 multichannel layouts; first-order ambisonic output.
* A counter-based seeded RNG with bit-exact preset reproducibility across platforms.
* A preset format with versioning and a `factory_palette` of 128 presets covering all four sonic-character corners.
* A live substrate visualiser (1D strip and 2D heatmap modes).
* Sample-accurate parameter automation per VST3.
* MPE Note Expression input as a first-class modulation source.

v1.0 explicitly does **not** include:

* The Möbius and Klein substrate topologies (deferred to v1.1; the macro selector reserves the values, but they fall back to torus).
* Higher-order ambisonics beyond first order (deferred to v1.2).
* External audio input as substrate excitation (deferred to v2.0; the architecture keeps the seam open).
* AU and AAX wrappers (deferred to v1.3; the engine is plug-in-format-agnostic but only the VST3 wrapper ships at v1.0).
* User-extensible agent waveform plugins (deferred to v2.0).

## Versioning and compatibility commitment

The specification follows semver. **v1.0** establishes the wire format for presets and the audio behaviour of every macro. The commitment for v1.x:

* Preset files written by v1.0 will load identically in v1.x for all x.
* Audio output for a fixed preset on a fixed buffer-size and sample-rate will be bit-identical across v1.x patch releases. v1.x **minor** releases may add macros or modulation slots that, when zero or default, do not perturb the v1.0 audio.
* v2.0 may break preset format if external excitation requires it; a migrator will ship.

A SHA-256 hash of every shipping preset is generated at build time and stored next to the preset; the test suite verifies that the rendered audio for that preset matches a stored 30-second reference WAV (see document 08).

## The four sonic-character corners as a contract

The engine must, at v1.0 release, ship a factory preset for each of the four corners that is both subjectively recognisable and measurably distinguishable. Operationally:

| Corner | Operational test (document 08) |
|---|---|
| Slow evolving drone | Spectral centroid varies < 5% over a 60-second sustain. RMS variance < 1 dB. |
| Organic, breathing | Spectral centroid 1-octave wandering at 0.1–0.5 Hz; partial tracking shows non-stationary inharmonicity. |
| Pitched, playable | Fundamental detectable with > 0.95 voicing confidence under YIN at all keys C2–C7. |
| Glitchy, unpredictable | Onset-detection function shows ≥ 1 sub-100 ms transient per second; texture remains within −12 dBFS RMS bounds. |

These are not mere benchmarks — they are the contract that any v1.0 candidate build must satisfy before being green-lit to release. Document 08 defines the test rig.

## Glossary pointer

The most important terms used throughout: **substrate**, **agent**, **harvester**, **deposit**, **migration**, **coherence**, **excitation**, **ignition**, **cohort**, **listening point**. All are defined formally in [document 09](09_glossary_and_parameters.md).

## Ownership and review

The DSP, agent-pool, and harvester subsystems are owned by the DSP-engineer track. The GUI, preset, and visualisation work is owned by the product/GUI track. Macros and modulation matrix are jointly owned and require sign-off from both. The roadmap in document 08 lays out which milestones gate which.
