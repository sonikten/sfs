# 02 — Substrate DSP

The substrate is the single most important subsystem in SFS. It is the medium that makes the engine an instrument rather than a parallel oscillator bank. This document specifies its mathematics, stability, parameter ranges, and reference implementation in detail sufficient for direct C++ implementation.

---

## 1. State

A substrate is a pair of buffers:

* `u[x]`: displacement (samples)
* `v[x]`: velocity (samples/step)

Each buffer holds **N** floating-point cells. **N** is a power of two for SIMD efficiency and modulo-by-mask wrap-around. v1.0 supports:

| Variant | Topology | N | Memory (`float`) |
|---|---|---|---|
| 1D | Ring | 1024 (default), 256–4096 | 8 KB |
| 2D | Toroidal grid | 64×64 (default), up to 256×256 | 64 KB–1 MB |

Both `u` and `v` are stored in 64-byte-aligned `float` arrays for SIMD load/store.

## 2. The 1D update equation

Per audio sample `n`, the substrate updates as follows. All loops are over cell index `x ∈ {0, ..., N-1}` with periodic boundary (cells wrap, modulo N).

```
// 1. Compute deposits from agents (see document 03)
for each agent i:
    deposit_kernel(u_inject_buffer, p_i, w_i · y_i[n])

// 2. Update velocity
v[x, n+1] = (1 - γ) · v[x, n]
          + c² · ( u[x-1, n] - 2·u[x, n] + u[x+1, n] )
          + κ  · ( v[x-1, n] - 2·v[x, n] + v[x+1, n] )
          + u_inject[x]

// 3. Update displacement
u[x, n+1] = u[x, n] + v[x, n+1]

// 4. DC block (per sample; cutoff fixed at 5 Hz; see §6)
apply_dc_block(u)
```

The order of operations matters: deposits go into the velocity update step (they are forces, not displacements), and the displacement integrates the new velocity. This is a leapfrog-style symplectic integrator and is what gives the substrate its stable wave behaviour. The whole step is one tick of the canonical per-sample engine step (01 §5.4).

### 2.1 Symbol definitions

| Symbol | Meaning | Parameter ID | v1.0 Range |
|---|---|---|---|
| `c²` | squared wave-speed | internal: `substrate.c2` (fanout target of `macro.tension`) | 0.0 to 0.49 (user-facing); internally clamped to 0.475 jointly with κ in 1D and 0.225 in 2D |
| `κ` | velocity-diffusion coefficient | internal: `substrate.viscosity` (fanout target of `macro.damping`) | 0.0 to 0.45 (1D); 0.0 to 0.20 (2D, joint clamp) |
| `γ` | per-step loss coefficient | internal: `substrate.gamma` (fanout target of `macro.damping`) | 0.0 to 0.05 |
| `N` | number of cells (1D) | internal: `substrate.size` | 256 to 4096, default 1024 |
| `u_inject[x]` | total agent injection at cell x this sample | (computed) | bounded |
| `α_dc` | DC-blocker coefficient | (derived from `fs`) | typically 0.999–0.9999 |

The internal field names (`substrate.c2`, `substrate.viscosity`, `substrate.gamma`, `substrate.size`) are catalogued in document 09 §"Internal engine fields"; they are private engine state and are not directly user-controllable. The user reaches them through the macros `macro.tension`, `macro.damping`, etc. (09 §3.1) via the fan-out described in 05.

The user-facing `c²` range is `[0.0, 0.49]`. When `c² + κ` would exceed `0.475`, both are softly compressed proportionally to remain within the safe bound; the user perceives a smooth saturation at the high end of the TENSION knob rather than a hard clip. Document 09's symbol table reports the user-facing range; the 0.475 internal clamp is a v1.0 implementation detail.

### 2.2 Stability bounds (1D)

Combined wave + diffusion + loss schemes admit a closed-form stability constraint via von Neumann analysis. For the discrete update above with cell spacing `Δx = 1` and time step `Δt = 1`:

