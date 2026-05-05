// tests/unit/voice_manager_test.cpp
//
// Phase 2 polyphony sanity tests. Asserts:
//   - 8 simultaneous note-ons don't crash; activeVoiceCount reaches 8
//   - 9th noteOn steals a voice (count stays at 8)
//   - noteOff for a held note marks the voice as not gated
//   - allNotesOff gates everything off
//   - rendering a chord produces non-silent stereo with bounded peak

#include "engine/voice_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using sfs::engine::VoiceManager;

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 8; // smaller per-voice for the test (faster)
constexpr float kSampleRateF = 48000.0f;
} // namespace

TEST_CASE("VoiceManager allocates up to 8 voices, then steals", "[voice_manager]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    REQUIRE(mgr.activeVoiceCount() == 0);

    // Fire 8 distinct notes.
    for (int i = 0; i < VoiceManager::kMaxVoices; ++i)
    {
        mgr.noteOn(48 + i, 1.0f);
    }
    REQUIRE(mgr.activeVoiceCount() == VoiceManager::kMaxVoices);

    // 9th note steals — count stays at 8.
    mgr.noteOn(60, 1.0f);
    REQUIRE(mgr.activeVoiceCount() == VoiceManager::kMaxVoices);
}

TEST_CASE("noteOff gates the matching voice", "[voice_manager][note-off]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    mgr.noteOn(60, 1.0f);
    mgr.noteOn(64, 1.0f);
    mgr.noteOn(67, 1.0f);
    REQUIRE(mgr.activeVoiceCount() == 3);

    mgr.noteOff(64);
    REQUIRE(mgr.activeVoiceCount() == 2);

    mgr.allNotesOff();
    REQUIRE(mgr.activeVoiceCount() == 0);
}

TEST_CASE("Chord render: 3 notes produce non-silent bounded stereo", "[voice_manager][chord]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    mgr.noteOn(60, 1.0f); // C4
    mgr.noteOn(64, 1.0f); // E4
    mgr.noteOn(67, 1.0f); // G4

    constexpr int kSamples = kSampleRate / 2; // 0.5 s
    std::vector<float> L(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockStereo(L.data() + written, R.data() + written, n);
    }

    // Non-silent.
    float peakL = 0, peakR = 0;
    double rmsL = 0, rmsR = 0;
    for (int i = 0; i < kSamples; ++i)
    {
        const float lv = L[static_cast<std::size_t>(i)];
        const float rv = R[static_cast<std::size_t>(i)];
        peakL = std::max(peakL, std::fabs(lv));
        peakR = std::max(peakR, std::fabs(rv));
        rmsL += static_cast<double>(lv) * static_cast<double>(lv);
        rmsR += static_cast<double>(rv) * static_cast<double>(rv);
    }
    rmsL = std::sqrt(rmsL / static_cast<double>(kSamples));
    rmsR = std::sqrt(rmsR / static_cast<double>(kSamples));

    CAPTURE(peakL, peakR, rmsL, rmsR);
    REQUIRE(peakL > 1e-3f);
    REQUIRE(peakR > 1e-3f);
    // 3-voice sum should fit in [-3, 3] before saturation; our soft-clip
    // bounds each voice at ~1, so 3-voice sum bounded at ~3. Loose check
    // against catastrophic blow-up.
    REQUIRE(peakL < 5.0f);
    REQUIRE(peakR < 5.0f);
}
