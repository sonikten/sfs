// tests/integration/editor_layout_test.cpp
//
// Phase 4 GUI regression coverage. Asserts the SfsEditor and its child
// components stay within sensible bounds and use the right control
// types — catches "GUI takes up too much screen real estate" + "wrong
// control type" issues before they reach a user.
//
// Specifically:
//   * Default editor size fits within a 1280×800 viewport (Doc 07 §2
//     default size + reasonable safety margin).
//   * Every child Slider in the macro-panel area is rotary, not
//     horizontal/vertical strip — Doc 07 §5 calls for knobs.
//   * No child component extends outside the editor bounds (off-screen
//     means inaccessible).
//
// JUCE GUI requires juce::initialiseJuce_GUI() / shutdownJuce_GUI() at
// process scope before any Component construction. We use Catch2's
// session-listener machinery to bracket the test process.

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

// Walk the component tree rooted at `root` and call visit(child) for each.
template <class Visitor> void walkChildren(juce::Component& root, Visitor&& visit)
{
    for (int i = 0; i < root.getNumChildComponents(); ++i)
    {
        if (auto* child = root.getChildComponent(i))
        {
            visit(*child);
            walkChildren(*child, visit);
        }
    }
}

} // namespace

TEST_CASE("Editor default size fits a typical DAW plug-in window", "[integration][editor][layout]")
{
    JuceGuiSession session;
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    REQUIRE(editor != nullptr);

    // Doc 07 §2: default 1280×800. Most DAW plug-in windows give the
    // host-controlled editor up to about that. Anything bigger forces
    // the user to scroll. Cap at 1280×800.
    const auto w = editor->getWidth();
    const auto h = editor->getHeight();
    INFO("default editor size: " << w << "×" << h);
    REQUIRE(w > 0);
    REQUIRE(h > 0);
    REQUIRE(w <= 1280);
    REQUIRE(h <= 800);
}

TEST_CASE("All child components fit within the editor bounds (no clipping)", "[integration][editor][layout]")
{
    JuceGuiSession session;
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    REQUIRE(editor != nullptr);

    // Force layout pass at default size.
    editor->setSize(editor->getWidth(), editor->getHeight());

    const auto editorBounds = editor->getLocalBounds();
    int outOfBounds = 0;
    walkChildren(*editor,
                 [&](juce::Component& child)
                 {
                     if (!child.isVisible() || child.getNumChildComponents() > 0)
                     {
                         // Container components are allowed to extend; we only
                         // check leaf components (Sliders, ComboBoxes, Buttons,
                         // Labels) for the off-screen case. Containers that
                         // properly clip their children pass automatically.
                         return;
                     }
                     // The child's bounds in editor coordinates.
                     const auto rect = child.getBoundsInParent();
                     auto* p = child.getParentComponent();
                     juce::Rectangle<int> editorRect = rect;
                     while (p != nullptr && p != editor.get())
                     {
                         editorRect += p->getPosition();
                         p = p->getParentComponent();
                     }
                     if (!editorBounds.contains(editorRect))
                     {
                         ++outOfBounds;
                         UNSCOPED_INFO("out-of-bounds child class="
                                       << typeid(child).name() << " bounds=" << editorRect.toString().toStdString()
                                       << " editor=" << editorBounds.toString().toStdString());
                     }
                 });
    REQUIRE(outOfBounds == 0);
}

TEST_CASE("All Sliders use rotary style", "[integration][editor][controls]")
{
    JuceGuiSession session;
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    REQUIRE(editor != nullptr);

    int linearSliders = 0;
    walkChildren(*editor,
                 [&](juce::Component& child)
                 {
                     if (auto* slider = dynamic_cast<juce::Slider*>(&child))
                     {
                         const auto style = slider->getSliderStyle();
                         const bool rotary = (style == juce::Slider::Rotary) ||
                                             (style == juce::Slider::RotaryHorizontalDrag) ||
                                             (style == juce::Slider::RotaryVerticalDrag) ||
                                             (style == juce::Slider::RotaryHorizontalVerticalDrag);
                         if (!rotary)
                         {
                             ++linearSliders;
                             UNSCOPED_INFO("non-rotary slider: name=" << slider->getName().toStdString()
                                                                      << " style=" << static_cast<int>(style));
                         }
                     }
                 });
    REQUIRE(linearSliders == 0);
}
