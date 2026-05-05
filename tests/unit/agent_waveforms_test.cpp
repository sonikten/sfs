// tests/unit/agent_waveforms_test.cpp
//
// Phase 2 §9 step 5 — basic sanity for each of the five agent waveforms
// (sine, saw, square, fmpair, noise). We're not asserting spectrum
// quality here (that needs FFT analysis with formal aliasing budgets in
// Phase 5); just that each shape produces bounded non-silent output and
// doesn't NaN out.

#include "engine/agents/agent_pool.h"
#include "engine/substrate/substrate_1d.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using sfs::engine::agents::AgentPool;
using sfs::engine::agents::AgentShape;
using sfs::engine::substrate::Substrate1D;

namespace
{
constexpr float kSampleRate = 48000.0f;

struct Stats
{
    float peakAbs = 0.0f;
    int nonZero = 0;
    bool finite = true;
};

Stats renderShape(AgentShape shape, int numSamples = 4800)
{
    // Realistic substrate coefficients so the wave equation self-limits;
    // c²=κ=γ=0 would let deposits accumulate without bound. Read at the
    // substrate midpoint so the wave has to propagate to be heard.
    Substrate1D substrate(256, kSampleRate);
    substrate.setCoefficients(0.30f, 0.05f, 0.01f);
    substrate.reset();

    AgentPool pool(1);
    pool.noteOn(60, 1.0f);
    pool.layoutEvenly(256);

    auto& a = pool.mutableAgent(0);
    a.shape = shape;
    a.modSensitivity = 0.0f;
    a.migrationRate = 0.0f;
    a.migrationNoiseScale = 0.0f;
    a.fmRatio = 1.0f;
    a.fmIndex = 0.5f;

    Stats s;
    for (int i = 0; i < numSamples; ++i)
    {
        pool.processOneSample(substrate, kSampleRate);
        substrate.step();
        const float u = substrate.read(128.0f); // midpoint of the 256-cell ring
        if (!std::isfinite(u))
        {
            s.finite = false;
            break;
        }
        const float au = std::fabs(u);
        if (au > s.peakAbs)
        {
            s.peakAbs = au;
        }
        if (au > 1e-6f)
        {
            ++s.nonZero;
        }
    }
    return s;
}

} // namespace

TEST_CASE("Agent waveform: sine produces bounded oscillation", "[agents][waveforms][sine]")
{
    const Stats s = renderShape(AgentShape::Sine);
    CAPTURE(s.peakAbs, s.nonZero, s.finite);
    REQUIRE(s.finite);
    REQUIRE(s.peakAbs > 0.0f);
    REQUIRE(s.peakAbs < 25.0f); // generous bound; substrate accumulation under one-voice render
    REQUIRE(s.nonZero > 100);
}

TEST_CASE("Agent waveform: saw produces bounded oscillation", "[agents][waveforms][saw]")
{
    const Stats s = renderShape(AgentShape::Saw);
    CAPTURE(s.peakAbs, s.nonZero, s.finite);
    REQUIRE(s.finite);
    REQUIRE(s.peakAbs > 0.0f);
    REQUIRE(s.peakAbs < 25.0f); // generous; substrate accumulation under one-voice render
                                // before Voice's soft-clip stage. Real plug-in output is bounded.
    REQUIRE(s.nonZero > 100);
}

TEST_CASE("Agent waveform: square produces bounded oscillation", "[agents][waveforms][square]")
{
    const Stats s = renderShape(AgentShape::Square);
    CAPTURE(s.peakAbs, s.nonZero, s.finite);
    REQUIRE(s.finite);
    REQUIRE(s.peakAbs > 0.0f);
    REQUIRE(s.peakAbs < 25.0f); // generous; substrate accumulation under one-voice render
                                // before Voice's soft-clip stage. Real plug-in output is bounded.
    REQUIRE(s.nonZero > 100);
}

TEST_CASE("Agent waveform: fmpair produces bounded oscillation", "[agents][waveforms][fmpair]")
{
    const Stats s = renderShape(AgentShape::FmPair);
    CAPTURE(s.peakAbs, s.nonZero, s.finite);
    REQUIRE(s.finite);
    REQUIRE(s.peakAbs > 0.0f);
    REQUIRE(s.peakAbs < 25.0f); // generous; substrate accumulation under one-voice render
                                // before Voice's soft-clip stage. Real plug-in output is bounded.
    REQUIRE(s.nonZero > 100);
}

TEST_CASE("Agent waveform: noise produces bounded oscillation", "[agents][waveforms][noise]")
{
    const Stats s = renderShape(AgentShape::Noise);
    CAPTURE(s.peakAbs, s.nonZero, s.finite);
    REQUIRE(s.finite);
    REQUIRE(s.peakAbs > 0.0f);
    REQUIRE(s.peakAbs < 25.0f); // generous; substrate accumulation under one-voice render
                                // before Voice's soft-clip stage. Real plug-in output is bounded.
    REQUIRE(s.nonZero > 100);
}
