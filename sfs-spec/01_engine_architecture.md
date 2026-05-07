# 01 — Engine architecture

This document defines the top-level architecture of the SFS engine: the subsystem boundaries, the threading and rate-domain decisions, the voice model, and the plug-in lifecycle.

---

## 1. Subsystem boundaries

The SFS engine is structured into five subsystems, each implementable and testable independently:

```
                    ┌──────────────────────────────────┐
                    │           Plug-in shell          │
                    │ (VST3 wrapper, GUI, preset I/O)  │
                    └───────────────┬──────────────────┘
                                    │
                    ┌───────────────▼──────────────────┐
                    │           Control plane          │
                    │ Macros, mod matrix, MIDI/MPE,    │
                    │ smoothing, voice manager         │
                    └───────────────┬──────────────────┘
                                    │ (per-voice block-rate parameters)
       ┌────────────────────────────┼────────────────────────────┐
       │                            │                            │
┌──────▼─────────┐         ┌────────▼───────┐         ┌──────────▼────────┐
│  Agent pool    │         │   Substrate    │         │  Harvester bank   │
│ (per voice)    │ deposit │  (per voice)   │ readout │  (per voice)      │
│                ├────────►│                ├────────►│                   │
│                │ ◄───────┤                │         │                   │
│                │  bend   │                │         │                   │
└────────────────┘         └────────────────┘         └─────────┬─────────┘
                                                                │
                                                       ┌────────▼─────────┐
                                                       │ Output stage     │
                                                       │ Mixer, soft sat, │
                                                       │ DC block, lim    │
                                                       └────────┬─────────┘
                                                                │
                                                       ┌────────▼─────────┐
                                                       │  Host channel    │
                                                       │  buffers (out)   │
                                                       └──────────────────┘
```

The five subsystems are:

| Subsystem | Document | Responsibility |
|---|---|---|
| Plug-in shell | 07, 06 | VST3 lifecycle, parameter exposure, preset I/O, GUI hosting |
| Control plane | 05 | Macro fan-out, modulation matrix evaluation, MIDI/MPE ingest, smoothing, voice allocation |
| Agent pool | 03 | Per-voice population of audio-rate oscillator agents; deposit/bend interface to substrate |
| Substrate | 02 | Per-voice continuous spatial field; wave + diffusion + loss update |
| Harvester bank | 04 | Per-voice fixed listening points; output stage merges harvester signals across voices |

The substrate is the **only** shared-state subsystem with the agent pool — agents do not see each other directly. This is the architectural realisation of the stigmergy principle in code, not just on the marketing page. Any code path that lets agents see each other's state is a v1.0 bug.

## 2. Per-voice instancing

The default v1.0 voice model is **per-voice substrate instancing**: each polyphonic voice owns its own substrate, its own agent pool, and its own harvester bank. There is no shared substrate across voices.

This decision is non-obvious and worth justifying. A shared-substrate model would let voices interact (one note's substrate ringing would bend another note's agents), producing a richer paraphonic instrument. We rejected shared-substrate as the **default** for three reasons:

* **Voice independence.** Players expect note-on/note-off to behave like a normal polyphonic synth. Shared-substrate makes one note's sustain audibly affect a later note's attack — astonishing and unwelcome on first encounter.
* **Voice stealing.** With a shared substrate, killing a voice mid-ring leaves substrate energy behind that has no clear owner. With per-voice substrates, a stolen voice's substrate decays cleanly and is reset before reassignment.
* **Reproducibility.** A preset's audio output is a deterministic function of its seed, the played notes, and the macro automation. Shared-substrate makes the audio depend on inter-note timing in ways that are hard to reproduce or test.

A `shared_substrate` flag is reserved in the preset format for v1.x but is not exposed in the v1.0 GUI. The architecture leaves the seam open.

## 3. Rate domains

The engine runs at three distinct rates:

| Rate | Frequency | What runs here |
|---|---|---|
| Audio rate (`fs`) | 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz | Substrate update, agent oscillator, harvester read |
| Block rate (`fb`) | `fs / blockSize`, host-defined (typ. 64–2048 samples) | Macro fan-out, mod matrix evaluation, voice allocation, GUI param updates |
| GUI rate | 30 / 60 Hz | Visualiser, parameter display |

