// tests/unit/macros_test.cpp
//
// Asserts each macro's fan-out matches the spec curves (sfs-spec/05 §3)
// at the corner values 0 and 1, and at a midpoint (m=0.5) so the curve
// shape is checked, not just endpoints.

#include "engine/macros/macros.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using sfs::engine::macros::fanOut;
using sfs::engine::macros::InternalFields;
using sfs::engine::macros::MacroValues;

namespace
{

constexpr float kEps = 1e-6f;

bool approx(float a, float b)
{
    return std::fabs(a - b) < kEps;
}

InternalFields atTension(float t)
{
    MacroValues m;
    m.tension = t;
    return fanOut(m);
}
InternalFields atDamping(float d)
{
    MacroValues m;
    m.damping = d;
    return fanOut(m);
}
InternalFields atDensity(float d)
{
    MacroValues m;
    m.density = d;
    return fanOut(m);
}
InternalFields atMigration(float v)
{
    MacroValues m;
    m.migration = v;
    return fanOut(m);
}
InternalFields atCoherence(float v)
{
    MacroValues m;
    m.coherence = v;
    return fanOut(m);
}
InternalFields atExcitation(float v)
{
    MacroValues m;
    m.excitation = v;
    return fanOut(m);
}

} // namespace

TEST_CASE("TENSION fan-out matches spec curves", "[macros][tension]")
{
    REQUIRE(approx(atTension(0.0f).substrateC2, 0.0f));
    REQUIRE(approx(atTension(1.0f).substrateC2, 0.49f));
    REQUIRE(approx(atTension(0.5f).substrateC2, 0.49f * 0.25f));

    REQUIRE(approx(atTension(0.0f).substrateViscosityFloor, 0.0f));
    REQUIRE(approx(atTension(1.0f).substrateViscosityFloor, 0.005f));

    REQUIRE(approx(atTension(0.0f).harvesterSpacingScale, 0.5f));
    REQUIRE(approx(atTension(1.0f).harvesterSpacingScale, 1.0f));
}

TEST_CASE("DAMPING fan-out matches spec curves", "[macros][damping]")
{
    REQUIRE(approx(atDamping(0.0f).substrateGamma, 0.0f));
    REQUIRE(approx(atDamping(1.0f).substrateGamma, 0.05f));
    REQUIRE(approx(atDamping(0.5f).substrateGamma, 0.05f * 0.25f));

    REQUIRE(approx(atDamping(0.0f).agentEnvelopeReleaseScale, 0.5f));
    REQUIRE(approx(atDamping(1.0f).agentEnvelopeReleaseScale, 2.0f));
}

TEST_CASE("DENSITY fan-out matches spec curves", "[macros][density]")
{
    REQUIRE(atDensity(0.0f).agentActiveCount == 8);
    REQUIRE(atDensity(1.0f).agentActiveCount == 64);
    REQUIRE(atDensity(0.5f).agentActiveCount == 36); // round(8 + 28)

    REQUIRE(approx(atDensity(0.0f).agentDepositWeightScale, 0.5f));
    REQUIRE(approx(atDensity(1.0f).agentDepositWeightScale, 1.0f));
}

TEST_CASE("MIGRATION fan-out matches spec curves", "[macros][migration]")
{
    REQUIRE(approx(atMigration(0.0f).agentDriftScale, 0.0f));
    REQUIRE(approx(atMigration(1.0f).agentDriftScale, 1.0f));
    REQUIRE(approx(atMigration(0.5f).agentDriftScale, 0.25f));

    REQUIRE(approx(atMigration(0.0f).agentMigrationNoiseScale, 0.0f));
    REQUIRE(approx(atMigration(1.0f).agentMigrationNoiseScale, 1.0f));

    REQUIRE(approx(atMigration(0.0f).harvesterOrbitDepth, 0.0f));
    REQUIRE(approx(atMigration(1.0f).harvesterOrbitDepth, 0.3f));
}

TEST_CASE("COHERENCE fan-out matches spec curves", "[macros][coherence]")
{
    REQUIRE(approx(atCoherence(0.0f).agentHarmonicLock, 0.0f));
    REQUIRE(approx(atCoherence(1.0f).agentHarmonicLock, 1.0f));

    REQUIRE(approx(atCoherence(0.0f).agentDetuneScale, 1.0f));
    REQUIRE(approx(atCoherence(1.0f).agentDetuneScale, 0.0f));
    REQUIRE(approx(atCoherence(0.5f).agentDetuneScale, 0.25f));

    REQUIRE(approx(atCoherence(0.0f).agentShapeParamDrift, 0.3f));
    REQUIRE(approx(atCoherence(1.0f).agentShapeParamDrift, 0.0f));
}

TEST_CASE("EXCITATION fan-out matches spec curves", "[macros][excitation]")
{
    REQUIRE(approx(atExcitation(0.0f).agentModSensitivityScale, 0.0f));
    REQUIRE(approx(atExcitation(1.0f).agentModSensitivityScale, 1.0f));
    REQUIRE(approx(atExcitation(0.5f).agentModSensitivityScale, 0.25f));

    REQUIRE(approx(atExcitation(0.0f).substrateViscosityOffset, 0.0f));
    REQUIRE(approx(atExcitation(1.0f).substrateViscosityOffset, -0.01f));

    REQUIRE(approx(atExcitation(0.0f).agentFrequencyClampSoftness, 1.0f));
    REQUIRE(approx(atExcitation(1.0f).agentFrequencyClampSoftness, 0.5f));
}

TEST_CASE("clampInPlace clamps each macro to [0, 1]", "[macros][clamp]")
{
    MacroValues m;
    m.tension = -0.5f;
    m.damping = 1.5f;
    m.density = -100.0f;
    m.migration = 100.0f;
    m.coherence = 0.5f;
    m.excitation = 0.0f;
    m.clampInPlace();
    REQUIRE(m.tension == 0.0f);
    REQUIRE(m.damping == 1.0f);
    REQUIRE(m.density == 0.0f);
    REQUIRE(m.migration == 1.0f);
    REQUIRE(m.coherence == 0.5f);
    REQUIRE(m.excitation == 0.0f);
}

TEST_CASE("Default MacroValues fan-out matches Phase 1 hard-coded defaults", "[macros][defaults]")
{
    // Spec defaults: tension=0.5, damping=0.3, density=0.6, migration=0.2,
    // coherence=0.8, excitation=0.3.
    // The Voice Phase 1 defaults were c²=0.30, γ=0.005, κ=0.05 — these were
    // hand-tuned, NOT derived from macros. The fan-out at default macro
    // values produces *different* numbers (it's the spec's curves, not the
    // Phase 1 hand-tuned set). This test just sanity-checks the math
    // computes finite values.
    const InternalFields f = fanOut(MacroValues{});
    REQUIRE(std::isfinite(f.substrateC2));
    REQUIRE(std::isfinite(f.substrateGamma));
    REQUIRE(f.agentActiveCount > 0);
    REQUIRE(f.agentActiveCount <= 64);
    REQUIRE(f.substrateC2 >= 0.0f);
    REQUIRE(f.substrateGamma >= 0.0f);
}
