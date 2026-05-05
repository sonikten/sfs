// src/engine/macros/macros.h
//
// Six primary user-facing macros (sfs-spec/05 §2 + §3). Each is a float in
// [0, 1] that fans out to one or more internal substrate/agent/harvester
// fields via spec-mandated curves. The fan-out functions live here so the
// math is testable without dragging Voice / Substrate / AgentPool in.
//
// Per-parameter smoothing (one-pole low-pass at 10-200 ms — sfs-spec/05
// §4) is applied at the macro layer (input clamped, smoothed, then
// fanned out). Phase 2 ships block-rate fan-out + sample-rate smoothing
// on the macro outputs that Voice consumes.

#pragma once

#include <algorithm>

namespace sfs::engine::macros
{

// Six raw macro values, each clamped to [0, 1]. The plug-in's host
// parameters write into these directly; the smoother and fan-out read.
struct MacroValues
{
    float tension = 0.5f;
    float damping = 0.3f;
    float density = 0.6f;
    float migration = 0.2f;
    float coherence = 0.8f;
    float excitation = 0.3f;

    void clampInPlace() noexcept
    {
        tension = std::clamp(tension, 0.0f, 1.0f);
        damping = std::clamp(damping, 0.0f, 1.0f);
        density = std::clamp(density, 0.0f, 1.0f);
        migration = std::clamp(migration, 0.0f, 1.0f);
        coherence = std::clamp(coherence, 0.0f, 1.0f);
        excitation = std::clamp(excitation, 0.0f, 1.0f);
    }
};

// The full fan-out result: every internal field a macro can drive,
// computed in one pass from a single MacroValues snapshot. Unmentioned
// fields (e.g. structural.topology) are not macro-driven and stay at
// their preset/default value.
struct InternalFields
{
    // Substrate
    float substrateC2 = 0.30f;
    float substrateGamma = 0.005f;
    float substrateViscosityFloor = 0.005f;
    float substrateViscosityOffset = 0.0f;

    // Agent
    int agentActiveCount = 32;
    float agentDepositWeightScale = 1.0f;
    float agentDriftScale = 0.0f;
    float agentMigrationNoiseScale = 0.0f;
    float agentHarmonicLock = 1.0f;
    float agentDetuneScale = 0.0f;
    float agentShapeParamDrift = 0.0f;
    float agentModSensitivityScale = 0.0f;
    float agentEnvelopeReleaseScale = 1.0f;
    float agentFrequencyClampSoftness = 1.0f;

    // Harvester
    float harvesterSpacingScale = 1.0f;
    float harvesterOrbitDepth = 0.0f;
};

// Apply the spec's documented curves (sfs-spec/05 §3) to fan a MacroValues
// snapshot out to all driven internal fields. Pure function; no state.
[[nodiscard]] inline InternalFields fanOut(const MacroValues& m) noexcept
{
    InternalFields f;

    // TENSION (sfs-spec/05 §3.1)
    {
        const float t = m.tension;
        f.substrateC2 = 0.49f * t * t;             // 0.49 · m²
        f.substrateViscosityFloor = 0.005f * t;    // 0.005 · m
        f.harvesterSpacingScale = 0.5f + 0.5f * t; // 0.5 + 0.5·m
    }

    // DAMPING (sfs-spec/05 §3.2)
    {
        const float d = m.damping;
        f.substrateGamma = 0.05f * d * d;              // 0.05 · m²
        f.agentEnvelopeReleaseScale = 0.5f + 1.5f * d; // 0.5 + 1.5·m
    }

    // DENSITY (sfs-spec/05 §3.3)
    {
        const float d = m.density;
        // round(8 + 56·m). std::round is deterministic across platforms.
        f.agentActiveCount = static_cast<int>(8.0f + 56.0f * d + 0.5f);
        f.agentDepositWeightScale = 0.5f + 0.5f * d; // 0.5 + 0.5·m
    }

    // MIGRATION (sfs-spec/05 §3.4)
    {
        const float mig = m.migration;
        f.agentDriftScale = mig * mig;      // m²
        f.agentMigrationNoiseScale = mig;   // m
        f.harvesterOrbitDepth = 0.3f * mig; // 0.3·m
    }

    // COHERENCE (sfs-spec/05 §3.5)
    {
        const float c = m.coherence;
        const float oneMinusC = 1.0f - c;
        f.agentHarmonicLock = c;                    // m
        f.agentDetuneScale = oneMinusC * oneMinusC; // (1-m)²
        f.agentShapeParamDrift = oneMinusC * 0.3f;  // (1-m)·0.3
    }

    // EXCITATION (sfs-spec/05 §3.6)
    {
        const float e = m.excitation;
        f.agentModSensitivityScale = e * e;              // m²
        f.substrateViscosityOffset = -0.01f * e;         // -0.01·m
        f.agentFrequencyClampSoftness = 1.0f - 0.5f * e; // 1 - 0.5·m
    }

    return f;
}

} // namespace sfs::engine::macros