* The wave term alone requires `c² ≤ 0.5` (CFL condition for the centered second-order scheme).
* The diffusion term alone (explicit Euler on velocity) requires `κ ≤ 0.5`.
* The combined term has an interaction: empirical safe envelope is `c² + κ ≤ 0.5` with a 5% margin.
* The damping term `(1 - γ)` is unconditionally stable for `0 ≤ γ < 1`; values close to 1 just kill the substrate.

**Engineering rule.** Internally clamp `(c² + κ) ≤ 0.475` with a soft compression as the user pushes the macros high. The macro-to-coefficient mapping (see 05) implements this clamp transparently — `TENSION` and `DAMPING` macros never let the user reach an unstable point.

### 2.3 Why this scheme

The chosen scheme is a discretisation of the damped Klein–Gordon-like equation:

```
∂²u/∂t² = c² · ∂²u/∂x² + κ · ∂²(∂u/∂t)/∂x² - γ · ∂u/∂t + F(x, t)
```

where `F(x, t)` is the agent forcing field. The `κ` term is **velocity diffusion** (sometimes called "Kelvin–Voigt damping"), not displacement diffusion — this distinction matters. Displacement diffusion would smear the wave shape destructively; velocity diffusion preferentially attenuates high-frequency velocity fluctuations, producing a frequency-dependent loss that sounds like the substrate has soft, lossy material edges. This is what makes the substrate musical rather than just a numerical wave equation.

## 3. The 2D update equation

The 2D substrate uses a 5-point Laplacian on a toroidal grid:

```
v[x, y, n+1] = (1 - γ) · v[x, y, n]
             + c² · ( u[x-1, y] + u[x+1, y] + u[x, y-1] + u[x, y+1] - 4·u[x, y] )
             + κ  · ( v[x-1, y] + v[x+1, y] + v[x, y-1] + v[x, y+1] - 4·v[x, y] )
             + u_inject[x, y]

u[x, y, n+1] = u[x, y, n] + v[x, y, n+1]
```

Stability bound for 2D: `c² + κ ≤ 0.25` (the Laplacian sum is now 4× the 1D case). Internally clamp to `0.225` with a 10% margin.

A 9-point Laplacian (`+ 0.5 ·` corner contributions) is offered as an experimental option in the preset format (`substrate.laplacian_order = 9`); it produces less anisotropy at the cost of one extra add per cell and is recommended for the 2D variant when `c²` is high.

## 4. Topology

v1.0 ships **two** topologies:

| Topology | 1D wrap | 2D wrap | Status |
|---|---|---|---|
| `ring` | `x = (x + N) mod N` | n/a | shipped |
| `torus` | n/a | `(x mod W, y mod H)` | shipped |
| `mobius` | `u[x = -1] = -u[N-1]` | sign-flip on one boundary | reserved (falls back to ring/torus) |
| `klein` | n/a | sign-flip on one axis only | reserved |

Möbius and Klein are reserved in the preset enum but fall back to ring/torus in v1.0. Their implementation is 2 lines of code per topology — they are deferred only because the listening tests for "do they actually sound different?" haven't been run, and committing to a sound contract requires the listening tests.

## 5. Deposit kernel

Agents have continuous-valued positions. The deposit kernel converts an agent's `(p_i, contribution)` pair into a vector of cell increments.

### 5.1 Linear deposit (default)

For `p_i` with integer part `x0` and fractional part `f ∈ [0, 1)`:

```
u_inject[x0]     += (1 - f) · contribution
u_inject[x0 + 1] += f       · contribution
```

This is fast (two adds per agent), wraps trivially, and introduces a small high-frequency rolloff equivalent to a `sinc(x)` shape, which we want.

### 5.2 4-tap windowed-sinc deposit (premium)

For better preservation of agent waveform high-frequency content at low `κ`:

