// src/plugin/MacroPanel.h
//
// Phase 4 §B13 — curated macro knob panel per sfs-spec/07 §5.
//
// Six rotary sliders for the primary macros (TENSION, DAMPING, DENSITY,
// MIGRATION, COHERENCE, EXCITATION) plus a topology dropdown and a
// shape dropdown. Each control is wired to its host AudioParameter via
// JUCE's SliderParameterAttachment / ComboBoxParameterAttachment — the
// recommended raw-parameter binding (NOT AudioProcessorValueTreeState,
// which CLAUDE.md forbids in src/engine due to its locking model).
//
// The full Doc 07 §5 spec calls for ASPECT and richer click behaviour
// (double-click reset, ctrl-drag fine, etc.). Phase 4 ships the basic
// layout; the Phase 5 polish pass adds those interactions.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace sfs::plugin
{

class SfsAudioProcessor;

class MacroPanel final : public juce::Component
{
public:
    explicit MacroPanel(SfsAudioProcessor& processor);
    ~MacroPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kNumMacros = 6;

    void styleKnob(juce::Slider& s);

    // 6 macro knobs + their labels.
    std::array<juce::Slider, kNumMacros> macroSliders_;
    std::array<juce::Label, kNumMacros> macroLabels_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kNumMacros> macroAttachments_;

    // Topology + shape dropdowns + their labels.
    juce::ComboBox topologyBox_;
    juce::Label topologyLabel_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> topologyAttachment_;

    juce::ComboBox shapeBox_;
    juce::Label shapeLabel_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> shapeAttachment_;
};

} // namespace sfs::plugin
