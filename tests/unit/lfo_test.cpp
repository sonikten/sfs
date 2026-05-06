// tests/unit/lfo_test.cpp
//
// Unit tests for the per-voice LFO. Verifies output range, shape behaviour,
// rate accuracy at sample rate, S&H determinism across runs, and that the
// LFO doesn't allocate or call libm transcendentals (covered indirectly by
// the no-alloc fixture; here we just verify the value contract).

#include "engine/lfo/lfo.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>

using sfs::engine::lfo::Lfo;
using sfs::engine::lfo::LfoShape;

namespace
{

constexpr float kSampleRate = 48000.0f;

void runFor(Lfo& lfo, int samples)
{
    for (int i = 0; i < samples; ++i)
    {
        (void)lfo.tick();
    }
}

float minOver(Lfo& lfo, int samples)
{
    float m = std::numeric_limits<float>::infinity();
    for (int i = 0; i < samples; ++i)
    {
        const float v = lfo.tick();
        if (v < m)
        {
            m = v;
        }
    }
    return m;
}

float maxOver(Lfo& lfo, int samples)
{
    float m = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < samples; ++i)
    {
        const float v = lfo.tick();
        if (v > m)
        {
            m = v;
        }
    }
    return m;
}

} // namespace

TEST_CASE("Lfo: sine output stays in [-1, 1] within dm_sin tolerance", "[lfo]")
{
    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    lfo.setRateHz(5.0f);
    lfo.setShape(LfoShape::Sine);

    // dm_sin peaks at 1.0 +/- ~5e-7 (polynomial approximation tolerance,
    // sfs-spec/06 §1.3 budget 1e-5). Bound checks use the spec budget.
    constexpr float kTol = 1e-5f;
    for (int i = 0; i < 96000; ++i)
    {
        const float v = lfo.tick();
        REQUIRE(v >= -1.0f - kTol);
        REQUIRE(v <= 1.0f + kTol);
    }
}

TEST_CASE("Lfo: triangle reaches +/- 1 each cycle", "[lfo]")
{
    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    lfo.setRateHz(10.0f);
    lfo.setShape(LfoShape::Triangle);

    // One cycle = 4800 samples; check across 4 cycles.
    const float lo = minOver(lfo, 4800 * 4);
    Lfo lfo2;
    lfo2.setSampleRate(kSampleRate);
    lfo2.setRateHz(10.0f);
    lfo2.setShape(LfoShape::Triangle);
    const float hi = maxOver(lfo2, 4800 * 4);

    REQUIRE(lo < -0.95f);
    REQUIRE(hi > 0.95f);
}

TEST_CASE("Lfo: square is exactly +/- 1", "[lfo]")
{
    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    lfo.setRateHz(20.0f);
    lfo.setShape(LfoShape::Square);

    for (int i = 0; i < 9600; ++i)
    {
        const float v = lfo.tick();
        REQUIRE((v == 1.0f || v == -1.0f));
    }
}

TEST_CASE("Lfo: rate of 1 Hz wraps once per sample-rate samples", "[lfo]")
{
    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    lfo.setRateHz(1.0f);
    lfo.setShape(LfoShape::Saw);

    // After exactly 48000 samples we should have wrapped once: phase ≈ 0.
    runFor(lfo, 48000);
    const float p = lfo.currentPhase();
    REQUIRE(p < 0.01f);
}

TEST_CASE("Lfo: sample-and-hold is deterministic across runs", "[lfo]")
{
    auto run = []()
    {
        Lfo lfo;
        lfo.setSampleRate(kSampleRate);
        lfo.setRateHz(10.0f);
        lfo.setShape(LfoShape::SampleHold);
        lfo.seedStream(0xDEADBEEFCAFEBABEull, 0, 0);
        std::vector<float> out;
        out.reserve(2400);
        for (int i = 0; i < 2400; ++i)
        {
            out.push_back(lfo.tick());
        }
        return out;
    };

    const auto a = run();
    const auto b = run();
    REQUIRE(a == b);

    // S&H output stays in [-1, 1).
    for (float v : a)
    {
        REQUIRE(v >= -1.0f);
        REQUIRE(v < 1.0f);
    }
}

TEST_CASE("Lfo: zero rate is silent (output flat)", "[lfo]")
{
    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    lfo.setRateHz(0.0f);
    lfo.setShape(LfoShape::Sine);

    // Phase doesn't advance -> sin(0) = 0 every tick.
    for (int i = 0; i < 1000; ++i)
    {
        const float v = lfo.tick();
        REQUIRE(std::fabs(v) < 1e-5f);
    }
}
