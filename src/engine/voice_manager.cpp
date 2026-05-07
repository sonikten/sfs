// src/engine/voice_manager.cpp

#include "voice_manager.h"

#include <cmath>
#include <vector>

namespace sfs::engine
{

VoiceManager::VoiceManager(int substrateCells, int agentCount, float sampleRate)
    : slots_{
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
          VoiceSlot{Voice{substrateCells, agentCount, sampleRate}, -1, 0, 0},
      },
      // smoothedMacros_ uses the same default field values as macroTargets_
      // (both default-initialise to MacroValues{}), so the first render
      // block won't have to ramp from zero.
      sampleRate_(sampleRate),
      scratchL_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchR_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchFoaW_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchFoaX_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchFoaY_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratchFoaZ_(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      scratch51_{
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      },
      scratch714_{
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
          std::vector<float>(static_cast<std::size_t>(kMaxBlockSize), 0.0f),
      }
{
    // MPE timbre default is centred — MPE 1.0 §6.4 reset value.
    for (auto& v : channelTimbre_)
    {
        v = 0.5f;
    }

    // Per-voice scratch for the threaded render paths. Sized for the
    // worst case 12-channel render (7.1.4); only the channels actually
    // used by the active layout are touched at render time.
    for (auto& voiceScratch : perVoiceScratch_)
    {
        for (auto& chBuf : voiceScratch)
        {
            chBuf.assign(static_cast<std::size_t>(kMaxBlockSize), 0.0f);
        }
    }
}

void VoiceManager::enableThreading(int numWorkers)
{
    if (numWorkers <= 0)
    {
        pool_.reset();
        return;
    }
    if (numWorkers > kMaxVoices)
    {
        numWorkers = kMaxVoices;
    }
    pool_ = std::make_unique<VoicePool>(numWorkers);
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
    noteOn(/*midiChannel*/ 0, midiNote, velocity);
}

void VoiceManager::noteOn(int midiChannel, int midiNote, float velocity)
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
    slot.midiChannel = midiChannel;
    slot.ageCounter = nextAge_++;

    // Inherit MPE state from the side table (MPE 1.0 §6.4: pressure +
    // pitch bend + timbre persist on the channel and apply immediately to
    // any new note on that channel). channelPitchBendSemitones_[0] etc.
    // are zero-init'd, so non-MPE callers (channel == 0) get neutral state.
    if (midiChannel >= 0 && midiChannel <= kNumMidiChannels)
    {
        const auto cIdx = static_cast<std::size_t>(midiChannel);
        slot.voice.setPitchBendSemitones(channelPitchBendSemitones_[cIdx]);
        slot.voice.setMpePressure(channelPressure_[cIdx]);
        slot.voice.setMpeTimbre(channelTimbre_[cIdx]);
    }
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

void VoiceManager::noteOff(int midiChannel, int midiNote)
{
    // Channel-scoped match — required for MPE because the same note number
    // may be held on multiple member channels simultaneously.
    for (auto& s : slots_)
    {
        if (s.midiNote == midiNote && s.midiChannel == midiChannel && s.voice.isGated())
        {
            s.voice.noteOff();
            return;
        }
    }
}

void VoiceManager::setChannelPitchBendSemitones(int midiChannel, float semitones) noexcept
{
    if (midiChannel < 0 || midiChannel > kNumMidiChannels)
    {
        return;
    }
    channelPitchBendSemitones_[static_cast<std::size_t>(midiChannel)] = semitones;
    for (auto& s : slots_)
    {
        if (s.midiChannel == midiChannel && s.midiNote >= 0)
        {
            s.voice.setPitchBendSemitones(semitones);
        }
    }
}

void VoiceManager::setChannelPressure(int midiChannel, float pressure01) noexcept
{
    if (midiChannel < 0 || midiChannel > kNumMidiChannels)
    {
        return;
    }
    channelPressure_[static_cast<std::size_t>(midiChannel)] = pressure01;
    for (auto& s : slots_)
    {
        if (s.midiChannel == midiChannel && s.midiNote >= 0)
        {
            s.voice.setMpePressure(pressure01);
        }
    }
}

void VoiceManager::setChannelTimbre(int midiChannel, float timbre01) noexcept
{
    if (midiChannel < 0 || midiChannel > kNumMidiChannels)
    {
        return;
    }
    channelTimbre_[static_cast<std::size_t>(midiChannel)] = timbre01;
    for (auto& s : slots_)
    {
        if (s.midiChannel == midiChannel && s.midiNote >= 0)
        {
            s.voice.setMpeTimbre(timbre01);
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

void VoiceManager::setTopology(Topology t) noexcept
{
    if (topology_ == t)
    {
        return;
    }
    topology_ = t;
    for (auto& s : slots_)
    {
        s.voice.setTopology(t);
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

void VoiceManager::setModMatrixSlot(int slotIndex,
                                    sfs::engine::mod_matrix::Source source,
                                    sfs::engine::mod_matrix::Destination dest,
                                    float depth) noexcept
{
    if (slotIndex < 0 || slotIndex >= sfs::engine::mod_matrix::ModMatrix::kNumSlots)
    {
        return;
    }
    for (auto& s : slots_)
    {
        s.voice.modMatrix().setSlot(slotIndex, source, dest, depth);
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

    if (pool_ != nullptr)
    {
        // Parallel path. Per-voice render runs on workers; merge happens
        // sequentially in voice-index order so the output is bit-exact
        // with the serial path.
        struct StereoArgs
        {
            Voice* voice;
            float* outL;
            float* outR;
            int n;
        };
        std::array<StereoArgs, kMaxVoices> args{};
        int numActive = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                j.active.store(false, std::memory_order_release);
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);
            args[static_cast<std::size_t>(v)] = StereoArgs{&s.voice,
                                                           perVoiceScratch_[static_cast<std::size_t>(v)][0].data(),
                                                           perVoiceScratch_[static_cast<std::size_t>(v)][1].data(),
                                                           numSamples};
            j.userData = &args[static_cast<std::size_t>(v)];
            j.work = [](void* ud) noexcept
            {
                auto* a = static_cast<StereoArgs*>(ud);
                a->voice->renderBlockStereo(a->outL, a->outR, a->n);
            };
            j.claimed.store(false, std::memory_order_release);
            j.done.store(false, std::memory_order_release);
            j.active.store(true, std::memory_order_release);
            ++numActive;
        }
        pool_->submit(numActive);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            if (!j.active.load(std::memory_order_acquire))
            {
                continue;
            }
            pool_->waitFor(v);
            const float* const sL = perVoiceScratch_[static_cast<std::size_t>(v)][0].data();
            const float* const sR = perVoiceScratch_[static_cast<std::size_t>(v)][1].data();
            for (int i = 0; i < numSamples; ++i)
            {
                outL[i] += sL[i];
                outR[i] += sR[i];
            }
            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }
    else
    {
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

void VoiceManager::renderBlockFoa(float* outW, float* outX, float* outY, float* outZ, int numSamples) noexcept
{
    if (outW == nullptr || outX == nullptr || outY == nullptr || outZ == nullptr || numSamples <= 0)
    {
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        outW[i] = 0.0f;
        outX[i] = 0.0f;
        outY[i] = 0.0f;
        outZ[i] = 0.0f;
    }

    if (numSamples > kMaxBlockSize)
    {
        for (int written = 0; written < numSamples;)
        {
            const int chunk = std::min(kMaxBlockSize, numSamples - written);
            renderBlockFoa(outW + written, outX + written, outY + written, outZ + written, chunk);
            written += chunk;
        }
        return;
    }

    float* const bufW = scratchFoaW_.data();
    float* const bufX = scratchFoaX_.data();
    float* const bufY = scratchFoaY_.data();
    float* const bufZ = scratchFoaZ_.data();

    // Same macro smoothing as the stereo path. Padé approximation for
    // the one-pole alpha (std::exp forbidden in src/engine).
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

    if (pool_ != nullptr)
    {
        struct FoaArgs
        {
            Voice* voice;
            float* outW;
            float* outX;
            float* outY;
            float* outZ;
            int n;
        };
        std::array<FoaArgs, kMaxVoices> args{};
        int numActive = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                j.active.store(false, std::memory_order_release);
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);
            args[static_cast<std::size_t>(v)] = FoaArgs{&s.voice,
                                                        perVoiceScratch_[static_cast<std::size_t>(v)][0].data(),
                                                        perVoiceScratch_[static_cast<std::size_t>(v)][1].data(),
                                                        perVoiceScratch_[static_cast<std::size_t>(v)][2].data(),
                                                        perVoiceScratch_[static_cast<std::size_t>(v)][3].data(),
                                                        numSamples};
            j.userData = &args[static_cast<std::size_t>(v)];
            j.work = [](void* ud) noexcept
            {
                auto* a = static_cast<FoaArgs*>(ud);
                a->voice->renderBlockFoa(a->outW, a->outX, a->outY, a->outZ, a->n);
            };
            j.claimed.store(false, std::memory_order_release);
            j.done.store(false, std::memory_order_release);
            j.active.store(true, std::memory_order_release);
            ++numActive;
        }
        pool_->submit(numActive);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            if (!j.active.load(std::memory_order_acquire))
            {
                continue;
            }
            pool_->waitFor(v);
            const float* const sW = perVoiceScratch_[static_cast<std::size_t>(v)][0].data();
            const float* const sX = perVoiceScratch_[static_cast<std::size_t>(v)][1].data();
            const float* const sY = perVoiceScratch_[static_cast<std::size_t>(v)][2].data();
            const float* const sZ = perVoiceScratch_[static_cast<std::size_t>(v)][3].data();
            for (int i = 0; i < numSamples; ++i)
            {
                outW[i] += sW[i];
                outX[i] += sX[i];
                outY[i] += sY[i];
                outZ[i] += sZ[i];
            }
            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }
    else
    {
        for (auto& s : slots_)
        {
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);

            s.voice.renderBlockFoa(bufW, bufX, bufY, bufZ, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                outW[i] += bufW[i];
                outX[i] += bufX[i];
                outY[i] += bufY[i];
                outZ[i] += bufZ[i];
            }

            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }

    auto busSoftClip = [](float v) noexcept { return v / (1.0f + std::fabs(v)); };
    for (int i = 0; i < numSamples; ++i)
    {
        outW[i] = busSoftClip(outW[i]);
        outX[i] = busSoftClip(outX[i]);
        outY[i] = busSoftClip(outY[i]);
        outZ[i] = busSoftClip(outZ[i]); // pass-through (Z=0)
    }
}

void VoiceManager::renderBlockSurround51(
    float* outL, float* outR, float* outC, float* outLfe, float* outLs, float* outRs, int numSamples) noexcept
{
    if (outL == nullptr || outR == nullptr || outC == nullptr || outLfe == nullptr || outLs == nullptr ||
        outRs == nullptr || numSamples <= 0)
    {
        return;
    }

    float* const channelOut[6] = {outL, outR, outC, outLfe, outLs, outRs};
    for (auto* ch : channelOut)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            ch[i] = 0.0f;
        }
    }

    if (numSamples > kMaxBlockSize)
    {
        for (int written = 0; written < numSamples;)
        {
            const int chunk = std::min(kMaxBlockSize, numSamples - written);
            renderBlockSurround51(outL + written,
                                  outR + written,
                                  outC + written,
                                  outLfe + written,
                                  outLs + written,
                                  outRs + written,
                                  chunk);
            written += chunk;
        }
        return;
    }

    constexpr float kTauSec = 0.030f;
    const float denom = std::max(0.001f, kTauSec * sampleRate_);
    const float xS = static_cast<float>(numSamples) / denom;
    const float alpha = (2.0f * xS) / (2.0f + xS);
    smoothedMacros_.tension += (macroTargets_.tension - smoothedMacros_.tension) * alpha;
    smoothedMacros_.damping += (macroTargets_.damping - smoothedMacros_.damping) * alpha;
    smoothedMacros_.density += (macroTargets_.density - smoothedMacros_.density) * alpha;
    smoothedMacros_.migration += (macroTargets_.migration - smoothedMacros_.migration) * alpha;
    smoothedMacros_.coherence += (macroTargets_.coherence - smoothedMacros_.coherence) * alpha;
    smoothedMacros_.excitation += (macroTargets_.excitation - smoothedMacros_.excitation) * alpha;
    smoothedMacros_.clampInPlace();

    float* const buf[6] = {scratch51_[0].data(),
                           scratch51_[1].data(),
                           scratch51_[2].data(),
                           scratch51_[3].data(),
                           scratch51_[4].data(),
                           scratch51_[5].data()};

    if (pool_ != nullptr)
    {
        struct Surround51Args
        {
            Voice* voice;
            float* outs[6];
            int n;
        };
        std::array<Surround51Args, kMaxVoices> args{};
        int numActive = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                j.active.store(false, std::memory_order_release);
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);
            auto& a = args[static_cast<std::size_t>(v)];
            a.voice = &s.voice;
            for (int c = 0; c < 6; ++c)
            {
                a.outs[c] = perVoiceScratch_[static_cast<std::size_t>(v)][static_cast<std::size_t>(c)].data();
            }
            a.n = numSamples;
            j.userData = &a;
            j.work = [](void* ud) noexcept
            {
                auto* aa = static_cast<Surround51Args*>(ud);
                aa->voice->renderBlockSurround51(
                    aa->outs[0], aa->outs[1], aa->outs[2], aa->outs[3], aa->outs[4], aa->outs[5], aa->n);
            };
            j.claimed.store(false, std::memory_order_release);
            j.done.store(false, std::memory_order_release);
            j.active.store(true, std::memory_order_release);
            ++numActive;
        }
        pool_->submit(numActive);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            if (!j.active.load(std::memory_order_acquire))
            {
                continue;
            }
            pool_->waitFor(v);
            for (int c = 0; c < 6; ++c)
            {
                const float* const src =
                    perVoiceScratch_[static_cast<std::size_t>(v)][static_cast<std::size_t>(c)].data();
                for (int i = 0; i < numSamples; ++i)
                {
                    channelOut[c][i] += src[i];
                }
            }
            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }
    else
    {
        for (auto& s : slots_)
        {
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);

            s.voice.renderBlockSurround51(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], numSamples);
            for (int c = 0; c < 6; ++c)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    channelOut[c][i] += buf[c][i];
                }
            }

            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }

    auto busSoftClip = [](float v) noexcept { return v / (1.0f + std::fabs(v)); };
    for (int c = 0; c < 6; ++c)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            channelOut[c][i] = busSoftClip(channelOut[c][i]);
        }
    }
}

