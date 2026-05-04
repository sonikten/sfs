// src/engine/voice.cpp
//
// Per-voice engine glue. See voice.h for the contract.

#include "voice.h"

#include <cmath>

namespace sfs::engine
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDcCutoffHz = 5.0f;

// Phase 1 output-stage saturator. The full sfs-spec/04 §4 output chain
// (master gain + dm_tanh saturator + DC block + optional limiter) lands
// in Phase 2 / Phase 4; for Phase 1 a rational soft-clip keeps the
// substrate's natural amplitude swings inside (-1, 1) without needing
// dm_tanh. x / (1 + |x|) is C¹, monotonic, and bounded.
//
//   x = 0.0 → 0.0   (linear at small signals)
//   x = 0.5 → 0.333 (gentle compression)
//   x = 1.0 → 0.5
//   x = 2.0 → 0.667
//   x = 5.0 → 0.833 (asymptote toward ±1)
//
// Pre-gain is intentionally < 1 so the typical Phase 1 substrate
// amplitude (RMS ≈ 0.6 raw) lands at perceived RMS ≈ 0.3 after clip.
constexpr float kOutputPreGain = 0.5f;

[[nodiscard]] inline float softClip(float x) noexcept
{
    return x / (1.0f + std::fabs(x));
}

} // namespace

Voice::Voice(int substrateCells, int agentCount, float sampleRate)
    : substrate_(substrateCells, sampleRate), agents_(agentCount), sampleRate_(sampleRate)
{
    // Default coefficients chosen for an audible "alive" feel out of the box,
    // close to sfs-spec/09 §3.7 internal defaults. Phase 2's macro fan-out
    // will set these from TENSION/DAMPING/etc.
    substrate_.setCoefficients(0.30f, 0.05f, 0.005f);
    agents_.layoutEvenly(substrateCells);

    // DC blocker α from the host sample rate (sfs-spec/02 §6).
    dcBlockerAlpha_ = 1.0f - 2.0f * kPi * kDcCutoffHz / sampleRate_;

    // Mono harvester at the midpoint of the ring — maximally separated from
    // any single-agent deposit at position 0 so the substrate has to actually
    // propagate the wave to be heard.
    harvesterPosition_ = static_cast<float>(substrateCells) * 0.5f;
}

void Voice::noteOn(int midiNote, float velocity)
{
    agents_.noteOn(midiNote, velocity);
    gated_ = true;
}

void Voice::noteOff()
{
    agents_.noteOff();
    gated_ = false;
}

void Voice::renderBlock(float* out, int numSamples) noexcept
{
    if (out == nullptr || numSamples <= 0)
    {
        return;
    }

    const float pos = harvesterPosition_;
    const float a = dcBlockerAlpha_;
    float prevIn = dcBlockerLastInput_;
    float prevOut = dcBlockerLastOutput_;
    for (int i = 0; i < numSamples; ++i)
    {
        agents_.processOneSample(substrate_, sampleRate_);
        substrate_.step();
        const float raw = substrate_.read(pos);
        const float blocked = raw - prevIn + a * prevOut; // first-order DC block
        prevIn = raw;
        prevOut = blocked;
        out[i] = softClip(kOutputPreGain * blocked);
    }
    dcBlockerLastInput_ = prevIn;
    dcBlockerLastOutput_ = prevOut;
}

} // namespace sfs::engine