```
for k in 0..3:
    x = (x0 + k - 1) mod N
    w = lanczos_kernel(k - 1 - f)
    u_inject[x] += w · contribution
```

with the Lanczos kernel `lanczos_kernel(t) = sinc(t) · sinc(t / 2)` for `|t| < 2`, zero otherwise. A precomputed 64-step LUT covers `f ∈ [0, 1)` to 6 decimal places.

The kernel is selected by `substrate.deposit_kernel ∈ {linear, lanczos4}`. Default `linear` for CPU; `lanczos4` for offline/preview rendering.

### 5.3 2D deposit

Bilinear (4-tap) for default; bicubic Hermite (16-tap) for premium. Same selection logic.

## 6. DC blocking and stability hygiene

A first-order DC blocker is applied to each cell of `u` **every audio sample** at the substrate update step. The block uses a single-pole high-pass with a fixed cutoff of 5 Hz, which is below all musical content of interest:

```
α = 1 - 2π · f_dc / fs                     // f_dc = 5 Hz; computed at setActive()
for each cell x:
    u_blocked[x] = u[x] - u_prev[x] + α · u_blocked_prev[x]
    u_prev[x]     = u[x]
    u[x]          = u_blocked[x]
```

`α` is derived from the host sample rate at `setActive(true)` and held constant until `setActive(false)`. Typical values: at 48 kHz, `α = 1 - 2π · 5 / 48000 ≈ 0.999346`; at 96 kHz, `α ≈ 0.999673`. The cutoff is therefore consistent across sample rates by construction.

This is overkill for stability — the loss term `γ` already prevents long-term DC accumulation — but eliminates audible bias drift over multi-minute drones, particularly under asymmetric agent deposit patterns.

Cost: `3N` operations per sample (one subtract, one multiply-add, one store of `u_prev`). For `N = 1024`, that's ~3000 ops/sample, which adds about 0.5 µs to the substrate update on SSE2. The previous "every 256 samples" sweep is replaced by this per-sample variant because the latter has a sample-rate-correct cutoff and no aliasing artifacts at the K-sample boundary.

A separate **infinity guard** runs on every voice every block: if `‖u‖∞ > 100.0`, the voice is reset to silent and the host is sent a `kVst3WarnInternalNumeric` warning. This catches any stability-bound violation that survives the macro clamp; in normal operation it never fires.

## 7. SIMD layout and vectorisation

### 7.1 1D substrate, SSE2/NEON (4-wide)

Process cells in groups of 4. The Laplacian needs cells `x-1, x, x+1`, so loads are unaligned for the neighbours. Use `_mm_loadu_ps` / `vld1q_f32` for unaligned loads; aligned stores are fine.

Reference inner loop (SSE2 intrinsics, conceptual):

```c++
__m128 vc2 = _mm_set1_ps(c2);
__m128 vk  = _mm_set1_ps(k);
__m128 vg  = _mm_set1_ps(1.0f - gamma);

for (int x = 0; x < N; x += 4) {
    __m128 u_c  = _mm_load_ps(&u[x]);
    __m128 u_l  = _mm_loadu_ps(&u[(x - 1 + N) & (N - 1)]);   // wrap handled in scalar prologue/epilogue
    __m128 u_r  = _mm_loadu_ps(&u[(x + 1) & (N - 1)]);
    __m128 v_c  = _mm_load_ps(&v[x]);
    __m128 v_l  = _mm_loadu_ps(&v[(x - 1 + N) & (N - 1)]);
    __m128 v_r  = _mm_loadu_ps(&v[(x + 1) & (N - 1)]);
    __m128 inj  = _mm_load_ps(&u_inject[x]);

    __m128 lap_u = _mm_sub_ps(_mm_add_ps(u_l, u_r), _mm_add_ps(u_c, u_c));
    __m128 lap_v = _mm_sub_ps(_mm_add_ps(v_l, v_r), _mm_add_ps(v_c, v_c));

    __m128 v_new = _mm_add_ps(_mm_mul_ps(vg, v_c),
                              _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc2, lap_u),
                                                    _mm_mul_ps(vk,  lap_v)),
                                         inj));
    __m128 u_new = _mm_add_ps(u_c, v_new);

    _mm_store_ps(&v[x], v_new);
    _mm_store_ps(&u[x], u_new);
}
// Then handle the two boundary lanes (x = 0 and x = N-4..N-1) scalar to manage wrap.
```

