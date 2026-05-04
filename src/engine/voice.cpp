// src/engine/voice.cpp
//
// Per-voice engine glue. See voice.h for the contract.

#include "voice.h"

namespace sfs::engine
{

Voice::Voice(int substrateCells, int agentCount, float sampleRate)
    : substrate_(substrateCells, sampleRate), agents_(agentCount), sampleRate_(sampleRate)
{
    // Default coefficients chosen for an audible "alive" feel out of the box,
    // close to sfs-spec/09 §3.7 internal defaults. Phase 2's macro fan-out
    // will set these from TENSION/DAMPING/etc.
    substrate_.setCoefficients(0.30f, 0.05f, 0.005f);
    agents_.layoutEvenly(substrateCells);

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
    for (int i = 0; i < numSamples; ++i)
    {
        agents_.processOneSample(substrate_, sampleRate_);
        substrate_.step();
        out[i] = substrate_.read(pos);
    }
}

} // namespace sfs::engine
