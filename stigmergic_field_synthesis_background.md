Prompt (Claude Code 4.7)

We've made several attempts to create a new form of synthesis to power a unique new VST3 soft synth, but with middling results at best. We're talking a genuinely new form of synthesis, not additive, subtractive, FM, granular, etc. As this is software, digital synthesis is an ideal starting point. Research the landscape of different forms of synthesis and derive a completely novel one. To help, the new type of synthesis would be ideally suited to producing ambient and experimental soundscapes. Ask as many questions as you need and deliver the research in a markdown document.

Research Summary

The proposed method is Stigmergic Field Synthesis: a population of audio-rate oscillator agents that both deposit their output into and are frequency-modulated by a shared continuous spatial substrate (1D ring or 2D toroidal grid) which itself evolves under a programmable wave-plus-diffusion equation. Audio is read at one or more fixed harvester points on the substrate, giving native multichannel output as a direct consequence of the synthesis rather than a downstream effect.
The novelty case rests on the compound primitive — agents-as-sources + shared continuous substrate + mutual coupling at audio rate + harvester readout. After surveying classical, physical, spectral, granular, and non-standard methods (including the closest neighbours: scanned synthesis, Madrona Labs Kaivo, Kuramoto coupled-oscillator networks, swarm-controlled granular, FDTD membranes, GENDY, and reaction-diffusion synthesis), each differs from SFS on at least two independent axes. The document includes the full DSP equations, stability/Courant analysis, a six-macro control surface (TENSION, DAMPING, DENSITY, MIGRATION, COHERENCE, EXCITATION), an explanation of how each of the four target sonic characters falls out of a different region of the same parameter space, a four-phase VST3 implementation roadmap with CPU budget, and brand-name candidates (STRATA, MURMUR, PALIMPSEST among them).
Sources:

Taxonomy of Digital Synthesis Techniques — CCRMA
https://ccrma.stanford.edu/~jos/kna/Taxonomy_Digital_Synthesis_Techniques.html

Scanned Synthesis — Verplank, Shaw, Mathews
https://ccrma.stanford.edu/~verplank/S2S/ScannedSynthesis.PDF

Stochastic Synthesis: Origins and Extensions — Luque
https://sonology.org/wp-content/uploads/2021/04/Luque-Stochastic_Synthesis-Origins_and_Extensions.pdf

Kuroscillator: Coupled-Oscillator Networks for Sound Synthesis
https://nime.org/proceedings/2024/nime2024_60.pdf

Phase Distortion Synthesis — Wikipedia
https://en.wikipedia.org/wiki/Phase_distortion_synthesis

Vector Synthesis — Wikipedia
https://en.wikipedia.org/wiki/Vector_synthesis

Real-Time Corpus-Based Concatenative Synthesis with CataRT
https://www.dafx.de/paper-archive/2006/papers/p_279.pdf

Cellular Automata Music — Springer
https://link.springer.com/chapter/10.1007/978-1-84628-600-1_8

Discretizing the 2D Wave Equation
https://vanhunteradams.com/DE1/Drum/Discretization.html

Modal Synthesis for Vibrating Objects
http://www.persianney.com/kvdoelcsubc/publications/modalpaper.pdf

Madrona Labs Kaivo
https://madronalabs.com/products/kaivo

Courant–Friedrichs–Lewy condition
https://en.wikipedia.org/wiki/Courant%E2%80%93Friedrichs%E2%80%93Lewy_condition