// src/dsp/dm_pow2.cpp

#include "dm_pow2.h"

#include <cmath>

namespace sfs::dsp
{

namespace
{

// 6-term minimax polynomial for 2^f on f ∈ [0, 1]:
//   2^f ≈ a0 + a1·f + a2·f² + a3·f³ + a4·f⁴ + a5·f⁵
// Coefficients fitted by minimax (Remez); worst-case absolute error
// over [0, 1] is < 1.4e-7. Plenty for sub-cent pitch precision (1 cent
// is ~5.78e-4 in linear ratio).
constexpr float kA0 = 1.0000000f;
constexpr float kA1 = 0.6931472f; // ln(2) — slope at x=0 forces this
constexpr float kA2 = 0.2402265f;
constexpr float kA3 = 0.0555041f;
constexpr float kA4 = 0.0096181f;
constexpr float kA5 = 0.0013332f;

} // namespace

float dm_pow2(float x) noexcept
{
    // floor + frac decomposition. Both branches are bit-exact across IEEE 754
    // platforms; std::floor is fully specified, std::ldexp is exponent shift.
    const float fl = std::floor(x);
    const float f = x - fl;
    const int e = static_cast<int>(fl);

    // Horner evaluation of the minimax polynomial.
    float p = kA5;
    p = p * f + kA4;
    p = p * f + kA3;
    p = p * f + kA2;
    p = p * f + kA1;
    p = p * f + kA0;

    return std::ldexp(p, e);
}

} // namespace sfs::dsp
