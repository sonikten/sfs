// src/engine/voice_manager.h
//
// 8-voice polyphonic voice manager (Phase 2 §9 step 7). Owns a fixed
// pool of Voice instances; routes MIDI note-on/off; allocates voices
// on note-on (free voice → use; all busy → steal oldest); sums voice
// renders into the stereo output bus.
//
// Per sfs-spec/01 §6:
//   MAX_VOICES = 8
//   Voice stealing: pick oldest released (or oldest gated if none
//                   released); 5 ms ramp on the stolen voice.
//
// Phase 2 simplification: no 5 ms ramp yet. The stolen voice's
// substrate keeps its current state (per-voice instancing); the new
// note's agents start depositing on top, and γ decays the prior
// content. Audibly OK; ramp is a follow-up if click artefacts appear.

#pragma once

#include "engine/voice.h"

#include <array>
#include <cstdint>

namespace sfs::engine
{

class VoiceManager
{
public:
    static constexpr int kMaxVoices = 8;

    VoiceManager(int substrateCells, int agentCount, float sampleRate);

    // MIDI dispatch. The (midiNote, velocity) overload is the legacy
    // mono-channel path used by tests + non-MPE hosts. The MPE-aware
    // overload binds the voice to a MIDI channel so subsequent per-
    // channel pitch-bend / pressure / timbre updates can find it.
    void noteOn(int midiNote, float velocity);
    void noteOn(int midiChannel, int midiNote, float velocity);
    void noteOff(int midiNote);
    void noteOff(int midiChannel, int midiNote);
    void allNotesOff();

    // Phase 3 §9 step 8 — MPE Note Expression. Per-channel state is
    // applied to whichever voice currently holds that channel; held in
    // a 16-slot side table so a noteOn on a channel inherits the most
    // recent bend / pressure / timbre values (MPE convention).
    static constexpr int kNumMidiChannels = 16;
    void setChannelPitchBendSemitones(int midiChannel, float semitones) noexcept;
    void setChannelPressure(int midiChannel, float pressure01) noexcept;
    void setChannelTimbre(int midiChannel, float timbre01) noexcept;

    // Per-block render. Sums all active voices into the stereo output bus.
    void renderBlockStereo(float* outL, float* outR, int numSamples) noexcept;

    // Per-block first-order ambisonic render (sfs-spec/04 §3.6). Sums all
    // active voices into 4 channels (W, X, Y, Z); Z is always 0 because
    // the substrate is at most 2D.
    void renderBlockFoa(float* outW, float* outX, float* outY, float* outZ, int numSamples) noexcept;

    // Per-block 5.1 surround render (sfs-spec/04 §3.4). Six channels
    // L / R / C / LFE / Ls / Rs, ITU-R BS.775 layout.
    void renderBlockSurround51(
        float* outL, float* outR, float* outC, float* outLfe, float* outLs, float* outRs, int numSamples) noexcept;

    // Mutable macros that ALL voices share. The plug-in shell writes once
    // before renderBlockStereo; each voice copies the snapshot at its own
    // block boundary.
    [[nodiscard]] sfs::engine::macros::MacroValues& macros() noexcept { return macroTargets_; }
    [[nodiscard]] const sfs::engine::macros::MacroValues& macros() const noexcept { return macroTargets_; }

    // Inspection: the smoothed values currently driving the engine. Lags
    // macros() by one-pole at ~30 ms time constant. Useful for tests +
    // GUI readouts that want to show the actual rendered macro state.
    [[nodiscard]] const sfs::engine::macros::MacroValues& smoothedMacros() const noexcept { return smoothedMacros_; }

    // Phase 2 uniform-shape selection: every voice's agents share the
    // same waveform. Plug-in shell writes from a host parameter.
    void setUniformShape(sfs::engine::agents::AgentShape s) noexcept { uniformShape_ = s; }
    [[nodiscard]] sfs::engine::agents::AgentShape uniformShape() const noexcept { return uniformShape_; }

