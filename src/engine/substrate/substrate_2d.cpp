// src/engine/substrate/substrate_2d.cpp
//
// 2D substrate scalar reference. See substrate_2d.h for API contract.

#include "substrate_2d.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace sfs::engine::substrate
{

namespace
{

[[maybe_unused, nodiscard]] bool isPowerOfTwo(int x) noexcept
{
    return x > 0 && (x & (x - 1)) == 0;
}

} // namespace

Substrate2D::Substrate2D(int cellsX, int cellsY, float sampleRate)
    : cellsX_(cellsX),
      cellsY_(cellsY),
      maskX_(cellsX - 1),
      maskY_(cellsY - 1),
      sampleRate_(sampleRate),
      u_(static_cast<std::size_t>(cellsX * cellsY), 0.0f),
      v_(static_cast<std::size_t>(cellsX * cellsY), 0.0f),
      uInject_(static_cast<std::size_t>(cellsX * cellsY), 0.0f),
      vNewBuf_(static_cast<std::size_t>(cellsX * cellsY), 0.0f)
{
    assert(isPowerOfTwo(cellsX) && cellsX >= kMinCellsPerAxis && cellsX <= kMaxCellsPerAxis);
    assert(isPowerOfTwo(cellsY) && cellsY >= kMinCellsPerAxis && cellsY <= kMaxCellsPerAxis);
    assert(sampleRate > 0.0f);
    setCoefficients(c2_, kappa_, gamma_);
}

void Substrate2D::reset() noexcept
{
    std::fill(u_.begin(), u_.end(), 0.0f);
    std::fill(v_.begin(), v_.end(), 0.0f);
    std::fill(uInject_.begin(), uInject_.end(), 0.0f);
}

void Substrate2D::setCoefficients(float c2, float kappa, float gamma) noexcept
{
    c2 = std::max(c2, 0.0f);
    kappa = std::max(kappa, 0.0f);
    gamma = std::clamp(gamma, 0.0f, 0.99f);

    const float sum = c2 + kappa;
    if (sum > kCflBound2D)
    {
        const float scale = kCflBound2D / sum;
        c2 *= scale;
        kappa *= scale;
    }

    c2_ = c2;
    kappa_ = kappa;
    gamma_ = gamma;
    oneMinusGamma_ = 1.0f - gamma;
}

void Substrate2D::deposit(float x, float y, float amount) noexcept
{
    const float nx = static_cast<float>(cellsX_);
    const float ny = static_cast<float>(cellsY_);
    float px = std::fmod(x, nx);
    if (px < 0.0f)
    {
        px += nx;
    }
    float py = std::fmod(y, ny);
    if (py < 0.0f)
    {
        py += ny;
    }

    const int x0 = static_cast<int>(px);
    const int y0 = static_cast<int>(py);
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const int x1 = (x0 + 1) & maskX_;
    const int y1 = (y0 + 1) & maskY_;

    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;

    uInject_[indexOf(x0, y0)] += amount * w00;
    uInject_[indexOf(x1, y0)] += amount * w10;
    uInject_[indexOf(x0, y1)] += amount * w01;
    uInject_[indexOf(x1, y1)] += amount * w11;
}

void Substrate2D::step() noexcept
{
    const int nx = cellsX_;
    const int ny = cellsY_;
    const int mx = maskX_;
    const int my = maskY_;
    const float c2 = c2_;
    const float kp = kappa_;
    const float omg = oneMinusGamma_;
    const int total = nx * ny;

    auto* const u = u_.data();
    auto* const v = v_.data();
    auto* const uInject = uInject_.data();
    auto* const vNew = vNewBuf_.data();

    // Phase A: compute new v from current u and v 5-point Laplacians.
    for (int y = 0; y < ny; ++y)
    {
        const int yU = ((y - 1 + ny) & my) * nx;
        const int yD = ((y + 1) & my) * nx;
        const int yC = y * nx;
        for (int x = 0; x < nx; ++x)
        {
            const int xL = (x - 1 + nx) & mx;
            const int xR = (x + 1) & mx;
            const int idx = yC + x;
            const float lapU = u[yU + x] + u[yD + x] + u[yC + xL] + u[yC + xR] - 4.0f * u[idx];
            const float lapV = v[yU + x] + v[yD + x] + v[yC + xL] + v[yC + xR] - 4.0f * v[idx];
            vNew[idx] = omg * v[idx] + c2 * lapU + kp * lapV + uInject[idx];
        }
    }

    // Phase B: integrate u, commit v.
    for (int i = 0; i < total; ++i)
    {
        u[i] += vNew[i];
        v[i] = vNew[i];
    }

    // Spatial DC removal (k=0,0 mode projection). Same rationale as 1D:
    // the wave equation alone doesn't damp the DC mode; without removal,
    // asymmetric agent deposits over time accumulate a constant offset
    // across the grid that the harvester output's high-pass alone can't
    // catch up with.
    double meanD = 0.0;
    for (int i = 0; i < total; ++i)
    {
        meanD += static_cast<double>(u[i]);
    }
    const float mean = static_cast<float>(meanD / static_cast<double>(total));
    if (std::fabs(mean) > 0.0f)
    {
        for (int i = 0; i < total; ++i)
        {
            u[i] -= mean;
        }
    }

    // Runaway clamp — same ±20 bound as 1D (sfs-spec/02 §3 acceptance:
    // degenerate parameter corners must not blow up).
    constexpr float kUMax = 20.0f;
    constexpr float kVMax = 20.0f;
    for (int i = 0; i < total; ++i)
    {
        u[i] = std::clamp(u[i], -kUMax, kUMax);
        v[i] = std::clamp(v[i], -kVMax, kVMax);
    }

    std::fill(uInject_.begin(), uInject_.end(), 0.0f);
}

float Substrate2D::read(float x, float y) const noexcept
{
    const float nx = static_cast<float>(cellsX_);
    const float ny = static_cast<float>(cellsY_);
    float px = std::fmod(x, nx);
    if (px < 0.0f)
    {
        px += nx;
    }
    float py = std::fmod(y, ny);
    if (py < 0.0f)
    {
        py += ny;
    }

    const int x0 = static_cast<int>(px);
    const int y0 = static_cast<int>(py);
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const int x1 = (x0 + 1) & maskX_;
    const int y1 = (y0 + 1) & maskY_;

    const float u00 = u_[indexOf(x0, y0)];
    const float u10 = u_[indexOf(x1, y0)];
    const float u01 = u_[indexOf(x0, y1)];
    const float u11 = u_[indexOf(x1, y1)];

    return (1.0f - fx) * (1.0f - fy) * u00 + fx * (1.0f - fy) * u10 + (1.0f - fx) * fy * u01 + fx * fy * u11;
}

void Substrate2D::processNoDeposits(std::size_t numSamples) noexcept
{
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        step();
    }
}

void Substrate2D::snapshot(float* dst, std::size_t dstSize) const noexcept
{
    const std::size_t n = std::min(dstSize, u_.size());
    std::copy(u_.begin(), u_.begin() + static_cast<std::ptrdiff_t>(n), dst);
}

} // namespace sfs::engine::substrate
