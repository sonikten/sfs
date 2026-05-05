// src/dsp/dm_sqrt.h
//
// Deterministic square root. Per sfs-spec/06 §1.6: "sqrtf is on most
// platforms a single hardware instruction with IEEE 754 correct rounding;
// we treat it as deterministic when the engine sets round-to-nearest mode
// at thread entry. dm_sqrt may simply call sqrtf."
//
// Wrapping it in our namespace anyway so a future Phase if we discover
// any platform whose sqrtf isn't IEEE-correct (we haven't), we have one
// place to swap in a custom implementation.

#pragma once

#include <cmath>

namespace sfs::dsp
{

[[nodiscard]] inline float dm_sqrt(float x) noexcept
{
    return std::sqrt(x);
}

} // namespace sfs::dsp
