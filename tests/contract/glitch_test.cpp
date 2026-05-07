// tests/contract/glitch_test.cpp
//
// Sonic-corner contract test: glitch (Phase 2 form per sfs-spec/08 §2.3).
// Hold a note with a glitch-style preset (high EXCITATION, S&H LFOs
// driving DENSITY + EXCITATION at fast rates) and count onsets — sudden
// energy bursts in a short-term RMS envelope.
//
// Spec criterion: ≥ 50 onsets in 50 s. Phase 2 trims to a 5 s render
// with the proportional threshold of ≥ 5 onsets so test wall-clock
// stays under 1 s; the full 50 s window enables when the contract
// harness escalates to release-gate.

#include "engine/voice.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

int countGlitchOnsets(sfs::engine::Voice& voice)
{
    using namespace sfs::engine::mod_matrix;
    using sfs::engine::lfo::LfoShape;

    constexpr int kSampleRate = 48000;
    constexpr float kHoldSeconds = 5.0f;
    constexpr int kBlock = 256;

    voice.macros().tension = 0.6f;
    voice.macros().damping = 0.2f;
    voice.macros().density = 0.5f;
    voice.macros().migration = 0.0f;
    voice.macros().coherence = 0.5f;
    voice.macros().excitation = 0.5f;

    voice.lfo(2).setShape(LfoShape::SampleHold);
    voice.lfo(2).setRateHz(8.0f);
    voice.lfo(3).setShape(LfoShape::SampleHold);
    voice.lfo(3).setRateHz(11.0f);

    voice.modMatrix().clearAllSlots();
    voice.modMatrix().setSlot(0, Source::Lfo3, Destination::Density, 0.5f);
    voice.modMatrix().setSlot(1, Source::Lfo4, Destination::Excitation, 0.5f);

    voice.noteOn(60, 1.0f);

    const int totalSamples = static_cast<int>(kHoldSeconds * static_cast<float>(kSampleRate));
    std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
    for (int written = 0; written < totalSamples; written += kBlock)
    {
        const int n = std::min(kBlock, totalSamples - written);
        voice.renderBlock(mono.data() + written, n);
    }

    constexpr int kWin = 1024;
    constexpr int kHop = 256;
    std::vector<float> rms;
    rms.reserve(static_cast<std::size_t>(totalSamples / kHop));
    for (int start = 0; start + kWin <= totalSamples; start += kHop)
    {
        double sumSq = 0.0;
        for (int i = 0; i < kWin; ++i)
        {
            const double v = static_cast<double>(mono[static_cast<std::size_t>(start + i)]);
            sumSq += v * v;
        }
        rms.push_back(static_cast<float>(std::sqrt(sumSq / static_cast<double>(kWin))));
    }
    if (rms.empty())
    {
        return 0;
    }

    double sumRms = 0.0;
    for (float r : rms)
    {
        sumRms += static_cast<double>(r);
    }
    const double meanRms = sumRms / static_cast<double>(rms.size());
    const double thresholdHi = meanRms * 1.5;
    const double thresholdLo = meanRms * 0.7;

    int onsets = 0;
    bool above = false;
    for (float r : rms)
    {
        if (!above && static_cast<double>(r) > thresholdHi)
        {
            ++onsets;
            above = true;
        }
        else if (above && static_cast<double>(r) < thresholdLo)
        {
            above = false;
        }
    }

    std::printf("[glitch contract] frames=%zu meanRms=%.4f onsets=%d\n", rms.size(), meanRms, onsets);
    return onsets;
}

} // namespace

TEST_CASE("Glitch contract (1D Phase 2 form): >= 5 onsets in 5 s", "[contract][glitch][1d]")
{
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    const int onsets = countGlitchOnsets(voice);
    REQUIRE(onsets >= 5);
}

TEST_CASE("Glitch contract (2D torus): >= 5 onsets in 5 s", "[contract][glitch][2d]")
{
    using sfs::engine::Topology;
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    voice.setTopology(Topology::Torus2D);
    const int onsets = countGlitchOnsets(voice);
    REQUIRE(onsets >= 5);
}
