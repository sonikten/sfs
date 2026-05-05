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
#include <cstdint>

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

// PolyBLEP residual at a given phase position. `t` is the fractional phase
// distance from the discontinuity (within ±dt). dt is the per-sample phase
// increment (frequency / sampleRate). Returns the smoothing correction.
//
// Source: well-known formulation from Välimäki & Huovilainen, "Antialiasing
// oscillators in subtractive synthesis" (2007). Costs ~4 ops per call.
[[nodiscard]] inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt)
    {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

constexpr float kInvTwoPow24 = 1.0f / 16777216.0f;

} // namespace

AgentPool::AgentPool(int maxAgents) : agents_(static_cast<std::size_t>(std::clamp(maxAgents, 1, kMaxAgents))) {}

void AgentPool::noteOn(int midiNote, float velocity)
{
    const float baseHz = midiNoteToHz(midiNote);
    const float scaledAmp = std::clamp(velocity, 0.0f, 1.0f);

    activeCount_ = static_cast<int>(agents_.size());
    currentSampleIndex_ = 0;

    // Spec default deposit weight (sfs-spec/09 §3.3): 0.05 / sqrt(activeCount).
    const float defaultDepositWeight = 0.05f / std::sqrt(static_cast<float>(std::max(1, activeCount_)));

    // Phase 2 migration per sfs-spec/03 §5:
    //   r_i ∼ MIGRATION · uniform(-1, 1) · 0.001 · N      (drawn at noteOn)
    //   ε_i ~ MIGRATION · 0.0005 · N · gaussian()         (per sample)
    //
    // Phase 2 simplification: no MIGRATION macro yet (lands in macros step),
    // so we apply a "MIGRATION ≈ 0.1" overall scale here — gentle enough not
    // to pump the substrate, audibly more interesting than Phase 1's static
    // alternating pattern.
    constexpr float kPhase2MigrationScale = 0.1f;
    constexpr float kSubstrateCellsRef = 1024.0f; // r_i scale uses N
    constexpr float kRDriftScale = kPhase2MigrationScale * 0.001f * kSubstrateCellsRef;
    constexpr float kEpsNoiseScale = kPhase2MigrationScale * 0.0005f * kSubstrateCellsRef;
    // Phase 2 hard-coded preset seed (preset format with `seed` field lands
    // in P4 — until then every render uses the same seed).
    constexpr std::uint64_t kPhase2PresetSeed = 0x5F5'5F5'5F5'5F5ull;
    constexpr std::uint16_t kPhase2VoiceIndex = 0; // single voice in P1/P2

    for (int i = 0; i < activeCount_; ++i)
    {
        auto& a = agents_[static_cast<std::size_t>(i)];
        a.frequency = baseHz;
        a.phase = 0.0f;
        a.amplitude = scaledAmp;
        a.envelope = 1.0f;
        a.depositWeight = defaultDepositWeight;

        // Per-agent migration noise stream: AgentMigrationNoise (sample-indexed).
        a.noiseStream.seed(kPhase2PresetSeed,
                           kPhase2VoiceIndex,
                           static_cast<std::uint16_t>(i),
                           sfs::engine::rng::StreamId::AgentMigrationNoise);

        // r_i: per-agent constant drift, drawn at noteOn from a separate
        // init stream (AgentPositionInit, sample_index = 0). Uniform(-1, 1)
        // scaled by kRDriftScale.
        sfs::engine::rng::Philox4x32Stream initStream;
        initStream.seed(kPhase2PresetSeed,
                        kPhase2VoiceIndex,
                        static_cast<std::uint16_t>(i),
                        sfs::engine::rng::StreamId::AgentPositionInit);
        const float u01 = initStream.nextFloat01();
        const float uSym = u01 * 2.0f - 1.0f; // [-1, 1)
        a.migrationRate = uSym * kRDriftScale;
        a.migrationNoiseScale = kEpsNoiseScale;
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

        // 3. Generate the waveform sample by shape. Phase is in [0, 1) cycles.
        const float dt = std::fabs(fInst) * invSampleRate; // for PolyBLEP
        float y = 0.0f;
        switch (a.shape)
        {
        case AgentShape::Sine:
        {
            y = sfs::dsp::dm_sin(kTwoPi * a.phase);
            break;
        }
        case AgentShape::Saw:
        {
            // Naive ramp 2·phase − 1 ∈ [−1, 1) plus PolyBLEP residual at the
            // wrap discontinuity (sfs-spec/03 §3). ~10 ops total.
            y = 2.0f * a.phase - 1.0f;
            y -= polyBlep(a.phase, dt);
            break;
        }
        case AgentShape::Square:
        {
            // ±1 with PolyBLEP residuals at phase 0 and phase 0.5
            // (the two square-wave discontinuities per cycle).
            y = (a.phase < 0.5f) ? 1.0f : -1.0f;
            y += polyBlep(a.phase, dt);
            const float halfShifted = a.phase + 0.5f;
            const float halfWrapped = halfShifted - std::floor(halfShifted);
            y -= polyBlep(halfWrapped, dt);
            break;
        }
        case AgentShape::FmPair:
        {
            // Two-op FM: carrier sin(2π·phase + index·sin(2π·ratio·phase)).
            // Phase 2 simple form; the spec's full DX-style ratio table
            // arrives with the preset format in Phase 4.
            const float modPhase = kTwoPi * a.fmRatio * a.phase;
            const float modSig = sfs::dsp::dm_sin(modPhase);
            y = sfs::dsp::dm_sin(kTwoPi * a.phase + a.fmIndex * modSig);
            break;
        }
        case AgentShape::Noise:
        {
            // Sample-and-hold at agent frequency. The held value updates
            // each time the phase wraps (one new sample per cycle ≈
            // f_inst Hz refresh rate). Per spec stream ID 5
            // (AgentSampleHoldNoise), sample-indexed via the same
            // noiseStream we use for migration ε — Phase 2 simplification;
            // a separate per-agent sample-and-hold stream is a follow-up
            // when noise determinism races appear.
            if (a.phase < a.noiseLastPhase)
            {
                // Phase wrapped — refresh the held value with a fresh draw.
                const std::uint32_t bits = a.noiseStream.next32();
                // Map 24-bit uniform to [-1, 1).
                a.noiseHoldValue = static_cast<float>(bits >> 8) * kInvTwoPow24 * 2.0f - 1.0f;
            }
            a.noiseLastPhase = a.phase;
            y = a.noiseHoldValue;
            break;
        }
        }

        // 4. Deposit the scaled contribution back into the substrate.
        const float contribution = a.depositWeight * a.amplitude * a.envelope * y;
        substrate.deposit(a.position, contribution);

        // 5. Advance phase using the bent frequency. Wrap to [0, 1).
        a.phase += fInst * invSampleRate;
        if (a.phase >= 1.0f || a.phase < 0.0f)
        {
            a.phase -= std::floor(a.phase);
        }

        // 6. Migrate. r_i is the per-agent constant drift drawn at noteOn;
        // ε_i is the per-sample Gaussian noise per sfs-spec/03 §5. The
        // Substrate's deposit/read both wrap modulo N, so positions
        // outside [0, N) are legal — no explicit wrap needed.
        a.noiseStream.setSampleIndex(currentSampleIndex_);
        const float epsilon = sfs::engine::rng::nextGaussian(a.noiseStream);
        a.position += a.migrationRate + epsilon * a.migrationNoiseScale;
    }

    ++currentSampleIndex_;
}

} // namespace sfs::engine::agents
