# 04 — Harvester and output stage

This document specifies how audio is harvested from the substrate, mixed across voices, processed through the global output stage, and delivered to the host channel buffers.

---

## 1. The harvester model

A **harvester** is a fixed (or slowly-moving) **listening point** on the substrate. The sample value of `u` at the harvester's position, sampled every audio sample, is one channel of audio output. Multiple harvesters per voice produce multichannel output as a direct consequence of synthesis — not as a downstream stereo widener.

Each harvester is described by:

```c++
struct Harvester {
    float position;        // for 1D: cells [0, N)
    float position2D[2];   // for 2D: (x, y) ∈ [0, W) × [0, H)
    float gain;            // typical 1.0; may be reduced for higher-channel counts
    int   channelIndex;    // which output channel this harvester feeds
    HarvesterOrbit orbit;  // optional slow motion; see §4
};
```

A `HarvesterBank` holds up to 16 harvesters per voice (enough for 7.1.4 + 1st-order ambisonic).

## 2. The harvester read

Per audio sample, per harvester:

```
out_ch += harvester.gain · substrate.read(harvester.position)
```

`substrate.read(p)` returns the substrate value at fractional position `p` using the matching interpolation kernel (linear by default, cubic Hermite for premium):

### 2.1 1D linear interpolation

```c++
float read1D(float p) const {
    p = wrapPosition(p, N);
    int   i0 = static_cast<int>(p);
    float f  = p - i0;
    int   i1 = (i0 + 1) & (N - 1);
    return (1.0f - f) * u[i0] + f * u[i1];
}
```

### 2.2 1D cubic Hermite interpolation (premium)

```c++
float read1DHermite(float p) const {
    p = wrapPosition(p, N);
    int   i  = static_cast<int>(p);
    float f  = p - i;
    float y0 = u[(i - 1 + N) & (N - 1)];
    float y1 = u[i];
    float y2 = u[(i + 1) & (N - 1)];
    float y3 = u[(i + 2) & (N - 1)];

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}
```

Cubic Hermite produces noticeably less high-frequency rolloff at the harvester. v1.0 default is linear; cubic is selected by `harvester.read_kernel = hermite4`.

### 2.3 2D bilinear interpolation

Standard 4-tap bilinear over `(x, y)` cell. Bicubic 16-tap is reserved as `read_kernel = bicubic16` for premium 2D rendering.

## 3. Channel layouts

v1.0 supports the following layouts. The plug-in shell negotiates layout with the host via VST3's `IComponent::getBusArrangements`.

### 3.1 Mono

Single harvester at `position = 0.0`. Used for null/diagnostic configurations only — not exposed as a normal output mode.

### 3.2 Stereo (default)

Two harvesters at substrate positions `0.0` and `N/2.0` for the 1D ring. The ring topology means these two points are maximally separated, and the substrate's wave propagation between them produces natural inter-channel decorrelation.

For 2D substrate: harvester `L` at `(0.25 W, 0.5 H)`, harvester `R` at `(0.75 W, 0.5 H)`.

The harvester positions are user-tweakable around the defaults via a `HARVESTER_SPACING` macro (continuous, controls the angular separation on the ring).

### 3.3 Quad (4.0)

Four harvesters at `(N/4) · k` for `k = 0, 1, 2, 3`. Channel order follows the host's reported layout (typically L, R, Ls, Rs).

### 3.4 5.1

Five full-bandwidth harvesters; the LFE channel is a low-passed (120 Hz) sum of the five at unity gain. LFE is filtered with a 2nd-order Linkwitz–Riley low-pass.

Harvester positions in 1D: equally spaced for L, R, Ls, Rs at `(N/4) · k`, with C harvester at `N/8`. Yes, this is asymmetric; symmetric is `(0.5 - sin(2π·k/5))` style placement, which is what we use.

For 2D, harvesters are placed in a horseshoe in the front half of the torus, with surround channels in the back half.

