# 03 — Agent DSP

Agents are the "voices within the voice." Each agent is a small, self-contained audio-rate oscillator that writes a contribution into the substrate at its current position and is, in turn, frequency-modulated by the substrate value at that position. This document specifies their internal state, oscillator math, waveform palette, envelope, migration physics, and their pool-level memory layout.

---

## 1. Agent state

Per agent (struct of arrays for SIMD; struct shown for clarity):

```c++
struct Agent {
    float position;        // p_i ∈ [0, N) for 1D; (x, y) struct for 2D
    float frequency;       // f_i in Hz, base before modulation
    float phase;           // φ_i ∈ [0, 1), normalised
    float amplitude;       // a_i, current envelope level
    float depositWeight;   // w_i, scaling of agent's contribution to substrate
    float modSensitivity;  // m_i, scaling of substrate's bend on agent frequency
    float drift;           // r_i, baseline migration rate (cells/sample)
    float migrationNoise;  // ε_i RMS amplitude
    EnvelopeState env;     // ADSR state
    uint8_t shape;         // 0=sine, 1=saw, 2=square, 3=fmpair, 4=noise
    uint8_t flags;
    uint16_t shapeParam;   // shape-specific (e.g., FM ratio×256)
};
```

Pool size: 64 agents per voice × 8 voices = 512 agents engine-wide. At ~48 bytes per agent, this is ~24 KB — negligible.

## 2. The agent inner loop

Per audio sample, per agent:

```
// 1. Read substrate at agent's position (uses linear interp; substrate read is cheap)
u_at = substrate.read(p_i)

// 2. Compute instantaneous frequency
f_inst = f_i + m_i · u_at · f_i      // bend is proportional to base freq → consistent feel

// 3. Advance phase
phase_inc = f_inst / fs
φ_i = φ_i + phase_inc - floor(φ_i + phase_inc)   // wrap to [0, 1)

// 4. Generate waveform sample
y_i = waveform(shape, φ_i, shapeParam, phase_inc)

// 5. Update envelope
e_i = env_step(env)

// 6. Deposit into substrate
substrate.deposit(p_i, w_i · a_i · e_i · y_i)

// 7. Update position (migration)
ε_i = rng.normal() · migrationNoise
p_i = p_i + r_i + ε_i
p_i = wrap(p_i, N)
```

### 2.1 Why the bend is multiplicative on frequency, not additive

The choice `f_inst = f_i + m_i · u_at · f_i` (multiplicative) rather than `f_inst = f_i + m_i · u_at` (additive) is deliberate. With multiplicative bend, an agent at base 100 Hz and one at base 1000 Hz both bend by the same *fraction* under the same substrate value — preserving harmonic relationships under modulation. Additive bend would unequally distort high-frequency agents. For musical sounds you almost always want fractional bend.

### 2.2 Order matters: deposit then migrate

Each agent reads the substrate, generates output, deposits, then migrates. This ordering ensures:

* All agents see the substrate as it was *before* this sample's deposits (agents don't see each other's current sample).
* Migration happens after deposit, so an agent's deposit is at the position where it just generated.

This is the discrete-time analog of "act on local state, then move." Reordering breaks determinism subtly.

## 3. Waveform palette

v1.0 ships five agent waveforms. All are antialiased to within 60 dB of the Nyquist for `f ≤ fs/4`, falling off above. Above `fs/2`, agents are silenced (handled in §6 via per-agent gating).

### 3.1 Sine

Bandlimited by definition. Uses a polynomial approximation of `sin(2π · φ)`:

```c++
// 5th-order polynomial approximation, max error 1e-6
inline float sineApprox(float phase01) {
    float x = phase01 * 2.0f - 1.0f;     // [-1, 1]
    float x2 = x * x;
    float y = x * (1.5708f + x2 * (-0.6435f + x2 * (0.0795f - x2 * 0.0049f)));
    return -y;  // matches sin(2π · phase01)
}
```

