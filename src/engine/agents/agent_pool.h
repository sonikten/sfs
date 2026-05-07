// src/engine/agents/agent_pool.h
//
// Per-voice agent pool. Each agent reads the substrate at its position, gets
// frequency-bent by the substrate amplitude, generates a waveform sample,
// and deposits a scaled contribution back into the substrate. Stigmergy in
// action — agents only see each other through the substrate. (sfs-spec/03.)
//
// Phase 2 scope:
//   - 5 waveforms (Sine, Saw/PolyBLEP, Square/PolyBLEP, FmPair, Noise/S&H).
//   - Per-agent migration: constant drift r_i + Gaussian ε_i (sample-indexed
//     Philox). Magnitudes scale block-rate from MIGRATION macro fan-out.
//   - Per-agent detune from baseFrequency × (1-COHERENCE)² fan-out.
//   - Per-voice ADSR amplitude shaping via setVoiceGain() (per-agent
//     `envelope` field still binary; the voice envelope multiplies on top).
//   - Multiplicative bend per sfs-spec/03 §2: f_inst = f_i · (1 + m_i · u_at)
//
// The pool owns the Agent state, but the per-sample compute lives in
// processOneSample(Substrate1D&) which interleaves agent reads, deposits,
// and phase advances inside the canonical engine step (sfs-spec/01 §5.4).
// Block-rate macro→agent rewriting lives in Voice::applyMacroFanOut.

#pragma once

#include "engine/rng/philox.h"

#include <cstdint>
#include <vector>

namespace sfs::engine::substrate
{
class Substrate1D;
class Substrate2D;
} // namespace sfs::engine::substrate

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
    float position = 0.0f;            // 1D / 2D-X substrate cell position [0, N), continuous
    float positionY = 0.0f;           // 2D-only Y position [0, Ny). Ignored in 1D mode.
    float frequency = 440.0f;         // base frequency in Hz
    float phase = 0.0f;               // current phase in [0, 1) cycles (NOT radians)
    float amplitude = 1.0f;           // [0, 1]
    float envelope = 0.0f;            // gate envelope, [0, 1]; binary on/off in Phase 1
    float depositWeight = 0.05f;      // w_i — sfs-spec/09 default 0.05/sqrt(activeCount)
    float modSensitivity = 0.2f;      // m_i — substrate→frequency coupling, [0, 1]
    float migrationRate = 0.0f;       // r_i — effective cells/sample drift; scaled
                                      // each block from migrationDirection ×
                                      // MIGRATION fan-out (sfs-spec/05 §3.4).
    float migrationNoiseScale = 0.0f; // sigma for ε_i per sample, scaled each
                                      // block from MIGRATION fan-out.

    // Block-rate macro inputs. The Voice rewrites the *_eff fields above
    // each block from the macro fan-out × these per-agent draws (which
    // are drawn once at noteOn from per-agent Philox streams).
    float migrationDirection = 0.0f; // uniform(-1, 1) at noteOn — sign+magnitude
                                     // shaper for migrationRate.
    float baseDepositWeight = 0.05f; // 0.05 / sqrt(maxActiveCount) at noteOn;
                                     // scaled by DENSITY fan-out per block.
    float baseFrequency = 440.0f;    // f0 at noteOn; live frequency derived
                                     // from this × COHERENCE detune.
    float detuneCents = 0.0f;        // uniform(-50, 50) at noteOn; scaled by
                                     // (1 - COHERENCE)² each block.

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
    // Per-sample migration update: position += r_i + ε_i · noiseScale, where
    // r_i and noiseScale are scaled at block boundaries by Voice from the
    // MIGRATION fan-out (sfs-spec/03 §5).
    void processOneSample(sfs::engine::substrate::Substrate1D& substrate, float sampleRate) noexcept;

    // 2D overload — reads / deposits using (position, positionY) into a
    // Substrate2D. Other behaviour identical to the 1D path: bend,
    // generate, deposit, advance phase, update position. Migration drifts
    // the X axis only (positionY is fixed per noteOn for now); 2D-aware
    // migration is a Phase 4+ task.
    void processOneSample(sfs::engine::substrate::Substrate2D& substrate, float sampleRate) noexcept;

    [[nodiscard]] int activeCount() const noexcept { return activeCount_; }

    // Phase 2 voice-level amplitude gain. Voice writes the per-sample
    // envelope value here just before calling processOneSample(); the
    // deposit contribution = w · a · e · y is multiplied by voiceGain.
    // Default 1.0f preserves Phase 1 behaviour for legacy callers.
    void setVoiceGain(float g) noexcept { voiceGain_ = g; }
    [[nodiscard]] float voiceGain() const noexcept { return voiceGain_; }

    // Phase 2 DENSITY wiring: live-resize the active subset of agents.
    // Clamps to [0, agents_.size()]. Newly activated agents inherit the
    // state populated at noteOn (frequency, phase, envelope) — the pool
    // pre-fills all slots, activeCount_ just controls how many are
    // processed each sample.
    void setActiveCount(int n) noexcept;
    [[nodiscard]] const Agent& agent(int i) const noexcept { return agents_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] Agent& mutableAgent(int i) noexcept { return agents_[static_cast<std::size_t>(i)]; }

    // Convenience: spread N agents at evenly spaced substrate positions
    // (Phase 1 placeholder; Phase 2 reads from agent.shape_distribution).
    void layoutEvenly(int substrateCellCount) noexcept;

    // 2D layout — distribute agents across the (cellsX, cellsY) grid in
    // a row-major pattern. Each agent gets a unique (position, positionY)
    // so deposits don't all stack at one site. Used by Voice when the
    // topology is 2D.
    void layoutEvenly2D(int cellsX, int cellsY) noexcept;

private:
    int activeCount_ = 0;
    std::uint64_t currentSampleIndex_ = 0; // sample counter since last noteOn
    float voiceGain_ = 1.0f;
    std::vector<Agent> agents_;
};

} // namespace sfs::engine::agents