The SSE2 path delivers ~3× speedup over scalar on x86_64; AVX2 (8-wide) delivers ~6×. NEON delivers ~3× on Apple Silicon.

### 7.2 SIMD abstraction

We do **not** write three handwritten paths. We use a single `simd<4>` abstraction (built on top of [SIMDe](https://github.com/simd-everywhere/simde) or [xsimd](https://github.com/xtensor-stack/xsimd)) that compiles to SSE2, NEON, or scalar. AVX2 is built as a runtime-dispatched fast path: at engine init we detect support and pick the kernel pointer. AVX-512 is reserved for v1.x.

### 7.3 Cost summary (per voice, per audio sample)

| Substrate | Cells | Scalar | SSE2/NEON | AVX2 |
|---|---|---|---|---|
| 1D, N=1024 | 1024 | ~14 µs | ~5 µs | ~3 µs |
| 1D, N=4096 | 4096 | ~56 µs | ~20 µs | ~12 µs |
| 2D, 64×64 | 4096 | ~70 µs | ~25 µs | ~14 µs |
| 2D, 256×256 | 65536 | ~1.1 ms | ~400 µs | ~220 µs |

Numbers are per-call; at 48 kHz a single 1D-1024 voice on SSE2 consumes ~24% of one core. 8 voices is feasible only with downsampling (`S = 2`) or AVX2.

## 8. Substrate parameters in detail

### 8.1 `c²` (TENSION) — squared wave-speed

* Range: `[0.0, 0.49]` (clamped jointly with `κ` per §2.2)
* Default: `0.30`
* Curve from macro: `c² = 0.49 · TENSION^2` (quadratic, gives finer control at low values)
* Sonic effect: at `0.0`, no propagation — agents only deposit locally and the substrate behaves as a parallel summing bus. At maximum `c² = 0.49`, the wave speed is `c = √0.49 ≈ 0.7` cells per sample. Full-substrate traversal of `N = 1024` cells therefore takes `N/c ≈ 1463 samples`, which at 48 kHz is roughly **30 ms**. (An earlier draft of this document mistakenly stated "30 samples" — a units error.) Standing-wave modes arrange themselves around this traversal time: the lowest mode rings at roughly `c · fs / (2N) ≈ 16 Hz` for the maximum-tension case, and the substrate's audible timbral content lives in standing waves whose mode numbers `m` produce frequencies `m · 16 Hz` up through tens of kHz.

### 8.2 `κ` (VISCOSITY, internal) — velocity diffusion

* Range: `[0.0, 0.45]` (clamped jointly with `c²`)
* Default: `0.05`
* Curve from macro: derived from `DAMPING` macro indirectly (see 05); also affected by `EXCITATION` macro for nonlinear regimes.
* Sonic effect: `κ = 0` is a "lossless" propagation (combined with `γ = 0`, the substrate rings forever). Higher `κ` softens the substrate — high frequencies decay faster than low.

### 8.3 `γ` (DAMPING) — per-step loss

* Range: `[0.0, 0.05]`
* Default: `0.005`
* Curve from macro: `γ = 0.05 · DAMPING^2`
* Sonic effect: this is the substrate's "release time" knob. At `γ = 0.001`, substrate energy decays slowly (note tails of 5+ seconds); at `γ = 0.05`, decay is sub-100 ms.

### 8.4 `N` (substrate.size)

* Range: `{256, 512, 1024, 2048, 4096}`
* Default: `1024`
* Effect: larger `N` gives more standing-wave modes and more spatial resolution for harvester placement; larger `N` is proportionally more expensive.

### 8.5 `topology`

* Enum: `{ring, torus_64, torus_128, torus_256, mobius (reserved), klein (reserved)}`
* Default: `ring`

### 8.6 `aspect`

* Continuous parameter in 2D mode: aspect ratio of the toroidal grid.
* Range: `[0.5, 2.0]`, default `1.0`
* Effect: changes the mode set of the 2D substrate (modes at `(m, n)` with `m, n ≠ 0`); audible as a shift in standing-wave pitch ratios.

## 9. Optional nonlinear extensions

The base substrate is linear in `u` and `v` (only the cross-product `c² · ∇²u` is bilinear in coefficient × state). A **soft nonlinear** extension is reserved for v1.x and described here for forward compatibility:

```
v[x, n+1] += -β · u[x, n]^3
```

A cubic restoring force adds soft saturation and pitch-bend behaviour as displacement grows — the substrate's effective wave-speed becomes amplitude-dependent. `β` would be exposed as a `STIFFNESS` macro. Stability requires `β` small (`< 0.01`) and additional headroom in the joint `(c² + κ)` clamp.

This is documented now so the substrate struct reserves a `nonlinear_beta` field at the cost of 4 bytes per voice.

## 10. Reference implementation

```c++
class Substrate1D {
public:
    Substrate1D(size_t cellCount, float sampleRate);
    void reset();                                  // zero u, v
    void setCoefficients(float c2, float kappa, float gamma);

    // The canonical per-sample API (call exactly once per audio sample)
    void deposit(float position, float amount);    // called by agents; accumulates into uInject
    void step();                                   // consumes uInject; advances v, u; zeros uInject
    float read(float position) const;              // harvester read (linear interp)

    // Convenience wrapper for use cases that batch-process N samples
    // with no agent interaction (e.g., offline rendering of substrate-only test fixtures).
    // Equivalent to calling step() N times. Should NOT be used in the live engine; use
    // engineStep() in 01 §5.4 instead.
    void processNoDeposits(size_t numSamples);

    // Visualisation snapshot (called by GUI thread)
    void snapshot(float* dst, size_t dstSize) const;

private:
    alignas(64) std::vector<float> u, v;
    std::vector<float> uInject;                    // per-sample deposit accumulator
    DcBlocker dcBlocker;                            // per-cell state; runs every step()
    float c2, kappa, gamma, alphaDc;
    size_t cellCount;
    size_t cellMask;                               // cellCount - 1 for power-of-two
};
```

The same structure applies to `Substrate2D` with `(width, height)` instead of `cellCount` and 2D arrays.

The substrate has no `process(N)` method that internally batches — the canonical contract is exactly one `step()` per audio sample, interleaved with agent deposits per the schedule in 01 §5.4. `deposit()` may be called multiple times per sample (once per agent) before `step()` consumes the accumulated `uInject`. After `step()`, `uInject` is zeroed and ready for the next sample's agent loop. `read()` may be called multiple times per sample (once per harvester, once per agent's bend read) and returns the current substrate value.

## 11. Test vectors

To validate an implementation, the spec provides reference test vectors. Each test fixes `(N, c², κ, γ, deposits)` and provides the expected `u[x]` after a fixed number of samples, in a JSON file shipped with the repo. v1.0 ships with 12 test vectors covering:

* Pure ring (zero loss): an impulse at cell 0 should reach cell `N/2` at sample `N/(2c)`.
* Lossy: same impulse, with `γ = 0.01`, displacement should decay exponentially.
* Standing wave: two anti-phase impulses at opposite cells should produce a stable mode.
* Two-agent interference: two sinusoidal injections at fixed positions should produce a beat pattern at the harvester whose frequency matches the difference.

Test vectors are bit-exact at single precision. Implementation must match to within 1 ULP across all supported platforms.