Cost: ~5 mul, 4 add per sample. Cheaper than `std::sin` and reproducible across platforms.

### 3.2 Saw (PolyBLEP)

Naive saw `y = 2·φ - 1` plus a polyBLEP correction at the discontinuity. Following the standard derivation (see [polyBLEP article](https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/)):

```c++
inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

inline float sawPolyBlep(float phase01, float dt) {
    return 2.0f * phase01 - 1.0f - polyBlep(phase01, dt);
}
```

`dt = phase_inc` (normalised frequency). Cost: ~10 ops. Aliasing 60 dB below fundamental at `f < fs/4`.

### 3.3 Square (PolyBLEP)

Same polyBLEP at both `phase01 = 0` and `phase01 = 0.5`:

```c++
inline float squarePolyBlep(float phase01, float dt) {
    float y = (phase01 < 0.5f) ? 1.0f : -1.0f;
    y -= polyBlep(phase01, dt);
    y += polyBlep(fmodf(phase01 + 0.5f, 1.0f), dt);
    return y;
}
```

### 3.4 FM pair

A two-operator FM oscillator: a single carrier-modulator pair with fixed ratio and depth. Inexpensive way to give agents bell-like and metallic timbres without a full FM tree.

```c++
// shapeParam encodes ratio (0..255 → 0.25 to 4.0) and index (0..255 → 0 to 8.0)
ratio = 0.25f * powf(2.0f, (shapeParam & 0xFF) / 64.0f);
index = 8.0f * ((shapeParam >> 8) & 0xFF) / 255.0f;

mod_phase = fmodf(φ_i * ratio, 1.0f);
mod_out   = sineApprox(mod_phase);
y_i       = sineApprox(fmodf(φ_i + index * mod_out / (2π), 1.0f));
```

Bandlimited only in the loose sense — at extreme `index`, FM produces high partials that approach Nyquist. Mitigated by gating the agent above `f_inst > fs/4 / (1 + index)`.

### 3.5 Noise

Filtered white noise from the agent's own RNG stream (see 06):

```c++
// sample-and-hold at the agent's frequency (turns "noise" into pitched textures at high f)
if (φ_i wrapped this sample):
    held_value = rng.next_float() * 2.0f - 1.0f;
y_i = held_value;
```

