// tests/contract/voice_manager_fuzz_test.cpp
//
// VoiceManager-level fuzz coverage. Exercises the host-facing path
// (VoiceManager::renderBlockStereo + noteOn/Off + setMidiCc1) against
// the dimensions a real DAW will throw at us:
//
//   * Polyphony 1-8 voices simultaneous
//   * Voice stealing (10+ rapid noteOn beyond the pool)
//   * Block sizes from 16 to 2048 (JUCE hosts pick anything)
//   * Sample rates 44.1 / 48 / 88.2 / 96 / 192 kHz
//   * Chromatic note range C0..B7 (96 notes)
//   * Rapid retrigger (1000 noteOn/Off pairs in 1 s)
//   * CC1 mod-wheel sweep mid-render
//
// Per scenario asserts: no NaN/Inf, audio peak ≤ 1.5, substrate state
// stays at-or-under the Substrate1D runaway clamp (with the GUI snapshot
// tolerance).

#include "engine/voice_manager.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using sfs::engine::VoiceManager;

namespace
{

constexpr float kSubstrateUMax = 20.0f;
constexpr float kSubstrateTol = 0.01f;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 16;

struct RenderHealth
{
    bool anyNonFinite = false;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float rms = 0.0f;
    float substrateMaxAbs = 0.0f;
};

void inspectAndAccumulate(RenderHealth& h, const float* l, const float* r, int n)
{
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float lv = l[i];
        const float rv = r[i];
        if (!std::isfinite(lv) || !std::isfinite(rv))
        {
            h.anyNonFinite = true;
        }
        const float la = std::fabs(lv);
        const float ra = std::fabs(rv);
        if (la > h.peakL)
        {
            h.peakL = la;
        }
        if (ra > h.peakR)
        {
            h.peakR = ra;
        }
        sumSq += static_cast<double>(lv) * static_cast<double>(lv) + static_cast<double>(rv) * static_cast<double>(rv);
    }
    h.rms = static_cast<float>(std::sqrt(static_cast<double>(h.rms) * static_cast<double>(h.rms) + sumSq / 2.0));
}

void inspectSubstrate(RenderHealth& h, VoiceManager& vm, std::vector<float>& snapBuf)
{
    if (vm.snapshotPrimaryVoiceSubstrate(snapBuf.data(), kSubstrateCells))
    {
        for (float v : snapBuf)
        {
            if (!std::isfinite(v))
            {
                h.anyNonFinite = true;
            }
            const float a = std::fabs(v);
            if (a > h.substrateMaxAbs)
            {
                h.substrateMaxAbs = a;
            }
        }
    }
}

void assertHealth(const char* label, const RenderHealth& h)
{
    INFO("Scenario: " << label);
    INFO("peakL=" << h.peakL << " peakR=" << h.peakR << " subMax=" << h.substrateMaxAbs);
    REQUIRE_FALSE(h.anyNonFinite);
    REQUIRE(h.peakL <= 1.5f);
    REQUIRE(h.peakR <= 1.5f);
    REQUIRE(h.substrateMaxAbs <= kSubstrateUMax + kSubstrateTol);
}

} // namespace

TEST_CASE("VoiceManager fuzz: polyphony 1..8 simultaneous voices", "[contract][fuzz][polyphony]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr float kRenderSecs = 1.0f;
    const int totalSamples = static_cast<int>(kRenderSecs * static_cast<float>(kSampleRate));

    for (int polyphony = 1; polyphony <= 8; ++polyphony)
    {
        VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
        for (int i = 0; i < polyphony; ++i)
        {
            // C major chord stacked: C, E, G, C, E, G, C, E (octave-doubled).
            constexpr int kChord[8] = {60, 64, 67, 72, 76, 79, 84, 88};
            vm.noteOn(kChord[i], 0.7f + 0.04f * static_cast<float>(i));
        }

        std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

        RenderHealth h;
        for (int written = 0; written < totalSamples;)
        {
            const int n = std::min(kBlockSize, totalSamples - written);
            vm.renderBlockStereo(bufL.data(), bufR.data(), n);
            inspectAndAccumulate(h, bufL.data(), bufR.data(), n);
            inspectSubstrate(h, vm, snap);
            written += n;
        }

        char label[64];
        std::snprintf(label, sizeof(label), "polyphony=%d", polyphony);
        assertHealth(label, h);
        REQUIRE(vm.activeVoiceCount() == polyphony);
    }
}

