// src/engine/agents/agent_pool.cpp
//
// Per-voice agent pool — Phase 1 reference. Sine waveforms only; no migration.
// See agent_pool.h for the contract.

#include "agent_pool.h"

#include "dsp/dm_sin.h"
#include "engine/substrate/substrate_1d.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace sfs::engine::agents
{

namespace
{

constexpr float kTwoPi = 6.28318530717958647692f;

// Standard MIDI note → Hz: A4 (69) = 440 Hz, 12-TET.
[[nodiscard]] float midiNoteToHz(int midiNote) noexcept
{
    return 440.0f * std::exp2((static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

} // namespace

AgentPool::AgentPool(int maxAgents) : agents_(static_cast<std::size_t>(std::clamp(maxAgents, 1, kMaxAgents))) {}

void AgentPool::noteOn(int midiNote, float velocity)
{
    const float baseHz = midiNoteToHz(midiNote);
    const float scaledAmp = std::clamp(velocity, 0.0f, 1.0f);

    activeCount_ = static_cast<int>(agents_.size());

    // Spec default deposit weight (sfs-spec/09 §3.3): 0.05 / sqrt(activeCount).
    // Caps the substrate energy injection per sample so a many-agent voice
    // doesn't accumulate faster than γ can bleed off.
    const float defaultDepositWeight = 0.05f / std::sqrt(static_cast<float>(std::max(1, activeCount_)));

    // Phase 1 placeholder migration rates: each agent gets a unique small
    // drift so positions spread/recombine across the ring over time —
    // gives the audibly evolving timbre that the Phase 1 acceptance test
    // calls for. Magnitudes well below sfs-spec/09's `[-0.001 N, +0.001 N]`
    // bound. Phase 2 replaces with the spec's MIGRATION-macro-driven
    // uniform draw (and adds Gaussian ε_i per sample per §5).
    //
    // Pattern: alternating + and -, magnitudes spread linearly. Two agents
    // with the same |rate| but opposite signs slowly counter-rotate.
    for (int i = 0; i < activeCount_; ++i)
    {
        auto& a = agents_[static_cast<std::size_t>(i)];
        a.frequency = baseHz; // Phase 1: every agent on the fundamental.
                              // Phase 2 scales by harmonic_set[i].
        a.phase = 0.0f;
        a.amplitude = scaledAmp;
        a.envelope = 1.0f; // gate ON
        a.depositWeight = defaultDepositWeight;

        // i=0 → 0.0020 cells/sample (~96 cells/sec, ring wraps every ~10 s
        // for the slowest agent at 48 kHz). 10× smaller than the first attempt:
        // moving agents inject fresh energy into still-undamped substrate
        // cells, so high migration rates push the system into a self-pumping
        // regime that the static-position version never hit. Combined with
        // the Voice output saturator, this keeps Phase 1 audible without
        // needing aggressive output limiting.
        const float magnitude = 0.0020f + 0.0004f * static_cast<float>(i);
        a.migrationRate = (i % 2 == 0) ? magnitude : -magnitude;
    }
}

void AgentPool::noteOff()
{
    for (int i = 0; i < activeCount_; ++i)
    {
        agents_[static_cast<std::size_t>(i)].envelope = 0.0f; // gate OFF
    }
}

void AgentPool::layoutEvenly(int substrateCellCount) noexcept
{
    if (agents_.empty() || substrateCellCount <= 0)
    {
        return;
    }
    const float spacing = static_cast<float>(substrateCellCount) / static_cast<float>(agents_.size());
    for (std::size_t i = 0; i < agents_.size(); ++i)
    {
        agents_[i].position = spacing * static_cast<float>(i);
    }
}

void AgentPool::processOneSample(sfs::engine::substrate::Substrate1D& substrate, float sampleRate) noexcept
{
    if (activeCount_ == 0 || sampleRate <= 0.0f)
    {
        return;
    }
    const float invSampleRate = 1.0f / sampleRate;

    for (int i = 0; i < activeCount_; ++i)
    {
        Agent& a = agents_[static_cast<std::size_t>(i)];

        // 1. Read substrate at this agent's position (for the bend feedback).
        const float uAt = substrate.read(a.position);

        // 2. Bend the instantaneous frequency multiplicatively (sfs-spec/03 §2).
        //    Multiplicative form preserves harmonic relationships.
        const float fInst = a.frequency * (1.0f + a.modSensitivity * uAt);

        // 3. Generate the waveform sample. dm_sin takes radians.
        const float y = sfs::dsp::dm_sin(kTwoPi * a.phase);

        // 4. Deposit the scaled contribution back into the substrate.
        const float contribution = a.depositWeight * a.amplitude * a.envelope * y;
        substrate.deposit(a.position, contribution);

        // 5. Advance phase using the bent frequency. Wrap to [0, 1).
        a.phase += fInst * invSampleRate;
        if (a.phase >= 1.0f || a.phase < 0.0f)
        {
            a.phase -= std::floor(a.phase);
        }

        // 6. Migrate. Phase 1 uses deterministic per-agent drift only;
        // sfs-spec/03 §5's Gaussian ε_i lands when dm_log / dm_cos /
        // dm_sqrt arrive (deferred from this commit).
        a.position += a.migrationRate;
        // Substrate1D::deposit and read both wrap internally, so positions
        // outside [0, N) are still legal — we only normalise to keep the
        // float magnitude bounded.
        // (No-op for the small drift rates we use in Phase 1.)
    }
}

} // namespace sfs::engine::agents
