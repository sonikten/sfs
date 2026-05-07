// tests/unit/substrate_2d_test.cpp
//
// Unit tests for the 2D substrate. Mirrors substrate_1d_test.cpp's
// coverage:
//   * Default-constructed energy is zero
//   * CFL clamp on (c² + κ) ≤ 0.225
//   * Bilinear deposit splits across 4 cells
//   * Bilinear read interpolates
//   * Position wraps modulo (Nx, Ny)
//   * Impulse propagates across the torus (lossless ring extension)
//   * γ damping makes the substrate strictly less energetic
//   * AC content decays under standard parameters
//   * Reset zeroes state
//   * processNoDeposits == N step() calls

#include "engine/substrate/substrate_2d.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using sfs::engine::substrate::Substrate2D;

namespace
{

float l2Energy(const Substrate2D& s) noexcept
{
    const auto* u = s.displacement();
    const auto* v = s.velocity();
    float e = 0.0f;
    const int n = s.cellCount();
    for (int i = 0; i < n; ++i)
    {
        e += u[i] * u[i] + v[i] * v[i];
    }
    return e;
}

} // namespace

TEST_CASE("Substrate2D constructs with default coefficients", "[substrate][2d]")
{
    Substrate2D s(64, 64, 48000.0f);
    REQUIRE(s.cellsX() == 64);
    REQUIRE(s.cellsY() == 64);
    REQUIRE(s.cellCount() == 4096);
    REQUIRE(s.sampleRate() == Catch::Approx(48000.0f));
    REQUIRE(l2Energy(s) == Catch::Approx(0.0f));
}

TEST_CASE("Substrate2D setCoefficients applies the joint 2D CFL clamp", "[substrate][2d][cfl]")
{
    Substrate2D s(64, 64, 48000.0f);

    // Below the bound.
    s.setCoefficients(0.10f, 0.05f, 0.005f);
    REQUIRE(s.c2() == Catch::Approx(0.10f));
    REQUIRE(s.kappa() == Catch::Approx(0.05f));

    // Above the bound — proportional scale.
    s.setCoefficients(0.30f, 0.20f, 0.005f);
    const float sum = s.c2() + s.kappa();
    REQUIRE(sum == Catch::Approx(Substrate2D::kCflBound2D));
    REQUIRE(s.c2() / s.kappa() == Catch::Approx(0.30f / 0.20f));

    // Negatives clamp to 0; gamma clamps to [0, 0.99].
    s.setCoefficients(-1.0f, -1.0f, -1.0f);
    REQUIRE(s.c2() == Catch::Approx(0.0f));
    REQUIRE(s.kappa() == Catch::Approx(0.0f));
    REQUIRE(s.gamma() == Catch::Approx(0.0f));

    s.setCoefficients(0.10f, 0.05f, 5.0f);
    REQUIRE(s.gamma() < 1.0f);
}

TEST_CASE("Substrate2D bilinear deposit splits across 4 cells", "[substrate][2d][deposit]")
{
    // Use a small grid (32×32 = 1024 cells) so the 2D DC removal at
    // 1/1024 ≈ 0.001 per cell is small relative to the deposit weights.
    Substrate2D s(32, 32, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f); // no propagation
    s.reset();

    s.deposit(10.25f, 5.5f, 1.0f);
    s.step();

    constexpr float kDc = 1.0f / (32.0f * 32.0f);
    const auto* u = s.displacement();
    // Expected weights (before DC removal):
    //   w(10, 5) = (1-0.25)(1-0.5) = 0.75 * 0.5 = 0.375
    //   w(11, 5) = 0.25 * 0.5      = 0.125
    //   w(10, 6) = 0.75 * 0.5      = 0.375
    //   w(11, 6) = 0.25 * 0.5      = 0.125
    const auto idx = [&](int x, int y) { return static_cast<std::size_t>(y * 32 + x); };
    REQUIRE(u[idx(10, 5)] == Catch::Approx(0.375f - kDc).margin(1e-4f));
    REQUIRE(u[idx(11, 5)] == Catch::Approx(0.125f - kDc).margin(1e-4f));
    REQUIRE(u[idx(10, 6)] == Catch::Approx(0.375f - kDc).margin(1e-4f));
    REQUIRE(u[idx(11, 6)] == Catch::Approx(0.125f - kDc).margin(1e-4f));
    // A non-deposit cell should sit at -kDc.
    REQUIRE(u[idx(0, 0)] == Catch::Approx(-kDc).margin(1e-4f));
}

