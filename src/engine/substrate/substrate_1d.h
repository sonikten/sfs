// src/engine/substrate/substrate_1d.h
//
// 1D substrate: ring topology, displacement u[] + velocity v[], leapfrog
// symplectic update, linear deposit + read kernels, per-sample DC block at
// 5 Hz. Per sfs-spec/02 §2 (1D update) and §10 (reference impl).
//
// API contract (sfs-spec/02 §10):
//   Per audio sample, the engine glue does exactly:
//     1. for each agent: substrate.deposit(position, amount)
//     2. substrate.step()
//     3. for each harvester: out += substrate.read(position)
//   No batched process(N) — the substrate is interleaved with agents per
//   sample. processNoDeposits(N) exists for offline test fixtures only.
//
// Phase 1: pure scalar implementation, no SIMD. The SSE2/NEON/AVX2 paths
// from sfs-spec/02 §7 are deferred to whenever the CPU profile says they
// pay off (Phase 1 §9 step 15 — optional).

#pragma once

#include <cstddef>
#include <vector>

namespace sfs::engine::substrate
{

class Substrate1D
{
public:
    // CFL clamp upper bound: c² + κ ≤ 0.475 in 1D (sfs-spec/02 §2.2 — internal
    // clamp with 5% margin below the analytic 0.5 bound).
    static constexpr float kCflBound1D = 0.475f;

    // Sane substrate sizes per sfs-spec/02 §1 / §8.4. Power-of-two is required
    // for the modulo-by-mask wrap.
    static constexpr int kMinCells = 256;
    static constexpr int kMaxCells = 4096;

    Substrate1D(int cellCount, float sampleRate);

    // Zero u, v, and uInject. Leaves coefficients and DC-block state intact.
    void reset() noexcept;

    // Set wave-speed², viscosity, loss. Applies the joint CFL clamp transparently:
    // if (c² + κ) > kCflBound1D, both are scaled proportionally to fit the bound.
    // Negative inputs are clamped to 0; γ is clamped to [0, 1).
    void setCoefficients(float c2, float kappa, float gamma) noexcept;

    // Linear deposit kernel (sfs-spec/02 §5.1):
    //   u_inject[floor(p)]   += amount * (1 - frac)
    //   u_inject[floor(p)+1] += amount * frac
    // p wraps modulo cellCount. Amount may be negative.
    void deposit(float position, float amount) noexcept;

    // Leapfrog symplectic update. Consumes u_inject (zeros it after).
    //   v[x, n+1] = (1 - γ) v[x, n] + c² Lap_u[x] + κ Lap_v[x] + u_inject[x]
    //   u[x, n+1] = u[x, n] + v[x, n+1]
    //
    // Note: the DC blocker described in sfs-spec/02 §6 is intentionally NOT
    // applied to the substrate state here. Applying a DC block to u in-place
    // every sample (as §6 originally prescribes) violates the wave equation's
    // discrete energy conservation and causes the substrate to pump energy
    // over multi-second horizons (see commit notes for the empirical trace).
    // The DC removal happens at the harvester output instead — same audible
    // result, conservative substrate, and cheaper (one filter per harvester
    // instead of one per cell).
    void step() noexcept;

    // Linear read kernel (sfs-spec/04 §2.1):
    //   read(p) = (1-frac) u[floor(p)] + frac u[floor(p)+1]
    [[nodiscard]] float read(float position) const noexcept;

    // Offline-only convenience for substrate-test fixtures.
    void processNoDeposits(std::size_t numSamples) noexcept;

    // Read-only snapshot for the visualiser (GUI thread reads, audio thread
    // writes via step()).
    void snapshot(float* dst, std::size_t dstSize) const noexcept;

    // Inspectors (no audio-thread state mutation).
    [[nodiscard]] int size() const noexcept { return cellCount_; }
    [[nodiscard]] float c2() const noexcept { return c2_; }
    [[nodiscard]] float kappa() const noexcept { return kappa_; }
    [[nodiscard]] float gamma() const noexcept { return gamma_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] const float* displacement() const noexcept { return u_.data(); }
    [[nodiscard]] const float* velocity() const noexcept { return v_.data(); }

private:
    int cellCount_ = 0;
    int cellMask_ = 0; // cellCount_ - 1, for power-of-two modulo
    float sampleRate_ = 48000.0f;

    float c2_ = 0.30f;
    float kappa_ = 0.05f;
    float gamma_ = 0.005f;
    float oneMinusGamma_ = 0.995f;

    // 64-byte aligned via vector<float>'s allocator + size guarantees on the
    // platforms we ship to. Phase 1 stays scalar; SIMD pass would tighten the
    // alignment with a custom allocator.
    std::vector<float> u_;       // displacement
    std::vector<float> v_;       // velocity
    std::vector<float> uInject_; // per-sample deposit accumulator
};

} // namespace sfs::engine::substrate
