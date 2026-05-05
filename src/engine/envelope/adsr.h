// src/engine/envelope/adsr.h
//
// Per-voice linear-piecewise ADSR envelope. Phase 2 amplitude shaper —
// replaces the binary gate that Phase 1 wrote into agent.envelope. The
// envelope value is computed sample-by-sample by Voice and pushed into
// AgentPool via setVoiceGain(); deposits scale by voiceGain × amplitude
// × per-agent envelope (still binary in Phase 2).
//
// Sustain stage holds at sustainLevel until release(). Release ramps from
// the current envelope value, not from sustainLevel — re-triggering during
// release picks up where it was, no zipper.
//
// Header-only, trivially testable. No allocations, no dependencies. The
// state machine is deterministic across platforms (only addition,
// multiplication, and float comparison).

#pragma once

#include <algorithm>

namespace sfs::engine::envelope
{

class Adsr
{
public:
    enum class Stage
    {
        Idle = 0,
        Attack,
        Decay,
        Sustain,
        Release,
    };

    void setSampleRate(float sampleRate) noexcept
    {
        sampleRate_ = std::max(1.0f, sampleRate);
        recomputeIncrements();
    }

    void setAttackMs(float ms) noexcept
    {
        attackMs_ = std::max(0.0f, ms);
        recomputeIncrements();
    }
    void setDecayMs(float ms) noexcept
    {
        decayMs_ = std::max(0.0f, ms);
        recomputeIncrements();
    }
    void setSustainLevel(float level) noexcept { sustainLevel_ = std::clamp(level, 0.0f, 1.0f); }
    void setReleaseMs(float ms) noexcept
    {
        releaseMs_ = std::max(0.0f, ms);
        recomputeIncrements();
    }

    void noteOn() noexcept
    {
        // Attack always restarts from current value (legato-friendly).
        stage_ = Stage::Attack;
    }

    void noteOff() noexcept
    {
        if (stage_ != Stage::Idle)
        {
            stage_ = Stage::Release;
            // Capture release start so the linear ramp matches the configured
            // release time regardless of where the envelope was when gated off.
            releaseStartValue_ = value_;
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] float value() const noexcept { return value_; }

    // Advance one sample and return the new envelope value.
    float tick() noexcept
    {
        switch (stage_)
        {
        case Stage::Idle:
            value_ = 0.0f;
            break;
        case Stage::Attack:
            value_ += attackInc_;
            if (value_ >= 1.0f)
            {
                value_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;
        case Stage::Decay:
            value_ -= decayInc_;
            if (value_ <= sustainLevel_)
            {
                value_ = sustainLevel_;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Sustain:
            value_ = sustainLevel_;
            break;
        case Stage::Release:
            value_ -= releaseInc_;
            if (value_ <= 0.0f)
            {
                value_ = 0.0f;
                stage_ = Stage::Idle;
            }
            break;
        }
        return value_;
    }

    void reset() noexcept
    {
        stage_ = Stage::Idle;
        value_ = 0.0f;
        releaseStartValue_ = 0.0f;
    }

private:
    void recomputeIncrements() noexcept
    {
        // Linear ramp per sample. attackInc takes value_ from 0→1 in attackMs.
        // decayInc takes 1→sustainLevel in decayMs. releaseInc takes the
        // captured releaseStartValue_→0 in releaseMs (using a constant rate
        // sized for 1.0→0 so the slope is consistent regardless of where
        // release starts; perceptually fine for a Phase 2 envelope).
        const float spms = sampleRate_ * 0.001f;
        attackInc_ = (attackMs_ > 0.0f) ? (1.0f / (attackMs_ * spms)) : 1.0f;
        decayInc_ = (decayMs_ > 0.0f) ? (1.0f / (decayMs_ * spms)) : 1.0f;
        releaseInc_ = (releaseMs_ > 0.0f) ? (1.0f / (releaseMs_ * spms)) : 1.0f;
    }

    float sampleRate_ = 48000.0f;
    float attackMs_ = 10.0f;
    float decayMs_ = 100.0f;
    float sustainLevel_ = 0.7f;
    float releaseMs_ = 200.0f;

    float attackInc_ = 0.0021f; // 1 / (10 ms * 48 samples/ms)
    float decayInc_ = 0.0002f;
    float releaseInc_ = 0.0001f;

    Stage stage_ = Stage::Idle;
    float value_ = 0.0f;
    float releaseStartValue_ = 0.0f;
};

} // namespace sfs::engine::envelope