TEST_CASE("Substrate2D bilinear read interpolates between 4 cells", "[substrate][2d][read]")
{
    Substrate2D s(32, 32, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f);
    s.reset();
    s.deposit(10.0f, 5.0f, 1.0f); // exactly at cell (10,5)
    s.step();

    constexpr float kDc = 1.0f / (32.0f * 32.0f);
    REQUIRE(s.read(10.0f, 5.0f) == Catch::Approx(1.0f - kDc).margin(1e-4f));
    // Halfway to (11, 5) → average of u[10,5] and u[11,5] = (1−Dc + −Dc)/2.
    REQUIRE(s.read(10.5f, 5.0f) == Catch::Approx(0.5f - kDc).margin(1e-4f));
    REQUIRE(s.read(11.0f, 5.0f) == Catch::Approx(-kDc).margin(1e-4f));
    // Diagonal halfway.
    REQUIRE(s.read(10.5f, 5.5f) == Catch::Approx(0.25f - kDc).margin(1e-4f));
}

TEST_CASE("Substrate2D position wraps in both axes", "[substrate][2d][wrap]")
{
    Substrate2D s(32, 32, 48000.0f);
    s.setCoefficients(0.0f, 0.0f, 0.0f);
    s.reset();

    constexpr float kDc = 1.0f / (32.0f * 32.0f);
    s.deposit(32.0f, 32.0f, 1.0f); // wraps to (0, 0)
    s.step();
    REQUIRE(s.read(0.0f, 0.0f) == Catch::Approx(1.0f - kDc).margin(1e-4f));

    s.reset();
    s.deposit(-1.0f, -1.0f, 1.0f); // wraps to (31, 31)
    s.step();
    REQUIRE(s.read(31.0f, 31.0f) == Catch::Approx(1.0f - kDc).margin(1e-4f));
}

TEST_CASE("Substrate2D impulse propagates across the torus", "[substrate][2d][propagation]")
{
    constexpr int kN = 64;
    Substrate2D s(kN, kN, 48000.0f);
    s.setCoefficients(0.20f, 0.0f, 0.0f);
    s.reset();
    s.deposit(0.0f, 0.0f, 1.0f);
    s.step();
    const float initialEnergy = l2Energy(s);
    REQUIRE(initialEnergy > 0.0f);

    // Step long enough for the wavefront to reach the opposite corner.
    // Phase velocity ≈ √c² ≈ 0.45 cells/sample; corner distance ≈ N/√2.
    const int steps = static_cast<int>(static_cast<float>(kN) / 0.45f) + 32;
    for (int i = 0; i < steps; ++i)
    {
        s.step();
    }

    // The opposite quadrant should have non-zero energy.
    REQUIRE(std::fabs(s.read(static_cast<float>(kN) / 2.0f, static_cast<float>(kN) / 2.0f)) > 1e-5f);

    const float endEnergy = l2Energy(s);
    REQUIRE(std::isfinite(endEnergy));
    REQUIRE(endEnergy > 0.0f);
    REQUIRE(endEnergy < 1000.0f * initialEnergy);
}

TEST_CASE("Substrate2D damping makes the substrate strictly less energetic", "[substrate][2d][damping]")
{
    constexpr int kN = 32;
    constexpr float kC2 = 0.10f;

    auto runForSteps = [](float gamma, int steps)
    {
        Substrate2D s(kN, kN, 48000.0f);
        s.setCoefficients(kC2, 0.0f, gamma);
        s.reset();
        s.deposit(0.0f, 0.0f, 1.0f);
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
    REQUIRE(energyDamped > 0.0f);
    REQUIRE(energyDamped < 100.0f);
}

TEST_CASE("Substrate2D reset zeros state", "[substrate][2d][reset]")
{
    Substrate2D s(32, 32, 48000.0f);
    s.setCoefficients(0.10f, 0.02f, 0.005f);
    s.deposit(5.0f, 5.0f, 1.0f);
    s.step();
    REQUIRE(l2Energy(s) > 0.0f);

    s.reset();
    REQUIRE(l2Energy(s) == Catch::Approx(0.0f));
}

TEST_CASE("Substrate2D processNoDeposits == N step() calls", "[substrate][2d][batched]")
{
    Substrate2D a(32, 32, 48000.0f);
    Substrate2D b(32, 32, 48000.0f);
    a.setCoefficients(0.10f, 0.02f, 0.005f);
    b.setCoefficients(0.10f, 0.02f, 0.005f);
    a.deposit(5.0f, 5.0f, 1.0f);
    b.deposit(5.0f, 5.0f, 1.0f);

    constexpr int kSteps = 100;
    for (int i = 0; i < kSteps; ++i)
    {
        a.step();
    }
    b.processNoDeposits(kSteps);

    const auto* uA = a.displacement();
    const auto* uB = b.displacement();
    for (int i = 0; i < a.cellCount(); ++i)
    {
        REQUIRE(uA[i] == Catch::Approx(uB[i]).margin(1e-6f));
    }
}
