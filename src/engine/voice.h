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
#include "engine/mod_matrix/mod_matrix.h"
#include "engine/substrate/substrate_1d.h"
#include "engine/substrate/substrate_2d.h"

#include <array>
#include <cstdint>

namespace sfs::engine
{

// Substrate topology selector. Phase 3 §9 step 4 introduces 2D — the
// 1D ring stays as the default (Phase 2 behaviour preserved bit-exact).
enum class Topology : std::uint8_t
{
    Ring1D = 0, // Substrate1D, 1024 cells
    Torus2D = 1 // Substrate2D, 32×32 cells (same total cell count)
};

class Voice
{
public:
    static constexpr int kDefaultSubstrateCells = 1024;
    static constexpr int kDefaultAgentCount = 16;

    Voice(int substrateCells, int agentCount, float sampleRate);

    void noteOn(int midiNote, float velocity);
    void noteOff();

    // Same as noteOn but also resets the substrate to zero and starts a
    // 5 ms output-gain ramp from 0 → 1. Used by VoiceManager when a
    // voice is stolen — without these the prior note's substrate state
    // produces a click as the new attack arrives. Phase 3 §9 step 2;
    // closes the Phase 2 deferred risk from sfs-spec/01 §6.
    void noteOnAfterSteal(int midiNote, float velocity);

    // Render `numSamples` of mono float audio. Reads at harvesterPosition_;
    // independent DC blocker state.
    void renderBlock(float* out, int numSamples) noexcept;

    // Render `numSamples` of stereo audio into separate left + right buffers.
    // Two harvesters at positions 0 and N/2 (spec sfs-spec/04 §3.2 "stereo
    // default"); independent DC blockers per channel. Voice spends roughly
    // 2× the read-side cost vs mono — substrate.step() and the agent loop
    // are unchanged, only the harvester read + DC + soft-clip happen twice.
    void renderBlockStereo(float* outL, float* outR, int numSamples) noexcept;

    // Render `numSamples` of 5.1 surround (sfs-spec/04 §3.4). Six
    // channels: L, R, C, LFE, Ls, Rs. 2D topology only — in 1D mode the
    // five full-bandwidth channels downmix to stereo (L/R) and Ls/Rs
    // copy from L/R, C is a sum, LFE is the LPF'd sum (spec downmix).
    //
    // Harvester placement (2D, ITU-R BS.775 angles around a circle of
    // radius 0.4·min(Nx,Ny) centred at (0.5·Nx, 0.5·Ny)):
    //   L  = -30°  R  = +30°  C  =  0°
    //   Ls = -110° Rs = +110°
    //
    // LFE: 120 Hz first-order low-pass of the 5-channel sum (sfs-spec
    // §3.4 specifies 2nd-order Linkwitz–Riley; Phase 3 ships 1st-order
    // for simplicity and tightens to LR-2 in Phase 4 alongside the
    // master output stage).
    void renderBlockSurround51(
        float* outL, float* outR, float* outC, float* outLfe, float* outLs, float* outRs, int numSamples) noexcept;

    // Render `numSamples` of 7.1.4 immersive surround (sfs-spec/04 §3.5).
    // Twelve channels in JUCE order: L, R, C, LFE, Ls, Rs, Lr, Rr, Tfl,
    // Tfr, Trl, Trr.
    //
    // 2D placement (sfs-spec/04 §3.5):
    //   Floor (7 ch) — y < 0.3·Ny, ITU-R BS.775+ angles around centre
    //                  (0.5·Nx, 0.2·Ny), radius 0.15·min(Nx, Ny):
    //                    L  = -30°  R  = +30°  C  =  0°
    //                    Ls = -90°  Rs = +90°
    //                    Lr = -150° Rr = +150°
    //   Height (4 ch) — y > 0.7·Ny, four corners around centre
    //                   (0.5·Nx, 0.8·Ny), radius 0.15·min(Nx, Ny):
    //                    Tfl = -45°  Tfr = +45°
    //                    Trl = -135° Trr = +135°
    //   LFE — 120 Hz LPF of the 11-channel sum (1st-order, same stub as
    //         5.1; LR-2 in Phase 4).
    //
    // 1D topology: spec downmix-to-stereo. L/R from the existing stereo
    // render; C = (L+R)/2; Ls/Rs/Lr/Rr copy L/R; height channels zeroed
    // (no genuine height information from a 1D substrate).
    //
    // Channel pointers passed as a 12-element array so we don't burn the
    // visual budget on twelve named parameters.
    void renderBlockSurround714(float* const* outs, int numSamples) noexcept;

