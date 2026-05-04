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
// Phase 0 implementation: 5th-order odd minimax polynomial on the symmetry
// range [-π/2, π/2], with explicit modulo-2π range reduction. Phase 1 will
// replace this with the spec-exact 5th-order polynomial that hits the
// 1e-6 worst-case error budget across [-π, π] (sfs-spec/02 §"deterministic
// math primitives").

#pragma once

namespace sfs::dsp
{

// Compute sin(x) to better than ~5e-6 absolute error on the entire real line.
// Pure function; no global state; thread-safe.
[[nodiscard]] float dm_sin(float x) noexcept;

} // namespace sfs::dsp
