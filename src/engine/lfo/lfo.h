// src/engine/lfo/lfo.h
//
// Per-voice LFO. Sine, triangle, saw, square, and sample-and-hold shapes,
// all bit-exact across platforms (sine via dm_sin, S&H via Philox stream
// LfoRandomWalk). Phase 2 ships 4 LFOs per voice; they're plumbed by
// Voice as mod-matrix sources and have no hardwired audio destination.
//
// Per-sample tick advances the phase by rateHz / sampleRate. Output is in
// [-1, 1) for periodic shapes; S&H is a uniform draw in the same range,
// refreshed each phase wrap.
//
// Header-only: the body is small (~30 ops worst case for sine), and the
// state is just (phase, lastPhase, holdValue, stream). LFOs run in the
// audio loop, so no allocations and no libm.

#pragma once

#include "dsp/dm_sin.h"
#include "engine/rng/philox.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sfs::engine::lfo
{

enum class LfoShape : std::uint8_t
{
    Sine = 0,
    Triangle = 1,
    Saw = 2,
    Square = 3,
    SampleHold = 4,
};

class Lfo
{
public:
    void setSampleRate(float sampleRate) noexcept
    {
        sampleRate_ = std::max(1.0f, sampleRate);
        recomputeIncrement();
    }

    void setRateHz(float rateHz) noexcept
    {
        rateHz_ = std::max(0.0f, rateHz);
        recomputeIncrement();
    }

    void setShape(LfoShape s) noexcept { shape_ = s; }
    [[nodiscard]] LfoShape shape() const noexcept { return shape_; }

    // Reset phase to 0 (typically called at noteOn for retrigger LFOs).
    void reset() noexcept
    {
        phase_ = 0.0f;
        lastPhase_ = 0.0f;
        holdValue_ = 0.0f;
        holdInitialised_ = false;
    }

    // Seed the S&H stream. Other shapes don't read this; safe to call
    // unconditionally at noteOn for determinism.
    void seedStream(std::uint64_t presetSeed, std::uint16_t voiceIndex, std::uint16_t lfoIndex) noexcept
    {
        stream_.seed(presetSeed, voiceIndex, lfoIndex, sfs::engine::rng::StreamId::LfoRandomWalk);
    }

    // Advance one sample, return value in [-1, 1).
    float tick() noexcept
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kInvTwoPow24 = 1.0f / 16777216.0f;

        // Detect phase wrap for S&H refresh.
        const bool wrapped = phase_ < lastPhase_;

        float out = 0.0f;
        switch (shape_)
        {
        case LfoShape::Sine:
            out = sfs::dsp::dm_sin(kTwoPi * phase_);
            break;
        case LfoShape::Triangle:
            // 0..0.5 ramps -1..1; 0.5..1 ramps 1..-1.
            out = (phase_ < 0.5f) ? (4.0f * phase_ - 1.0f) : (3.0f - 4.0f * phase_);
            break;
        case LfoShape::Saw:
            out = 2.0f * phase_ - 1.0f;
            break;
        case LfoShape::Square:
            out = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;
        case LfoShape::SampleHold:
            if (!holdInitialised_ || wrapped)
            {
                const std::uint32_t bits = stream_.next32();
                holdValue_ = static_cast<float>(bits >> 8) * kInvTwoPow24 * 2.0f - 1.0f;
                holdInitialised_ = true;
            }
            out = holdValue_;
            break;
        }

        // Advance phase.
        lastPhase_ = phase_;
        phase_ += increment_;
        if (phase_ >= 1.0f)
        {
            phase_ -= std::floor(phase_);
        }
        return out;
    }

    [[nodiscard]] float currentPhase() const noexcept { return phase_; }
    [[nodiscard]] float rateHz() const noexcept { return rateHz_; }

private:
    void recomputeIncrement() noexcept { increment_ = (sampleRate_ > 0.0f) ? (rateHz_ / sampleRate_) : 0.0f; }

    LfoShape shape_ = LfoShape::Sine;
    float sampleRate_ = 48000.0f;
    float rateHz_ = 1.0f;
    float increment_ = 1.0f / 48000.0f;
    float phase_ = 0.0f;
    float lastPhase_ = 0.0f;
    float holdValue_ = 0.0f;
    bool holdInitialised_ = false;

    sfs::engine::rng::Philox4x32Stream stream_{};
};

} // namespace sfs::engine::lfo