### 3.5 7.1.4 (Atmos)

Eleven full-bandwidth harvesters + one LFE. In 2D mode, the four height channels (`Lts`, `Rts`, `Lrs`, `Rrs`) are placed at `y > 0.7 H` while the seven floor channels span `y < 0.3 H`. The mid-region of the torus is "above the audience" and shows as visual interest in the GUI.

In 1D mode, 7.1.4 is downmixed to stereo — the 1D substrate doesn't have enough spatial complexity to justify 11 channels.

### 3.6 1st-order Ambisonic (W, X, Y, Z)

Reserved for v1.0 listeners using ambisonic chain. Encoded by treating four harvester positions as the corners of a tetrahedron projected onto the substrate, then linearly combining their outputs into the W (omni), X (front-back), Y (left-right), Z (up-down) channels following the SN3D / ACN convention.

For 1D substrate (no Z), Z = 0 and the encoding reduces to 2D first-order (W, X, Y).

For 2D substrate, the 4 harvesters are placed at `(0.25 W, 0.25 H), (0.25 W, 0.75 H), (0.75 W, 0.25 H), (0.75 W, 0.75 H)`. The X/Y/Z encoding then becomes:

```
W = 0.5 · (h0 + h1 + h2 + h3)
X = 0.5 · (h2 + h3 - h0 - h1)
Y = 0.5 · (h1 + h3 - h0 - h2)
Z = 0.0  (no third dimension in 2D substrate)
```

Higher-order ambisonics are deferred to v1.2.

## 4. Harvester orbits

Each harvester has an optional slow-motion offset:

```c++
struct HarvesterOrbit {
    float speed;        // Hz (tempo-syncable in v1.x)
    float radius;       // cells
    float phaseOffset;
    OrbitShape shape;   // CIRCLE, FIGURE_EIGHT, RANDOM_WALK, OFF
};
```

For `CIRCLE`:

```
position[t] = basePosition + radius · sin(2π · speed · t / fs + phaseOffset)
```

For `FIGURE_EIGHT`: a Lissajous figure in 2D, or a slower-secondary-LFO in 1D.

For `RANDOM_WALK`: a seeded Brownian motion of the harvester position; the seed is derived from the per-voice RNG (06).

Orbits are extremely subtle — radii up to `0.05 · N` produce barely-perceptible spatial wandering that animates the stereo image without sounding like a chorus or phaser. In testing, the orbit is what makes 60-second drones avoid feeling static.

`HARVESTER_ORBIT_DEPTH` is a global macro (default 0.0; max 1.0) that scales all harvester orbit radii together. Off by default; users opt in.

## 5. Voice mixing

Each voice's harvester bank produces a vector of channel samples. The output stage sums voices into the host's output buffer:

```c++
for (int v = 0; v < activeVoiceCount; ++v) {
    for (int ch = 0; ch < channelCount; ++ch) {
        hostOutput[ch][n] += voices[v].harvesterBank.read(ch, n);
    }
}
```

No additional gain staging is applied at the voice mix; voice outputs already include the agent envelopes and the substrate's natural decay.

## 6. Output stage

After voice mixing, the global output stage applies (in order):

### 6.1 Master gain

User-controlled `MASTER_GAIN` macro, default 0 dB. Range −60 dB to +12 dB.

### 6.2 Soft saturator

A tanh-based soft saturator runs continuously to catch pathological bursts (e.g., a `EXCITATION = 1.0` patch on an extreme `TENSION` setting). The saturator threshold is fixed at −3 dBFS; below that, it is mathematically identity to within 0.05 dB.

```c++
inline float softSat(float x, float drive = 1.0f) {
    return tanhf(drive * x) / drive;
}
```

A polynomial approximation of tanh (5th order, max error 0.001) is used for reproducibility:

```c++
inline float tanhApprox(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
```

