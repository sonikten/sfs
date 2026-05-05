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
    if (c2 < 0.0f)
        c2 = 0.0f;
    if (kappa < 0.0f)
        kappa = 0.0f;
    if (gamma < 0.0f)
        gamma = 0.0f;
    if (gamma > 0.99f)
        gamma = 0.99f;

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

    // Phase B: integrate u, commit v. NO DC block here — that lives at the
    // harvester output (Voice::renderBlock) so the substrate state stays
    // conservative under the wave equation's energy theorem.
    for (int x = 0; x < N; ++x)
    {
        u[x] += vNew[x];
        v[x] = vNew[x];
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
