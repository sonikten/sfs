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

#include <array>
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

    // ----- Editor (Phase 2 — substrate visualiser + auto-knob panel) ------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // GUI accessor — read-only handle to the engine for the visualiser.
    [[nodiscard]] sfs::engine::VoiceManager* voiceManager() noexcept { return voiceManager_.get(); }

    // Phase 4 GUI bindings: typed accessors so panels can wire JUCE
    // SliderParameterAttachment / ComboBoxParameterAttachment without
    // reaching into the param list by index. Pointers are non-owning;
    // JUCE owns the parameters once they're added in the constructor.
    [[nodiscard]] juce::AudioParameterFloat* tensionParam() noexcept { return tensionParam_; }
    [[nodiscard]] juce::AudioParameterFloat* dampingParam() noexcept { return dampingParam_; }
    [[nodiscard]] juce::AudioParameterFloat* densityParam() noexcept { return densityParam_; }
    [[nodiscard]] juce::AudioParameterFloat* migrationParam() noexcept { return migrationParam_; }
    [[nodiscard]] juce::AudioParameterFloat* coherenceParam() noexcept { return coherenceParam_; }
    [[nodiscard]] juce::AudioParameterFloat* excitationParam() noexcept { return excitationParam_; }
    [[nodiscard]] juce::AudioParameterChoice* topologyParam() noexcept { return topologyParam_; }
    [[nodiscard]] juce::AudioParameterChoice* shapeParam() noexcept { return shapeParam_; }

    // ----- Identity -------------------------------------------------------
    const juce::String getName() const override { return "SFS (Phase 2)"; }

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

    // ----- State (raw-parameter serialisation; full preset format at P4) --
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
    juce::AudioParameterChoice* shapeParam_ = nullptr;    // Phase 2 uniform agent shape
    juce::AudioParameterChoice* topologyParam_ = nullptr; // Phase 3 substrate topology

    // Amp ADSR.
    juce::AudioParameterFloat* attackMsParam_ = nullptr;
    juce::AudioParameterFloat* decayMsParam_ = nullptr;
    juce::AudioParameterFloat* sustainLevelParam_ = nullptr;
    juce::AudioParameterFloat* releaseMsParam_ = nullptr;

    // 4 LFOs × {rate Hz, shape} (Phase 2 §9 step 9).
    static constexpr int kLfoCount = 4;
    std::array<juce::AudioParameterFloat*, kLfoCount> lfoRateParams_{};
    std::array<juce::AudioParameterChoice*, kLfoCount> lfoShapeParams_{};

    // Mod matrix depths for the 4 active default slots — exposed so the
    // user can dial them in from the host before the preset format lands.
    juce::AudioParameterFloat* slot0DepthParam_ = nullptr; // CC1 → MIGRATION
    juce::AudioParameterFloat* slot1DepthParam_ = nullptr; // LFO1 → TENSION
    juce::AudioParameterFloat* slot2DepthParam_ = nullptr; // LFO2 → COHERENCE
    juce::AudioParameterFloat* slot3DepthParam_ = nullptr; // KeyVel → EXCITATION

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SfsAudioProcessor)
};

} // namespace sfs::plugin