    // Phase 3 topology selector. Fans out to every voice; if a voice is
    // currently rendering the prior topology it'll switch on the next
    // block (with a brief substrate-reset transient — same trade as
    // voice stealing). Plug-in shell writes from a host parameter.
    void setTopology(Topology t) noexcept;
    [[nodiscard]] Topology topology() const noexcept { return topology_; }

    // Phase 2 mod-matrix block-rate inputs. The plug-in shell writes
    // these from incoming MIDI; each voice picks them up via its mod
    // matrix evaluation each block.
    void setMidiCc1(float v) noexcept { midiCc1_ = v; }
    [[nodiscard]] float midiCc1() const noexcept { return midiCc1_; }

    // Phase 2 control-surface plumbing — every voice receives the same
    // ADSR / LFO / mod-matrix-depth configuration. Called block-rate
    // from PluginProcessor; the underlying state assignments are O(1)
    // per voice so 8 voices × N params is well under the audio budget.
    void setAdsr(float attackMs, float decayMs, float sustainLevel, float releaseMs) noexcept;
    void setLfoConfig(int lfoIndex, float rateHz, sfs::engine::lfo::LfoShape shape) noexcept;
    void setModMatrixSlotDepth(int slotIndex, float depth) noexcept;

    // GUI snapshot: copies the most-recently-active voice's substrate state
    // into dst. Returns true if a voice was active enough to copy from;
    // returns false when every slot is idle (caller should clear the
    // visualisation buffer). Lock-free; safe to call from a Timer thread.
    bool snapshotPrimaryVoiceSubstrate(float* dst, int dstSize) const noexcept;

    // Inspection.
    [[nodiscard]] int activeVoiceCount() const noexcept;

private:
    struct VoiceSlot
    {
        Voice voice;
        int midiNote = -1;            // -1 = free; else the held MIDI note
        int midiChannel = 0;          // 0 = unbound; 1-16 = held MIDI channel
        std::uint64_t ageCounter = 0; // ticks per renderBlock — older = larger
    };

    // Find the index of a free voice (midiNote == -1 and not gated). Returns
    // -1 if none free.
    [[nodiscard]] int findFreeVoice() const noexcept;

    // Find the index of the best voice to steal: oldest non-gated first;
    // if all are gated, oldest overall. Always returns a valid index.
    [[nodiscard]] int findStealVictim() const noexcept;

    std::array<VoiceSlot, kMaxVoices> slots_;
    // Macro layer: targets come from the host (PluginProcessor::processBlock
    // writes them). smoothedMacros_ is what each Voice actually receives
    // each block — a one-pole low-pass that catches up to targets gradually.
    sfs::engine::macros::MacroValues macroTargets_{};
    sfs::engine::macros::MacroValues smoothedMacros_{};
    sfs::engine::agents::AgentShape uniformShape_ = sfs::engine::agents::AgentShape::Sine;
    Topology topology_ = Topology::Ring1D;
    float midiCc1_ = 0.0f;
    float sampleRate_ = 48000.0f;
    std::uint64_t nextAge_ = 1; // monotonically increasing

    // MPE channel state. Index 0 is unused (MIDI channels are 1-16).
    // A noteOn on channel N inherits the side-table values; subsequent
    // setChannel*() calls update both the side table and any voice bound
    // to that channel.
    std::array<float, kNumMidiChannels + 1> channelPitchBendSemitones_{};
    std::array<float, kNumMidiChannels + 1> channelPressure_{};
    std::array<float, kNumMidiChannels + 1> channelTimbre_{};

    // Scratch buffers for per-voice render summing — one allocation at
    // construction (sized to a generous max block); the audio thread
    // never resizes. Keeps renderBlockStereo allocation-free per the
    // audio-thread invariant (CLAUDE.md + sfs-spec/01 §4).
    static constexpr int kMaxBlockSize = 8192;
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> scratchFoaW_;
    std::vector<float> scratchFoaX_;
    std::vector<float> scratchFoaY_;
    std::vector<float> scratchFoaZ_;
    std::array<std::vector<float>, 6> scratch51_; // L, R, C, LFE, Ls, Rs
};

} // namespace sfs::engine
