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

    // MIDI dispatch.
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();

    // Per-block render. Sums all active voices into the stereo output bus.
    void renderBlockStereo(float* outL, float* outR, int numSamples) noexcept;

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
        std::uint64_t ageCounter = 0; // ticks per renderBlock — older = larger
    };

    // Find the index of a free voice (midiNote == -1 and not gated). Returns
    // -1 if none free.
    int findFreeVoice() const noexcept;

    // Find the index of the best voice to steal: oldest non-gated first;
    // if all are gated, oldest overall. Always returns a valid index.
    int findStealVictim() const noexcept;

    std::array<VoiceSlot, kMaxVoices> slots_;
    // Macro layer: targets come from the host (PluginProcessor::processBlock
    // writes them). smoothedMacros_ is what each Voice actually receives
    // each block — a one-pole low-pass that catches up to targets gradually.
    sfs::engine::macros::MacroValues macroTargets_{};
    sfs::engine::macros::MacroValues smoothedMacros_{};
    sfs::engine::agents::AgentShape uniformShape_ = sfs::engine::agents::AgentShape::Sine;
    float midiCc1_ = 0.0f;
    float sampleRate_ = 48000.0f;
    std::uint64_t nextAge_ = 1; // monotonically increasing
};

} // namespace sfs::engine
