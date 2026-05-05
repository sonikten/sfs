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
    [[nodiscard]] sfs::engine::macros::MacroValues& macros() noexcept { return macros_; }
    [[nodiscard]] const sfs::engine::macros::MacroValues& macros() const noexcept { return macros_; }

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
    sfs::engine::macros::MacroValues macros_{};
    std::uint64_t nextAge_ = 1; // monotonically increasing
};

} // namespace sfs::engine
