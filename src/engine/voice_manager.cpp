// src/engine/voice_manager.cpp

#include "voice_manager.h"

#include <vector>

namespace sfs::engine
{

VoiceManager::VoiceManager(int substrateCells, int agentCount, float sampleRate)
    : slots_{
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0},
      }
{
}

int VoiceManager::findFreeVoice() const noexcept
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (slots_[static_cast<std::size_t>(i)].midiNote < 0 && !slots_[static_cast<std::size_t>(i)].voice.isGated())
        {
            return i;
        }
    }
    return -1;
}

int VoiceManager::findStealVictim() const noexcept
{
    // Prefer non-gated (released, decaying). If all are gated, pick the
    // oldest. Tie-break on age (older = stolen first).
    int bestNonGated = -1;
    std::uint64_t bestNonGatedAge = 0;
    int oldest = 0;
    std::uint64_t oldestAge = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto& s = slots_[static_cast<std::size_t>(i)];
        if (s.ageCounter >= oldestAge)
        {
            oldestAge = s.ageCounter;
            oldest = i;
        }
        if (!s.voice.isGated() && s.ageCounter >= bestNonGatedAge)
        {
            bestNonGatedAge = s.ageCounter;
            bestNonGated = i;
        }
    }
    return (bestNonGated >= 0) ? bestNonGated : oldest;
}

void VoiceManager::noteOn(int midiNote, float velocity)
{
    int idx = findFreeVoice();
    if (idx < 0)
    {
        idx = findStealVictim();
    }
    auto& slot = slots_[static_cast<std::size_t>(idx)];
    slot.voice.noteOn(midiNote, velocity);
    slot.midiNote = midiNote;
    slot.ageCounter = nextAge_++;
}

void VoiceManager::noteOff(int midiNote)
{
    // First match wins (most synths route this way).
    for (auto& s : slots_)
    {
        if (s.midiNote == midiNote && s.voice.isGated())
        {
            s.voice.noteOff();
            // s.midiNote stays set so noteOff(same note) twice doesn't double-
            // gate-off. It clears when the slot is reassigned (noteOn or steal).
            return;
        }
    }
}

void VoiceManager::allNotesOff()
{
    for (auto& s : slots_)
    {
        if (s.voice.isGated())
        {
            s.voice.noteOff();
        }
    }
}

int VoiceManager::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& s : slots_)
    {
        if (s.voice.isGated())
        {
            ++n;
        }
    }
    return n;
}

void VoiceManager::renderBlockStereo(float* outL, float* outR, int numSamples) noexcept
{
    if (outL == nullptr || outR == nullptr || numSamples <= 0)
    {
        return;
    }

    // Clear the output bus once.
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    // Per-voice scratch buffers (block-rate stack allocations are NOT in
    // the audio-thread invariant — Voice::renderBlockStereo allocates
    // nothing). Phase 2 follow-up: hoist these to a member to skip the
    // vector construction in the hot block.
    std::vector<float> bufL(static_cast<std::size_t>(numSamples), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(numSamples), 0.0f);

    for (auto& s : slots_)
    {
        // Skip voices that aren't producing sound: not gated AND no
        // recent activity (the substrate has decayed; we trust γ to have
        // killed it within ~100 ms of noteOff).
        // Simpler heuristic for Phase 2: render every voice that's gated
        // OR was recently active. A cleaner version uses an idle-sample
        // counter on the voice; deferred.
        const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
        if (!shouldRender)
        {
            continue;
        }

        // Push the shared macros snapshot + shape selection into this voice.
        s.voice.macros() = macros_;
        s.voice.setUniformShape(uniformShape_);
        s.voice.setMidiCc1(midiCc1_);

        s.voice.renderBlockStereo(bufL.data(), bufR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] += bufL[static_cast<std::size_t>(i)];
            outR[i] += bufR[static_cast<std::size_t>(i)];
        }

        // Voice "completes" — frees up the slot — when it has fully
        // decayed. Phase 2 simplification: clear the midiNote tag once
        // the voice is no longer gated AND has been ungated for at
        // least one block. The substrate's γ handles the actual decay
        // tail; the slot just becomes available for stealing first.
        if (!s.voice.isGated() && s.midiNote >= 0)
        {
            // Mark as released; eligible for re-allocation on next noteOn.
            s.midiNote = -1;
        }
    }
}

} // namespace sfs::engine
