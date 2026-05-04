// src/plugin/PluginProcessor.cpp
//
// Phase 1 implementation. Per audio sample: agents → substrate → harvester
// read → output. MIDI note-on/off drives the gate envelope.

#include "PluginProcessor.h"

namespace sfs::plugin
{

SfsAudioProcessor::SfsAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void SfsAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    voice_ = std::make_unique<sfs::engine::Voice>(kSubstrateCells, kAgentCount, static_cast<float>(sampleRate));
}

void SfsAudioProcessor::releaseResources()
{
    voice_.reset();
}

bool SfsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SfsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Set FTZ/DAZ for the duration of this block. Mandatory per the
    // determinism contract (CLAUDE.md "Hard invariants" + sfs-spec/01 §9).
    const juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    // Clear any host-supplied input that may have been mistakenly routed in.
    for (int ch = getTotalNumInputChannels(); ch < numChannels; ++ch)
    {
        buffer.clear(ch, 0, numSamples);
    }

    if (voice_ == nullptr || numChannels == 0 || numSamples == 0)
    {
        return;
    }

    // Phase 1 MIDI: take the LAST note-on/off in the block as the active gate.
    // Sample-accurate MIDI dispatch lands in Phase 2 along with the macro
    // automation pipeline; for now, gate state changes happen at block
    // boundaries which is audible only on extremely short blocks.
    for (const auto meta : midiMessages)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            voice_->noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            voice_->noteOff();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voice_->noteOff();
        }
    }

    // Render mono into channel 0 then duplicate.
    auto* const left = buffer.getWritePointer(0);
    voice_->renderBlock(left, numSamples);

    for (int ch = 1; ch < numChannels; ++ch)
    {
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }
}

} // namespace sfs::plugin
// (createPluginFilter() lives in PluginEntry.cpp so this TU can be linked
// into the headless render rig without dragging the VST3 wrapper symbols.)
