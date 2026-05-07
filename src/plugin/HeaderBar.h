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

    SfsAudioProcessor& processor_;
    juce::Label productLabel_;
    juce::Label presetLabel_;
    juce::TextButton browseButton_{"Load..."};
    juce::TextButton saveButton_{"Save As..."};
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

} // namespace sfs::plugin
