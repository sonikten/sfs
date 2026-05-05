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
#include "engine/macros/macros.h"
#include "engine/substrate/substrate_1d.h"

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

    // Render `numSamples` of mono float audio. Caller is responsible for any
    // multichannel duplication; Phase 1 is mono-only.
    void renderBlock(float* out, int numSamples) noexcept;

    [[nodiscard]] bool isGated() const noexcept { return gated_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }

    // Direct access for tests / instrumentation. Don't reach into these from
    // the audio thread of a real host.
    [[nodiscard]] substrate::Substrate1D& substrate() noexcept { return substrate_; }
    [[nodiscard]] const substrate::Substrate1D& substrate() const noexcept { return substrate_; }
    [[nodiscard]] agents::AgentPool& agents() noexcept { return agents_; }
    [[nodiscard]] const agents::AgentPool& agents() const noexcept { return agents_; }

    void setHarvesterPosition(float position) noexcept { harvesterPosition_ = position; }
    [[nodiscard]] float harvesterPosition() const noexcept { return harvesterPosition_; }

    // Mutable access to the macro values. Plug-in shell writes the current
    // host-parameter values into this struct before each renderBlock call;
    // renderBlock applies the fan-out to substrate + agent fields at block
    // boundaries. Phase 2: block-rate apply, no smoothing yet.
    [[nodiscard]] sfs::engine::macros::MacroValues& macros() noexcept { return macros_; }
    [[nodiscard]] const sfs::engine::macros::MacroValues& macros() const noexcept { return macros_; }

private:
    substrate::Substrate1D substrate_;
    agents::AgentPool agents_;
    float sampleRate_;
    float harvesterPosition_ = 0.0f;
    bool gated_ = false;

    // Phase 2 macro layer. The plug-in shell writes host-parameter values
    // into macros_ before renderBlock; renderBlock applies the fan-out at
    // block start to substrate + agent fields.
    sfs::engine::macros::MacroValues macros_{};

    // Substrate κ (velocity diffusion) is not macro-driven in Phase 2 (the
    // spec leaves it to indirect control via DAMPING + EXCITATION but that
    // pathway isn't fully fleshed). Phase 2 keeps κ at the Phase 1 default
    // and revisits when DAMPING's full fan-out lands.
    float substrateKappa_ = 0.05f;

    // DC blocker state for the harvester read (sfs-spec/02 §6 — applied at
    // the output, not on the substrate state). First-order high-pass:
    //   y[n] = x[n] - x[n-1] + α·y[n-1]
    // α derived from sampleRate at construction.
    float dcBlockerAlpha_ = 0.999346f; // 1 - 2π·5/48000
    float dcBlockerLastInput_ = 0.0f;  // x[n-1]
    float dcBlockerLastOutput_ = 0.0f; // y[n-1]
};

} // namespace sfs::engine
