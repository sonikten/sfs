// src/plugin/AdvancedPanel.cpp

#include "AdvancedPanel.h"

#include "PluginProcessor.h"

namespace sfs::plugin
{

namespace
{

constexpr const char* kAdsrLabels[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};

constexpr const char* kMatrixLabels[4] = {
    "CC1>MIG",
    "LFO1>TEN",
    "LFO2>COH",
    "VEL>EXC",
};

const juce::Colour kPanelBg = juce::Colour::fromRGB(20, 22, 30);
const juce::Colour kSectionDivider = juce::Colour::fromRGB(40, 48, 60);
const juce::Colour kSectionText = juce::Colour::fromRGB(150, 170, 200);
const juce::Colour kControlText = juce::Colour::fromRGB(180, 195, 215);

} // namespace

AdvancedPanel::AdvancedPanel(SfsAudioProcessor& processor)
{
    // ----- ADSR section ------------------------------------------------------
    adsrSectionLabel_.setText("ENV1 (AMP)", juce::dontSendNotification);
    styleSectionLabel(adsrSectionLabel_);
    addAndMakeVisible(adsrSectionLabel_);

    juce::AudioParameterFloat* adsrParams[kAdsrCount] = {
        processor.attackMsParam(),
        processor.decayMsParam(),
        processor.sustainLevelParam(),
        processor.releaseMsParam(),
    };
    for (int i = 0; i < kAdsrCount; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        styleKnob(adsrSliders_[idx]);
        addAndMakeVisible(adsrSliders_[idx]);

        adsrLabels_[idx].setText(kAdsrLabels[i], juce::dontSendNotification);
        styleControlLabel(adsrLabels_[idx]);
        addAndMakeVisible(adsrLabels_[idx]);

        if (adsrParams[i] != nullptr)
        {
            adsrAttachments_[idx] = std::make_unique<juce::SliderParameterAttachment>(*adsrParams[i],
                                                                                      adsrSliders_[idx]);
        }
    }

    // ----- LFO section -------------------------------------------------------
    lfoSectionLabel_.setText("LFOs", juce::dontSendNotification);
    styleSectionLabel(lfoSectionLabel_);
    addAndMakeVisible(lfoSectionLabel_);

    for (int i = 0; i < kLfoCount; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        styleKnob(lfoRateSliders_[idx]);
        addAndMakeVisible(lfoRateSliders_[idx]);

        lfoRateLabels_[idx].setText("LFO" + juce::String(i + 1), juce::dontSendNotification);
        styleControlLabel(lfoRateLabels_[idx]);
        addAndMakeVisible(lfoRateLabels_[idx]);

        if (auto* p = processor.lfoRateParam(i))
        {
            lfoRateAttachments_[idx] = std::make_unique<juce::SliderParameterAttachment>(*p, lfoRateSliders_[idx]);
        }

        if (auto* sp = processor.lfoShapeParam(i))
        {
            lfoShapeBoxes_[idx].addItemList(sp->choices, 1);
            lfoShapeBoxes_[idx].setSelectedItemIndex(sp->getIndex(), juce::dontSendNotification);
            lfoShapeBoxes_[idx].setColour(juce::ComboBox::textColourId, kControlText);
            lfoShapeBoxes_[idx].setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(28, 32, 42));
            lfoShapeBoxes_[idx].setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(50, 60, 75));
            lfoShapeBoxes_[idx].setJustificationType(juce::Justification::centred);
            addAndMakeVisible(lfoShapeBoxes_[idx]);
            lfoShapeAttachments_[idx] = std::make_unique<juce::ComboBoxParameterAttachment>(*sp, lfoShapeBoxes_[idx]);
        }
    }

    // ----- Mod-matrix section -----------------------------------------------
    matrixSectionLabel_.setText("MATRIX", juce::dontSendNotification);
    styleSectionLabel(matrixSectionLabel_);
    addAndMakeVisible(matrixSectionLabel_);

    for (int i = 0; i < kMatrixCount; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        styleKnob(matrixSliders_[idx]);
        // Matrix depths are bipolar (-1..1) — visually centre-zero.
        matrixSliders_[idx].getProperties().set("centreZero", true);
        addAndMakeVisible(matrixSliders_[idx]);

        matrixLabels_[idx].setText(kMatrixLabels[i], juce::dontSendNotification);
        styleControlLabel(matrixLabels_[idx]);
        addAndMakeVisible(matrixLabels_[idx]);

        if (auto* p = processor.modSlotDepthParam(i))
        {
            matrixAttachments_[idx] = std::make_unique<juce::SliderParameterAttachment>(*p, matrixSliders_[idx]);
        }
    }
}

