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
            quantizeToTenSteps(adsrSliders_[idx]);
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
            quantizeToTenSteps(lfoRateSliders_[idx]);
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
            quantizeToTenSteps(matrixSliders_[idx]);
        }
    }
}

void AdvancedPanel::quantizeToTenSteps(juce::Slider& s) // NOLINT(misc-use-anonymous-namespace)
{
    // 10 detents across the parameter's range. The user wants knobs that step
    // 1..10 musically; the parameter still gets the underlying continuous
    // value derived from those 10 evenly-spaced positions.
    const auto range = s.getRange();
    const double span = range.getEnd() - range.getStart();
    if (span > 0.0)
    {
        s.setRange(range.getStart(), range.getEnd(), span / 9.0);
    }
}

void AdvancedPanel::styleKnob(juce::Slider& s) // NOLINT(misc-use-anonymous-namespace)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    // No numerical text box — knob position alone is the value indicator.
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(140, 200, 220));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(45, 55, 70));
    s.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 230, 240));
    // Linear drag — see MacroPanel::styleKnob for rationale (velocity-based
    // mode + 10 detents = unpredictable interaction).
    s.setVelocityBasedMode(false);
    s.setMouseDragSensitivity(150);
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
    constexpr int kSectionHeaderH = 14;
    constexpr int kLabelH = 14;
    constexpr int kRowGap = 4;
    constexpr int kComboH = 24;
    constexpr int kComboMaxW = 130;

    auto adsrRow = bounds.removeFromTop(bounds.getHeight() / 3);
    bounds.removeFromTop(kRowGap);
    auto lfoRow = bounds.removeFromTop(bounds.getHeight() / 2);
    bounds.removeFromTop(kRowGap);
    auto matrixRow = bounds;

    // Lay out a row of N square knobs, each in its own cell. Knobs are
    // square (min of width / available height) so they're visually
    // consistent across panels — same logic as MacroPanel.
    auto layoutKnobRow =
        [](juce::Rectangle<int> row, juce::Label& sectionLabel, juce::Label* perKnobLabels, juce::Slider* knobs, int n)
    {
        sectionLabel.setBounds(row.removeFromTop(kSectionHeaderH));
        const int cellW = row.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = row.removeFromLeft(cellW);
            perKnobLabels[i].setBounds(cell.removeFromTop(kLabelH));
            const int side = std::min(cell.getWidth(), cell.getHeight()) - 4;
            const int x = cell.getX() + (cell.getWidth() - side) / 2;
            const int y = cell.getY() + (cell.getHeight() - side) / 2;
            knobs[i].setBounds(x, y, side, side);
        }
    };

    layoutKnobRow(adsrRow, adsrSectionLabel_, adsrLabels_.data(), adsrSliders_.data(), kAdsrCount);
    layoutKnobRow(matrixRow, matrixSectionLabel_, matrixLabels_.data(), matrixSliders_.data(), kMatrixCount);

    // ----- LFO row: knob + dropdown per column -------------------------------
    // Each column has: label (top) → square knob (middle) → narrow combo
    // (bottom, capped at kComboMaxW so the dropdowns don't stretch across
    // the column).
    {
        lfoSectionLabel_.setBounds(lfoRow.removeFromTop(kSectionHeaderH));
        const int colW = lfoRow.getWidth() / kLfoCount;
        for (int i = 0; i < kLfoCount; ++i)
        {
            auto cell = lfoRow.removeFromLeft(colW);
            const auto idx = static_cast<std::size_t>(i);
            lfoRateLabels_[idx].setBounds(cell.removeFromTop(kLabelH));
            auto comboArea = cell.removeFromBottom(kComboH);
            const int comboW = std::min(kComboMaxW, comboArea.getWidth() - 8);
            const int comboX = comboArea.getX() + (comboArea.getWidth() - comboW) / 2;
            lfoShapeBoxes_[idx].setBounds(comboX, comboArea.getY(), comboW, comboArea.getHeight());
            // Knob fills the remaining cell, square-centred.
            const int side = std::min(cell.getWidth(), cell.getHeight()) - 4;
            const int x = cell.getX() + (cell.getWidth() - side) / 2;
            const int y = cell.getY() + (cell.getHeight() - side) / 2;
            lfoRateSliders_[idx].setBounds(x, y, side, side);
        }
    }
}

} // namespace sfs::plugin
