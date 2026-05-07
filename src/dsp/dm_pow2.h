// src/dsp/dm_pow2.h
//
// Deterministic 2^x, bit-exact across platforms. Intended for block-rate
// pitch-bend ratio computation (semitones → frequency multiplier) and
// any future code that needs exponential pitch scaling.
//
// Algorithm:
//   1. Split x into integer and fractional parts: e = floor(x), f = x - e.
//      f is in [0, 1).
//   2. Polynomial approximation for 2^f on [0, 1] (5th-degree minimax-
//      style fit, worst-case |relative err| < 1e-4 — about 0.17 cents,
//      sub-perceptible; 1 cent ratio ≈ 5.78e-4).
//   3. Reconstitute: 2^x = 2^f · 2^e via std::ldexp.
//
// std::floor and std::ldexp are NOT transcendental: they're bit/exponent
// manipulation fully specified by IEEE 754, so they're allowed in
// src/engine/ even under the "polynomials only" determinism rule.
//
// Domain: any finite float. For huge |x| the result will overflow to
// inf or denormal (caller is responsible for keeping the input in a
// reasonable range; pitch bend caps at ±48 semitones = ±4 octaves so
// x stays in [-4, 4]).

#pragma once

namespace sfs::dsp
{

[[nodiscard]] float dm_pow2(float x) noexcept;

} // namespace sfs::dsp