**Block rate is the seam between control and DSP.** Macros and modulation evaluate once per block, then **smoothed** across the block at audio rate inside the affected DSP nodes (substrate `c²`, agent `f_i`, harvester position). Smoothing is per-parameter with parameter-specific time constants documented in 05.

Sample-accurate automation per VST3 is supported by **subdividing** the block at parameter-change boundaries. This is implemented in the plug-in shell, not the engine: the shell calls the engine's top-level `Engine::process(numSamples, audioOut)` multiple times per host block when a parameter automation event arrives mid-block. The engine internally loops `engineStep()` per sample (§5.4); the multiple `process()` calls per block are the shell's slicing, not the engine's. The engine sees a normal sequence of variable-length blocks.

## 4. Threading model

### 4.1 Real-time audio thread

A single audio thread services every host process call. Within that thread, the engine processes each active voice **sequentially** by default. Voice rendering is fully thread-safe and re-entrant; a future v1.x can render voices in parallel via a thread pool.

* The substrate update is **vectorisable but not parallelisable across voices** at v1.0; substrate updates within a single voice are SIMD across cells (4-wide SSE2/NEON, 8-wide AVX2 where available).
* The agent loop is SIMD across agents (process 4 or 8 agents at a time).
* The harvester read is trivial.

The audio thread is **lock-free**. All control-plane data flows in via single-producer-single-consumer atomic message queues from the GUI/host threads (see 4.4).

### 4.2 GUI thread

A separate GUI thread runs the editor at 60 Hz. It reads from a triple-buffered snapshot of the substrate (the audio thread writes a downsampled snapshot every N ms; the GUI thread reads the most recent complete snapshot). It writes parameter changes back via the message queue.

### 4.3 Worker threads

A background worker thread handles preset load/save (file I/O is never on the audio thread) and one-shot expensive precomputation (e.g., re-deriving harvester position lookup tables when `TOPOLOGY` changes). The worker signals completion via the same atomic queue mechanism.

### 4.4 Cross-thread communication

The engine maintains **one SPSC (single-producer, single-consumer) ring buffer per non-audio producer thread**, all consumed by the audio thread. v1.0 has three such queues:

* `guiToAudio` — produced by the GUI thread (parameter edits from knob drags, preset loads).
* `hostToAudio` — produced by the VST3 wrapper from host-side parameter automation, MIDI events, and transport messages.
* `workerToAudio` — produced by the worker thread for one-shot completions (e.g., 2D substrate allocation finished, preset migration finished).

Each queue carries the same payload type:

```
struct ControlEvent {
  uint32_t timestamp;          // sample offset within host block (0 if not block-relative)
  uint16_t targetId;            // parameter / mod-slot / voice-event ID
  uint16_t flags;
  union {
    float    fValue;
    uint32_t iValue;
  } value;
};
```

The audio thread drains all three queues at the start of each block in fixed order (host → gui → worker, so host automation always wins on the same parameter, and worker confirmations are last). Each queue has a capacity of 4096 events.

**Overflow policy**: the producer drops the new event and increments a per-queue `dropped` counter. The counters are exposed via the engine API:

```c++
struct ControlQueueStats {
    uint64_t enqueued;
    uint64_t consumed;
    uint64_t dropped;
};

const ControlQueueStats& Engine::getQueueStats(QueueId q) const noexcept;
```

`QueueId ∈ {GuiToAudio, HostToAudio, WorkerToAudio}`. The counters are `std::atomic<uint64_t>` updated by the producer (for `enqueued`/`dropped`) and by the audio thread (for `consumed`); reads are eventually consistent. The CI test rig asserts `dropped == 0` after running each integration test, treating any non-zero value as a P0 bug. The Advanced parameter panel (07 §8a) displays the counters in development builds.

We use SPSC rather than MPSC because SPSC is provably lock-free with simpler memory ordering, and because the producers are entirely separate threads with no shared back-pressure needs.

## 5. Sample rate, oversampling, and downsampling

### 5.1 Native sample rate

The engine runs at the host's reported sample rate up to 192 kHz. There is no internal upsampling at v1.0 — the substrate dynamics produce no aliasing-prone discontinuities by themselves, and the agents are individually antialiased (BLEP/polyBLEP, see 03).

### 5.2 Substrate downsampling

For high-CPU configurations (e.g., 2D substrate with 64 agents per voice and 8-voice polyphony), the substrate may run at `fs / S` for `S ∈ {1, 2, 4}`. Agent deposits are accumulated at audio rate into a small ring buffer, then summed and integrated into the substrate at every `S` samples. Harvester reads upsample using cubic Hermite interpolation from the substrate's slower clock.

