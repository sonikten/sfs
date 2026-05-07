// src/preset/preset_engine.h
//
// Phase 4 §A2 — push a Preset into a live VoiceManager.
//
// The conversion is intentionally permissive: unknown enum strings fall
// back to documented defaults rather than throwing, so a preset authored
// in a future engine version (with new mod-matrix sources, say) still
// loads in the current engine — just without the new routing applied.
// This matches Doc 06 §3.4's forward-compatibility policy.
//
// Conversions applied:
//   - macros (6 floats) → VoiceManager::macros()
//   - structural.topology string → Topology enum (ring → Ring1D, torus_*
//                                                  → Torus2D)
//   - agents.shape_distribution → uniform shape pick (Phase 4 stub: the
//     dominant entry; full per-agent distribution lands in Phase 5+)
//   - env1 (seconds) → setAdsr(ms)  (Phase 4: env2 deferred — engine
//                                     hasn't wired a second envelope yet)
//   - lfos[i] → setLfoConfig(i, rate_hz, shape)
//   - modulation_matrix.slots[i] → setModMatrixSlot(i, source, dest,
//                                                    active ? depth : 0)
//
// Not yet applied (engine doesn't expose a setter):
//   - structural.aspect, substrate_size, deposit_kernel, read_kernel
//   - agents.harmonic_set, fm_ratio_index, fm_index_index
//   - harvesters.*
//   - output.*
// These persist round-trip through the JSON layer so future engine
// updates can pick them up without preset-format churn.

#pragma once

#include "preset/preset.h"

namespace sfs::engine
{
class VoiceManager;
}

namespace sfs::preset
{

// Apply every supported field of `p` to `vm`. Values not yet wired
// through VoiceManager are silently ignored (they round-trip through
// the JSON layer for future engine versions).
void applyToEngine(const Preset& p, sfs::engine::VoiceManager& vm) noexcept;

} // namespace sfs::preset
