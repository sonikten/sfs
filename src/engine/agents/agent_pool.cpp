// src/engine/agents/agent_pool.cpp
//
// Per-voice agent pool. See agent_pool.h for the contract.

#include "agent_pool.h"

#include "dsp/dm_sin.h"
#include "engine/substrate/substrate_1d.h"
#include "engine/substrate/substrate_2d.h"

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

// Shared waveform generator used by the 1D and 2D processOneSample
// paths. Returns y in roughly [-1, 1]; advances the agent's phase.
[[nodiscard]] inline float generateAgentSample(Agent& a, float fInst, float invSampleRate) noexcept
{
    const float dt = std::fabs(fInst) * invSampleRate; // for PolyBLEP
    float y = 0.0f;
    switch (a.shape)
    {
    case AgentShape::Sine:
        y = sfs::dsp::dm_sin(kTwoPi * a.phase);
        break;
    case AgentShape::Saw:
        y = 2.0f * a.phase - 1.0f;
        y -= polyBlep(a.phase, dt);
        break;
    case AgentShape::Square:
    {
        y = (a.phase < 0.5f) ? 1.0f : -1.0f;
        y += polyBlep(a.phase, dt);
        const float halfShifted = a.phase + 0.5f;
        const float halfWrapped = halfShifted - std::floor(halfShifted);
        y -= polyBlep(halfWrapped, dt);
        break;
    }
    case AgentShape::FmPair:
    {
        const float modPhase = kTwoPi * a.fmRatio * a.phase;
        const float modSig = sfs::dsp::dm_sin(modPhase);
        y = sfs::dsp::dm_sin(kTwoPi * a.phase + a.fmIndex * modSig);
        break;
    }
    case AgentShape::Noise:
        if (a.phase < a.noiseLastPhase)
        {
            const std::uint32_t bits = a.noiseStream.next32();
            a.noiseHoldValue = static_cast<float>(bits >> 8) * kInvTwoPow24 * 2.0f - 1.0f;
        }
        a.noiseLastPhase = a.phase;
        y = a.noiseHoldValue;
        break;
    }
    return y;
}

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

    // Per sfs-spec/03 §5 + sfs-spec/05 §3.4:
    //   r_i  ∼ MIGRATION · uniform(-1, 1) · 0.001 · N     (drawn at noteOn)
    //   ε_i  ~ MIGRATION · 0.0005 · N · gaussian()        (per sample)
    // Per sfs-spec/05 §3.5 (COHERENCE):
    //   detune_i ∝ (1 - COHERENCE)² · uniform(-50, 50) cents
    //
    // The MIGRATION/COHERENCE macros aren't read here — they're applied
    // each block by Voice from the macro fan-out. This routine just draws
    // the per-agent direction/cents shape; magnitudes scale at block rate.
    constexpr float kRDirectionUnit = 0.001f * 1024.0f; // r_i unit
    constexpr float kEpsNoiseUnit = 0.0005f * 1024.0f;  // ε_i unit
    constexpr float kMaxDetuneCents = 50.0f;
    constexpr std::uint64_t kPhase2PresetSeed = 0x5F5'5F5'5F5'5F5ull;
    constexpr std::uint16_t kPhase2VoiceIndex = 0;

    for (int i = 0; i < activeCount_; ++i)
    {
        auto& a = agents_[static_cast<std::size_t>(i)];
        a.frequency = baseHz;
        a.baseFrequency = baseHz;
        a.phase = 0.0f;
        a.amplitude = scaledAmp;
        a.envelope = 1.0f;
        a.baseDepositWeight = defaultDepositWeight;
        a.depositWeight = defaultDepositWeight;

        // Per-agent migration noise stream: AgentMigrationNoise (sample-indexed).
        a.noiseStream.seed(kPhase2PresetSeed,
                           kPhase2VoiceIndex,
                           static_cast<std::uint16_t>(i),
                           sfs::engine::rng::StreamId::AgentMigrationNoise);

        // Direction draw for r_i (AgentPositionInit, sample_index = 0).
        sfs::engine::rng::Philox4x32Stream initStream;
        initStream.seed(kPhase2PresetSeed,
                        kPhase2VoiceIndex,
                        static_cast<std::uint16_t>(i),
                        sfs::engine::rng::StreamId::AgentPositionInit);
        const float migU01 = initStream.nextFloat01();
        a.migrationDirection = migU01 * 2.0f - 1.0f;              // [-1, 1)
        a.migrationRate = a.migrationDirection * kRDirectionUnit; // default macro=1
        a.migrationNoiseScale = kEpsNoiseUnit;

        // Per-agent detune cents (AgentHarmonicSelect, sample_index = 0).
        sfs::engine::rng::Philox4x32Stream detuneStream;
        detuneStream.seed(kPhase2PresetSeed,
                          kPhase2VoiceIndex,
                          static_cast<std::uint16_t>(i),
                          sfs::engine::rng::StreamId::AgentHarmonicSelect);
        const float detU01 = detuneStream.nextFloat01();
        a.detuneCents = (detU01 * 2.0f - 1.0f) * kMaxDetuneCents; // [-50, 50)
    }
}