`S = 2` is inaudible for most macro positions and halves the substrate cost. `S = 4` is audibly different — high-frequency substrate ringing is attenuated — and is reserved for the explicit "ECO" preference mode.

The default `S` factor depends jointly on the active substrate topology and the user's `engine.oversample_mode` preference (09 §3.12):

| Topology | ECO | STD (default) | PREMIUM |
|---|---|---|---|
| 1D ring | `S = 2` | `S = 1` | `S = 1` |
| 2D torus | `S = 4` | `S = 2` | `S = 1` |

These defaults are the operating contract. v1.0 ships with `engine.oversample_mode = STD`, which means 1D presets run at full audio rate and 2D presets at half rate. Users can override via preferences. Document 02's CPU budget tables assume STD mode unless otherwise stated.

### 5.3 At sample-rate switches

When the host changes sample rate (rare; usually only at project change or device switch), the engine:

1. Recomputes substrate coefficients (`c²`, `κ`, `γ` derived in their normalised form, but envelope rates and tempo-synced LFO rates need to be re-quantised against the new `fs`).
2. Resamples or reinitialises the substrate displacement and velocity buffers (current state is interpolated to the new rate using a one-shot polyphase resample). Reinitialisation to zero is acceptable as a fallback; the `setActive(false)` → `setActive(true)` sequence the host typically issues around a sample-rate change makes a silent flush invisible.
3. Resumes envelopes in their current stage at the appropriate per-sample rate. **Note-on events are not replayed** — VST3 hosts do not re-issue MIDI on sample-rate change, and SFS does not synthesise note-on events. Voices that were holding gate when the rate changed remain in their current envelope stage with recalculated rates.

In practice, hosts call `setActive(false)` before rate change and `setActive(true)` after — so this code path is rarely exercised mid-playback. It is documented to specify the contract, not because it is a hot path.

## 5.4 The canonical per-sample engine step

The engine is fundamentally **sample-driven**: agents and substrate are interleaved per audio sample, not per block. `Engine::process(numSamples, audioOut)` is a thin loop that calls `engineStep()` once per sample. The block boundary exists only for the host's benefit; nothing in the engine state behaves block-wise.

The canonical per-sample ordering for one voice is:

```
engineStep(voice, n):
    1. Voice envelope tick:        voice.envelopeTick(n)
    2. Agent loop (per agent i):
        a. u_at = substrate.read(p_i)                       // pre-update read
        b. f_inst = bend(f_i, m_i, u_at)
        c. y_i = voice.agents.waveform(i, f_inst, n)
        d. e_i = voice.agents.envelope(i, n)
        e. substrate.deposit(p_i, w_i · e_i · y_i)          // accumulates into u_inject
        f. p_i = migrate(p_i, r_i, ε_i)
    3. Substrate update:
        a. consume u_inject; advance v[x], u[x] (per 02 §2.1)
        b. zero u_inject
    4. Harvester read: out[ch, n] = substrate.read(harvester[ch].position)
```

Steps 1–4 happen in this order, every sample, deterministically. Multiple voices' steps are computed independently then summed at step 4 across voices into the host output buffer.

This ordering is the single source of truth. Documents 02 and 03 both reference back to this section for any timing or scheduling question. The substrate API in 02 §10 exposes `step()` (one-sample advance) for the live engine and `processNoDeposits(N)` (a loop of `step()` calls) only as an offline-test convenience for substrate-only test fixtures. There is no API that batches many samples of agent deposits before applying the substrate update — implementers must preserve the per-sample interleaving even when fusing the loop for SIMD efficiency.

## 6. Voice model

### 6.1 Voice allocation

A `VoiceManager` in the control plane handles MIDI note-on / note-off:

* On note-on with all `MAX_VOICES` voices active, the longest-released voice (or oldest if none released) is stolen. Stolen voices fade out over 5 ms via a hard-coded amplitude ramp before reassignment, regardless of macro state.
* Note-off triggers the voice's release stage but the voice continues rendering until its substrate energy falls below a noise-gate threshold (-72 dBFS RMS for 50 ms continuous), at which point the voice is freed.
* MPE channels are tracked by note-on channel; per-note pitch-bend, slide (CC74), and pressure attach to the voice owning that note.

