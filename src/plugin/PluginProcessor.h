// src/plugin/PluginProcessor.h
//
// Phase 0 skeleton AudioProcessor: emits a continuous 440 Hz sine via the
// polynomial dm_sin so the toolchain end-to-end (CMake + JUCE 8 + dm_sin +
// VST3 wrapper) is exercised. No DSP engine, no presets, no GUI. The whole
// purpose is to prove the build, the plug-in load, and the cross-platform
// determinism contract before any synthesis code lands.
//
// Hard invariants enforced here:
//   * juce::ScopedNoDenormals at the top of processBlock sets FTZ/DAZ.
//   * The sine is generated via sfs::dsp::dm_sin (NOT std::sin / std::sinf).
//   * No juce::AudioProcessorValueTreeState. Parameters (when added in
//     Phase 1+) will be raw juce::AudioProcessorParameter objects fed by
//     the SPSC ring buffer per sfs-spec/01_engine_architecture.md §4.4.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sfs::plugin
{

class SfsAudioProcessor final : public juce::AudioProcessor
{
public:
    SfsAudioProcessor();
    ~SfsAudioProcessor() override = default;

    // ----- AudioProcessor lifecycle ---------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ----- Editor (none in Phase 0) ---------------------------------------
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool                        hasEditor() const override { return false; }

    // ----- Identity --------------------------------------------------------
    const juce::String getName() const override { return "SFS (Phase 0)"; }

    bool acceptsMidi()  const override { return true;  }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    // ----- Programs (placeholder) -----------------------------------------
    int                getNumPrograms() override                              { return 1; }
    int                getCurrentProgram() override                           { return 0; }
    void               setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override                          { return {}; }
    void               changeProgramName (int, const juce::String&) override  {}

    // ----- State (no-op in Phase 0; preset format arrives in Phase 4) ------
    void getStateInformation (juce::MemoryBlock&)        override {}
    void setStateInformation (const void*, int)          override {}

private:
    static constexpr float kTestToneFrequencyHz = 440.0f;
    static constexpr float kTestToneAmplitude   = 0.05f;   // ~−26 dBFS, quiet on load

    double sampleRate_ = 48000.0;
    float  phase_      = 0.0f;
    float  phaseInc_   = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfsAudioProcessor)
};

} // namespace sfs::plugin
