// src/engine/voice_manager.cpp

#include "voice_manager.h"

#include <cmath>
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
      },
      // smoothedMacros_ uses the same default field values as macroTargets_
      // (both default-initialise to MacroValues{}), so the first render
      // block won't have to ramp from zero.
      sampleRate_(sampleRate),
      scratchL_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchR_(static_cast<std::size_t>(kMaxBlockSize), 0.0f)
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
    bool stealing = false;
    int idx = findFreeVoice();
    if (idx < 0)
    {
        idx = findStealVictim();
        stealing = true;
    }
    auto& slot = slots_[static_cast<std::size_t>(idx)];
    if (stealing)
    {
        slot.voice.noteOnAfterSteal(midiNote, velocity);
    }
    else
    {
        slot.voice.noteOn(midiNote, velocity);
    }
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

void VoiceManager::setAdsr(float attackMs, float decayMs, float sustainLevel, float releaseMs) noexcept
{
    for (auto& s : slots_)
    {
        auto& env = s.voice.ampEnv();
        env.setAttackMs(attackMs);
        env.setDecayMs(decayMs);
        env.setSustainLevel(sustainLevel);
        env.setReleaseMs(releaseMs);
    }
}

void VoiceManager::setLfoConfig(int lfoIndex, float rateHz, sfs::engine::lfo::LfoShape shape) noexcept
{
    if (lfoIndex < 0 || lfoIndex >= Voice::kLfoCount)
    {
        return;
    }
    for (auto& s : slots_)
    {
        auto& l = s.voice.lfo(lfoIndex);
        l.setRateHz(rateHz);
        l.setShape(shape);
    }
}

void VoiceManager::setModMatrixSlotDepth(int slotIndex, float depth) noexcept
{
    if (slotIndex < 0 || slotIndex >= sfs::engine::mod_matrix::ModMatrix::kNumSlots)
    {
        return;
    }
    for (auto& s : slots_)
    {
        auto& mm = s.voice.modMatrix();
        const auto cur = mm.slot(slotIndex);
        mm.setSlot(slotIndex, cur.source, cur.dest, depth);
    }
}

bool VoiceManager::snapshotPrimaryVoiceSubstrate(float* dst, int dstSize) const noexcept
{
    if (dst == nullptr || dstSize <= 0)
    {
        return false;
    }
    // Pick the youngest active voice (latest noteOn) for the visualiser —
    // this is what the user just hit and is the most useful "what is the
    // engine doing right now" view. Falls back to the latest released
    // voice if nothing is gated.
    int best = -1;
    std::uint64_t bestAge = 0;
    int bestRelnGd = -1;
    std::uint64_t bestRelAge = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto& s = slots_[static_cast<std::size_t>(i)];
        if (s.voice.isGated())
        {
            if (best < 0 || s.ageCounter > bestAge)
            {
                best = i;
                bestAge = s.ageCounter;
            }
        }
        else if (s.midiNote >= 0)
        {
            if (bestRelnGd < 0 || s.ageCounter > bestRelAge)
            {
                bestRelnGd = i;
                bestRelAge = s.ageCounter;
            }
        }
    }
    const int chosen = (best >= 0) ? best : bestRelnGd;
    if (chosen < 0)
    {
        return false;
    }
    slots_[static_cast<std::size_t>(chosen)].voice.snapshotSubstrate(dst, dstSize);
    return true;
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

    // Per-voice scratch buffers — pre-allocated members. If the host
    // hands us a block bigger than kMaxBlockSize (8192 samples), we
    // process it in chunks. Standard JUCE block sizes top out at 2048;
    // 8192 is comfortable headroom.
    if (numSamples > kMaxBlockSize)
    {
        for (int written = 0; written < numSamples;)
        {
            const int chunk = std::min(kMaxBlockSize, numSamples - written);
            renderBlockStereo(outL + written, outR + written, chunk);
            written += chunk;
        }
        return;
    }
    float* const bufL = scratchL_.data();
    float* const bufR = scratchR_.data();

    // Per-parameter smoothing (sfs-spec/05 §4). One-pole low-pass on each
    // macro at ~30 ms time constant. Block-rate smoothing keeps Voice's
    // existing block-rate fan-out intact while removing the audible
    // "zipper" of step-change automation. α derived from block size:
    //   α = 1 - exp(-x), x = N/(τ·sr)
    // std::exp is forbidden in src/engine for determinism (CLAUDE.md
    // hard invariants), so we use the Padé approximation:
    //   1 - exp(-x) ≈ 2x / (2 + x)
    // Exact at x=0; matches std::exp within 3 sig figs for x ≤ 0.5
    // (typical block-size / sample-rate range), within 10% up to x=1.5.
    // Deterministic by construction (only float arithmetic).
    constexpr float kTauSec = 0.030f;
    const float denom = std::max(0.001f, kTauSec * sampleRate_);
    const float x = static_cast<float>(numSamples) / denom;
    const float alpha = (2.0f * x) / (2.0f + x);
    smoothedMacros_.tension += (macroTargets_.tension - smoothedMacros_.tension) * alpha;
    smoothedMacros_.damping += (macroTargets_.damping - smoothedMacros_.damping) * alpha;
    smoothedMacros_.density += (macroTargets_.density - smoothedMacros_.density) * alpha;
    smoothedMacros_.migration += (macroTargets_.migration - smoothedMacros_.migration) * alpha;
    smoothedMacros_.coherence += (macroTargets_.coherence - smoothedMacros_.coherence) * alpha;
    smoothedMacros_.excitation += (macroTargets_.excitation - smoothedMacros_.excitation) * alpha;
    smoothedMacros_.clampInPlace();

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
        s.voice.macros() = smoothedMacros_;
        s.voice.setUniformShape(uniformShape_);
        s.voice.setMidiCc1(midiCc1_);

        s.voice.renderBlockStereo(bufL, bufR, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] += bufL[i];
            outR[i] += bufR[i];
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

    // Master bus soft-clip. Each Voice already soft-clips its own output
    // near ±0.5, but summing N polyphonic voices stacks linearly — 8
    // voices can push the bus up to ~4.0 before any limiting. Apply a
    // second rational saturator at the bus so the master output stays
    // within ±1.0 regardless of polyphony. Same x / (1 + |x|) form as
    // Voice's per-voice clip; the cascade is gentle (two soft saturators
    // never produce hard fold-back). Future Phase 4 output stage replaces
    // this with the spec's full sfs-spec/04 §4 chain (master gain + dm_tanh
    // + DC block + optional limiter).
    auto busSoftClip = [](float v) noexcept { return v / (1.0f + std::fabs(v)); };
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = busSoftClip(outL[i]);
        outR[i] = busSoftClip(outR[i]);
    }
}

} // namespace sfs::engine
