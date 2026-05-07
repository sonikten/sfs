// src/engine/substrate/substrate_2d.h
//
// 2D substrate: toroidal topology, displacement u[Nx×Ny] + velocity
// v[Nx×Ny], leapfrog symplectic update with 5-point Laplacian,
// bilinear deposit + read kernels. Per sfs-spec/02 §3.
//
// API contract (mirrors Substrate1D):
//   Per audio sample, the engine glue does:
//     1. for each agent: substrate.deposit(x, y, amount)
//     2. substrate.step()
//     3. for each harvester: out += substrate.read(x, y)
//   Position arguments wrap modulo (Nx, Ny).
//
// CFL bound for 2D: c² + κ ≤ 0.225 (sfs-spec/02 §3.2 — half the 1D bound
// because the von Neumann stencil has twice the spectral radius of the
// 3-point 1D stencil at the same coefficients). Engine clamp = 0.225.
//
// Phase 3 ships pure scalar; SIMD pass deferred. Layout is row-major
// (x fastest), N = Nx × Ny power-of-two for the modulo-by-mask wrap.

#pragma once

#include <cstddef>
#include <vector>

namespace sfs::engine::substrate
{

class Substrate2D
{
public:
    // Joint CFL clamp: c² + κ ≤ 0.225 (5% slack below the analytic 0.25).
    static constexpr float kCflBound2D = 0.225f;

    // Sane substrate sizes per sfs-spec/02 §3.1. Per-axis counts must be
    // power-of-two (modulo-by-mask wrap). Practical range 32..256;
    // 32×32=1024 cells matches Phase 1 / Phase 2 1D total cell count.
    static constexpr int kMinCellsPerAxis = 32;
    static constexpr int kMaxCellsPerAxis = 256;

    Substrate2D(int cellsX, int cellsY, float sampleRate);

    // Zero u, v, and uInject. Leaves coefficients intact.
    void reset() noexcept;

    // Set wave-speed², viscosity, loss. Joint CFL clamp applied — if
    // (c² + κ) > kCflBound2D, both scale proportionally to fit. Negative
    // values clamp to 0; γ clamps to [0, 0.99].
    void setCoefficients(float c2, float kappa, float gamma) noexcept;

    // Bilinear deposit: amount distributes over the four cells closest
    // to (x, y), each weighted by (1 − fx) or fx times (1 − fy) or fy.
    // Position wraps modulo (cellsX_, cellsY_). Amount may be negative.
    void deposit(float x, float y, float amount) noexcept;

    // Leapfrog update (sfs-spec/02 §3.4):
    //   v[i,j,n+1] = (1−γ) v + c² Lap5(u) + κ Lap5(v) + u_inject
    //   u[i,j,n+1] = u + v
    // 5-point Laplacian: Lap5(f)[i,j] = f[i-1,j] + f[i+1,j] + f[i,j-1] +
    //                                   f[i,j+1] − 4 f[i,j]
    // After the leapfrog: spatial-mean removal (k=0,0 mode projection,
    // analogous to Substrate1D's k=0 removal — keeps DC accumulation
    // bounded over long sustains). Plus the same ±20 runaway clamp on
    // u and v so degenerate parameter corners stay well-behaved.
    void step() noexcept;

    // Bilinear read (sfs-spec/04 §3.6 — 2D harvester read kernel):
    //   read(x, y) = (1−fx)(1−fy) u[i,j]   + fx(1−fy) u[i+1,j]
    //              + (1−fx) fy   u[i,j+1] + fx fy     u[i+1,j+1]
    [[nodiscard]] float read(float x, float y) const noexcept;

    // Offline test fixture convenience.
    void processNoDeposits(std::size_t numSamples) noexcept;

    // Visualiser snapshot — copies u into dst (size = cellsX * cellsY).
    void snapshot(float* dst, std::size_t dstSize) const noexcept;

    // Inspectors.
    [[nodiscard]] int cellsX() const noexcept { return cellsX_; }
    [[nodiscard]] int cellsY() const noexcept { return cellsY_; }
    [[nodiscard]] int cellCount() const noexcept { return cellsX_ * cellsY_; }
    [[nodiscard]] float c2() const noexcept { return c2_; }
    [[nodiscard]] float kappa() const noexcept { return kappa_; }
    [[nodiscard]] float gamma() const noexcept { return gamma_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] const float* displacement() const noexcept { return u_.data(); }
    [[nodiscard]] const float* velocity() const noexcept { return v_.data(); }

private:
    [[nodiscard]] std::size_t indexOf(int x, int y) const noexcept
    {
        return static_cast<std::size_t>((y & maskY_) * cellsX_ + (x & maskX_));
    }

    int cellsX_ = 0;
    int cellsY_ = 0;
    int maskX_ = 0; // cellsX_ - 1
    int maskY_ = 0; // cellsY_ - 1
    float sampleRate_ = 48000.0f;

    float c2_ = 0.10f;
    float kappa_ = 0.02f;
    float gamma_ = 0.005f;
    float oneMinusGamma_ = 0.995f;

    std::vector<float> u_;       // displacement (row-major, x fastest)
    std::vector<float> v_;       // velocity
    std::vector<float> uInject_; // per-sample deposit accumulator
    std::vector<float> vNewBuf_; // leapfrog v scratch (member to keep step alloc-free)
};

} // namespace sfs::engine::substrate
