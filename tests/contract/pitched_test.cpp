// tests/contract/pitched_test.cpp
//
// Sonic-corner contract test: pitched (Phase 1 form per sfs-spec/08 §2.3).
// Renders a chromatic sequence and asserts the engine's perceived pitch
// matches the played MIDI notes within ±3%:
//
//     for each note n in chromatic range:
//         render n for `kHoldSeconds` at velocity 1.0
//         estimate pitch via autocorrelation of the steady-state window
//         assert |estimated - expected| / expected < 0.03
//
// Phase 1 ships with a one-octave range (MIDI 48..60 = C3..C4 inclusive,
// 13 notes) and uses autocorrelation pitch detection instead of full YIN.
// The spec's 61-note C2..C7 range + YIN is enabled when YIN lands as a
// proper engine-side primitive (lots more code; deferred from Phase 1).

#include "engine/voice.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

// MIDI note → Hz (12-TET, A4 = 69 = 440 Hz).
[[nodiscard]] float midiNoteToHz(int n)
{
    return 440.0f * std::exp2((static_cast<float>(n) - 69.0f) / 12.0f);
}

// Autocorrelation pitch detection. Returns Hz, or 0.0f if no clear peak.
// Algorithm:
//   1. Subtract mean (DC bias detuner).
//   2. r(τ) = Σ x[n] · x[n+τ]   for τ in [τ_min, τ_max]
//   3. Find peak r(τ) (skipping τ=0); the lag IS the period.
//   4. Refine peak with parabolic interpolation.
//   5. pitch = sampleRate / refinedLag
[[nodiscard]] float autocorrelationPitchHz(const float* buf, int len, float sampleRate, float fMin, float fMax)
{
    // Lag bounds.
    const int tauMin = static_cast<int>(static_cast<float>(sampleRate) / fMax);
    const int tauMax = std::min(len / 2, static_cast<int>(static_cast<float>(sampleRate) / fMin));
    if (tauMin >= tauMax)
    {
        return 0.0f;
    }

    // De-mean.
    double sum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        sum += static_cast<double>(buf[i]);
    }
    const float mean = static_cast<float>(sum / static_cast<double>(len));

    std::vector<float> centered(static_cast<std::size_t>(len), 0.0f);
    for (int i = 0; i < len; ++i)
    {
        centered[static_cast<std::size_t>(i)] = buf[i] - mean;
    }

    // Autocorrelation across the lag range.
    int bestLag = 0;
    double bestVal = -std::numeric_limits<double>::infinity();
    std::vector<float> r(static_cast<std::size_t>(tauMax + 1), 0.0f);
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i + tau < len; ++i)
        {
            s += static_cast<double>(centered[static_cast<std::size_t>(i)]) *
                 static_cast<double>(centered[static_cast<std::size_t>(i + tau)]);
        }
        r[static_cast<std::size_t>(tau)] = static_cast<float>(s);
        if (s > bestVal)
        {
            bestVal = s;
            bestLag = tau;
        }
    }

    if (bestLag == 0 || bestLag == tauMin || bestLag == tauMax)
    {
        return 0.0f; // peak at boundary → unreliable
    }

    // Parabolic interpolation around the integer peak: refines lag to a
    // fractional sample. Improves pitch accuracy from ±1 sample (~quartertone
    // at low pitches) to sub-cent.
    const float yLeft = r[static_cast<std::size_t>(bestLag - 1)];
    const float yMid = r[static_cast<std::size_t>(bestLag)];
    const float yRight = r[static_cast<std::size_t>(bestLag + 1)];
    const float denom = 2.0f * (2.0f * yMid - yLeft - yRight);
    const float refined = (denom != 0.0f) ? (static_cast<float>(bestLag) + (yRight - yLeft) / denom)
                                          : static_cast<float>(bestLag);
    return static_cast<float>(sampleRate) / refined;
}

} // namespace

