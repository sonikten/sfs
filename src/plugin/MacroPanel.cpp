// src/plugin/MacroPanel.cpp

#include "MacroPanel.h"

#include "PluginProcessor.h"

namespace sfs::plugin
{

namespace
{

constexpr const char* kMacroNames[6] = {"TENSION", "DAMPING", "DENSITY", "MIGRATION", "COHERENCE", "EXCITATION"};

} // namespace

MacroPanel::MacroPanel(SfsAudioProcessor& processor)
{
    juce::AudioParameterFloat* macroParams[6] = {
        processor.tensionParam(),
        processor.dampingParam(),
        processor.densityParam(),
        processor.migrationParam(),
        processor.coherenceParam(),
        processor.excitationParam(),
    };

    for (int i = 0; i < kNumMacros; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        styleKnob(macroSliders_[idx]);
        addAndMakeVisible(macroSliders_[idx]);

        macroLabels_[idx].setText(kMacroNames[i], juce::dontSendNotification);
        macroLabels_[idx].setJustificationType(juce::Justification::centredTop);
        macroLabels_[idx].setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        macroLabels_[idx].setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 195, 215));
        addAndMakeVisible(macroLabels_[idx]);

        if (macroParams[i] != nullptr)
        {
            macroAttachments_[idx] = std::make_unique<juce::SliderParameterAttachment>(*macroParams[i],
                                                                                       macroSliders_[idx]);
        }
    }

    // Topology dropdown.
    if (auto* p = processor.topologyParam())
    {
        topologyBox_.addItemList(p->choices, 1);
        topologyBox_.setSelectedItemIndex(p->getIndex(), juce::dontSendNotification);
        addAndMakeVisible(topologyBox_);
        topologyLabel_.setText("TOPOLOGY", juce::dontSendNotification);
        topologyLabel_.setJustificationType(juce::Justification::centredLeft);
        topologyLabel_.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        topologyLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 195, 215));
        addAndMakeVisible(topologyLabel_);
        topologyAttachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(*p, topologyBox_);
    }

    // Shape dropdown.
    if (auto* p = processor.shapeParam())
    {
        shapeBox_.addItemList(p->choices, 1);
        shapeBox_.setSelectedItemIndex(p->getIndex(), juce::dontSendNotification);
        addAndMakeVisible(shapeBox_);
        shapeLabel_.setText("SHAPE", juce::dontSendNotification);
        shapeLabel_.setJustificationType(juce::Justification::centredLeft);
        shapeLabel_.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        shapeLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 195, 215));
        addAndMakeVisible(shapeLabel_);
        shapeAttachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(*p, shapeBox_);
    }
}

void MacroPanel::styleKnob(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    s.setRange(0.0, 1.0, 0.0001);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(140, 200, 220));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(45, 55, 70));
    s.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 230, 240));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(200, 210, 220));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB(60, 70, 85));
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB(20, 25, 32));
    s.setVelocityBasedMode(true);
    s.setMouseDragSensitivity(160);
}

void MacroPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(22, 26, 34));
    g.setColour(juce::Colour::fromRGB(45, 55, 70));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void MacroPanel::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
    {
        return;
    }

    // Top row: 6 macro knobs evenly spaced. Bottom row: topology + shape
    // dropdowns split horizontally.
    auto knobRow = bounds.removeFromTop(static_cast<int>(bounds.getHeight() * 0.72f));
    bounds.removeFromTop(8);
    auto bottomRow = bounds;

    const int knobW = knobRow.getWidth() / kNumMacros;
    for (int i = 0; i < kNumMacros; ++i)
    {
        auto cell = knobRow.removeFromLeft(knobW);
        const auto idx = static_cast<std::size_t>(i);
        macroLabels_[idx].setBounds(cell.removeFromTop(16));
        macroSliders_[idx].setBounds(cell.reduced(4));
    }

    // Bottom row: dropdowns + labels. Left half = topology, right half = shape.
    auto leftHalf = bottomRow.removeFromLeft(bottomRow.getWidth() / 2).reduced(2);
    auto rightHalf = bottomRow.reduced(2);

    topologyLabel_.setBounds(leftHalf.removeFromLeft(80));
    topologyBox_.setBounds(leftHalf);
    shapeLabel_.setBounds(rightHalf.removeFromLeft(70));
    shapeBox_.setBounds(rightHalf);
}

} // namespace sfs::plugin
