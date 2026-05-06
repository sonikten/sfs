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

TEST_CASE("Glitch contract (Phase 2 form): >= 5 onsets in 5 s for glitch preset", "[contract][glitch]")
{
    using sfs::engine::Voice;
    using namespace sfs::engine::mod_matrix;
    using sfs::engine::lfo::LfoShape;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;
    constexpr float kHoldSeconds = 5.0f;
    constexpr int kBlock = 256;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    // Glitch preset (sfs-spec/08 §2.3): high EXCITATION + sample-and-hold
    // LFOs driving DENSITY + EXCITATION at audio-rate-adjacent rates.
    voice.macros().tension = 0.6f;
    voice.macros().damping = 0.2f;
    voice.macros().density = 0.5f;
    voice.macros().migration = 0.0f;
    voice.macros().coherence = 0.5f;
    voice.macros().excitation = 0.5f;

    // S&H LFOs at fast rates — discrete jumps each cycle.
    voice.lfo(2).setShape(LfoShape::SampleHold);
    voice.lfo(2).setRateHz(8.0f);
    voice.lfo(3).setShape(LfoShape::SampleHold);
    voice.lfo(3).setRateHz(11.0f);

    voice.modMatrix().clearAllSlots();
    voice.modMatrix().setSlot(0, Source::Lfo3, Destination::Density, 0.5f);
    voice.modMatrix().setSlot(1, Source::Lfo4, Destination::Excitation, 0.5f);

    voice.noteOn(60, 1.0f); // C4

    // Render mono.
    const int totalSamples = static_cast<int>(kHoldSeconds * static_cast<float>(kSampleRate));
    std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
    for (int written = 0; written < totalSamples; written += kBlock)
    {
        const int n = std::min(kBlock, totalSamples - written);
        voice.renderBlock(mono.data() + written, n);
    }

    // Short-term RMS envelope (1024-sample window, 256-sample hop ≈ 5 ms).
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
    REQUIRE(!rms.empty());

    // Onset = rising-edge crossing of (meanRms × 1.5) with hysteresis at
    // (meanRms × 0.7). The hysteresis gap prevents single-sample chatter
    // from inflating the count when the RMS is near the threshold.
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

    std::printf("\n[glitch contract] frames=%zu meanRms=%.4f onsets=%d\n", rms.size(), meanRms, onsets);

    // Spec: 50 in 50s → 5 in 5s.
    REQUIRE(onsets >= 5);
}
