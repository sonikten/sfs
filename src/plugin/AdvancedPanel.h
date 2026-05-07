// src/plugin/AdvancedPanel.h
//
// Phase 4 §B13b — curated "advanced parameters" panel that replaces the
// stop-gap juce::GenericAudioProcessorEditor used through Phase 2/3.
//
// Hosts every host-automatable parameter that isn't on the macro panel:
//   * Amp ADSR row     — 4 rotary knobs (A, D, S, R)
//   * LFO grid         — 4 columns × { rate rotary, shape combo }
//   * Mod matrix depths — 4 rotary knobs for the 4 active default slots
//
// Every Slider uses a rotary style (Doc 07 §5 — knobs, not strips).
// Every control is wired to its host AudioParameter via JUCE's
// SliderParameterAttachment / ComboBoxParameterAttachment (raw-parameter
// path, NOT AudioProcessorValueTreeState — CLAUDE.md hard invariant).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace sfs::plugin
{

class SfsAudioProcessor;

class AdvancedPanel final : public juce::Component
{
public:
    explicit AdvancedPanel(SfsAudioProcessor& processor);
    ~AdvancedPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kAdsrCount = 4;
    static constexpr int kLfoCount = 4;
    static constexpr int kMatrixCount = 4;

    static void styleKnob(juce::Slider& s);
    static void styleSectionLabel(juce::Label& l);
    static void styleControlLabel(juce::Label& l);

    juce::Label adsrSectionLabel_;
    std::array<juce::Slider, kAdsrCount> adsrSliders_;
    std::array<juce::Label, kAdsrCount> adsrLabels_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kAdsrCount> adsrAttachments_;

    juce::Label lfoSectionLabel_;
    std::array<juce::Slider, kLfoCount> lfoRateSliders_;
    std::array<juce::Label, kLfoCount> lfoRateLabels_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kLfoCount> lfoRateAttachments_;
    std::array<juce::ComboBox, kLfoCount> lfoShapeBoxes_;
    std::array<std::unique_ptr<juce::ComboBoxParameterAttachment>, kLfoCount> lfoShapeAttachments_;

    juce::Label matrixSectionLabel_;
    std::array<juce::Slider, kMatrixCount> matrixSliders_;
    std::array<juce::Label, kMatrixCount> matrixLabels_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kMatrixCount> matrixAttachments_;
};

} // namespace sfs::plugin
