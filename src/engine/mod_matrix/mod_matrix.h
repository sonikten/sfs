// src/engine/mod_matrix/mod_matrix.h
//
// Modulation matrix per sfs-spec/05 §5. 16 slots; each slot routes one
// source through a signed depth to one destination. Per block the matrix
// is evaluated once: per-destination sum = Σ depth_i · source_value(i).
//
// Phase 2 source set (8 of the spec's 12):
//   Lfo1..Lfo4, Env1 (amp), KeyVelocity, MidiCc1, Random (per-note draw).
// Phase 2 destination set (6 of the spec's 24): the six primary macros.
// The remaining sources/destinations from the spec land alongside the
// preset format in Phase 4 — the slot data layout already accommodates
// them (the source/destination IDs are u8 enums, slack room for growth).
//
// Slots are preset-only state, NOT host-automatable parameters. The
// mod-matrix class provides setSlot()/clearSlot() for the preset loader
// and tests; PluginProcessor never wires it into AudioParameterFloat.

#pragma once

#include <array>
#include <cstdint>

namespace sfs::engine::mod_matrix
{

enum class Source : std::uint8_t
{
    Lfo1 = 0,
    Lfo2,
    Lfo3,
    Lfo4,
    Env1,        // amplitude envelope value, [0, 1]
    KeyVelocity, // [0, 1] at noteOn
    MidiCc1,     // mod wheel, [0, 1]
    Random,      // per-note uniform draw, [-1, 1)
    Count
};

enum class Destination : std::uint8_t
{
    Tension = 0,
    Damping,
    Density,
    Migration,
    Coherence,
    Excitation,
    Count
};

struct Slot
{
    Source source = Source::Lfo1;
    Destination dest = Destination::Tension;
    float depth = 0.0f; // signed; 0 = slot inactive
};

class ModMatrix
{
public:
    static constexpr int kNumSlots = 16;
    static constexpr int kNumSources = static_cast<int>(Source::Count);
    static constexpr int kNumDestinations = static_cast<int>(Destination::Count);

    void setSlot(int index, Source src, Destination dst, float depth) noexcept
    {
        if (index < 0 || index >= kNumSlots)
        {
            return;
        }
        auto& s = slots_[static_cast<std::size_t>(index)];
        s.source = src;
        s.dest = dst;
        s.depth = depth;
    }

    void clearSlot(int index) noexcept
    {
        if (index < 0 || index >= kNumSlots)
        {
            return;
        }
        slots_[static_cast<std::size_t>(index)] = Slot{};
    }

    void clearAllSlots() noexcept
    {
        for (auto& s : slots_)
        {
            s = Slot{};
        }
    }

    [[nodiscard]] const Slot& slot(int index) const noexcept { return slots_[static_cast<std::size_t>(index)]; }

    // Evaluate: per-destination summed delta from all active slots. Caller
    // applies these deltas to the macro values (clamped to [0, 1]) before
    // running the fan-out.
    void evaluate(const std::array<float, kNumSources>& sources,
                  std::array<float, kNumDestinations>& destinations) const noexcept
    {
        for (auto& d : destinations)
        {
            d = 0.0f;
        }
        for (const auto& s : slots_)
        {
            if (s.depth == 0.0f)
            {
                continue; // inactive
            }
            const auto srcIdx = static_cast<std::size_t>(s.source);
            const auto dstIdx = static_cast<std::size_t>(s.dest);
            destinations[dstIdx] += s.depth * sources[srcIdx];
        }
    }

private:
    std::array<Slot, kNumSlots> slots_{};
};

} // namespace sfs::engine::mod_matrix
