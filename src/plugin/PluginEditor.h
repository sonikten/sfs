// src/plugin/PluginEditor.h
//
// Phase 2 plug-in GUI. Two pieces:
//   1. Substrate visualiser — top half of the window. Live waveform of
//      the youngest active voice's substrate state (1024 samples), read
//      from a Timer at 30 Hz via VoiceManager::snapshotPrimaryVoiceSubstrate.
//   2. Auto-generated knob panel — bottom half. Wraps JUCE's
//      GenericAudioProcessorEditor so every host parameter (currently 23)
//      shows up as a slider/dropdown without per-control wiring.
//
// The full curated knob layout from sfs-spec/07 lands in Phase 4 alongside
// the preset browser; this Phase 2 editor is the meta-plan's risk-mitigation
// "Phase 1 visualiser as standalone diagnostic" deliverable, expanded to
// cover the whole control surface.

#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace sfs::plugin
{

class SubstrateView final : public juce::Component, private juce::Timer
{
public:
    explicit SubstrateView(SfsAudioProcessor& processor);
    ~SubstrateView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    void timerCallback() override;

    SfsAudioProcessor& processor_;
    static constexpr int kCells = 1024;
    std::array<float, kCells> snapshot_{};
    float displayScale_ = 1.0f; // auto-normalising peak
    bool hasSignal_ = false;
};

class SfsEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SfsEditor(SfsAudioProcessor& processor);
    ~SfsEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SubstrateView substrateView_;
    juce::GenericAudioProcessorEditor knobs_;
};

} // namespace sfs::plugin
