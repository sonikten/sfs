// src/plugin/HeaderBar.h
//
// Phase 4 §B16 — header strip per sfs-spec/07 §3.
//
// Layout: [SFS logo/name] [preset name display] [Browse] [Save As] ...
// Doc 07 §3 calls for prev/next arrows and a dice randomiser too — those
// land alongside the preset browser (B20). v1 here ships the working
// load/save loop via juce::FileChooser dialogs so the plug-in is
// musically usable from the GUI.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace sfs::plugin
{

class SfsAudioProcessor;

class HeaderBar final : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar(SfsAudioProcessor& processor);
    ~HeaderBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void onBrowse();
    void onSaveAs();

    void onPresetMenu();
    void rescanFactory();

    SfsAudioProcessor& processor_;
    juce::Label productLabel_;
    juce::TextButton presetButton_{"Init"}; // shows current preset name; click → factory popup
    juce::TextButton browseButton_{"Load..."};
    juce::TextButton saveButton_{"Save As..."};
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // Factory preset index: relative paths grouped by category (folder
    // name under presets/factory/). Built at first popup open + cached
    // for subsequent opens.
    struct FactoryEntry
    {
        juce::String displayName;
        juce::File path;
    };
    juce::HashMap<juce::String, juce::Array<FactoryEntry>> factoryByCategory_;
    bool factoryScanned_ = false;
};

} // namespace sfs::plugin
