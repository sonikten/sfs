// tests/unit/voice_test.cpp
//
// End-to-end smoke tests for the Phase 1 Voice (substrate + agent pool +
// harvester glue). These are NOT the sonic-corner contract tests — those
// live under tests/contract/ and arrive when the canonical Phase 1 preset
// JSON loader lands. These just assert that the per-sample loop runs,
// produces bounded non-zero output when gated, and stays sane.

#include "engine/voice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>
#include <vector>

using sfs::engine::Voice;

namespace
{

constexpr float kSampleRate = 48000.0f;

float rms(const std::vector<float>& v)
{
    if (v.empty())
    {
        return 0.0f;
    }
    double sumSq = 0.0;
    for (float s : v)
    {
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(v.size())));
}

bool allFinite(const std::vector<float>& v)
{
    for (float s : v)
    {
        if (!std::isfinite(s))
        {
            return false;
        }
    }
    return true;
}

float peakAbs(const std::vector<float>& v)
{
    float m = 0.0f;
    for (float s : v)
    {
        const float a = std::fabs(s);
        if (a > m)
        {
            m = a;
        }
    }
    return m;
}

} // namespace

TEST_CASE("Voice constructs with sane defaults", "[voice]")
{
    Voice voice(1024, 16, kSampleRate);
    REQUIRE(voice.sampleRate() == Catch::Approx(kSampleRate));
    REQUIRE(voice.isGated() == false);
    REQUIRE(voice.harvesterPosition() == Catch::Approx(512.0f)); // midpoint of 1024
}

TEST_CASE("Ungated voice produces silent output", "[voice][quiet]")
{
    Voice voice(256, 4, kSampleRate);
    std::vector<float> out(1024, 0.0f);
    voice.renderBlock(out.data(), static_cast<int>(out.size()));

    REQUIRE(allFinite(out));
    REQUIRE(peakAbs(out) == Catch::Approx(0.0f).margin(1e-7f));
}

TEST_CASE("Gated voice produces non-zero, bounded output", "[voice][audible]")
{
    Voice voice(256, 4, kSampleRate);
    voice.noteOn(60, 1.0f); // C4
    REQUIRE(voice.isGated() == true);

    std::vector<float> out(static_cast<std::size_t>(kSampleRate), 0.0f); // 1 second
    voice.renderBlock(out.data(), static_cast<int>(out.size()));

    REQUIRE(allFinite(out));
    // Some audible energy after 1s of sustain.
    REQUIRE(rms(out) > 1e-5f);
    // No catastrophic blow-up; substrate clamp keeps things bounded.
    REQUIRE(peakAbs(out) < 10.0f);
}

TEST_CASE("noteOff stops new energy injection but substrate decay continues", "[voice][release]")
{
    Voice voice(256, 4, kSampleRate);
    std::vector<float> heldOut(static_cast<std::size_t>(kSampleRate / 4), 0.0f); // 0.25 s held
    std::vector<float> tailOut(static_cast<std::size_t>(kSampleRate), 0.0f);     // 1 s tail

    voice.noteOn(60, 1.0f);
    voice.renderBlock(heldOut.data(), static_cast<int>(heldOut.size()));
    voice.noteOff();
    REQUIRE(voice.isGated() == false);
    voice.renderBlock(tailOut.data(), static_cast<int>(tailOut.size()));

    REQUIRE(allFinite(tailOut));
    // The substrate keeps ringing for a while after noteOff (γ is small in defaults).
    // Peak in the early tail should be > peak in the late tail.
    const std::vector<float> earlyTail(tailOut.begin(), tailOut.begin() + static_cast<long>(tailOut.size() / 8));
    const std::vector<float> lateTail(tailOut.end() - static_cast<long>(tailOut.size() / 8), tailOut.end());
    REQUIRE(peakAbs(lateTail) <= peakAbs(earlyTail) * 2.0f);
}

TEST_CASE("Determinism: same noteOn → identical samples on two voices", "[voice][determinism]")
{
    Voice a(256, 4, kSampleRate);
    Voice b(256, 4, kSampleRate);
    a.noteOn(60, 1.0f);
    b.noteOn(60, 1.0f);

    std::vector<float> outA(2048, 0.0f);
    std::vector<float> outB(2048, 0.0f);
    a.renderBlock(outA.data(), static_cast<int>(outA.size()));
    b.renderBlock(outB.data(), static_cast<int>(outB.size()));

    for (std::size_t i = 0; i < outA.size(); ++i)
    {
        REQUIRE(outA[i] == outB[i]); // bit-identical, no margin
    }
}

TEST_CASE("Voice 2D topology produces bounded non-silent output", "[voice][2d]")
{
    using sfs::engine::Topology;

    Voice voice(1024, 16, kSampleRate);
    voice.setTopology(Topology::Torus2D);
    voice.macros().excitation = 0.0f; // pitched-like preset
    voice.macros().migration = 0.0f;
    voice.macros().coherence = 1.0f;
    voice.modMatrix().clearAllSlots();
    voice.noteOn(60, 1.0f);

    constexpr int kBlock = 256;
    constexpr int kBlocks = 64; // ~340 ms
    std::vector<float> outL(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlock), 0.0f);

    float peak = 0.0f;
    double sumSq = 0.0;
    for (int b = 0; b < kBlocks; ++b)
    {
        voice.renderBlockStereo(outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            REQUIRE(std::isfinite(outL[idx]));
            REQUIRE(std::isfinite(outR[idx]));
            peak = std::max(peak, std::max(std::fabs(outL[idx]), std::fabs(outR[idx])));
            sumSq += static_cast<double>(outL[idx]) * static_cast<double>(outL[idx]);
        }
    }
    const double rmsValue = std::sqrt(sumSq / static_cast<double>(kBlocks * kBlock));
    INFO("2D Voice peak=" << peak << " rms=" << rmsValue);
    REQUIRE(peak > 0.001f);     // produces audio
    REQUIRE(peak < 1.5f);       // bounded
    REQUIRE(rmsValue > 0.0001); // not silent on average
}

TEST_CASE("Voice 2D topology determinism: same input → identical samples", "[voice][2d][determinism]")
{
    using sfs::engine::Topology;

    auto run = []
    {
        Voice v(1024, 16, kSampleRate);
        v.setTopology(Topology::Torus2D);
        v.noteOn(60, 1.0f);
        std::vector<float> bufL(1024, 0.0f);
        std::vector<float> bufR(1024, 0.0f);
        v.renderBlockStereo(bufL.data(), bufR.data(), 1024);
        std::vector<float> out;
        out.reserve(2048);
        for (int i = 0; i < 1024; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            out.push_back(bufL[idx]);
            out.push_back(bufR[idx]);
        }
        return out;
    };

    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i] == b[i]);
    }
}

TEST_CASE("Voice 1D topology preserves Phase 2 bit-exact output", "[voice][1d-regression]")
{
    using sfs::engine::Topology;

    // Default topology must match Phase 2 behaviour exactly.
    Voice v1(256, 4, kSampleRate);
    Voice v2(256, 4, kSampleRate);
    REQUIRE(v1.topology() == Topology::Ring1D);

    // Explicit setTopology(Ring1D) on a fresh voice must be a no-op.
    v2.setTopology(Topology::Ring1D);
    v1.noteOn(60, 1.0f);
    v2.noteOn(60, 1.0f);

    std::vector<float> a(2048, 0.0f);
    std::vector<float> b(2048, 0.0f);
    v1.renderBlock(a.data(), 2048);
    v2.renderBlock(b.data(), 2048);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i] == b[i]);
    }
}
