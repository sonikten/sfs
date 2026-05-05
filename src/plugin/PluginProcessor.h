// src/plugin/PluginProcessor.h
//
// Phase 1: replaces the Phase 0 test-tone with a single Voice (substrate +
// agent pool + harvester). Still mono internally; stereo output duplicates
// the same harvester read across both channels. Polyphony arrives in P2.
//
// Hard invariants enforced here:
//   * juce::ScopedNoDenormals at the top of processBlock sets FTZ/DAZ.
//   * No std::sin / std::cos / std::sinf / std::cosf — all transcendentals
//     in the audio path go through sfs::dsp::dm_*.
//   * No juce::AudioProcessorValueTreeState. Parameters (when added in
//     Phase 2+) will be raw juce::AudioProcessorParameter objects fed by
//     the SPSC ring buffer per sfs-spec/01_engine_architecture.md §4.4.

#pragma once

#include "engine/voice_manager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace sfs::plugin
{

class SfsAudioProcessor final : public juce::AudioProcessor
{
public:
    SfsAudioProcessor();
    ~SfsAudioProcessor() override = default;

    // ----- AudioProcessor lifecycle ---------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // ----- Editor (none in Phase 1; arrives at Phase 4) -------------------
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    // ----- Identity -------------------------------------------------------
    const juce::String getName() const override { return "SFS (Phase 1)"; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 5.0; } // substrate decay tail

    // ----- Programs (placeholder) -----------------------------------------
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // ----- State (no-op until preset format lands at P4) ------------------
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    static constexpr int kSubstrateCells = 1024;
    static constexpr int kAgentCount = 16;

    // Owned by prepareToPlay so we can size to the host sample rate. Lives
    // for the duration of [setActive(true), setActive(false)]. Allocations
    // happen here (block-rate context), NOT inside processBlock.
    std::unique_ptr<sfs::engine::VoiceManager> voiceManager_;

    // Six host-automatable macro parameters (sfs-spec/05 §2). JUCE owns
    // them once addParameter() is called; we keep raw pointers for fast
    // get() reads inside processBlock. Deliberately NOT using
    // AudioProcessorValueTreeState (sfs-spec/08 §5: its locking model
    // breaks the audio-thread determinism contract).
    juce::AudioParameterFloat* tensionParam_ = nullptr;
    juce::AudioParameterFloat* dampingParam_ = nullptr;
    juce::AudioParameterFloat* densityParam_ = nullptr;
    juce::AudioParameterFloat* migrationParam_ = nullptr;
    juce::AudioParameterFloat* coherenceParam_ = nullptr;
    juce::AudioParameterFloat* excitationParam_ = nullptr;
    juce::AudioParameterChoice* shapeParam_ = nullptr; // Phase 2 uniform agent shape

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SfsAudioProcessor)
};

} // namespace sfs::plugin
