// src/engine/voice.h
//
// A Voice owns one substrate, one agent pool, and one harvester position.
// It is the canonical per-polyphonic-instance unit of SFS — sfs-spec/01 §1
// "Per-voice instancing." Phase 1 ships exactly one Voice per PluginProcessor;
// P2 introduces the VoiceManager + 8-voice polyphony.
//
// Per-sample loop (sfs-spec/01 §5.4):
//   1. agents.processOneSample(substrate, sampleRate)   // bend, deposit, advance
//   2. substrate.step()                                  // leapfrog + DC block
//   3. out[i] = substrate.read(harvesterPosition)         // mono harvester read

#pragma once

#include "engine/agents/agent_pool.h"
#include "engine/envelope/adsr.h"
#include "engine/lfo/lfo.h"
#include "engine/macros/macros.h"
#include "engine/substrate/substrate_1d.h"

#include <array>

namespace sfs::engine
{

class Voice
{
public:
    static constexpr int kDefaultSubstrateCells = 1024;
    static constexpr int kDefaultAgentCount = 16;

    Voice(int substrateCells, int agentCount, float sampleRate);

    void noteOn(int midiNote, float velocity);
    void noteOff();

    // Render `numSamples` of mono float audio. Reads at harvesterPosition_;
    // independent DC blocker state.
    void renderBlock(float* out, int numSamples) noexcept;

    // Render `numSamples` of stereo audio into separate left + right buffers.
    // Two harvesters at positions 0 and N/2 (spec sfs-spec/04 §3.2 "stereo
    // default"); independent DC blockers per channel. Voice spends roughly
    // 2× the read-side cost vs mono — substrate.step() and the agent loop
    // are unchanged, only the harvester read + DC + soft-clip happen twice.
    void renderBlockStereo(float* outL, float* outR, int numSamples) noexcept;

    [[nodiscard]] bool isGated() const noexcept { return gated_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }

    // Direct access for tests / instrumentation. Don't reach into these from
    // the audio thread of a real host.
    [[nodiscard]] substrate::Substrate1D& substrate() noexcept { return substrate_; }
    [[nodiscard]] const substrate::Substrate1D& substrate() const noexcept { return substrate_; }
    [[nodiscard]] agents::AgentPool& agents() noexcept { return agents_; }
    [[nodiscard]] const agents::AgentPool& agents() const noexcept { return agents_; }

    [[nodiscard]] sfs::engine::envelope::Adsr& ampEnv() noexcept { return ampEnv_; }
    [[nodiscard]] const sfs::engine::envelope::Adsr& ampEnv() const noexcept { return ampEnv_; }

    static constexpr int kLfoCount = 4;
    [[nodiscard]] sfs::engine::lfo::Lfo& lfo(int i) noexcept { return lfos_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] const sfs::engine::lfo::Lfo& lfo(int i) const noexcept { return lfos_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] float lfoValue(int i) const noexcept { return lfoValues_[static_cast<std::size_t>(i)]; }

    void setHarvesterPosition(float position) noexcept { harvesterPosition_ = position; }
    [[nodiscard]] float harvesterPosition() const noexcept { return harvesterPosition_; }

    // Phase 2 single-shape selection: every agent uses the same waveform.
    // Phase 3 will replace with per-agent shape from agent.shape_distribution
    // (sfs-spec/09 §3.3). Set by the plug-in shell from a host parameter.
    void setUniformShape(sfs::engine::agents::AgentShape s) noexcept { uniformShape_ = s; }
    [[nodiscard]] sfs::engine::agents::AgentShape uniformShape() const noexcept { return uniformShape_; }

    // Mutable access to the macro values. Plug-in shell writes the current
    // host-parameter values into this struct before each renderBlock call;
    // renderBlock applies the fan-out to substrate + agent fields at block
    // boundaries. Phase 2: block-rate apply, no smoothing yet.
    [[nodiscard]] sfs::engine::macros::MacroValues& macros() noexcept { return macros_; }
    [[nodiscard]] const sfs::engine::macros::MacroValues& macros() const noexcept { return macros_; }

private:
    void applyMacroFanOut(const sfs::engine::macros::InternalFields& fields) noexcept;

    substrate::Substrate1D substrate_;
    agents::AgentPool agents_;
    sfs::engine::envelope::Adsr ampEnv_;
    std::array<sfs::engine::lfo::Lfo, kLfoCount> lfos_{};
    std::array<float, kLfoCount> lfoValues_{}; // last-tick cache
    float sampleRate_;
    float harvesterPosition_ = 0.0f;
    bool gated_ = false;

    // Phase 2 macro layer. The plug-in shell writes host-parameter values
    // into macros_ before renderBlock; renderBlock applies the fan-out at
    // block start to substrate + agent fields.
    sfs::engine::macros::MacroValues macros_{};

    // Phase 2 uniform shape: every agent uses the same waveform. Set by
    // the plug-in shell from a host AudioParameterChoice. P3 replaces
    // with per-agent shape draws from the shape_distribution.
    sfs::engine::agents::AgentShape uniformShape_ = sfs::engine::agents::AgentShape::Sine;

    // Substrate κ (velocity diffusion) is not macro-driven in Phase 2 (the
    // spec leaves it to indirect control via DAMPING + EXCITATION but that
    // pathway isn't fully fleshed). Phase 2 keeps κ at the Phase 1 default
    // and revisits when DAMPING's full fan-out lands.
    float substrateKappa_ = 0.05f;

    // DC blocker state for the harvester reads (sfs-spec/02 §6 — applied at
    // the output, not on the substrate state). First-order high-pass:
    //   y[n] = x[n] - x[n-1] + α·y[n-1]
    // α derived from sampleRate at construction. Mono and stereo render
    // paths each have their own state — switching paths between blocks
    // doesn't share the cache, but for a single render path the state
    // carries cleanly between blocks.
    float dcBlockerAlpha_ = 0.999346f; // 1 - 2π·5/48000
    float dcBlockerLastInput_ = 0.0f;  // x[n-1]   (mono)
    float dcBlockerLastOutput_ = 0.0f; // y[n-1]   (mono)
    float dcBlockerLastInputL_ = 0.0f; // stereo L
    float dcBlockerLastOutputL_ = 0.0f;
    float dcBlockerLastInputR_ = 0.0f; // stereo R
    float dcBlockerLastOutputR_ = 0.0f;

    // Stereo harvester positions. Mono renderBlock uses harvesterPosition_
    // (set to substrate midpoint by default, kept for test compatibility).
    // Stereo uses these two — sfs-spec/04 §3.2 "two harvesters at substrate
    // positions 0.0 and N/2.0".
    float harvesterPositionStereoL_ = 0.0f;
    float harvesterPositionStereoR_ = 0.0f;
};

} // namespace sfs::engine