`MAX_VOICES = 8` at v1.0. Each voice consumes one substrate (~16 KB for 1D / ~16 MB for 2D 1024×1024) and one agent pool (~6 KB for 64 agents). Memory is preallocated at engine construction.

### 6.2 Per-voice initial state

When a voice activates (note-on), it executes an **ignition** sequence:

1. Substrate buffers (`u`, `v`) zeroed.
2. A short shaped noise burst of length `IGNITION_LEN` (default 64 samples) and amplitude `IGNITION_GAIN` (default −36 dBFS) is injected into the substrate at one or more positions chosen by the per-voice RNG stream.
3. Agent oscillators have their phases initialised from the per-voice RNG; their positions are placed at deterministic equispaced points around the substrate, plus a small jitter from the RNG.
4. Agent envelopes start.

The ignition burst is **not strictly mathematically required** — continuous nonzero agent deposits do excite the substrate (they are forces into `u_inject`). The role of ignition is **decorrelating startup**: agents at deterministic equispaced positions on a zeroed substrate would otherwise generate pathological standing-wave alignments that take many seconds to break — and would sound, at low EXCITATION, like a sterile additive bank. The ignition burst breaks the symmetry: it deposits a tiny per-voice-RNG-keyed pattern that gives the substrate's modes a non-zero initial state, so the first agent deposits land on a substrate that is already mid-ring rather than at perfect rest. The burst is energetically tiny and inaudible directly; its purpose is to make the voice's first 100 ms sound alive rather than awakening from a perfect null.

### 6.3 Voice deactivation and tail length

On gate-off, agent envelopes enter release. The substrate continues to evolve until its RMS over a 50 ms window falls below `VOICE_GATE_THRESHOLD = −72 dBFS`. The voice is then freed and its substrate buffers are returned to the per-voice pool.

Tail length depends on the preset's substrate damping `γ` and the envelope's release time. For a typical drone preset (`γ ≈ 0.001`, env release 4 s) the tail can extend several seconds beyond note-off. The plug-in reports a static **plug-in tail length** to the host via VST3's `getTailSamples()` of `MAX_TAIL_SECONDS · fs`, with `MAX_TAIL_SECONDS = 30` (covering all reasonable preset configurations). This is conservative — most presets release in < 5 s — but the static value lets the host correctly preserve voices when offline-rendering ends.

Per-voice harvester state (orbit phase, position offsets) is reset on voice reallocation, so a stolen-and-reallocated voice does not inherit the orbit position of its predecessor.

A "panic" message from the host (e.g., MIDI all-notes-off) immediately ramps all active voices to silence over 10 ms and frees them.

## 7. Plug-in lifecycle (VST3)

### 7.1 Construction

At plug-in instantiation, the shell:

1. Reads the host's reported max block size and sample rate.
2. Allocates `MAX_VOICES` × {substrate, agent pool, harvester bank} structures.
3. Allocates the SPSC control-event queue.
4. Registers VST3 parameters per the parameter table in document 09.
5. Loads the default preset.

Construction MUST NOT touch the file system except to load the embedded default preset (which is statically linked into the binary).

### 7.2 Initialisation

`setActive(true)` triggers final initialisation: substrate coefficients are derived from the current sample rate, RNG streams are seeded from the current preset's seed, and DC blockers are reset.

### 7.3 Process

Per host process call, the shell:

1. Drains MIDI events into MIDI handler.
2. Drains parameter automation events into the control-plane queue.
3. Subdivides the block at parameter-change boundaries if sample-accurate automation is enabled (default on).
4. For each subdivision: calls `engine.process(numSamples, audioOut[])`.
5. The engine internally iterates each active voice, summing into the host's output buffers.

### 7.4 State save/load

`getState()` serialises the current preset (active patch parameters + seed + modulation matrix) into the v1.0 preset format (see 06). It does NOT save runtime state (active notes, substrate contents). Save is performed on the GUI/main thread.

`setState()` deserialises and applies. The engine flushes all voices, applies the new parameter values, and is ready to play.

### 7.5 Destruction

`setActive(false)` flushes voices. Destructor frees all preallocated memory. No allocations or deallocations occur on the audio thread between `setActive(true)` and `setActive(false)`.

## 8. Memory layout

### 8.1 Per-voice memory