    // Render `numSamples` of first-order horizontal-plane ambisonic
    // (sfs-spec/04 §3.6). 4 channels: W (omni), X (front-back), Y
    // (left-right), Z (up-down, always 0 — substrate is at most 2D).
    //
    // 2D topology: 4 harvesters at the substrate's quadrant centres
    // [(0.25·Nx, 0.25·Ny), (0.25·Nx, 0.75·Ny), (0.75·Nx, 0.25·Ny),
    // (0.75·Nx, 0.75·Ny)] feed the SN3D/ACN encoding matrix from the
    // spec (W = 0.5·Σ, X = 0.5·(h2+h3-h0-h1), Y = 0.5·(h1+h3-h0-h2)).
    //
    // 1D topology: spec calls for downmix-to-stereo (sfs-spec/04 §3.6
    // "FOA layouts downmix to stereo"). We render the stereo path into
    // W/X (L/R) and zero Y/Z so a downstream FOA decoder still produces
    // a sensible stereo image; the result is not "real" ambisonic.
    void renderBlockFoa(float* outW, float* outX, float* outY, float* outZ, int numSamples) noexcept;

    [[nodiscard]] bool isGated() const noexcept { return gated_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }

    // Direct access for tests / instrumentation. Don't reach into these from
    // the audio thread of a real host.
    [[nodiscard]] substrate::Substrate1D& substrate() noexcept { return substrate_; }
    [[nodiscard]] const substrate::Substrate1D& substrate() const noexcept { return substrate_; }
    [[nodiscard]] substrate::Substrate2D& substrate2D() noexcept { return substrate2D_; }
    [[nodiscard]] const substrate::Substrate2D& substrate2D() const noexcept { return substrate2D_; }
    [[nodiscard]] agents::AgentPool& agents() noexcept { return agents_; }
    [[nodiscard]] const agents::AgentPool& agents() const noexcept { return agents_; }

    // Topology selector. Default is Ring1D — Phase 2 bit-exact behaviour.
    // Switching to Torus2D resets BOTH substrates and re-lays the agents
    // on a 2D grid; switch on a non-active voice (or expect a brief
    // transient as the new substrate wakes up).
    void setTopology(Topology t) noexcept;
    [[nodiscard]] Topology topology() const noexcept { return topology_; }

    [[nodiscard]] sfs::engine::envelope::Adsr& ampEnv() noexcept { return ampEnv_; }
    [[nodiscard]] const sfs::engine::envelope::Adsr& ampEnv() const noexcept { return ampEnv_; }

    static constexpr int kLfoCount = 4;
    [[nodiscard]] sfs::engine::lfo::Lfo& lfo(int i) noexcept { return lfos_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] const sfs::engine::lfo::Lfo& lfo(int i) const noexcept { return lfos_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] float lfoValue(int i) const noexcept { return lfoValues_[static_cast<std::size_t>(i)]; }

    [[nodiscard]] sfs::engine::mod_matrix::ModMatrix& modMatrix() noexcept { return modMatrix_; }
    [[nodiscard]] const sfs::engine::mod_matrix::ModMatrix& modMatrix() const noexcept { return modMatrix_; }

    // Mod-matrix source inputs that the plug-in shell writes block-rate.
    void setMidiCc1(float v) noexcept { midiCc1_ = v; }
    [[nodiscard]] float midiCc1() const noexcept { return midiCc1_; }
    [[nodiscard]] float keyVelocity() const noexcept { return keyVelocity_; }
    [[nodiscard]] float randomPerNote() const noexcept { return randomPerNote_; }

    // Phase 3 §9 step 8 — MPE Note Expression. Pitch bend in semitones (typically
    // ±48 for full MPE range). Block-rate; applied multiplicatively to every
    // agent's frequency in applyMacroFanOut. Pressure / timbre route as mod
    // matrix sources (sfs-spec/05 §5.2).
    void setPitchBendSemitones(float s) noexcept { pitchBendSemitones_ = s; }
    [[nodiscard]] float pitchBendSemitones() const noexcept { return pitchBendSemitones_; }
    void setMpePressure(float v) noexcept { mpePressure_ = v; }
    [[nodiscard]] float mpePressure() const noexcept { return mpePressure_; }
    void setMpeTimbre(float v) noexcept { mpeTimbre_ = v; }
    [[nodiscard]] float mpeTimbre() const noexcept { return mpeTimbre_; }

    void setHarvesterPosition(float position) noexcept { harvesterPosition_ = position; }
    [[nodiscard]] float harvesterPosition() const noexcept { return harvesterPosition_; }

