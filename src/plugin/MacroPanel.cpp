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
            // Quantize to 10 detents (1..10 visually). The attachment maps slider
            // value linearly to the parameter's normalised range, so 10 even
            // steps give 10 musical positions across the parameter's full range.
            const auto range = macroSliders_[idx].getRange();
            macroSliders_[idx].setRange(range.getStart(), range.getEnd(), (range.getEnd() - range.getStart()) / 9.0);
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
    // No numerical text box — knob position alone is the value indicator.
    // Code maps the rotary position to the underlying parameter's range.
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(140, 200, 220));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(45, 55, 70));
    s.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 230, 240));
    // Linear drag — with 10 detents, velocity-based mode caused slow drags
    // to register as zero motion and fast drags to overshoot. ~150 px maps
    // the full rotation range, so each detent steps after ~15 px of drag.
    s.setVelocityBasedMode(false);
    s.setMouseDragSensitivity(150);
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

    // Bottom row reserved for the two dropdowns. Knob row gets the rest.
    constexpr int kBottomRowHeight = 28;
    auto bottomRow = bounds.removeFromBottom(kBottomRowHeight);
    bounds.removeFromBottom(6);
    auto& knobRow = bounds;

    // Six uniformly-sized knob cells. Each cell: 16 px label on top + square
    // knob filling the rest. Knobs are square (min of width / height-after-
    // label) so they're visually consistent across panels.
    const int cellW = knobRow.getWidth() / kNumMacros;
    constexpr int kLabelH = 16;
    for (int i = 0; i < kNumMacros; ++i)
    {
        auto cell = knobRow.removeFromLeft(cellW);
        const auto idx = static_cast<std::size_t>(i);
        macroLabels_[idx].setBounds(cell.removeFromTop(kLabelH));
        // Square knob centred in the cell.
        const int side = std::min(cell.getWidth(), cell.getHeight()) - 4;
        const int x = cell.getX() + (cell.getWidth() - side) / 2;
        const int y = cell.getY() + (cell.getHeight() - side) / 2;
        macroSliders_[idx].setBounds(x, y, side, side);
    }

    // Bottom row: two narrow dropdowns + their labels. Each dropdown gets a
    // fixed 130 px width — wider stretches looked cluttered next to the
    // knobs.
    constexpr int kLabelW = 80;
    constexpr int kComboW = 130;
    auto leftHalf = bottomRow.removeFromLeft(bottomRow.getWidth() / 2);
    topologyLabel_.setBounds(leftHalf.removeFromLeft(kLabelW));
    topologyBox_.setBounds(leftHalf.removeFromLeft(kComboW));

    shapeLabel_.setBounds(bottomRow.removeFromLeft(kLabelW));
    shapeBox_.setBounds(bottomRow.removeFromLeft(kComboW));
}

} // namespace sfs::plugin