This is the [Padé approximation](https://en.wikipedia.org/wiki/Pad%C3%A9_approximant) and is bit-stable across platforms.

### 6.3 DC blocker (output stage)

A first-order high-pass at 5 Hz on every output channel. Belt-and-braces; the substrate has its own DC block but channel-mix can introduce small DC if voice counts are unbalanced.

### 6.4 Inter-channel correlation check (release builds, optional)

A diagnostic test runs once per second on stereo outputs to verify L/R correlation is not stuck at 1.0 (which would indicate harvester-position bug). Only fires a warning in dev builds; release builds skip.

### 6.5 Brick-wall limiter (off by default)

A look-ahead limiter at −0.3 dBFS true peak is **off by default**, on the principle that an instrument plug-in should not surprise the user with limiting. Opt-in via `OUTPUT_LIMITER` macro.

## 7. Latency and tail

### 7.1 Reported latency

The engine reports latency to the host as follows:

| Configuration | Latency |
|---|---|
| 1D substrate, linear deposit, linear read | 0 samples |
| 1D substrate, lanczos4 deposit, hermite4 read | 2 samples (kernel pre-roll) |
| 2D substrate, bilinear, bilinear | 0 samples |
| 2D substrate, bicubic, bicubic | 2 samples |
| Output limiter ON | additional 64 samples (1.3 ms @ 48 kHz) |

`getLatencySamples()` returns the sum of the active stages. The host uses this to compensate plug-in delay when mixing alongside other tracks.

### 7.2 Reported tail length

`getTailSamples()` returns `MAX_TAIL_SECONDS · fs` with `MAX_TAIL_SECONDS = 30`, regardless of preset. This conservatively covers the longest preset tails (drone presets with very low `γ` and long envelope releases) and tells the host to keep rendering past the last note-off until the engine reports silence. Most presets fall well below this; the tail length is a static maximum, not a dynamic per-preset value, to keep host-side bookkeeping simple.

When a host performs offline rendering with "process tail" enabled, the engine continues producing audio until per-voice gating closes all voices (01 §6.3). Tail is measured by the host listening for true silence on its captured output.

## 8. Output level calibration

A "factory" preset playing C4 at MIDI velocity 100 should produce −18 dBFS peak with `MASTER_GAIN = 0 dB` and 8 active agents. This is the calibration target: presets that diverge significantly from this should be reviewed before factory inclusion.

The default deposit weight `w_i` is `0.05 / sqrt(activeAgentCount)` — this normalises substrate energy across voices with different agent counts.

## 9. Reference implementation

```c++
class HarvesterBank {
public:
    void configure(const ChannelLayout& layout, const Substrate1D* sub1d, const Substrate2D* sub2d);
    void process(int numSamples, float* const* outputs);   // outputs[ch][sample]
    void setOrbitDepth(float depth);
    void setSpacing(float spacing);

private:
    Harvester harvesters[16];
    int activeHarvesters;
    const Substrate1D* sub1d;
    const Substrate2D* sub2d;
    float orbitDepth;
};
```

## 10. Test vectors

* **Stereo decorrelation**: two harvesters on a 1D ring at maximally separated positions, with a single agent depositing a sine, should show inter-channel correlation < 0.9 across a 1-second window.
* **5.1 panning**: an agent migrating around the substrate should sweep audibly across the channels in clockwise order.
* **Ambisonic check**: a single agent's W channel should match the simple sum of its position-weighted contributions to within 0.1 dB.

## 11. Open questions

* **Higher-order ambisonics.** v1.2 will extend to 3rd order (16 harvesters). The architecture supports up to `HARVESTER_BANK_MAX = 16` already.
* **Per-channel substrate.** A future variant could give each output channel its own separate substrate, fed in parallel — increasing CPU but allowing per-channel tonal differentiation. Speculative.
* **Inter-voice harvester sharing.** Each voice has its own harvester bank in v1.0. A future shared-harvester mode would let voices co-mingle at the harvester level rather than the substrate level — a softer kind of voice interaction.
