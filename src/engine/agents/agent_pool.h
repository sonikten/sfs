// src/engine/agents/agent_pool.h
//
// Per-voice agent pool. Each agent reads the substrate at its position, gets
// frequency-bent by the substrate amplitude, generates a waveform sample,
// and deposits a scaled contribution back into the substrate. Stigmergy in
// action — agents only see each other through the substrate. (sfs-spec/03.)
//
// Phase 1 scope:
//   - Sine waveform only via sfs::dsp::dm_sin (other 4 waveforms in P2).
//   - Fixed agent positions; migration noise (Gaussian) is deferred until
//     dm_log / dm_cos / dm_sqrt land alongside the agent migration commit.
//   - Simple gate envelope (on/off, no ADSR shaping yet).
//   - Multiplicative bend per sfs-spec/03 §2: f_inst = f_i · (1 + m_i · u_at)
//
// The pool owns the Agent state, but the per-sample compute lives in
// processOneSample(Substrate1D&) which interleaves agent reads, deposits,
// and phase advances inside the canonical engine step (sfs-spec/01 §5.4).

#pragma once

#include "engine/rng/philox.h"

#include <cstdint>
#include <vector>

namespace sfs::engine::substrate
{
class Substrate1D;
}

namespace sfs::engine::agents
{

// Agent waveform types (sfs-spec/03 §3). Phase 1 was Sine only; Phase 2
// adds Saw, Square, FmPair, Noise.
enum class AgentShape : std::uint8_t
{
    Sine = 0,
    Saw = 1,
    Square = 2,
    FmPair = 3,
    Noise = 4,
};

struct Agent
{
    float position = 0.0f;            // substrate cell position [0, N), continuous
    float frequency = 440.0f;         // base frequency in Hz
    float phase = 0.0f;               // current phase in [0, 1) cycles (NOT radians)
    float amplitude = 1.0f;           // [0, 1]
    float envelope = 0.0f;            // gate envelope, [0, 1]; binary on/off in Phase 1
    float depositWeight = 0.05f;      // w_i — sfs-spec/09 default 0.05/sqrt(activeCount)
    float modSensitivity = 0.2f;      // m_i — substrate→frequency coupling, [0, 1]
    float migrationRate = 0.0f;       // r_i — constant cells/sample drift (drawn at
                                      // noteOn from uniform(-1, 1) · MIGRATION ·
                                      // 0.001 · N per spec §5)
    float migrationNoiseScale = 0.0f; // sigma for ε_i per sample (MIGRATION ·
                                      // 0.0005 · N per spec §5)

    // Phase 2: agent waveform shape + shape-specific parameters.
    AgentShape shape = AgentShape::Sine;
    float fmRatio = 1.0f;        // FmPair: modulator-to-carrier ratio
    float fmIndex = 0.5f;        // FmPair: modulation depth
    float noiseHoldValue = 0.0f; // Noise: current sample-and-hold value
    float noiseLastPhase = 0.0f; // Noise: phase at last hold refresh

    // Per-agent Philox stream for sample-indexed migration noise (stream
    // ID AgentMigrationNoise, sample-indexed). Seeded at noteOn from
    // (presetSeed, voiceIndex, agentIndex). Phase 2: P2 single voice +
    // fixed presetSeed; preset format lands in P4.
    sfs::engine::rng::Philox4x32Stream noiseStream{};
};

class AgentPool
{
public:
    static constexpr int kMaxAgents = 64;

    explicit AgentPool(int maxAgents = 32);

    // Phase 1 simplified note model: set every active agent's frequency to
    // the played note (multiplied by its harmonic ratio in later phases),
    // reset its phase, switch the gate envelope on. Velocity scales amplitude.
    void noteOn(int midiNote, float velocity);

    // Switch the gate envelope off; substrate decay handles the rest.
    void noteOff();

    // Per-sample:
    //   1. Read substrate at each agent's position (for bend).
    //   2. Compute bent instantaneous frequency.
    //   3. Generate waveform sample (dm_sin).
    //   4. Deposit (w_i · a_i · e_i · y_i) into the substrate at p_i.
    //   5. Advance phase using the bent frequency.
    //
    // Migration is intentionally absent; Phase 1's static positions still
    // exercise the read-bend-deposit substrate coupling.
    void processOneSample(sfs::engine::substrate::Substrate1D& substrate, float sampleRate) noexcept;

    [[nodiscard]] int activeCount() const noexcept { return activeCount_; }
    [[nodiscard]] const Agent& agent(int i) const noexcept { return agents_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] Agent& mutableAgent(int i) noexcept { return agents_[static_cast<std::size_t>(i)]; }

    // Convenience: spread N agents at evenly spaced substrate positions
    // (Phase 1 placeholder; Phase 2 reads from agent.shape_distribution).
    void layoutEvenly(int substrateCellCount) noexcept;

private:
    int activeCount_ = 0;
    std::uint64_t currentSampleIndex_ = 0; // sample counter since last noteOn
    std::vector<Agent> agents_;
};

} // namespace sfs::engine::agents
