// src/dsp/dm_sin.cpp
//
// Phase 3 implementation of the deterministic sine. See dm_sin.h for
// contract.
//
// Algorithm:
//   1. Range-reduce x to r ∈ [-π, π] via Cody-Waite — π split into
//      (kPiHi + kPiLo) so the multi-chunk subtraction preserves
//      precision the single-float subtraction would lose.
//   2. Fold to the principal half-range [-π/2, π/2] using sin(π − r) =
//      sin(r), again via the Cody-Waite split.
//   3. Apply a 9th-order odd Taylor polynomial in double precision and
//      cast to float at the return. Coefficients are exact rational
//      forms (-1/n!) so the polynomial truncation is <<1e-7 in double.
//
// Worst-case absolute error in float32: <1e-6 across [-π, π]. Closes
// the Phase 2 deferred risk from sfs-spec/08 §9 + sfs-spec/06 §1.3.
//
// Cody-Waite reference: Cody & Waite, "Software Manual for the
// Elementary Functions" (1980). Two-chunk π split (kPiHi single-float
// approximation; kPiLo = float(π − kPiHi) captures the residual).
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

// Cody-Waite π split. kPiHi is the closest float32 to π; kPiLo is
// (float)(π − kPiHi) — the residual the single-float kPi loses.
//   true π        = 3.14159265358979323846...
//   (float32)π    = 3.14159274... (kPiHi)
//   π − kPiHi     = -8.7422777e-08 (kPiLo, negative because (float)π > π)
constexpr float kPiHi = 3.1415927f;
constexpr float kPiLo = -8.7422777e-08f;
constexpr float kTwoPiHi = 6.2831855f;      // (float)(2π)
constexpr float kTwoPiLo = -1.7484555e-07f; // (float)(2π − kTwoPiHi)
constexpr float kHalfPi = 1.5707963f;
constexpr float kInvTwoPi = 0.15915494f; // 1 / (2π)

// 11th-order odd Taylor polynomial on [-π/2, π/2]:
//   sin(r) ≈ c1·r + c3·r³ + c5·r⁵ + c7·r⁷ + c9·r⁹ + c11·r¹¹
// where c_(2k+1) = (-1)^k / (2k+1)!
// Truncation error bound: |x|¹³ / 13! < 1.6e-9 at x=π/2 — well under
// the spec's 1e-6 budget. Coefficients in double; the polynomial
// evaluates in double.
constexpr double kC1 = 1.0;
constexpr double kC3 = -1.0 / 6.0;
constexpr double kC5 = 1.0 / 120.0;
constexpr double kC7 = -1.0 / 5040.0;
constexpr double kC9 = 1.0 / 362880.0;
constexpr double kC11 = -1.0 / 39916800.0;

} // namespace

float dm_sin(float x) noexcept
{
    // 1. Cody-Waite range reduction to [-π, π].
    //    Standard: r = x − 2π · k.
    //    Cody-Waite: r = (x − kTwoPiHi · k) − kTwoPiLo · k.
    //    The two-stage subtraction preserves precision that the
    //    single-float 2π would lose, especially for large |x|.
    const float k = std::round(x * kInvTwoPi);
    float r = (x - kTwoPiHi * k) - kTwoPiLo * k;

    // 2. Fold to principal range [-π/2, π/2] via sin(π − r) = sin(r).
    //    Cody-Waite split: kPi − r = (kPiHi − r) + kPiLo.
    if (r > kHalfPi)
    {
        r = (kPiHi - r) + kPiLo;
    }
    else if (r < -kHalfPi)
    {
        r = (-kPiHi - r) - kPiLo;
    }

    // 3. 11th-order Taylor polynomial in double precision; cast at the
    //    return so the small high-order terms contribute correctly.
    const double rd = static_cast<double>(r);
    const double r2 = rd * rd;
    return static_cast<float>(rd * (kC1 + r2 * (kC3 + r2 * (kC5 + r2 * (kC7 + r2 * (kC9 + r2 * kC11))))));
}

} // namespace sfs::dsp
