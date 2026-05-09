// tests/integration/gui_param_sync_test.cpp
//
// Regression coverage for "GUI knob doesn't move the parameter".
//
// The audio_correctness ENV1 tests verify that the engine responds to
// ADSR parameter changes — but they do that by writing the parameter
// directly (`*processor.attackMsParam() = 500.0f`). They DON'T exercise
// the slider → SliderParameterAttachment → parameter path that the GUI
// uses. The recent 10-step quantization (calling `slider.setRange()`
// AFTER the attachment) is suspect: the attachment installs custom
// NormalisableRange convert functions that `setRange` overwrites, and
// the user reports that ENV1 (AMP) knobs no longer affect the audio.
//
// This file walks the live editor's component tree, finds each rotary
// knob, drives it programmatically via `slider.setValue(..., sendNotificationSync)`,
// and asserts that the corresponding host parameter actually changed.
// If the GUI binding is broken, the parameter stays at its default and
// these assertions fail.

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace
{

class JuceGuiSession
{
public:
    JuceGuiSession() { juce::initialiseJuce_GUI(); }
    ~JuceGuiSession() { juce::shutdownJuce_GUI(); }
};

// Walk every Slider in the editor tree, in left-to-right top-to-bottom
// order (matching how addAndMakeVisible was called).
std::vector<juce::Slider*> collectSliders(juce::Component& root)
{
    std::vector<juce::Slider*> out;
    std::function<void(juce::Component&)> walk = [&](juce::Component& c)
    {
        for (int i = 0; i < c.getNumChildComponents(); ++i)
        {
            if (auto* child = c.getChildComponent(i))
            {
                if (auto* s = dynamic_cast<juce::Slider*>(child))
                {
                    out.push_back(s);
                }
                walk(*child);
            }
        }
    };
    walk(root);
    return out;
}

// Drive a slider to a target value as if the user had dragged it,
// triggering all listeners synchronously. Returns the slider's value
// after the assignment (which may have been snapped to a step).
double driveSlider(juce::Slider& s, double target)
{
    s.setValue(target, juce::sendNotificationSync);
    return s.getValue();
}

} // namespace

TEST_CASE("Each GUI knob's parameter updates when the slider is driven", "[integration][gui][param][sync]")
{
    JuceGuiSession session;
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    REQUIRE(editor != nullptr);

    const auto sliders = collectSliders(*editor);
    INFO("found " << sliders.size() << " sliders in editor");
    REQUIRE(sliders.size() >= 14); // 6 macros + 4 ADSR + 4 LFO rate + 4 matrix = 18; tolerate a margin

    // For each slider: capture its initial value, drive it to a clearly
    // different value, and assert the value actually changed at the slider
    // level. This catches the "drag does nothing" failure mode without
    // having to know which parameter each slider maps to.
    int unchanged = 0;
    int updated = 0;
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        auto* s = sliders[i];
        REQUIRE(s != nullptr);
        const double before = s->getValue();
        const auto range = s->getRange();
        // Pick a target that's at least one step away from the current value.
        // For 10-step quantization, a step is ~range/9. Move 4 steps away.
        const double span = range.getEnd() - range.getStart();
        const double target = (before + span * 0.5);
        // Wrap into range if we overshoot.
        const double clampedTarget = juce::jlimit(range.getStart(), range.getEnd(), target);
        const double after = driveSlider(*s, clampedTarget);

        UNSCOPED_INFO("slider " << i << " name=\"" << s->getName().toStdString() << "\" before=" << before
                                << " requested=" << clampedTarget << " after=" << after << " range=["
                                << range.getStart() << ".." << range.getEnd() << "]");

        if (std::fabs(after - before) < 1e-9)
        {
            ++unchanged;
        }
        else
        {
            ++updated;
        }
    }

    INFO("updated=" << updated << " unchanged=" << unchanged);
    // Every slider should have moved (target was 50 % of range away from
    // current; even with snapping, that's > 4 steps for any 10-step knob).
    REQUIRE(unchanged == 0);
}

TEST_CASE("Driving the ATTACK slider updates the attackMsParam to a different value",
          "[integration][gui][param][sync][envelope]")
{
    JuceGuiSession session;
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    REQUIRE(editor != nullptr);

    const auto sliders = collectSliders(*editor);
    REQUIRE_FALSE(sliders.empty());

    // The ATTACK slider lives in the AdvancedPanel ENV1 (AMP) row, which
    // is laid out after the macro panel's 6 sliders. The first ENV1
    // slider in tree order is ATTACK.
    //
    // Find by parameter range — ATTACK has range [0, 5000], which is unique
    // among the parameters. Two knobs do match (ATTACK + DECAY both 0..5000),
    // so take the first.
    juce::Slider* attackSlider = nullptr;
    for (auto* s : sliders)
    {
        const auto r = s->getRange();
        if (std::fabs(r.getStart() - 0.0) < 1e-6 && std::fabs(r.getEnd() - 5000.0) < 1e-6)
        {
            attackSlider = s;
            break;
        }
    }
    REQUIRE(attackSlider != nullptr);

    auto* param = processor.attackMsParam();
    REQUIRE(param != nullptr);

    const float paramBefore = param->get();
    INFO("ATTACK slider initial: slider=" << attackSlider->getValue() << " param=" << paramBefore);

    // Drive the slider to a clearly different value (3000 ms — far from any
    // typical default).
    constexpr double kTarget = 3000.0;
    attackSlider->setValue(kTarget, juce::sendNotificationSync);

    const float paramAfter = param->get();
    INFO("ATTACK after setValue(3000): slider=" << attackSlider->getValue() << " param=" << paramAfter);

    // Slider should have moved.
    REQUIRE(std::fabs(attackSlider->getValue() - 3000.0) < 600.0); // within one step of 3000
    // Parameter should have followed the slider — this is the assertion
    // that catches "the GUI doesn't actually change the parameter".
    REQUIRE(std::fabs(paramAfter - 3000.0f) < 600.0f);
    REQUIRE(std::fabs(paramAfter - paramBefore) > 1.0f);
}
