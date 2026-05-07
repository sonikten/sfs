// src/preset/preset_engine.cpp

#include "preset_engine.h"

#include "engine/agents/agent_pool.h"
#include "engine/lfo/lfo.h"
#include "engine/mod_matrix/mod_matrix.h"
#include "engine/voice.h"
#include "engine/voice_manager.h"

#include <algorithm>
#include <string>

namespace sfs::preset
{

namespace
{

using sfs::engine::Topology;
using sfs::engine::agents::AgentShape;
using sfs::engine::lfo::LfoShape;
using sfs::engine::mod_matrix::Destination;
using sfs::engine::mod_matrix::ModMatrix;
using sfs::engine::mod_matrix::Source;

// ---------------------------------------------------------------------------
// String → enum mappers. All return a documented default when the input
// doesn't match any known value (Doc 06 §3.4 forward-compat policy).
// ---------------------------------------------------------------------------

[[nodiscard]] Topology parseTopology(const std::string& s) noexcept
{
    // Phase 3 ships {Ring1D, Torus2D}. The preset format covers four
    // future torus sizes (32/64/128/256) for round-trip; all map to
    // Torus2D in the engine until larger sizes ship.
    if (s == "ring")
    {
        return Topology::Ring1D;
    }
    if (s == "torus_32" || s == "torus_64" || s == "torus_128" || s == "torus_256")
    {
        return Topology::Torus2D;
    }
    return Topology::Ring1D; // unknown → safe default
}

[[nodiscard]] LfoShape parseLfoShape(const std::string& s) noexcept
{
    if (s == "sine")
    {
        return LfoShape::Sine;
    }
    if (s == "triangle" || s == "tri")
    {
        return LfoShape::Triangle;
    }
    if (s == "saw")
    {
        return LfoShape::Saw;
    }
    if (s == "square")
    {
        return LfoShape::Square;
    }
    if (s == "sample_hold" || s == "random" || s == "s&h")
    {
        return LfoShape::SampleHold;
    }
    return LfoShape::Sine;
}

[[nodiscard]] Source parseSource(const std::string& s) noexcept
{
    if (s == "LFO1")
    {
        return Source::Lfo1;
    }
    if (s == "LFO2")
    {
        return Source::Lfo2;
    }
    if (s == "LFO3")
    {
        return Source::Lfo3;
    }
    if (s == "LFO4")
    {
        return Source::Lfo4;
    }
    if (s == "ENV1")
    {
        return Source::Env1;
    }
    if (s == "KEY_VELOCITY" || s == "KeyVelocity")
    {
        return Source::KeyVelocity;
    }
    if (s == "MIDI_CC1" || s == "MidiCc1")
    {
        return Source::MidiCc1;
    }
    if (s == "RANDOM" || s == "Random")
    {
        return Source::Random;
    }
    if (s == "MPE_PRESSURE")
    {
        return Source::MpePressure;
    }
    if (s == "MPE_TIMBRE" || s == "MPE_SLIDE")
    {
        return Source::MpeTimbre;
    }
    return Source::Lfo1; // unknown → safe default
}

[[nodiscard]] Destination parseDestination(const std::string& s) noexcept
{
    if (s == "TENSION")
    {
        return Destination::Tension;
    }
    if (s == "DAMPING")
    {
        return Destination::Damping;
    }
    if (s == "DENSITY")
    {
        return Destination::Density;
    }
    if (s == "MIGRATION")
    {
        return Destination::Migration;
    }
    if (s == "COHERENCE")
    {
        return Destination::Coherence;
    }
    if (s == "EXCITATION")
    {
        return Destination::Excitation;
    }
    return Destination::Tension; // unknown → safe default
}

// Pick the dominant agent waveform from the shape distribution. Phase 4
// stub: Voice's setUniformShape is per-voice not per-agent, so we collapse
// the probability distribution into a single dominant shape. The full
// per-agent shape draw from the distribution lands in a later engine
// commit; the JSON field round-trips through the preset layer either way.
[[nodiscard]] AgentShape dominantShape(const PresetShapeDistribution& d) noexcept
{
    const float ent[5] = {d.sine, d.saw, d.square, d.fmpair, d.noise};
    int bestIdx = 0;
    float bestVal = ent[0];
    for (int i = 1; i < 5; ++i)
    {
        if (ent[i] > bestVal)
        {
            bestVal = ent[i];
            bestIdx = i;
        }
    }
    static constexpr AgentShape kShapes[5] = {
        AgentShape::Sine,
        AgentShape::Saw,
        AgentShape::Square,
        AgentShape::FmPair,
        AgentShape::Noise,
    };
    return kShapes[bestIdx];
}

} // namespace

void applyToEngine(const Preset& p, sfs::engine::VoiceManager& vm) noexcept
{
    // Macros (clamped on the engine side via macros.clampInPlace() at
    // block start, but we clamp here too so a malformed preset can't
    // surprise the macro reader between the write and the next block).
    auto& m = vm.macros();
    m.tension = std::clamp(p.macros.tension, 0.0f, 1.0f);
    m.damping = std::clamp(p.macros.damping, 0.0f, 1.0f);
    m.density = std::clamp(p.macros.density, 0.0f, 1.0f);
    m.migration = std::clamp(p.macros.migration, 0.0f, 1.0f);
    m.coherence = std::clamp(p.macros.coherence, 0.0f, 1.0f);
    m.excitation = std::clamp(p.macros.excitation, 0.0f, 1.0f);

    // Structural — only topology is wired through the engine at Phase 4
    // start. aspect, substrate_size, deposit/read kernel are reserved.
    vm.setTopology(parseTopology(p.structural.topology));

    // Uniform agent shape (Phase 4 stub — see dominantShape comment).
    vm.setUniformShape(dominantShape(p.agents.shape_distribution));

    // ENV1 — preset times are seconds, engine ADSR setters take ms.
    constexpr float kSecToMs = 1000.0f;
    vm.setAdsr(p.env1.attack * kSecToMs,
               p.env1.decay * kSecToMs,
               std::clamp(p.env1.sustain, 0.0f, 1.0f),
               p.env1.release * kSecToMs);

    // LFOs (4).
    for (int i = 0; i < sfs::engine::Voice::kLfoCount; ++i)
    {
        const auto& l = p.lfos[static_cast<std::size_t>(i)];
        vm.setLfoConfig(i, l.rate_hz, parseLfoShape(l.shape));
    }

    // Mod matrix (16 slots). Inactive slots map to depth=0 so the engine
    // ignores them (slot.depth == 0 is the inactive marker per
    // ModMatrix::evaluate).
    for (int i = 0; i < ModMatrix::kNumSlots; ++i)
    {
        const auto& s = p.mod_matrix[static_cast<std::size_t>(i)];
        const float depth = s.active ? s.depth : 0.0f;
        vm.setModMatrixSlot(i, parseSource(s.source), parseDestination(s.destination), depth);
    }
}

} // namespace sfs::preset
