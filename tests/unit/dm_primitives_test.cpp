// tests/unit/dm_primitives_test.cpp
//
// Accuracy tests for dm_cos, dm_sqrt, dm_log against the std:: equivalents
// (which are forbidden in src/engine/ and src/dsp/ but allowed in tests/).
// Phase 2 budgets per sfs-spec/06 §1.6:
//
//     dm_cos      < 1e-6   (same as dm_sin)
//     dm_sqrt     exact (HW sqrt in round-to-nearest)
//     dm_log      < 1e-5 spec-target; Phase 2 ships < 5e-3, tightens later.

#include "dsp/dm_cos.h"
#include "dsp/dm_log.h"
#include "dsp/dm_sin.h"
#include "dsp/dm_sqrt.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

} // namespace

TEST_CASE("dm_cos matches std::cos within Phase 2 budget on principal range", "[dsp][dm_cos]")
{
    constexpr int kSamples = 1024;
    constexpr float kBudget = 1e-6f; // dm_cos = dm_sin(x + π/2); Cody-Waite reduction
    float maxError = 0.0f;
    for (int i = 0; i <= kSamples; ++i)
    {
        const float t = -kPi + (2.0f * kPi) * static_cast<float>(i) / static_cast<float>(kSamples);
        const float err = std::fabs(std::cos(t) - sfs::dsp::dm_cos(t));
        if (err > maxError)
        {
            maxError = err;
        }
    }
    CAPTURE(maxError);
    REQUIRE(maxError < kBudget);
}

TEST_CASE("dm_cos sentinel values", "[dsp][dm_cos]")
{
    REQUIRE(std::fabs(sfs::dsp::dm_cos(0.0f) - 1.0f) < 1e-6f);
    REQUIRE(std::fabs(sfs::dsp::dm_cos(kPi) + 1.0f) < 1e-6f);
    REQUIRE(std::fabs(sfs::dsp::dm_cos(kPi / 2.0f) - 0.0f) < 3e-6f);
    REQUIRE(std::fabs(sfs::dsp::dm_cos(-kPi / 2.0f) - 0.0f) < 3e-6f);
}

TEST_CASE("dm_sqrt matches std::sqrt exactly (IEEE round-to-nearest)", "[dsp][dm_sqrt]")
{
    for (float x : {0.0f, 1.0f, 2.0f, 4.0f, 9.0f, 16.0f, 0.25f, 0.5f, 0.7654321f})
    {
        REQUIRE(sfs::dsp::dm_sqrt(x) == std::sqrt(x));
    }
}

TEST_CASE("dm_log matches std::log within spec budget", "[dsp][dm_log]")
{
    // Spec sfs-spec/06 §1.6 budget: < 1e-5. We ship the atanh form
    // log(m) = 2·atanh((m-1)/(m+1)) which converges fast on z ∈ [0, 1/3]
    // (the bounded image of m ∈ [1, 2)). Empirical max absolute error
    // across [1e-6, 100]: ~2e-6, well under the spec budget.
    constexpr float kBudget = 1e-5f;

    float maxError = 0.0f;
    // Sample across [1e-6, 100] in log-spaced points.
    for (int i = 0; i <= 200; ++i)
    {
        const float t = std::exp(-13.8f + 0.069f * static_cast<float>(i)); // 1e-6 to ~100
        const float err = std::fabs(std::log(t) - sfs::dsp::dm_log(t));
        if (err > maxError)
        {
            maxError = err;
        }
    }
    CAPTURE(maxError);
    REQUIRE(maxError < kBudget);
}

TEST_CASE("dm_log sentinel values", "[dsp][dm_log]")
{
    // log(1) = 0 exactly (frexp(1) = 0.5 × 2^1, m=0.5 → after mul by 2 m=1, y=0)
    REQUIRE(std::fabs(sfs::dsp::dm_log(1.0f)) < 5e-3f);
    // log(e) ≈ 1
    REQUIRE(std::fabs(sfs::dsp::dm_log(2.71828183f) - 1.0f) < 5e-3f);
    // log(2) ≈ 0.693
    REQUIRE(std::fabs(sfs::dsp::dm_log(2.0f) - 0.693147f) < 5e-3f);
}