void AdvancedPanel::styleKnob(juce::Slider& s) // NOLINT(misc-use-anonymous-namespace)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(140, 200, 220));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(45, 55, 70));
    s.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 230, 240));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(200, 210, 220));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB(60, 70, 85));
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB(20, 25, 32));
    s.setVelocityBasedMode(true);
    s.setMouseDragSensitivity(140);
}

void AdvancedPanel::styleSectionLabel(juce::Label& l)
{
    l.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    l.setColour(juce::Label::textColourId, kSectionText);
    l.setJustificationType(juce::Justification::centredLeft);
}

void AdvancedPanel::styleControlLabel(juce::Label& l)
{
    l.setFont(juce::Font(juce::FontOptions(10.0f)));
    l.setColour(juce::Label::textColourId, kControlText);
    l.setJustificationType(juce::Justification::centredTop);
}

void AdvancedPanel::paint(juce::Graphics& g)
{
    g.fillAll(kPanelBg);
    g.setColour(kSectionDivider);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void AdvancedPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
    {
        return;
    }

    // Three vertically-stacked sub-rows: ADSR (top), LFOs (middle), Matrix (bottom).
    // Heights chosen so each sub-row fits a 14px section header + label row +
    // knob square comfortably without clipping at ≤ 360 px panel height.
    const int sectionHeaderH = 14;
    const int labelH = 14;
    const int rowGap = 4;

    auto adsrRow = bounds.removeFromTop(bounds.getHeight() / 3);
    bounds.removeFromTop(rowGap);
    auto lfoRow = bounds.removeFromTop(bounds.getHeight() / 2);
    bounds.removeFromTop(rowGap);
    auto matrixRow = bounds;

    // ----- ADSR row ----------------------------------------------------------
    {
        adsrSectionLabel_.setBounds(adsrRow.removeFromTop(sectionHeaderH));
        const int knobAreaW = adsrRow.getWidth() / kAdsrCount;
        for (int i = 0; i < kAdsrCount; ++i)
        {
            auto cell = adsrRow.removeFromLeft(knobAreaW);
            const auto idx = static_cast<std::size_t>(i);
            adsrLabels_[idx].setBounds(cell.removeFromTop(labelH));
            adsrSliders_[idx].setBounds(cell.reduced(2));
        }
    }

    // ----- LFO row -----------------------------------------------------------
    {
        lfoSectionLabel_.setBounds(lfoRow.removeFromTop(sectionHeaderH));
        const int colW = lfoRow.getWidth() / kLfoCount;
        for (int i = 0; i < kLfoCount; ++i)
        {
            auto cell = lfoRow.removeFromLeft(colW);
            const auto idx = static_cast<std::size_t>(i);
            lfoRateLabels_[idx].setBounds(cell.removeFromTop(labelH));
            // Reserve the bottom 22 px for the shape combo, knob takes the rest.
            const int comboH = 22;
            auto comboArea = cell.removeFromBottom(comboH);
            lfoShapeBoxes_[idx].setBounds(comboArea.reduced(4, 0));
            lfoRateSliders_[idx].setBounds(cell.reduced(2));
        }
    }

    // ----- Matrix row --------------------------------------------------------
    {
        matrixSectionLabel_.setBounds(matrixRow.removeFromTop(sectionHeaderH));
        const int knobAreaW = matrixRow.getWidth() / kMatrixCount;
        for (int i = 0; i < kMatrixCount; ++i)
        {
            auto cell = matrixRow.removeFromLeft(knobAreaW);
            const auto idx = static_cast<std::size_t>(i);
            matrixLabels_[idx].setBounds(cell.removeFromTop(labelH));
            matrixSliders_[idx].setBounds(cell.reduced(2));
        }
    }
}

} // namespace sfs::plugin
