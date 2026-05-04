// src/dsp/dm_sin.cpp
//
// Phase 0 implementation of the deterministic sine. See dm_sin.h for contract.
//
// Algorithm:
//   1. Range-reduce x to r ∈ [-π, π] via r = x − 2π·round(x / 2π).
//   2. Fold to the principal half-range [-π/2, π/2] using sin(π − r) = sin(r)
//      and sin(−π − r) = sin(r).
//   3. Apply a 7th-order odd polynomial (Hastings 1955). Worst-case absolute
//      error in float32 arithmetic is ~2e-6 on [-π/2, π/2]. Phase 1 will
//      replace the Hastings coefficients with float-tuned minimax (Remez)
//      coefficients to hit the spec's 1e-6 budget.
//
// Determinism notes:
//   * All arithmetic is IEEE 754 single precision.
//   * `std::round` is allowed: it is a discrete operation specified to
//     round half-away-from-zero, which is bit-deterministic across platforms.
//   * `-fno-fast-math` / `/fp:precise` (set in the top-level CMakeLists.txt)
//     prevent the compiler from rewriting the polynomial Horner form into a
//     reordered FMA chain whose result would differ across architectures.

#include "dm_sin.h"

#include <cmath>

namespace sfs::dsp
{

namespace
{

constexpr float kPi       = 3.1415927f;
constexpr float kTwoPi    = 6.2831853f;
constexpr float kHalfPi   = 1.5707963f;
constexpr float kInvTwoPi = 0.15915494f; // 1 / (2π)

// Hastings 1955 7th-order odd polynomial,
//   sin(r) ≈ c1·r + c3·r³ + c5·r⁵ + c7·r⁷  on [-π/2, π/2]
// Worst-case absolute error in float32 ≈ 2e-6 over the principal range.
constexpr float kC1 = 0.99999660f;
constexpr float kC3 = -0.16664824f;
constexpr float kC5 = 0.0083064850f;
constexpr float kC7 = -0.00018363f;

} // namespace

float dm_sin(float x) noexcept
{
    // 1. Range reduce to [-π, π] without std::fmod (which has implementation-
    //    defined rounding for some inputs and is slower).
    const float k = std::round(x * kInvTwoPi);
    float r = x - kTwoPi * k;

    // 2. Fold to principal range [-π/2, π/2].
    if (r > kHalfPi)
    {
        r = kPi - r;
    }
    else if (r < -kHalfPi)
    {
        r = -kPi - r;
    }

    // 3. Horner-form polynomial.
    const float r2 = r * r;
    return r * (kC1 + r2 * (kC3 + r2 * (kC5 + r2 * kC7)));
}

} // namespace sfs::dsp