    // GUI snapshot: copies the current substrate state into the user-supplied
    // buffer (lock-free). Runs from the GUI/Timer thread; reads atomic-stable
    // values that the audio thread writes at end-of-block. The substrate
    // module is single-writer so a non-atomic copy is acceptable: tearing
    // produces only a half-block visual artifact, never a use-after-free.
    void snapshotSubstrate(float* dst, int dstSize) const noexcept;

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
    [[nodiscard]] sfs::engine::macros::MacroValues
    applyModMatrix(const sfs::engine::macros::MacroValues& base) const noexcept;

    substrate::Substrate1D substrate_;
    substrate::Substrate2D substrate2D_;
    Topology topology_ = Topology::Ring1D;
    agents::AgentPool agents_;
    sfs::engine::envelope::Adsr ampEnv_;
    std::array<sfs::engine::lfo::Lfo, kLfoCount> lfos_{};
    std::array<float, kLfoCount> lfoValues_{}; // last-tick cache
    sfs::engine::mod_matrix::ModMatrix modMatrix_{};
    float keyVelocity_ = 0.0f;
    float midiCc1_ = 0.0f;
    float randomPerNote_ = 0.0f;
    float pitchBendSemitones_ = 0.0f; // MPE per-channel pitch bend, semitones
    float mpePressure_ = 0.0f;        // MPE per-channel pressure [0, 1]
    float mpeTimbre_ = 0.5f;          // MPE per-channel timbre (CC74) [0, 1], default centred
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

    // Substrate κ is now driven by TENSION (viscosityFloor) + EXCITATION
    // (viscosityOffset) per sfs-spec/05 §3.1 + §3.6, with a CFL clamp
    // applied in applyMacroFanOut. No persistent member needed — κ is
    // recomputed each block.

    // DC blocker state for the harvester reads (sfs-spec/02 §6 — applied at
    // the output, not on the substrate state). First-order high-pass:
    //   y[n] = x[n] - x[n-1] + α·y[n-1]
    // α derived from sampleRate at construction. Mono and stereo render
    // paths each have their own state — switching paths between blocks
    // doesn't share the cache, but for a single render path the state
    // carries cleanly between blocks.
    // Voice-steal anti-click ramp. 0.0..1.0; multiplies the per-voice
    // output. Set to 0 by noteOnAfterSteal; advances by stealRampInc_
    // per sample until it reaches 1.0, then stays at 1.0. Anti-click
    // for voice stealing — the substrate is also reset on steal so the
    // ramp covers the substrate's wake-up transient too.
    float stealRampGain_ = 1.0f;
    float stealRampInc_ = 1.0f;

    float dcBlockerAlpha_ = 0.999346f; // 1 - 2π·5/48000
    float dcBlockerLastInput_ = 0.0f;  // x[n-1]   (mono)
    float dcBlockerLastOutput_ = 0.0f; // y[n-1]   (mono)
    float dcBlockerLastInputL_ = 0.0f; // stereo L
    float dcBlockerLastOutputL_ = 0.0f;
    float dcBlockerLastInputR_ = 0.0f; // stereo R
    float dcBlockerLastOutputR_ = 0.0f;

    // FOA per-channel DC-blocker state (W, X, Y, Z). Z's blocker is wired
    // for symmetry but Z is always 0, so its state stays at 0 too.
    std::array<float, 4> dcBlockerLastInputFoa_{};
    std::array<float, 4> dcBlockerLastOutputFoa_{};

    // 5.1 per-channel DC-blocker state (L, R, C, LFE, Ls, Rs).
    std::array<float, 6> dcBlockerLastInput51_{};
    std::array<float, 6> dcBlockerLastOutput51_{};

    // 7.1.4 per-channel DC-blocker state (12 channels in JUCE order).
    std::array<float, 12> dcBlockerLastInput714_{};
    std::array<float, 12> dcBlockerLastOutput714_{};

    // LFE 120 Hz one-pole LPF state. y[n] = y[n-1] + α·(x[n] - y[n-1]).
    // α set at construction from sample rate.
    float lfeLpfState_ = 0.0f;
    float lfeLpfAlpha_ = 0.0157f; // ≈ 2π·120/48000

    // Stereo harvester positions. Mono renderBlock uses harvesterPosition_
    // (set to substrate midpoint by default, kept for test compatibility).
    // Stereo uses these two — sfs-spec/04 §3.2 "two harvesters at substrate
    // positions 0.0 and N/2.0".
    float harvesterPositionStereoL_ = 0.0f;
    float harvesterPositionStereoR_ = 0.0f;
};

} // namespace sfs::engine
