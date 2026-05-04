// tests/unit/agent_pool_test.cpp
//
// Unit tests for the Phase 1 sine-only agent pool. Coverage:
//   - construction with default agent state
//   - noteOn / noteOff toggle the envelope and set frequency
//   - layoutEvenly spreads positions
//   - processOneSample generates sine output via dm_sin
//   - processOneSample deposits into the substrate
//   - phase advance is stable under repeated calls (no NaN, bounded)
//   - the multiplicative bend reduces / increases f_inst as expected

#include "engine/agents/agent_pool.h"
#include "engine/substrate/substrate_1d.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using sfs::engine::agents::Agent;
using sfs::engine::agents::AgentPool;
using sfs::engine::substrate::Substrate1D;

namespace
{

constexpr float kSampleRate = 48000.0f;

float l2EnergyU(const Substrate1D& s) noexcept
{
    const auto* u = s.displacement();
    float e = 0.0f;
    for (int x = 0; x < s.size(); ++x)
    {
        e += u[x] * u[x];
    }
    return e;
}

} // namespace

TEST_CASE("AgentPool constructs with N default agents", "[agents]")
{
    AgentPool pool(8);
    REQUIRE(pool.activeCount() == 0); // not active until noteOn
    for (int i = 0; i < 8; ++i)
    {
        const Agent& a = pool.agent(i);
        REQUIRE(a.envelope == Catch::Approx(0.0f));
        REQUIRE(a.phase == Catch::Approx(0.0f));
        REQUIRE(a.frequency == Catch::Approx(440.0f));
    }
}

TEST_CASE("noteOn sets frequency, resets phase, switches envelope on", "[agents][noteOn]")
{
    AgentPool pool(4);
    pool.noteOn(69, 1.0f); // A4 = 440 Hz
    REQUIRE(pool.activeCount() == 4);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(pool.agent(i).frequency == Catch::Approx(440.0f).epsilon(1e-3f));
        REQUIRE(pool.agent(i).envelope == Catch::Approx(1.0f));
        REQUIRE(pool.agent(i).phase == Catch::Approx(0.0f));
    }

    // C4 = 261.63 Hz
    pool.noteOn(60, 0.5f);
    REQUIRE(pool.agent(0).frequency == Catch::Approx(261.63f).epsilon(1e-3f));
    REQUIRE(pool.agent(0).amplitude == Catch::Approx(0.5f));
}

TEST_CASE("noteOff zeros the envelope but keeps activeCount", "[agents][noteOff]")
{
    AgentPool pool(2);
    pool.noteOn(69, 1.0f);
    REQUIRE(pool.agent(0).envelope == Catch::Approx(1.0f));

    pool.noteOff();
    REQUIRE(pool.agent(0).envelope == Catch::Approx(0.0f));
    REQUIRE(pool.activeCount() == 2); // still allocated; substrate decay does the rest
}

TEST_CASE("layoutEvenly distributes positions across the substrate", "[agents][layout]")
{
    AgentPool pool(4);
    pool.layoutEvenly(256);
    REQUIRE(pool.agent(0).position == Catch::Approx(0.0f));
    REQUIRE(pool.agent(1).position == Catch::Approx(64.0f));
    REQUIRE(pool.agent(2).position == Catch::Approx(128.0f));
    REQUIRE(pool.agent(3).position == Catch::Approx(192.0f));
}

