// src/dsp/dm_cos.h
//
// Deterministic cosine. Same accuracy bound as dm_sin (Phase 2 budget
// 1e-6 in float32) — implemented as cos(x) = sin(x + π/2). One extra
// add and the same range reduction; no separate polynomial.

#pragma once

#include "dm_sin.h"

namespace sfs::dsp
{

[[nodiscard]] inline float dm_cos(float x) noexcept
{
    constexpr float kHalfPi = 1.5707963267948966f;
    return dm_sin(x + kHalfPi);
}

} // namespace sfs::dsp