void VoiceManager::renderBlockSurround714(float* const* outs, int numSamples) noexcept
{
    if (outs == nullptr || numSamples <= 0)
    {
        return;
    }
    for (int c = 0; c < 12; ++c)
    {
        if (outs[c] == nullptr)
        {
            return;
        }
    }

    for (int c = 0; c < 12; ++c)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            outs[c][i] = 0.0f;
        }
    }

    if (numSamples > kMaxBlockSize)
    {
        for (int written = 0; written < numSamples;)
        {
            const int chunk = std::min(kMaxBlockSize, numSamples - written);
            float* offset[12];
            for (int c = 0; c < 12; ++c)
            {
                offset[c] = outs[c] + written;
            }
            renderBlockSurround714(offset, chunk);
            written += chunk;
        }
        return;
    }

    constexpr float kTauSec = 0.030f;
    const float denom = std::max(0.001f, kTauSec * sampleRate_);
    const float xS = static_cast<float>(numSamples) / denom;
    const float alpha = (2.0f * xS) / (2.0f + xS);
    smoothedMacros_.tension += (macroTargets_.tension - smoothedMacros_.tension) * alpha;
    smoothedMacros_.damping += (macroTargets_.damping - smoothedMacros_.damping) * alpha;
    smoothedMacros_.density += (macroTargets_.density - smoothedMacros_.density) * alpha;
    smoothedMacros_.migration += (macroTargets_.migration - smoothedMacros_.migration) * alpha;
    smoothedMacros_.coherence += (macroTargets_.coherence - smoothedMacros_.coherence) * alpha;
    smoothedMacros_.excitation += (macroTargets_.excitation - smoothedMacros_.excitation) * alpha;
    smoothedMacros_.clampInPlace();

    float* buf[12];
    for (int c = 0; c < 12; ++c)
    {
        buf[c] = scratch714_[static_cast<std::size_t>(c)].data();
    }

    if (pool_ != nullptr)
    {
        struct Surround714Args
        {
            Voice* voice;
            float* outs[12];
            int n;
        };
        std::array<Surround714Args, kMaxVoices> args{};
        int numActive = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                j.active.store(false, std::memory_order_release);
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);
            auto& a = args[static_cast<std::size_t>(v)];
            a.voice = &s.voice;
            for (int c = 0; c < 12; ++c)
            {
                a.outs[c] = perVoiceScratch_[static_cast<std::size_t>(v)][static_cast<std::size_t>(c)].data();
            }
            a.n = numSamples;
            j.userData = &a;
            j.work = [](void* ud) noexcept
            {
                auto* aa = static_cast<Surround714Args*>(ud);
                aa->voice->renderBlockSurround714(aa->outs, aa->n);
            };
            j.claimed.store(false, std::memory_order_release);
            j.done.store(false, std::memory_order_release);
            j.active.store(true, std::memory_order_release);
            ++numActive;
        }
        pool_->submit(numActive);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = slots_[static_cast<std::size_t>(v)];
            auto& j = pool_->job(v);
            if (!j.active.load(std::memory_order_acquire))
            {
                continue;
            }
            pool_->waitFor(v);
            for (int c = 0; c < 12; ++c)
            {
                const float* const src =
                    perVoiceScratch_[static_cast<std::size_t>(v)][static_cast<std::size_t>(c)].data();
                for (int i = 0; i < numSamples; ++i)
                {
                    outs[c][i] += src[i];
                }
            }
            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }
    else
    {
        for (auto& s : slots_)
        {
            const bool shouldRender = s.voice.isGated() || s.midiNote >= 0;
            if (!shouldRender)
            {
                continue;
            }
            s.voice.macros() = smoothedMacros_;
            s.voice.setUniformShape(uniformShape_);
            s.voice.setMidiCc1(midiCc1_);

            s.voice.renderBlockSurround714(buf, numSamples);
            for (int c = 0; c < 12; ++c)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    outs[c][i] += buf[c][i];
                }
            }

            if (!s.voice.isGated() && s.midiNote >= 0)
            {
                s.midiNote = -1;
            }
        }
    }

    auto busSoftClip = [](float v) noexcept { return v / (1.0f + std::fabs(v)); };
    for (int c = 0; c < 12; ++c)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            outs[c][i] = busSoftClip(outs[c][i]);
        }
    }
}

} // namespace sfs::engine
