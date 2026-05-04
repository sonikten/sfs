// tests/unit/substrate_1d_test.cpp
//
// Unit tests for the 1D substrate. Per sfs-spec/02 §11 Phase 1 lands the
// first two test vectors:
//
//   1. Pure ring (zero loss): impulse at cell 0 should propagate; with
//      c²=0.49, γ=0, κ=0 the wave reaches cell N/2 around sample N/(2·c).
//   2. Lossy: same impulse with γ=0.01 — total energy decays approximately
//      exponentially.
//
// Plus structural tests on the CFL clamp, deposit/read kernels, and reset.

#include "engine/substrate/substrate_1d.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using sfs::engine::substrate::Substrate1D;

namespace
{

// Sum of |u[x]|² + |v[x]|² — proxy for substrate energy. Not the true
// Hamiltonian (which would weight v by 1/c²) but monotonic with it.
float l2Energy(const Substrate1D& s) noexcept
{
    const auto* u = s.displacement();
    const auto* v = s.velocity();
    float e = 0.0f;
    for (int x = 0; x < s.size(); ++x)
    {
        e += u[x] * u[x] + v[x] * v[x];
    }
    return e;
}

} // namespace

TEST_CASE("Substrate1D constructs with default coefficients", "[substrate][1d]")
{
    Substrate1D s(1024, 48000.0f);
    REQUIRE(s.size() == 1024);
    REQUIRE(s.sampleRate() == Catch::Approx(48000.0f));
    REQUIRE(s.c2() == Catch::Approx(0.30f));
    REQUIRE(s.kappa() == Catch::Approx(0.05f));
    REQUIRE(s.gamma() == Catch::Approx(0.005f));
    // u/v should start at zero.
    REQUIRE(l2Energy(s) == Catch::Approx(0.0f));
}

TEST_CASE("setCoefficients applies the joint CFL clamp", "[substrate][1d][cfl]")
{
    Substrate1D s(256, 48000.0f);

    // Below the bound: pass through unchanged.
    s.setCoefficients(0.30f, 0.10f, 0.005f);
    REQUIRE(s.c2() == Catch::Approx(0.30f));
    REQUIRE(s.kappa() == Catch::Approx(0.10f));

    // Above the bound: scale both proportionally so the sum equals kCflBound1D.
    s.setCoefficients(0.49f, 0.40f, 0.005f);
    const float sum = s.c2() + s.kappa();
    REQUIRE(sum == Catch::Approx(Substrate1D::kCflBound1D));
    // Ratio preserved.
    REQUIRE(s.c2() / s.kappa() == Catch::Approx(0.49f / 0.40f));

    // Negative inputs clamp to zero.
    s.setCoefficients(-1.0f, -1.0f, -1.0f);
    REQUIRE(s.c2() == Catch::Approx(0.0f));
    REQUIRE(s.kappa() == Catch::Approx(0.0f));
    REQUIRE(s.gamma() == Catch::Approx(0.0f));

    // Gamma clamps to [0, 1).
    s.setCoefficients(0.30f, 0.05f, 5.0f);
    REQUIRE(s.gamma() < 1.0f);
}