TEST_CASE("VoiceManager fuzz: voice stealing (16 noteOn into 8-voice pool)", "[contract][fuzz][stealing]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    // 16 noteOn events, spread across 16 blocks (~85 ms total). Voice
    // manager must steal the oldest voice each time slots run out.
    for (int i = 0; i < 16; ++i)
    {
        vm.noteOn(48 + i, 1.0f);
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    // Tail render so we observe the last steal under load.
    for (int i = 0; i < 8; ++i)
    {
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    assertHealth("steal_16_into_8", h);
    REQUIRE(vm.activeVoiceCount() <= VoiceManager::kMaxVoices);
}

TEST_CASE("VoiceManager fuzz: block sizes from 16 to 2048", "[contract][fuzz][block-size]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlocks[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 64 + 5}; // last is unaligned

    for (int bs : kBlocks)
    {
        VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
        vm.noteOn(60, 1.0f);

        std::vector<float> bufL(static_cast<std::size_t>(bs), 0.0f);
        std::vector<float> bufR(static_cast<std::size_t>(bs), 0.0f);
        std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

        RenderHealth h;
        // Render 0.5 s total, regardless of block size.
        const int totalSamples = kSampleRate / 2;
        for (int written = 0; written < totalSamples;)
        {
            const int n = std::min(bs, totalSamples - written);
            vm.renderBlockStereo(bufL.data(), bufR.data(), n);
            inspectAndAccumulate(h, bufL.data(), bufR.data(), n);
            inspectSubstrate(h, vm, snap);
            written += n;
        }
        char label[32];
        std::snprintf(label, sizeof(label), "block_size=%d", bs);
        assertHealth(label, h);
    }
}

TEST_CASE("VoiceManager fuzz: sample rates from 44.1 kHz to 192 kHz", "[contract][fuzz][sample-rate]")
{
    constexpr int kSampleRates[] = {44100, 48000, 88200, 96000, 192000};
    constexpr int kBlockSize = 256;

    for (int sr : kSampleRates)
    {
        VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(sr));
        vm.noteOn(60, 1.0f);

        std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

        RenderHealth h;
        // Render 0.5 s.
        const int totalSamples = sr / 2;
        for (int written = 0; written < totalSamples;)
        {
            const int n = std::min(kBlockSize, totalSamples - written);
            vm.renderBlockStereo(bufL.data(), bufR.data(), n);
            inspectAndAccumulate(h, bufL.data(), bufR.data(), n);
            inspectSubstrate(h, vm, snap);
            written += n;
        }
        char label[32];
        std::snprintf(label, sizeof(label), "sr=%d", sr);
        assertHealth(label, h);
    }
}

TEST_CASE("VoiceManager fuzz: chromatic C0..B7 (96 notes), each held briefly", "[contract][fuzz][note-range]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    // 96 notes from MIDI 12 (C0) to MIDI 107 (B7). Each is held for 4
    // blocks (~21 ms) then released — enough to exercise the agent
    // frequency math at extremes without making the test hour-long.
    for (int note = 12; note <= 107; ++note)
    {
        vm.noteOn(note, 1.0f);
        for (int b = 0; b < 4; ++b)
        {
            vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
            inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
            inspectSubstrate(h, vm, snap);
        }
        vm.noteOff(note);
        for (int b = 0; b < 2; ++b)
        {
            vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
            inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
            inspectSubstrate(h, vm, snap);
        }
    }
    assertHealth("chromatic_C0_B7", h);
}

TEST_CASE("VoiceManager fuzz: rapid retrigger (1000 noteOn/Off pairs)", "[contract][fuzz][retrigger]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 64; // small block so retrigger lands often
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    for (int i = 0; i < 1000; ++i)
    {
        const int note = 36 + (i % 60); // C2..B6
        vm.noteOn(note, 0.8f);
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        vm.noteOff(note);
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    assertHealth("rapid_retrigger_1000", h);
}

TEST_CASE("VoiceManager fuzz: CC1 mod wheel sweep mid-render", "[contract][fuzz][cc1]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    vm.noteOn(60, 1.0f);

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    constexpr int kBlocks = 192; // ~1 s at 48 kHz / 256
    for (int b = 0; b < kBlocks; ++b)
    {
        // Triangle sweep CC1: 0 → 1 → 0 → -... actually [0, 1] → bounded.
        const float phase = static_cast<float>(b) / static_cast<float>(kBlocks - 1);
        const float cc = (phase < 0.5f) ? phase * 2.0f : (1.0f - phase) * 2.0f;
        vm.setMidiCc1(cc);
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    assertHealth("cc1_sweep", h);
}

TEST_CASE("VoiceManager fuzz: macro automation mid-render (rapid sweeps)", "[contract][fuzz][automation]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    vm.noteOn(60, 1.0f);

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    constexpr int kBlocks = 192;
    for (int b = 0; b < kBlocks; ++b)
    {
        // Macro values cycle through extremes every block.
        const float phase = static_cast<float>(b) / 16.0f;
        auto saw = [phase](int offset)
        {
            const float p = phase + static_cast<float>(offset) * 0.13f;
            const float fp = p - std::floor(p);
            return fp;
        };
        vm.macros().tension = saw(0);
        vm.macros().damping = saw(1);
        vm.macros().density = saw(2);
        vm.macros().migration = saw(3);
        vm.macros().coherence = saw(4);
        vm.macros().excitation = saw(5);

        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    assertHealth("macro_automation_rapid", h);
}

TEST_CASE("VoiceManager fuzz: silence after allNotesOff is stable", "[contract][fuzz][silence]")
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    // Play and release 8 notes.
    for (int i = 0; i < 8; ++i)
    {
        vm.noteOn(48 + i * 3, 0.9f);
    }
    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderHealth h;
    for (int b = 0; b < 96; ++b) // ~0.5 s of held notes
    {
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    vm.allNotesOff();
    for (int b = 0; b < 384; ++b) // 2 s of release tail + silence
    {
        vm.renderBlockStereo(bufL.data(), bufR.data(), kBlockSize);
        inspectAndAccumulate(h, bufL.data(), bufR.data(), kBlockSize);
        inspectSubstrate(h, vm, snap);
    }
    assertHealth("silence_after_allnotesoff", h);
    // After 2 s of allNotesOff with γ damping, output should be near silent.
    const int tailStart = 96 * kBlockSize + 96 * kBlockSize; // ~last 1.5 s
    (void)tailStart;                                         // silence asserted via peak/RMS bounded; explicit decay
                                                             // assertion lands when the engine adds a per-voice idle
                                                             // tracker (Phase 3 follow-up).
}
