// src/engine/rng/philox.h
//
// Philox-4×32-10 wrapper used by every stochastic decision in the engine.
// Counter-based, key-splittable, bit-identical across platforms — see
// sfs-spec/06 §1 for the contract and §1.2 for the (key, counter) packing
// that maps to (preset_seed, voice, agent, stream_id, sample_index).
//
// std::default_random_engine, std::mt19937, rand(), random(), drand48() and
// every other platform RNG are forbidden anywhere in src/ (enforced by
// tools/check_determinism.sh). This wrapper is the only legal RNG in the
// audio path.
//
// nextGaussian() is intentionally not in this header yet — it requires
// dm_log / dm_cos / dm_sqrt and lands alongside agent-migration code in
// later Phase 1 commits (sfs-spec/06 §1.4).

#pragma once

#include <cstdint>

namespace sfs::engine::rng
{

// Stream IDs from sfs-spec/06 §1.2 — the canonical assignment. Adding a new
// stream means amending §1.2 first. IDs 12–31 are reserved for v1.x growth.
enum class StreamId : std::uint16_t
{
    IgnitionBurst = 0,        // per-voice ignition burst
    AgentPositionInit = 1,    // per-voice agent position init
    AgentShapeSelect = 2,     // per-voice agent shape selection
    AgentHarmonicSelect = 3,  // per-voice agent harmonic ratio selection
    AgentMigrationNoise = 4,  // per-agent migration noise (sample-indexed)
    AgentSampleHoldNoise = 5, // per-agent sample-and-hold (one draw per phase wrap)
    LfoRandomWalk = 6,        // per-voice random-walk LFO state (sample-indexed)
    EnvelopeRandom = 7,       // per-voice envelope time-randomisation
    ModRandomSource = 8,      // per-voice mod matrix RANDOM source (per note-on)
    MpeRandomSource = 9,      // per-voice MPE_RANDOM expression source (per note-on)
    HarvesterOrbit = 10,      // per-voice harvester orbit when shape = random_walk
    PresetDiceButton = 11,    // plug-in dice button (curated distribution draw)
};

// One Philox stream. Cheap to copy. NOT thread-safe — each thread/voice
// agent owns its own instance.
class Philox4x32Stream
{
public:
    // Seed for an init stream (sample_index = 0). After this, four next32()
    // calls yield 128 bits of fixed entropy keyed by (preset, voice, agent,
    // stream_id) — exactly four words, then the cursor is exhausted and
    // counter[0] auto-increments to give another four.
    void seed(std::uint64_t presetSeed, std::uint16_t voice, std::uint16_t agent, StreamId stream) noexcept;

    // For per-sample streams (e.g. AgentMigrationNoise). Setting a new sample
    // index invalidates the cached Philox output; the next next32() call
    // re-philoxes and the four output words are the canonical draw for that
    // (voice, agent, stream, sample_index) tuple.
    void setSampleIndex(std::uint64_t sampleIndex) noexcept;

    // Draw the next 32 bits of uniform output. Re-philoxes when the four
    // cached words have been consumed.
    [[nodiscard]] std::uint32_t next32() noexcept;

    // 24 bits of mantissa precision in [0, 1). Drops the low 8 bits of next32()
    // for IEEE-clean conversion to float — see sfs-spec/06 §1.3.
    [[nodiscard]] float nextFloat01() noexcept;

    // Direct accessors for testing / interop with the reference C library.
    [[nodiscard]] std::uint32_t keyWord(int i) const noexcept { return key_[i]; }
    [[nodiscard]] std::uint32_t counterWord(int i) const noexcept { return counter_[i]; }

private:
    std::uint32_t key_[2]{};
    std::uint32_t counter_[4]{};
    std::uint32_t lastOut_[4]{};
    std::uint8_t lastOutCursor_ = 4; // 4 = "cache empty, philox on next draw"
};

// Free-function variant for callers that just want a single Philox draw without
// constructing a stream object. Useful for one-shot init computations.
//
// `out[0..3]` receives 128 bits of output. `key` is two words; `ctr` is four.
void philox4x32_10(std::uint32_t out[4], const std::uint32_t ctr[4], const std::uint32_t key[2]) noexcept;

} // namespace sfs::engine::rng
