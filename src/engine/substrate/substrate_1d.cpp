// src/engine/substrate/substrate_1d.cpp
//
// 1D substrate scalar reference. See substrate_1d.h for API contract.
// Phase 1: scalar only. Correctness first; SIMD comes when the CPU profile
// says it's needed.

#include "substrate_1d.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace sfs::engine::substrate
{

namespace
{

// (DC blocker constants live in voice.cpp now — DC removal happens at the
// harvester output, not on the substrate state.)

// Used in the constructor's assert; under NDEBUG (release builds) the assert
// compiles away and this would be dead code without [[maybe_unused]].
[[maybe_unused, nodiscard]] bool isPowerOfTwo(int x) noexcept
{
    return x > 0 && (x & (x - 1)) == 0;
}

} // namespace

Substrate1D::Substrate1D(int cellCount, float sampleRate)
    : cellCount_(cellCount),
      cellMask_(cellCount - 1),
      sampleRate_(sampleRate),
      u_(static_cast<std::size_t>(cellCount), 0.0f),
      v_(static_cast<std::size_t>(cellCount), 0.0f),
      uInject_(static_cast<std::size_t>(cellCount), 0.0f),
      vNewBuf_(static_cast<std::size_t>(cellCount), 0.0f)
{
    assert(isPowerOfTwo(cellCount) && cellCount >= kMinCells && cellCount <= kMaxCells);
    assert(sampleRate > 0.0f);
    setCoefficients(c2_, kappa_, gamma_);
}

void Substrate1D::reset() noexcept
{
    std::fill(u_.begin(), u_.end(), 0.0f);
    std::fill(v_.begin(), v_.end(), 0.0f);
    std::fill(uInject_.begin(), uInject_.end(), 0.0f);
}

void Substrate1D::setCoefficients(float c2, float kappa, float gamma) noexcept
{
    c2 = std::max(c2, 0.0f);
    kappa = std::max(kappa, 0.0f);
    gamma = std::clamp(gamma, 0.0f, 0.99f);

    // Joint CFL clamp (sfs-spec/02 §2.2): if c² + κ > kCflBound1D, scale both
    // proportionally. The macro layer (Phase 2) will enforce this softly via
    // its TENSION/DAMPING fan-out; here we apply a hard scale so direct
    // setCoefficients() callers can't violate the bound.
    const float sum = c2 + kappa;
    if (sum > kCflBound1D)
    {
        const float scale = kCflBound1D / sum;
        c2 *= scale;
        kappa *= scale;
    }

    c2_ = c2;
    kappa_ = kappa;
    gamma_ = gamma;
    oneMinusGamma_ = 1.0f - gamma;
}

void Substrate1D::deposit(float position, float amount) noexcept
{
    // Wrap to [0, cellCount_).
    const float n = static_cast<float>(cellCount_);
    float p = std::fmod(position, n);
    if (p < 0.0f)
    {
        p += n;
    }

    const int x0 = static_cast<int>(p);
    const float frac = p - static_cast<float>(x0);
    const int x1 = (x0 + 1) & cellMask_;

    uInject_[static_cast<std::size_t>(x0 & cellMask_)] += amount * (1.0f - frac);
    uInject_[static_cast<std::size_t>(x1)] += amount * frac;
}

void Substrate1D::step() noexcept
{
    const int N = cellCount_;
    const int mask = cellMask_;
    const float c2 = c2_;
    const float kp = kappa_;
    const float omg = oneMinusGamma_;

    auto* const u = u_.data();
    auto* const v = v_.data();
    auto* const uInject = uInject_.data();
    auto* const vNew = vNewBuf_.data();

    // Two-pass to keep the v Laplacian reading the OLD v values, not the
    // newly-updated ones. The vNew buffer is a Substrate1D member to keep
    // step() allocation-free (audio-thread invariant; sfs-spec/01 §4).

    // Phase A: compute new v from current u and v Laplacians + injection.
    for (int x = 0; x < N; ++x)
    {
        const int xL = (x - 1 + N) & mask;
        const int xR = (x + 1) & mask;
        const float lapU = u[xL] - 2.0f * u[x] + u[xR];
        const float lapV = v[xL] - 2.0f * v[x] + v[xR];
        vNew[x] = omg * v[x] + c2 * lapU + kp * lapV + uInject[x];
    }

    // Phase B: integrate u, commit v.
    for (int x = 0; x < N; ++x)
    {
        u[x] += vNew[x];
        v[x] = vNew[x];
    }

    // Spatial DC removal — projects out the k=0 mode of u (a uniform
    // displacement across the ring). Unlike a per-cell first-order
    // high-pass (which suppresses all low-frequency modes and breaks the
    // wave equation's energy conservation), this only kills the truly
    // uniform offset. Physical AC modes (k ≥ 1) all have ∇²u ≠ 0 and
    // average to zero spatially, so subtracting the mean leaves them
    // untouched. Mandatory because asymmetric agent deposits during ADSR
    // ramps + migration produce a slow DC accumulation that the harvester
    // output's high-pass alone can't keep up with at high MIGRATION /
    // EXCITATION; see commit notes for the empirical RMS / ZC trace.
    double meanD = 0.0;
    for (int x = 0; x < N; ++x)
    {
        meanD += static_cast<double>(u[x]);
    }
    const float mean = static_cast<float>(meanD / static_cast<double>(N));
    if (std::fabs(mean) > 0.0f)
    {
        for (int x = 0; x < N; ++x)
        {
            u[x] -= mean;
        }
    }

    // Runaway clamp. The wave equation is well-posed under the CFL bound,
    // but degenerate parameter corners (c²≈0 + γ≈0 + non-zero deposits)
    // turn it into a pure integrator that grows linearly without bound.
    // A hard ±U_MAX clamp on u is a safety net so the user can crank macros
    // to extremes without the engine blowing up; audibly the substrate
    // saturates instead of NaN'ing. Picked U_MAX=20 — well above any
    // physically sensible substrate amplitude, low enough to keep the
    // harvester read + soft-clip in a sane range. Also clamps v so a
    // degenerate config can't pump kinetic energy into infinity.
    constexpr float kUMax = 20.0f;
    constexpr float kVMax = 20.0f;
    for (int x = 0; x < N; ++x)
    {
        if (u[x] > kUMax)
        {
            u[x] = kUMax;
        }
        else if (u[x] < -kUMax)
        {
            u[x] = -kUMax;
        }
        if (v[x] > kVMax)
        {
            v[x] = kVMax;
        }
        else if (v[x] < -kVMax)
        {
            v[x] = -kVMax;
        }
    }

    std::fill(uInject_.begin(), uInject_.end(), 0.0f);
}

float Substrate1D::read(float position) const noexcept
{
    const float n = static_cast<float>(cellCount_);
    float p = std::fmod(position, n);
    if (p < 0.0f)
    {
        p += n;
    }

    const int x0 = static_cast<int>(p);
    const float frac = p - static_cast<float>(x0);
    const int x1 = (x0 + 1) & cellMask_;

    return (1.0f - frac) * u_[static_cast<std::size_t>(x0 & cellMask_)] + frac * u_[static_cast<std::size_t>(x1)];
}

void Substrate1D::processNoDeposits(std::size_t numSamples) noexcept
{
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        step();
    }
}

void Substrate1D::snapshot(float* dst, std::size_t dstSize) const noexcept
{
    const std::size_t n = std::min(dstSize, static_cast<std::size_t>(cellCount_));
    std::copy(u_.begin(), u_.begin() + static_cast<std::ptrdiff_t>(n), dst);
}

} // namespace sfs::engine::substrate