TEST_CASE("processOneSample deposits into the substrate when gated on", "[agents][substrate-coupling]")
{
    Substrate1D substrate(256, kSampleRate);
    substrate.setCoefficients(0.0f, 0.0f, 0.0f); // freeze propagation so we can measure deposit only
    substrate.reset();

    AgentPool pool(1);
    pool.noteOn(69, 1.0f);
    pool.layoutEvenly(256); // single agent at position 0
    // Advance phase a quarter cycle so the sine has nonzero output.
    pool.mutableAgent(0).phase = 0.25f;

    pool.processOneSample(substrate, kSampleRate);
    substrate.step();

    // With c²=κ=γ=0, the deposit lands at u_inject[0..1] then becomes u[0..1] after step().
    // Sine of 2π·0.25 = sin(π/2) = 1. Deposit = w · a · e · y = 0.05 · 1 · 1 · 1 = 0.05.
    REQUIRE(l2EnergyU(substrate) > 0.0f);
    REQUIRE(substrate.displacement()[0] == Catch::Approx(0.05f).margin(1e-5f));
}

TEST_CASE("processOneSample does NOT deposit when gated off", "[agents][noteOff-quiet]")
{
    Substrate1D substrate(256, kSampleRate);
    substrate.setCoefficients(0.0f, 0.0f, 0.0f);
    substrate.reset();

    AgentPool pool(1);
    pool.noteOn(69, 1.0f);
    pool.layoutEvenly(256);
    pool.noteOff(); // envelope = 0
    pool.mutableAgent(0).phase = 0.25f;

    pool.processOneSample(substrate, kSampleRate);
    substrate.step();
    // Deposit scales by envelope; gated off → no deposit → no substrate energy.
    REQUIRE(l2EnergyU(substrate) == Catch::Approx(0.0f));
}

TEST_CASE("Phase stays bounded under many process calls (no NaN, no runaway)", "[agents][phase-stability]")
{
    Substrate1D substrate(256, kSampleRate);
    substrate.setCoefficients(0.30f, 0.05f, 0.005f);
    substrate.reset();

    AgentPool pool(8);
    pool.noteOn(69, 1.0f);
    pool.layoutEvenly(256);

    for (int n = 0; n < 10000; ++n)
    {
        pool.processOneSample(substrate, kSampleRate);
        substrate.step();
    }

    for (int i = 0; i < pool.activeCount(); ++i)
    {
        const float p = pool.agent(i).phase;
        REQUIRE(std::isfinite(p));
        REQUIRE(p >= 0.0f);
        REQUIRE(p < 1.0f);
    }
}

TEST_CASE("Multiplicative bend changes the phase advance rate", "[agents][bend]")
{
    // Two pools: one with mod_sensitivity = 0 (no bend), one with > 0.
    // After processing with the same non-zero substrate state, the bent
    // pool's phase should differ from the unbent one.
    Substrate1D substrate(256, kSampleRate);
    substrate.setCoefficients(0.0f, 0.0f, 0.0f);
    substrate.reset();
    substrate.deposit(0.0f, 1.0f); // put a 1.0 spike at cell 0
    substrate.step();
    REQUIRE(substrate.displacement()[0] == Catch::Approx(1.0f).margin(1e-5f));

    AgentPool unbent(1);
    AgentPool bent(1);
    unbent.noteOn(69, 1.0f);
    bent.noteOn(69, 1.0f);
    unbent.mutableAgent(0).position = 0.0f;
    bent.mutableAgent(0).position = 0.0f;
    unbent.mutableAgent(0).modSensitivity = 0.0f;
    bent.mutableAgent(0).modSensitivity = 0.5f;

    // Run one sample. With the substrate at u[0]=1.0, the bent agent gets
    // f_inst = 440 * (1 + 0.5 * 1.0) = 660 Hz; the unbent agent stays at 440.
    // Different f_inst → different phase advance.
    Substrate1D readOnlySubstrate = substrate; // copy so deposits don't pollute the read
    unbent.processOneSample(readOnlySubstrate, kSampleRate);
    Substrate1D s2 = substrate;
    bent.processOneSample(s2, kSampleRate);

    REQUIRE(bent.agent(0).phase != Catch::Approx(unbent.agent(0).phase));
    // Bent agent advanced faster (positive substrate amplitude → higher f_inst).
    REQUIRE(bent.agent(0).phase > unbent.agent(0).phase);
}
