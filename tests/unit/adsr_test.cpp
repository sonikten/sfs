// tests/unit/adsr_test.cpp
//
// Unit tests for the per-voice linear-piecewise ADSR envelope. Verifies
// stage transitions, ramp times, and that release picks up from current
// value (no zipper / no jump back to sustain).

#include "engine/envelope/adsr.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using sfs::engine::envelope::Adsr;

namespace
{

// Tick `n` samples and return the last value.
float tickN(Adsr& env, int n)
{
    float v = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        v = env.tick();
    }
    return v;
}

} // namespace

TEST_CASE("Adsr: idle stays at 0", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);

    REQUIRE(env.stage() == Adsr::Stage::Idle);
    REQUIRE(env.value() == 0.0f);

    for (int i = 0; i < 100; ++i)
    {
        const float v = env.tick();
        REQUIRE(v == 0.0f);
    }
    REQUIRE(env.stage() == Adsr::Stage::Idle);
}

TEST_CASE("Adsr: attack ramps 0 → 1 in attackMs", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);
    env.setAttackMs(10.0f); // 480 samples
    env.setDecayMs(100.0f);
    env.setSustainLevel(0.5f);
    env.setReleaseMs(50.0f);

    env.noteOn();

    // Halfway through attack: value ≈ 0.5
    const float midAttack = tickN(env, 240);
    REQUIRE(midAttack > 0.45f);
    REQUIRE(midAttack < 0.55f);

    // End of attack: should land at 1.0 and switch to decay.
    tickN(env, 240); // total 480 samples -> end of attack
    REQUIRE(env.stage() == Adsr::Stage::Decay);
    REQUIRE(env.value() <= 1.0f);
}

TEST_CASE("Adsr: decay reaches sustain level and holds", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);
    env.setAttackMs(1.0f); // 48 samples — fast
    env.setDecayMs(20.0f); // 960 samples
    env.setSustainLevel(0.4f);
    env.setReleaseMs(50.0f);

    env.noteOn();
    // Run well past attack + decay.
    tickN(env, 48 + 960 + 100);

    REQUIRE(env.stage() == Adsr::Stage::Sustain);
    REQUIRE(std::fabs(env.value() - 0.4f) < 1e-3f);

    // Sustain holds indefinitely.
    for (int i = 0; i < 10000; ++i)
    {
        const float v = env.tick();
        REQUIRE(std::fabs(v - 0.4f) < 1e-3f);
    }
}

TEST_CASE("Adsr: release ramps from current value, not sustain", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);
    env.setAttackMs(20.0f); // 960 samples
    env.setDecayMs(50.0f);
    env.setSustainLevel(0.5f);
    env.setReleaseMs(10.0f); // 480 samples

    env.noteOn();
    // Stop midway through attack — value ≈ 0.5
    tickN(env, 480);
    const float midAttackValue = env.value();
    REQUIRE(midAttackValue > 0.4f);
    REQUIRE(midAttackValue < 0.6f);

    env.noteOff();
    REQUIRE(env.stage() == Adsr::Stage::Release);

    // Release should bring it to 0 within ~480 samples (linear ramp at
    // 1.0/releaseSamples per tick — from 0.5 it's ~240 samples).
    tickN(env, 480);
    REQUIRE(env.value() == 0.0f);
    REQUIRE(env.stage() == Adsr::Stage::Idle);
}

TEST_CASE("Adsr: noteOn during release re-attacks from current value", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);
    env.setAttackMs(20.0f);
    env.setDecayMs(50.0f);
    env.setSustainLevel(0.5f);
    env.setReleaseMs(40.0f);

    env.noteOn();
    tickN(env, 48000); // long enough to reach sustain

    env.noteOff();
    tickN(env, 240); // partway through release
    const float partway = env.value();
    REQUIRE(partway > 0.0f);
    REQUIRE(partway < 0.5f);

    env.noteOn();
    REQUIRE(env.stage() == Adsr::Stage::Attack);

    // Value continues from `partway` upward.
    const float afterOneTick = env.tick();
    REQUIRE(afterOneTick > partway);
}

TEST_CASE("Adsr: zero attack lands at 1.0 in one tick", "[adsr]")
{
    Adsr env;
    env.setSampleRate(48000.0f);
    env.setAttackMs(0.0f); // immediate
    env.setDecayMs(100.0f);
    env.setSustainLevel(0.7f);
    env.setReleaseMs(100.0f);

    env.noteOn();
    const float first = env.tick();
    REQUIRE(first >= 1.0f);
    REQUIRE(env.stage() == Adsr::Stage::Decay);
}
