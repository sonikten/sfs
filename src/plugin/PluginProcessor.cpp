// src/plugin/PluginProcessor.cpp
//
// Phase 0 skeleton implementation. Quiet, always-on 440 Hz tone driven by
// the polynomial dm_sin so the cross-platform hash test (P0.8) has a stable
// signal to verify against.

#include "PluginProcessor.h"

#include "dsp/dm_sin.h"

namespace sfs::plugin
{

namespace
{

constexpr float kTwoPi = 6.2831853f;

} // namespace

SfsAudioProcessor::SfsAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void SfsAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    phaseInc_ = kTestToneFrequencyHz * kTwoPi / static_cast<float>(sampleRate);
    phase_ = 0.0f;
}

void SfsAudioProcessor::releaseResources()
{
    // Nothing to free in the Phase 0 skeleton.
}

bool SfsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SfsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    // Set FTZ/DAZ for the duration of this block. Mandatory per the
    // determinism contract (CLAUDE.md "Hard invariants" + sfs-spec/01 §9).
    const juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    // Clear any host-supplied input that may have been mistakenly routed in
    // (we're a synth; ignore inputs).
    for (int ch = getTotalNumInputChannels(); ch < numChannels; ++ch)
    {
        buffer.clear(ch, 0, numSamples);
    }

    // Generate the test tone once, then copy across channels. Single-source
    // generation keeps the hash test trivially mono-channel-correlated.
    if (numChannels == 0 || numSamples == 0)
    {
        return;
    }

    auto* const left = buffer.getWritePointer(0);
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = kTestToneAmplitude * sfs::dsp::dm_sin(phase_);
        phase_ += phaseInc_;
        if (phase_ >= kTwoPi)
        {
            phase_ -= kTwoPi;
        }
    }

    for (int ch = 1; ch < numChannels; ++ch)
    {
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }
}

} // namespace sfs::plugin
// (createPluginFilter() lives in PluginEntry.cpp so this TU can be linked
// into the headless render rig without dragging the VST3 wrapper symbols.)