This is sample-and-hold noise: at low `f`, sounds like noise; at high `f`, sounds increasingly pitched (since we're sampling random values at audio rate). This single waveform spans noise textures and pitched-noise textures depending on `f`.

### 3.6 Future: granular agent (reserved)

A reserved shape `5 = grain` for v2.0 would let an agent emit short windowed grains of a stored sample at its `f` rate. Not implemented in v1.0; the field is reserved.

## 4. Envelope

Each agent has an ADSR envelope. The envelope multiplies the agent's deposit weight, not its frequency. Agents share a master envelope from the voice manager (`voice.gate`, `voice.gateOff`); per-agent envelope rates can be offset by a small randomisation from the seeded RNG to give the colony a natural "swarm of attacks" rather than a rigid simultaneous start.

State machine: `IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE`.

```c++
struct EnvelopeState {
    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    Stage stage;
    float value;          // current amplitude [0, 1]
    float attackRate;     // 1/samples
    float decayRate;
    float sustainLevel;
    float releaseRate;
};

inline float envStep(EnvelopeState& e) {
    switch (e.stage) {
        case IDLE:    e.value = 0.0f; break;
        case ATTACK:  e.value += e.attackRate;
                      if (e.value >= 1.0f) { e.value = 1.0f; e.stage = DECAY; }
                      break;
        case DECAY:   e.value -= e.decayRate;
                      if (e.value <= e.sustainLevel) { e.value = e.sustainLevel; e.stage = SUSTAIN; }
                      break;
        case SUSTAIN: /* hold */ break;
        case RELEASE: e.value -= e.releaseRate;
                      if (e.value <= 0.0f) { e.value = 0.0f; e.stage = IDLE; }
                      break;
    }
    return e.value;
}
```

Envelope rates are exponential at the macro level (logarithmic time scaling: 1 ms to 30 s) and converted to linear-rate constants at block rate. This matches user expectation that knob position → musical time.

## 5. Migration physics

Each agent migrates across the substrate. The migration model is:

```
p_i[n+1] = p_i[n] + r_i + ε_i[n]
```

where `r_i` is a per-agent deterministic drift rate (set at voice ignition from the seeded RNG) and `ε_i[n]` is per-sample Gaussian noise with stddev `migrationNoise`.

### 5.1 Drift distribution

At voice ignition, drift rates are sampled as:

```
r_i ~ MIGRATION_macro · (uniform(-1, 1)) · DRIFT_SCALE
```

where `DRIFT_SCALE = 0.001 · N` cells/sample (the scale at which agents traverse the substrate over ~1 second at full macro). At `MIGRATION = 0`, all `r_i = 0` and agents are stationary except for noise.

### 5.2 Migration noise

Gaussian noise per agent per sample, sampled from the agent's RNG stream:

```
ε_i[n] = MIGRATION_macro · NOISE_SCALE · gaussian(rng_i)
```

with `NOISE_SCALE = 0.0005 · N` cells/sample. Use the [Box–Muller transform](https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform) on two uniform draws to generate Gaussians.

For SIMD, batch the Gaussian generation: use a small per-voice ring buffer of pre-generated Gaussians, refilled in 64-element batches outside the per-sample agent loop.

### 5.3 Wrap

Position wraps modulo `N` (1D) or `(W, H)` (2D). Always-positive wrap:

```c++
inline float wrapPosition(float p, float N) {
    return p - N * floorf(p / N);
}
```

## 6. Bandlimiting and silencing

Order of operations:

1. Compute the agent's instantaneous frequency `f_inst` (after substrate bend).
2. Generate the agent's waveform output, applying polyBLEP / polynomial antialiasing as defined in §3 — the antialiasing remains valid throughout the soft-mute region.
3. Apply soft-mute amplitude scaling based on `f_inst`: agents whose instantaneous frequency exceeds `fs/2` are silenced (output zero, but state continues to update). Agents above `fs/4` are amplitude-attenuated linearly to zero between `fs/4` and `fs/2`.

Antialiasing is applied at full agent amplitude before the soft-mute. This means that even an agent whose instantaneous frequency briefly excursions toward Nyquist remains alias-free up to the point where it is silenced. The soft mute prevents hard clicks when an agent is bent into the high frequency regime.

## 7. The agent pool

Agents are stored as a struct-of-arrays for SIMD friendliness:

```c++
struct AgentPool {
    static constexpr int MAX_AGENTS = 64;
    int activeCount;

    alignas(64) float position[MAX_AGENTS];
    alignas(64) float frequency[MAX_AGENTS];
    alignas(64) float phase[MAX_AGENTS];
    alignas(64) float amplitude[MAX_AGENTS];
    alignas(64) float depositWeight[MAX_AGENTS];
    alignas(64) float modSensitivity[MAX_AGENTS];
    alignas(64) float drift[MAX_AGENTS];
    alignas(64) float migrationNoise[MAX_AGENTS];

    EnvelopeState envelopes[MAX_AGENTS];
    uint8_t shape[MAX_AGENTS];
    uint8_t flags[MAX_AGENTS];
    uint16_t shapeParam[MAX_AGENTS];
};
```

The hot fields (`position`, `frequency`, `phase`, `amplitude`) live in their own cache lines and can be processed 4 or 8 at a time in SIMD. Cold fields (envelope state machines, shape selectors) are processed in scalar loops at minimal cost given `MAX_AGENTS = 64`.

### 7.1 Active count

`activeCount ≤ MAX_AGENTS`. Determined by `DENSITY` macro at voice ignition (and re-evaluated when `DENSITY` is automated mid-note: agents are smoothly faded in or out via amplitude ramp).

### 7.2 Deposit accumulation

Agents do not write directly into substrate cells. They write into a per-sample injection buffer:

```c++
float u_inject[N];   // zeroed at start of each sample's substrate update
```

This buffer is reset to zero, agents loop and accumulate into it, then the substrate's `process()` method consumes it. Separating accumulation from substrate update lets the SIMD-vectorised substrate kernel run on cleanly aligned arrays.

For the linear deposit kernel, agent contribution is two `+=` ops per agent. With 64 agents, that's 128 adds per sample (~3 ns of ALU on modern hardware). Negligible.

### 7.3 Bend read

Each agent reads `u[p_i]` once per sample. With 64 agents and a substrate of 1024 cells, the substrate is hot in L1 cache for both deposit and bend reads.

## 8. Cohort logic

A **cohort** is a subset of agents sharing harmonic relationship to the played MIDI note. The `COHERENCE` macro controls cohort behaviour:

* At `COHERENCE = 1`, all agents have `f_i = f_note · h_i` where `h_i` is drawn from a chosen integer (or just-intonation rational) ratio set per agent at ignition. The colony plays a chord consisting of harmonics of the note.
* At `COHERENCE = 0`, agents have `f_i ~ f_note · uniform(0.5, 2.0)` — fully detuned.
* In between, `f_i = f_note · ((1 - COHERENCE) · uniform(0.5, 2) + COHERENCE · h_i)`.

`h_i` is drawn at voice ignition from a discrete set:

```
h_set = {1, 2, 3, 4, 5, 6, 7, 8, 1.5, 2.5, 0.5, 0.25, 1.333, ...}
```

The set is configurable per preset via `agent.harmonic_set`. Default is the integer harmonic series 1..8 plus a handful of tritave/odd ratios.

This gives the user musical control over the colony: low `COHERENCE` = noisy swarm; high `COHERENCE` = harmonic stack with motion.

## 9. CPU cost summary

Per voice, per audio sample, with 64 active agents:

| Component | Approx ops/sample | Cost @ 48 kHz, scalar |
|---|---|---|
| Substrate read for bend (linear interp) | 64 × 4 = 256 | 0.5 µs |
| Phase update + waveform | 64 × 12 = 768 | 1.5 µs |
| Envelope step | 64 × 6 = 384 | 0.7 µs |
| Deposit (linear kernel) | 64 × 2 = 128 | 0.3 µs |
| Migration update | 64 × 4 = 256 | 0.5 µs |
| RNG (batched) | 64 / 8 = 8 | 0.1 µs |
| **Total per voice** | **~1800** | **~3.5 µs scalar; ~1.5 µs SSE2** |

Combined with substrate (~5 µs SIMD for 1D-1024), one voice is ~6.5 µs per sample at 48 kHz, or ~31% of one core for a single voice. SIMD agent loop and AVX2 brings this to ~3 µs per voice; 8 voices fits in roughly 25% of one modern core.

## 10. Test vectors

Reference test vectors for the agent loop (separate from substrate tests):

* **Single sine agent, no migration, no substrate bend**: with `m_i = 0`, output should be a pure sinusoid with phase determined by the seeded RNG.
* **Two-agent beat**: two sine agents at 220 Hz and 221 Hz, no bend, deposited at the same substrate position should produce a 1 Hz beat at the harvester.
* **Agent under substrate bend**: an agent with `m_i = 0.5` deposited next to a strong substrate energy should show measurable pitch deviation matching the substrate amplitude.
* **Migration determinism**: same seed + same MIDI input across two runs should produce identical agent positions at every sample.

These run in CI as part of the bit-exact test gate (08).

## 11. Open questions

* **Adaptive density.** A future research direction is to let agents spawn or die based on substrate energy thresholds (low energy → spawn agents to revive the substrate; high energy → kill some). Not in v1.0.
* **Grain agents.** Reserved shape ID `5 = grain` for sample-based agents. Not in v1.0.
