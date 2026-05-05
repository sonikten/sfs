// tests/unit/rng_philox_test.cpp
//
// Reference-vector tests for the Philox-4×32-10 wrapper. The whole point of
// using a counter-based PRNG is bit-exact reproducibility; this test asserts
// it against the published reference vectors before any engine code starts
// depending on it.
//
// Reference vectors are from the DE Shaw Research Random123 distribution
// (kat_vectors.txt) — three (key, counter) → output triples that any
// conforming Philox-4×32-10 implementation reproduces exactly.

#include "engine/rng/philox.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using sfs::engine::rng::philox4x32_10;
using sfs::engine::rng::Philox4x32Stream;
using sfs::engine::rng::StreamId;

namespace
{

struct PhiloxKAT
{
    std::array<std::uint32_t, 4> ctr;
    std::array<std::uint32_t, 2> key;
    std::array<std::uint32_t, 4> expected;
};

// Three known-answer tests for Philox-4×32-10 — published by DE Shaw
// Research. Any conforming implementation matches these exactly.
constexpr PhiloxKAT kKAT[] = {
    {{0x00000000, 0x00000000, 0x00000000, 0x00000000},
     {0x00000000, 0x00000000},
     {0x6627e8d5, 0xe169c58d, 0xbc57ac4c, 0x9b00dbd8}},
    {{0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
     {0xffffffff, 0xffffffff},
     {0x408f276d, 0x41c83b0e, 0xa20bc7c6, 0x6d5451fd}},
    {{0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344},
     {0xa4093822, 0x299f31d0},
     {0xd16cfe09, 0x94fdcceb, 0x5001e420, 0x24126ea1}},
};

} // namespace

TEST_CASE("Philox-4x32-10 matches DE Shaw published reference vectors", "[rng][philox]")
{
    for (const auto& kat : kKAT)
    {
        std::uint32_t out[4]{};
        philox4x32_10(out, kat.ctr.data(), kat.key.data());
        CAPTURE(kat.key[0], kat.key[1], kat.ctr[0], kat.ctr[1], kat.ctr[2], kat.ctr[3]);
        REQUIRE(out[0] == kat.expected[0]);
        REQUIRE(out[1] == kat.expected[1]);
        REQUIRE(out[2] == kat.expected[2]);
        REQUIRE(out[3] == kat.expected[3]);
    }
}

TEST_CASE("Philox4x32Stream::seed packs (preset, voice, agent, stream) into the canonical layout",
          "[rng][philox][seed]")
{
    Philox4x32Stream s;
    constexpr std::uint64_t kSeed = 0xDEADBEEFCAFEBABEull;
    constexpr std::uint16_t kVoice = 0x1234;
    constexpr std::uint16_t kAgent = 0x5678;
    s.seed(kSeed, kVoice, kAgent, StreamId::AgentMigrationNoise);

    // sfs-spec/06 §1.2 packing.
    REQUIRE(s.keyWord(0) == 0xCAFEBABEu);
    REQUIRE(s.keyWord(1) == 0xDEADBEEFu);
    REQUIRE(s.counterWord(3) == ((static_cast<std::uint32_t>(kVoice)) | (static_cast<std::uint32_t>(kAgent) << 16)));
    REQUIRE(s.counterWord(2) == static_cast<std::uint32_t>(StreamId::AgentMigrationNoise));
    REQUIRE(s.counterWord(1) == 0u);
    REQUIRE(s.counterWord(0) == 0u);
}

TEST_CASE("Philox4x32Stream::next32 yields the expected four-word block on the zero key/ctr", "[rng][philox][next32]")
{
    Philox4x32Stream s;
    s.seed(0, 0, 0, static_cast<StreamId>(0));

    // After seed, first four next32() calls should match the all-zero KAT
    // (counter starts at 0; cache empty triggers a philox call on the first
    // draw, then we read out four words from the cached output).
    REQUIRE(s.next32() == 0x6627e8d5u);
    REQUIRE(s.next32() == 0xe169c58du);
    REQUIRE(s.next32() == 0xbc57ac4cu);
    REQUIRE(s.next32() == 0x9b00dbd8u);
}

TEST_CASE("Philox4x32Stream is deterministic across re-seeds with the same key", "[rng][philox][determinism]")
{
    Philox4x32Stream s1, s2;
    s1.seed(0xA1B2C3D4E5F60718ull, 7, 13, StreamId::AgentSampleHoldNoise);
    s2.seed(0xA1B2C3D4E5F60718ull, 7, 13, StreamId::AgentSampleHoldNoise);

    for (int i = 0; i < 16; ++i)
    {
        const auto a = s1.next32();
        const auto b = s2.next32();
        REQUIRE(a == b);
    }
}

TEST_CASE("Philox4x32Stream::setSampleIndex advances per-sample streams predictably", "[rng][philox][setSampleIndex]")
{
    Philox4x32Stream s;
    s.seed(42, 0, 0, StreamId::AgentMigrationNoise);

    // sample_index = 100 should produce a different draw than sample_index = 101.
    s.setSampleIndex(100);
    const auto a = s.next32();
    s.setSampleIndex(101);
    const auto b = s.next32();
    REQUIRE(a != b);

    // Re-setting back to 100 must reproduce the original draw bit-for-bit.
    s.setSampleIndex(100);
    REQUIRE(s.next32() == a);
}

TEST_CASE("Philox4x32Stream::nextFloat01 is in [0, 1)", "[rng][philox][float]")
{
    Philox4x32Stream s;
    s.seed(0xABCD, 0, 0, StreamId::IgnitionBurst);
    for (int i = 0; i < 1024; ++i)
    {
        const float x = s.nextFloat01();
        REQUIRE(x >= 0.0f);
        REQUIRE(x < 1.0f);
    }
}

TEST_CASE("Different stream IDs produce independent draws on the same (preset, voice, agent)",
          "[rng][philox][stream_independence]")
{
    Philox4x32Stream a, b;
    a.seed(0xCAFEFACE, 1, 2, StreamId::AgentPositionInit);
    b.seed(0xCAFEFACE, 1, 2, StreamId::AgentShapeSelect);

    // Should be highly unlikely (probability ~ 2^-128) for the first 4 draws
    // to all collide if the streams are truly independent.
    int collisions = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (a.next32() == b.next32())
        {
            ++collisions;
        }
    }
    REQUIRE(collisions < 4);
}