```c++
struct Voice {
  alignas(64) Substrate1D substrate1d;     // ~16 KB for N=1024 cells
  alignas(64) Substrate2D* substrate2d;    // pointer; allocated on preset load (worker thread)
  alignas(64) AgentPool   agents;          // ~6 KB for 64 agents
  alignas(64) HarvesterBank harvesters;    // ~256 B
  EnvelopeState envelopes;                  // ~256 B
  RngStream rng;                            // 32 B counter-based
  VoiceState state;                         // ~64 B (note ID, MPE channel, gate, etc.)
};
```

64-byte alignment ensures cache-line-friendly access and supports AVX-512 in future versions.

**The 2D substrate is allocated lazily but never on the audio thread.** Allocation happens on the worker thread when:

* A preset that uses 2D topology is loaded.
* The user changes `TOPOLOGY` to a 2D variant in the GUI.

In both cases, the worker thread allocates the 2D buffers, populates them with zero state, and signals the audio thread (via `workerToAudio`) that 2D is ready. Until that signal arrives, the engine continues running the active voice in 1D mode if it was already playing, or defers the next voice ignition by a few milliseconds. The audio thread never blocks on allocation.

If a topology switch is requested for a 2D variant whose substrate has not yet been allocated, the request enters a pending state until the worker completes allocation. The GUI shows a brief "loading topology…" indicator (typically < 50 ms on modern hardware).

Voices that never see a 2D-mode preset do not pay the 2D memory cost.

### 8.2 Engine memory

```c++
struct Engine {
  std::array<Voice, MAX_VOICES> voices;
  ControlEventQueue queue;
  ModulationMatrix modMatrix;
  VoiceManager voiceManager;
  GlobalState global;        // sample rate, block size, oversample factor
};
```

Total v1.0 footprint with 1D substrate: ~24 KB × 8 voices = ~200 KB. With 2D substrate active on all voices (the worst case): ~130 MB. The 2D 1024×1024 size is excessive for normal use; the GUI exposes 64×64 to 256×256 by default with 1024×1024 reserved for offline / preview rendering.

## 9. Determinism guarantees

Given:

* The same SFS preset (seed + parameters + mod matrix)
* The same MIDI event stream with sample-accurate timestamps
* The same host sample rate, block size, and channel layout
* The same engine version

The engine MUST produce bit-identical audio output across runs and across platforms (x86_64 + ARM64). This is a hard test gate (08).

To meet this:

* All RNG is counter-based (Philox-4x32-10), keyed by (preset_seed, voice_index, agent_index, stream_id). No reliance on `std::default_random_engine` or any platform RNG.
* No `float` denormal flushing differences are tolerated. Set FTZ/DAZ on every audio thread entry.
* All transcendental and elementary math on the audio path goes through the **deterministic math primitives** defined in 06 §"Deterministic math primitives" — `dm_sin`, `dm_cos`, `dm_tanh`, `dm_exp`, `dm_log`, `dm_sqrt`, `dm_pow`, `dm_floor`, `dm_fmod`. These are bit-exact polynomial approximations or LUTs shipped with the engine. Library calls (`std::sin`, `std::cos`, `std::tanh`, `std::exp`, `std::log`, `sinf`, `cosf`, `logf`, `powf`, `fmodf`, `floorf`) are **forbidden on the audio path** (anywhere reached from `engineStep()`).
* Library math is allowed in init-time and block-rate non-audio code paths (preset migration, parameter smoothing constant derivation, GUI-side computations). The seam is whether the call participates in a per-sample render; if so, it must use a deterministic primitive.
* No platform-specific SIMD math intrinsics whose precision differs across vendors. Use the [SIMDe](https://github.com/simd-everywhere/simde) layer or hand-write polynomial-approximation kernels.

This is non-negotiable. Bit-exact reproducibility is what makes presets shareable, automation testable, and preset hashes meaningful.

## 10. Open questions

These are explicitly deferred decisions, listed so they don't get lost:

* **Voice rendering parallelism.** Voices are independent. Rendering them on a thread pool would scale linearly with cores. Deferred to v1.2 pending real-world CPU profiling.
* **External excitation input.** Conceptually, audio input could be deposited into the substrate as a virtual agent. The architecture supports this — the agent interface accepts `y_i[n]` from any source — but the GUI, modulation routing, and latency story for this feature are not designed for v1.0.
* **Substrate sharing across voices.** The architecture leaves the seam open via a `shared_substrate` flag in the preset format. Default false; not exposed in v1.0 GUI.
