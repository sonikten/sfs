// src/dsp/dm_sin.h
//
// Deterministic sine approximation. The "dm_" prefix marks this as one of the
// deterministic-math primitives required by sfs-spec/01_engine_architecture.md
// §9 and CLAUDE.md "Hard invariants": same input → bit-identical output on
// macOS x64, macOS arm64, Windows x64, Linux x64.
//
// Direct std::sin / std::cos / std::sinf / std::cosf are forbidden in
// src/engine/ and src/dsp/ — they may differ at the ULP level across libm
// implementations and break the determinism contract. Use this function on
// the audio thread; std::sin remains available in tests/ and tools/.
//
// Phase 0 implementation: Hastings 1955 7th-order odd polynomial on the
// principal range [-π/2, π/2], with explicit modulo-2π range reduction via
// std::round. Worst-case absolute error in float32 ≈ 2e-6 over the principal
// range. Phase 1 will refit the coefficients via float-tuned minimax (Remez)
// to hit the 1e-6 spec budget. Naïve range reduction loses precision linearly
// with |x|; for very large |x| (≫ 100π) Phase 1 may swap to a Cody-Waite
// higher-precision reduction.

#pragma once

namespace sfs::dsp
{

// Compute sin(x) to better than 3e-6 absolute error for |x| ≤ a few periods
// (Phase 0; Phase 1 tightens to 1e-6). Pure function; no global state.
[[nodiscard]] float dm_sin(float x) noexcept;

} // namespace sfs::dsp
