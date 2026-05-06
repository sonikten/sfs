// tests/unit/mod_matrix_test.cpp
//
// Unit tests for the mod matrix evaluator. Verifies slot routing,
// per-destination summation, depth signedness, and that empty / cleared
// slots contribute nothing.

#include "engine/mod_matrix/mod_matrix.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using sfs::engine::mod_matrix::Destination;
using sfs::engine::mod_matrix::ModMatrix;
using sfs::engine::mod_matrix::Source;

namespace
{

std::array<float, ModMatrix::kNumSources> makeSources()
{
    std::array<float, ModMatrix::kNumSources> s{};
    s[static_cast<std::size_t>(Source::Lfo1)] = 0.5f;
    s[static_cast<std::size_t>(Source::Lfo2)] = -0.25f;
    s[static_cast<std::size_t>(Source::Lfo3)] = 1.0f;
    s[static_cast<std::size_t>(Source::Lfo4)] = 0.0f;
    s[static_cast<std::size_t>(Source::Env1)] = 0.7f;
    s[static_cast<std::size_t>(Source::KeyVelocity)] = 0.8f;
    s[static_cast<std::size_t>(Source::MidiCc1)] = 0.4f;
    s[static_cast<std::size_t>(Source::Random)] = -0.3f;
    return s;
}

} // namespace

TEST_CASE("ModMatrix: empty matrix produces zero deltas", "[mod_matrix]")
{
    ModMatrix m;
    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);
    for (float v : out)
    {
        REQUIRE(v == 0.0f);
    }
}

TEST_CASE("ModMatrix: single slot routes source × depth to destination", "[mod_matrix]")
{
    ModMatrix m;
    m.setSlot(0, Source::Lfo1, Destination::Tension, 0.4f);

    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);

    REQUIRE(std::fabs(out[static_cast<std::size_t>(Destination::Tension)] - 0.4f * 0.5f) < 1e-6f);
    // Other destinations untouched.
    REQUIRE(out[static_cast<std::size_t>(Destination::Damping)] == 0.0f);
}

TEST_CASE("ModMatrix: multiple slots to same destination sum", "[mod_matrix]")
{
    ModMatrix m;
    // Two slots both target Migration: Lfo1 (0.5) × 0.6 + KeyVelocity (0.8) × 0.25
    m.setSlot(0, Source::Lfo1, Destination::Migration, 0.6f);
    m.setSlot(1, Source::KeyVelocity, Destination::Migration, 0.25f);

    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);

    const float expected = 0.6f * 0.5f + 0.25f * 0.8f;
    REQUIRE(std::fabs(out[static_cast<std::size_t>(Destination::Migration)] - expected) < 1e-6f);
}

TEST_CASE("ModMatrix: negative depth subtracts", "[mod_matrix]")
{
    ModMatrix m;
    m.setSlot(0, Source::Lfo3, Destination::Coherence, -0.5f);

    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);

    REQUIRE(std::fabs(out[static_cast<std::size_t>(Destination::Coherence)] + 0.5f) < 1e-6f);
}

TEST_CASE("ModMatrix: zero-depth slots contribute nothing", "[mod_matrix]")
{
    ModMatrix m;
    m.setSlot(0, Source::Lfo1, Destination::Tension, 0.0f);
    m.setSlot(1, Source::Env1, Destination::Tension, 0.5f);

    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);

    // Only slot 1 contributes.
    REQUIRE(std::fabs(out[static_cast<std::size_t>(Destination::Tension)] - 0.5f * 0.7f) < 1e-6f);
}

TEST_CASE("ModMatrix: clearAllSlots zeroes everything", "[mod_matrix]")
{
    ModMatrix m;
    for (int i = 0; i < ModMatrix::kNumSlots; ++i)
    {
        m.setSlot(i, Source::Lfo1, Destination::Density, 0.1f);
    }
    m.clearAllSlots();

    auto sources = makeSources();
    std::array<float, ModMatrix::kNumDestinations> out{};
    m.evaluate(sources, out);
    for (float v : out)
    {
        REQUIRE(v == 0.0f);
    }
}
