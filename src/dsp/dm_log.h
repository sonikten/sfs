// src/dsp/dm_log.h
//
// Deterministic natural log. Phase 2 budget < 1e-3 absolute error
// (sfs-spec/06 §1.6 spec-quoted budget is 5e-5; Phase 2 ships the looser
// approximation and tightens in a follow-up commit if Box-Muller noise
// quality demands it).
//
// Algorithm:
//   1. Decompose: x = m · 2^e via std::frexp (allowed; not transcendental).
//      m ∈ [0.5, 1).
//   2. log(x) = log(m) + e · log(2)
//   3. log(m) for m in [0.5, 1) via 5-term Taylor of log(1+y) around y=0
//      with y = m - 1 ∈ [-0.5, 0).
//
// Domain: x must be > 0. Caller is responsible (we don't check; callers
// in Box-Muller clamp u1 ≥ 1e-7).

#pragma once

namespace sfs::dsp
{

[[nodiscard]] float dm_log(float x) noexcept;

} // namespace sfs::dsp