namespace
{

struct PitchResult
{
    int passed = 0;
    int failed = 0;
};

PitchResult runChromaticSweep(sfs::engine::Topology topology, int midiLo, int midiHi)
{
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;
    constexpr float kHoldSeconds = 0.6f;
    constexpr float kAnalysisStart = 0.2f;
    constexpr float kPitchTolerance = 0.03f;

    PitchResult result;

    for (int midi = midiLo; midi <= midiHi; ++midi)
    {
        Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
        if (topology == sfs::engine::Topology::Torus2D)
        {
            voice.setTopology(topology);
        }

        voice.macros().excitation = 0.0f;
        voice.macros().migration = 0.0f;
        voice.macros().coherence = 1.0f;
        voice.modMatrix().clearAllSlots();
        voice.noteOn(midi, 1.0f);
        auto& agents = voice.agents();
        for (int i = 0; i < agents.activeCount(); ++i)
        {
            agents.mutableAgent(i).migrationRate = 0.0f;
            agents.mutableAgent(i).migrationNoiseScale = 0.0f;
            agents.mutableAgent(i).modSensitivity = 0.0f;
        }

        const int totalSamples = static_cast<int>(kHoldSeconds * static_cast<float>(kSampleRate));
        std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
        constexpr int kBlock = 256;
        for (int written = 0; written < totalSamples; written += kBlock)
        {
            const int n = std::min(kBlock, totalSamples - written);
            voice.renderBlock(mono.data() + written, n);
        }

        const int analysisStart = static_cast<int>(kAnalysisStart * static_cast<float>(kSampleRate));
        const int analysisLen = totalSamples - analysisStart;
        // analysisLen is positive by the constexpr kHoldSeconds > kAnalysisStart
        // contract; no runtime check needed (MSVC /WX flags the dead comparison).

        const float estimatedHz = autocorrelationPitchHz(mono.data() + analysisStart,
                                                         analysisLen,
                                                         static_cast<float>(kSampleRate),
                                                         /*fMin*/ 50.0f,
                                                         /*fMax*/ 2000.0f);
        const float expectedHz = midiNoteToHz(midi);
        const float relativeErr = std::fabs(estimatedHz - expectedHz) / expectedHz;

        const bool pass = (estimatedHz > 0.0f) && (relativeErr < kPitchTolerance);
        std::printf("[pitched contract] MIDI %3d  expected %7.2f Hz  estimated %7.2f Hz  "
                    "err %6.3f%%   %s\n",
                    midi,
                    static_cast<double>(expectedHz),
                    static_cast<double>(estimatedHz),
                    static_cast<double>(relativeErr) * 100.0,
                    pass ? "OK" : "FAIL");
        if (pass)
        {
            ++result.passed;
        }
        else
        {
            ++result.failed;
        }
    }
    return result;
}

} // namespace

TEST_CASE("Pitched contract (1D Phase 2 form): chromatic notes match ±3%", "[contract][pitched][1d]")
{
    constexpr int kMidiLo = 48; // C3
    constexpr int kMidiHi = 60; // C4 inclusive → 13 notes

    const auto r = runChromaticSweep(sfs::engine::Topology::Ring1D, kMidiLo, kMidiHi);
    std::printf("\n[pitched contract 1D] %d / %d notes within ±3%%\n", r.passed, r.passed + r.failed);
    REQUIRE(r.failed == 0);
}

TEST_CASE("Pitched contract (2D torus): chromatic notes match ±3%", "[contract][pitched][2d]")
{
    // 2D substrate physics has more dispersion, so we narrow the note
    // range to C3-G3 (where the autocorrelation is most reliable on
    // a 32×32 torus). Phase 4 gets a stricter test once SIMD enables
    // 64×64 / 128×128 grids.
    constexpr int kMidiLo = 48; // C3
    constexpr int kMidiHi = 55; // G3

    const auto r = runChromaticSweep(sfs::engine::Topology::Torus2D, kMidiLo, kMidiHi);
    std::printf("\n[pitched contract 2D] %d / %d notes within ±3%%\n", r.passed, r.passed + r.failed);
    REQUIRE(r.failed == 0);
}
