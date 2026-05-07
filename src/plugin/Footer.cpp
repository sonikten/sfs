// src/plugin/Footer.cpp

#include "Footer.h"

#include "PluginProcessor.h"

#include "engine/voice_manager.h"

namespace sfs::plugin
{

namespace
{

void styleStatusLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::Font(juce::FontOptions(11.0f)));
    l.setColour(juce::Label::textColourId, juce::Colour::fromRGB(140, 160, 180));
    l.setJustificationType(juce::Justification::centredLeft);
}

} // namespace

Footer::Footer(SfsAudioProcessor& processor) : processor_(processor)
{
    styleStatusLabel(voicesLabel_, "Voices  0/8");
    styleStatusLabel(latencyLabel_, "Latency  0 samples");
    styleStatusLabel(topologyReadout_, "Topology  Ring 1D");
    addAndMakeVisible(voicesLabel_);
    addAndMakeVisible(latencyLabel_);
    addAndMakeVisible(topologyReadout_);
    startTimerHz(8);
}

Footer::~Footer() = default;

void Footer::timerCallback()
{
    if (auto* vm = processor_.voiceManager())
    {
        const int n = vm->activeVoiceCount();
        voicesLabel_.setText(juce::String("Voices  ") + juce::String(n) + "/" +
                                 juce::String(sfs::engine::VoiceManager::kMaxVoices),
                             juce::dontSendNotification);
        const auto t = vm->topology();
        topologyReadout_.setText(juce::String("Topology  ") +
                                     ((t == sfs::engine::Topology::Torus2D) ? "Torus 2D" : "Ring 1D"),
                                 juce::dontSendNotification);
    }
    latencyLabel_.setText(juce::String("Latency  ") + juce::String(processor_.getLatencySamples()) + " samples",
                          juce::dontSendNotification);
}

void Footer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(15, 18, 24));
    g.setColour(juce::Colour::fromRGB(45, 55, 70));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void Footer::resized()
{
    auto area = getLocalBounds().reduced(8, 4);
    if (area.getWidth() <= 0)
    {
        return;
    }
    const int third = area.getWidth() / 3;
    voicesLabel_.setBounds(area.removeFromLeft(third));
    latencyLabel_.setBounds(area.removeFromLeft(third));
    topologyReadout_.setBounds(area);
}

} // namespace sfs::plugin
