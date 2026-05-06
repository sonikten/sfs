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
    // Six host-automatable macros (sfs-spec/05 §2 + §3). Defaults match
    // sfs-spec/09 §3.1.
    auto add = [&](juce::String id, juce::String name, float defaultValue)
    {
        auto* p = new juce::AudioParameterFloat(juce::ParameterID(std::move(id), 1),
                                                std::move(name),
                                                juce::NormalisableRange<float>{0.0f, 1.0f, 0.0001f},
                                                defaultValue);
        addParameter(p);
        return p;
    };
    tensionParam_ = add("tension", "TENSION", 0.5f);
    dampingParam_ = add("damping", "DAMPING", 0.3f);
    densityParam_ = add("density", "DENSITY", 0.6f);
    migrationParam_ = add("migration", "MIGRATION", 0.2f);
    coherenceParam_ = add("coherence", "COHERENCE", 0.8f);
    excitationParam_ = add("excitation", "EXCITATION", 0.3f);

    // Phase 2 uniform-shape selector: all agents in all voices use this
    // waveform. Phase 3 replaces with per-agent shape draws from
    // shape_distribution (sfs-spec/09 §3.3).
    shapeParam_ = new juce::AudioParameterChoice(juce::ParameterID("shape", 1),
                                                 "SHAPE",
                                                 juce::StringArray{"Sine", "Saw", "Square", "FmPair", "Noise"},
                                                 0); // default Sine
    addParameter(shapeParam_);
}

void SfsAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    voiceManager_ = std::make_unique<sfs::engine::VoiceManager>(kSubstrateCells,
                                                                kAgentCount,
                                                                static_cast<float>(sampleRate));
}

void SfsAudioProcessor::releaseResources()
{
    voiceManager_.reset();
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

    if (voiceManager_ == nullptr || numChannels == 0 || numSamples == 0)
    {
        return;
    }

    // Block-rate macro update: read host parameter snapshots into the
    // VoiceManager's MacroValues; per-voice copies happen inside
    // VoiceManager::renderBlockStereo. Spec §5.7 sample-accurate
    // automation lands as a Phase 2 follow-up.
    auto& macros = voiceManager_->macros();
    macros.tension = tensionParam_->get();
    macros.damping = dampingParam_->get();
    macros.density = densityParam_->get();
    macros.migration = migrationParam_->get();
    macros.coherence = coherenceParam_->get();
    macros.excitation = excitationParam_->get();

    // MIDI dispatch — VoiceManager handles allocation + stealing.
    // Phase 2 simplification: events apply at block boundaries (5 ms
    // granularity at 256-sample blocks @ 48 kHz). Sample-accurate
    // dispatch lands as part of step 12.
    for (const auto meta : midiMessages)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            voiceManager_->noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            voiceManager_->noteOff(msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voiceManager_->allNotesOff();
        }
        else if (msg.isController() && msg.getControllerNumber() == 1)
        {
            // Mod wheel (CC1) — fed into the mod matrix as a Phase 2
            // source. Normalised to [0, 1] from the MIDI 0..127 byte.
            voiceManager_->setMidiCc1(static_cast<float>(msg.getControllerValue()) / 127.0f);
        }
    }

    // Stereo render: two harvesters at substrate positions 0 and N/2 give
    // inter-channel decorrelation from the substrate's wave propagation
    // between them (sfs-spec/04 §3.2). Mono fallback duplicates L.
    if (numChannels >= 2)
    {
        auto* const outL = buffer.getWritePointer(0);
        auto* const outR = buffer.getWritePointer(1);
        voiceManager_->renderBlockStereo(outL, outR, numSamples);
        for (int ch = 2; ch < numChannels; ++ch)
        {
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
        }
    }
    else
    {
        // No mono path on the manager (Phase 2 stereo-default); render
        // stereo and downmix.
        std::vector<float> tmpR(static_cast<std::size_t>(numSamples), 0.0f);
        auto* const left = buffer.getWritePointer(0);
        voiceManager_->renderBlockStereo(left, tmpR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = 0.5f * (left[i] + tmpR[static_cast<std::size_t>(i)]);
        }
    }
}

} // namespace sfs::plugin
// (createPluginFilter() lives in PluginEntry.cpp so this TU can be linked
// into the headless render rig without dragging the VST3 wrapper symbols.)
