// src/plugin/Footer.h
//
// Phase 4 §B15 — footer status strip per sfs-spec/07 §8.
//
// Shows live readouts for voice count and latency. CPU% measurement
// needs render-block timing infra (Phase 5 task); for v1 we surface
// the cheap-to-compute fields and document the placeholder.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sfs::plugin
{

class SfsAudioProcessor;

class Footer final : public juce::Component, private juce::Timer
{
public:
    explicit Footer(SfsAudioProcessor& processor);
    ~Footer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    SfsAudioProcessor& processor_;
    juce::Label voicesLabel_;
    juce::Label latencyLabel_;
    juce::Label topologyReadout_;
};

} // namespace sfs::plugin