TEST_CASE("Linear deposit splits across two cells per the (1-frac, frac) kernel", "[substrate][1d][deposit]")
{
    Substrate1D s(64, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f); // no propagation, no decay
    s.reset();

    // Deposit 1.0 at fractional position 10.25. Linear kernel:
    //   u_inject[10] += 0.75; u_inject[11] += 0.25
    // After step(): u[10] = 0.75, u[11] = 0.25 (with c²=κ=0, the leapfrog is
    // v += inject; u += v, so first step puts the injection straight into u).
    s.deposit(10.25f, 1.0f);
    s.step();
    const auto* u = s.displacement();
    REQUIRE(u[10] == Catch::Approx(0.75f).margin(1e-6f));
    REQUIRE(u[11] == Catch::Approx(0.25f).margin(1e-6f));
    REQUIRE(u[12] == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Linear read interpolates between two cells", "[substrate][1d][read]")
{
    Substrate1D s(64, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f);
    s.reset();
    s.deposit(20.0f, 1.0f); // exactly at cell 20, no fractional
    s.step();

    REQUIRE(s.read(20.0f) == Catch::Approx(1.0f).margin(1e-6f));
    REQUIRE(s.read(20.5f) == Catch::Approx(0.5f).margin(1e-6f)); // 0.5 * u[20] + 0.5 * u[21]
    REQUIRE(s.read(21.0f) == Catch::Approx(0.0f).margin(1e-6f)); // u[21] is 0
}

TEST_CASE("Position wraps around the ring (modulo N)", "[substrate][1d][wrap]")
{
    Substrate1D s(64, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f);
    s.reset();
    s.deposit(64.0f, 1.0f); // wraps to position 0
    s.step();
    REQUIRE(s.displacement()[0] == Catch::Approx(1.0f).margin(1e-6f));

    s.reset();
    s.deposit(-1.0f, 1.0f); // wraps to position 63
    s.step();
    REQUIRE(s.displacement()[63] == Catch::Approx(1.0f).margin(1e-6f));
}

TEST_CASE("Test vector 1: impulse on a lossless ring propagates", "[substrate][1d][propagation]")
{
    // sfs-spec/02 §11: pure ring, zero loss. Impulse at cell 0; the wave
    // should reach cell N/2 around sample N/(2·c) = N/(2·sqrt(c²)).
    constexpr int kN = 256;
    constexpr float kC2 = 0.49f;
    Substrate1D s(kN, 48000.0f);
    s.setCoefficients(kC2, 0.0f, 0.0f);
    s.reset();
    s.deposit(0.0f, 1.0f);
    s.step();
    const float initialEnergy = l2Energy(s);
    REQUIRE(initialEnergy > 0.0f);

    // Phase velocity ≈ √c² ≈ 0.7 cells/sample. Step until the wavefront
    // should have reached cell N/2 (≈ 128 samples for N=256, c≈0.7).
    const float c = std::sqrt(kC2);
    const int expectedSamples = static_cast<int>(static_cast<float>(kN) / (2.0f * c));
    for (int i = 0; i < expectedSamples + 32; ++i) // a little past the ETA
    {
        s.step();
    }

    // u[N/2] should be measurably non-zero — the wavefront has passed through.
    // Don't assert a specific shape; the wave is dispersive in this discrete scheme.
    REQUIRE(std::abs(s.displacement()[kN / 2]) > 1e-4f);

    // With γ=0, the wave packet spreads/disperses across the ring. The true
    // Hamiltonian (1/2)·(u² + v²/c²) is conserved; the L2 norm sum(u²+v²)
    // we use here is NOT — it can grow as energy redistributes among cells
    // since the v term's contribution depends on c². So the bound is just
    // "bounded, finite, not catastrophically blown up". A proper Hamiltonian-
    // conservation test arrives in Phase 2 alongside the energy probe.
    const float endEnergy = l2Energy(s);
    REQUIRE(std::isfinite(endEnergy));
    REQUIRE(endEnergy > 0.0f);
    REQUIRE(endEnergy < 1000.0f * initialEnergy); // generous bound; just no NaN/explosion
}

TEST_CASE("Test vector 2: damping makes the substrate strictly less energetic", "[substrate][1d][damping]")
{
    // The undamped substrate (γ = 0) and the damped substrate (γ > 0) start
    // identically and run for the same number of steps. The damped version
    // must end with strictly less energy than the undamped one — that's the
    // contract of γ even though l2(u²+v²) is not the true Hamiltonian and
    // can fluctuate as energy sloshes between u and v.
    //
    // Also assert the substrate stays bounded: no NaN, no runaway. The
    // detailed exponential-decay vector from sfs-spec/02 §11 will land in a
    // follow-up commit alongside the JSON test-vector loader.
    constexpr int kN = 256;
    constexpr float kC2 = 0.30f;

    auto runForSteps = [](float gamma, int steps)
    {
        Substrate1D s(kN, 48000.0f);
        s.setCoefficients(kC2, 0.0f, gamma);
        s.reset();
        s.deposit(0.0f, 1.0f);
        s.step();
        for (int i = 0; i < steps; ++i)
        {
            s.step();
        }
        return l2Energy(s);
    };

    constexpr int kSteps = 1000;
    const float energyUndamped = runForSteps(0.0f, kSteps);
    const float energyDamped = runForSteps(0.01f, kSteps);

    REQUIRE(std::isfinite(energyUndamped));
    REQUIRE(std::isfinite(energyDamped));
    REQUIRE(energyDamped < energyUndamped);
    REQUIRE(energyDamped > 0.0f); // damped, not silenced

    // Bounded: with γ > 0 and bounded initial impulse, energy must not
    // exceed a reasonable factor over initial. Loose bound; real check is
    // the comparative one above.
    REQUIRE(energyDamped < 100.0f);
}

TEST_CASE("reset() returns u, v, and uInject to zero", "[substrate][1d][reset]")
{
    Substrate1D s(128, 48000.0f);
    s.setCoefficients(0.30f, 0.05f, 0.005f);
    s.deposit(10.0f, 1.0f);
    s.step();
    REQUIRE(l2Energy(s) > 0.0f);

    s.reset();
    REQUIRE(l2Energy(s) == Catch::Approx(0.0f));
}

TEST_CASE("processNoDeposits is equivalent to N step() calls", "[substrate][1d]")
{
    Substrate1D a(128, 48000.0f);
    Substrate1D b(128, 48000.0f);
    a.setCoefficients(0.30f, 0.05f, 0.005f);
    b.setCoefficients(0.30f, 0.05f, 0.005f);

    a.deposit(0.0f, 1.0f);
    a.step();
    b.deposit(0.0f, 1.0f);
    b.step();

    a.processNoDeposits(100);
    for (int i = 0; i < 100; ++i)
    {
        b.step();
    }

    // Both should land in the same state.
    for (int x = 0; x < a.size(); ++x)
    {
        REQUIRE(a.displacement()[x] == Catch::Approx(b.displacement()[x]).margin(1e-6f));
    }
}