TEST_CASE("nextGaussian produces approximately N(0, 1) over many draws", "[rng][philox][gaussian]")
{
    Philox4x32Stream s;
    s.seed(0xBEEF, 0, 0, StreamId::AgentMigrationNoise);

    constexpr int kN = 10000;
    double sum = 0.0;
    double sumSq = 0.0;
    float maxAbs = 0.0f;
    int bigCount = 0; // > 4σ
    for (int i = 0; i < kN; ++i)
    {
        s.setSampleIndex(static_cast<std::uint64_t>(i));
        const float g = sfs::engine::rng::nextGaussian(s);
        REQUIRE(std::isfinite(g));
        sum += static_cast<double>(g);
        sumSq += static_cast<double>(g) * static_cast<double>(g);
        if (std::fabs(g) > maxAbs)
        {
            maxAbs = std::fabs(g);
        }
        if (std::fabs(g) > 4.0f)
        {
            ++bigCount;
        }
    }
    const double mean = sum / kN;
    const double stddev = std::sqrt(sumSq / kN - mean * mean);

    CAPTURE(mean, stddev, maxAbs, bigCount);
    // Mean should be ~0; with N=10k, expected stderr ~0.01.
    REQUIRE(std::fabs(mean) < 0.05);
    // Stddev should be ~1; with N=10k, expected stderr ~0.007.
    REQUIRE(std::fabs(stddev - 1.0) < 0.05);
    // Bounded: u1 clamp at 1e-7 caps r at sqrt(-2·log(1e-7)) ≈ 5.7.
    REQUIRE(maxAbs < 6.0f);
    // Tail count: P(|Z| > 4) ≈ 6.3e-5; for N=10k, expect ~0-1 hits.
    REQUIRE(bigCount < 5);
}

TEST_CASE("nextGaussian is deterministic across re-seeds", "[rng][philox][gaussian]")
{
    Philox4x32Stream a, b;
    a.seed(0xC0DECAFE, 0, 5, StreamId::AgentMigrationNoise);
    b.seed(0xC0DECAFE, 0, 5, StreamId::AgentMigrationNoise);

    for (int i = 0; i < 100; ++i)
    {
        a.setSampleIndex(static_cast<std::uint64_t>(i));
        b.setSampleIndex(static_cast<std::uint64_t>(i));
        REQUIRE(sfs::engine::rng::nextGaussian(a) == sfs::engine::rng::nextGaussian(b));
    }
}