void AgentPool::setActiveCount(int n) noexcept
{
    activeCount_ = std::clamp(n, 0, static_cast<int>(agents_.size()));
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
        agents_[i].positionY = 0.0f;
    }
}

void AgentPool::layoutEvenly2D(int cellsX, int cellsY) noexcept
{
    if (agents_.empty() || cellsX <= 0 || cellsY <= 0)
    {
        return;
    }
    // Distribute agents on a √N × √N grid (or close enough). Two
    // sqrt(N) agents land on each axis; positions are evenly spaced
    // on each. For non-square N we round up rows and pack the last
    // row partially.
    const int n = static_cast<int>(agents_.size());
    int rows = static_cast<int>(std::sqrt(static_cast<float>(n)));
    if (rows < 1)
    {
        rows = 1;
    }
    const int cols = (n + rows - 1) / rows;
    const float dx = static_cast<float>(cellsX) / static_cast<float>(cols);
    const float dy = static_cast<float>(cellsY) / static_cast<float>(rows);
    for (std::size_t i = 0; i < agents_.size(); ++i)
    {
        const int r = static_cast<int>(i) / cols;
        const int c = static_cast<int>(i) % cols;
        agents_[i].position = dx * (static_cast<float>(c) + 0.5f);
        agents_[i].positionY = dy * (static_cast<float>(r) + 0.5f);
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

        // 1. Read substrate (for the bend feedback).
        const float uAt = substrate.read(a.position);

        // 2. Bend the instantaneous frequency multiplicatively.
        const float fInst = a.frequency * (1.0f + a.modSensitivity * uAt);

        // 3. Generate waveform sample (shared helper).
        const float y = generateAgentSample(a, fInst, invSampleRate);

        // 4. Deposit the scaled contribution back into the substrate.
        const float contribution = a.depositWeight * a.amplitude * a.envelope * voiceGain_ * y;
        substrate.deposit(a.position, contribution);

        // 5. Advance phase using the bent frequency. Wrap to [0, 1).
        a.phase += fInst * invSampleRate;
        if (a.phase >= 1.0f || a.phase < 0.0f)
        {
            a.phase -= std::floor(a.phase);
        }

        // 6. Migrate. r_i is the per-agent constant drift drawn at noteOn;
        // ε_i is the per-sample Gaussian noise per sfs-spec/03 §5.
        a.noiseStream.setSampleIndex(currentSampleIndex_);
        const float epsilon = sfs::engine::rng::nextGaussian(a.noiseStream);
        a.position += a.migrationRate + epsilon * a.migrationNoiseScale;
    }

    ++currentSampleIndex_;
}

void AgentPool::processOneSample(sfs::engine::substrate::Substrate2D& substrate, float sampleRate) noexcept
{
    if (activeCount_ == 0 || sampleRate <= 0.0f)
    {
        return;
    }
    const float invSampleRate = 1.0f / sampleRate;

    for (int i = 0; i < activeCount_; ++i)
    {
        Agent& a = agents_[static_cast<std::size_t>(i)];

        // 1. Read substrate at this agent's 2D position.
        const float uAt = substrate.read(a.position, a.positionY);

        // 2. Bend instantaneous frequency.
        const float fInst = a.frequency * (1.0f + a.modSensitivity * uAt);

        // 3. Waveform sample.
        const float y = generateAgentSample(a, fInst, invSampleRate);

        // 4. Deposit at 2D position.
        const float contribution = a.depositWeight * a.amplitude * a.envelope * voiceGain_ * y;
        substrate.deposit(a.position, a.positionY, contribution);

        // 5. Advance phase.
        a.phase += fInst * invSampleRate;
        if (a.phase >= 1.0f || a.phase < 0.0f)
        {
            a.phase -= std::floor(a.phase);
        }

        // 6. Migrate along X axis only — Y migration is a Phase 4 follow-up.
        a.noiseStream.setSampleIndex(currentSampleIndex_);
        const float epsilon = sfs::engine::rng::nextGaussian(a.noiseStream);
        a.position += a.migrationRate + epsilon * a.migrationNoiseScale;
    }

    ++currentSampleIndex_;
}

} // namespace sfs::engine::agents
