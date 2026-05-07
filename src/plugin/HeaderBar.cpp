// src/plugin/HeaderBar.cpp

#include "HeaderBar.h"

#include "PluginProcessor.h"

namespace sfs::plugin
{

namespace
{

[[nodiscard]] juce::File defaultPresetDir()
{
    // Probe a handful of candidate paths so the file chooser opens
    // somewhere sensible whether running from a build dir, the install
    // location, or an installed plugin host.
    juce::Array<juce::File> candidates{
        juce::File::getCurrentWorkingDirectory().getChildFile("presets/factory"),
        juce::File::getCurrentWorkingDirectory().getChildFile("../presets/factory"),
        juce::File::getCurrentWorkingDirectory().getChildFile("../../presets/factory"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("SFS Presets"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SFS Presets"),
    };
    for (const auto& c : candidates)
    {
        if (c.isDirectory())
        {
            return c;
        }
    }
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

} // namespace

HeaderBar::HeaderBar(SfsAudioProcessor& processor) : processor_(processor)
{
    productLabel_.setText("SFS", juce::dontSendNotification);
    productLabel_.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
    productLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 235, 245));
    addAndMakeVisible(productLabel_);

    presetLabel_.setText(processor_.currentPresetName(), juce::dontSendNotification);
    presetLabel_.setFont(juce::Font(juce::FontOptions(14.0f)));
    presetLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 195, 220));
    presetLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel_);

    browseButton_.onClick = [this] { onBrowse(); };
    addAndMakeVisible(browseButton_);
    saveButton_.onClick = [this] { onSaveAs(); };
    addAndMakeVisible(saveButton_);

    startTimerHz(4); // refresh preset name display when user-edited / loaded
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::timerCallback()
{
    const auto& name = processor_.currentPresetName();
    if (presetLabel_.getText() != name)
    {
        presetLabel_.setText(name, juce::dontSendNotification);
    }
}

void HeaderBar::onBrowse()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Load SFS preset", defaultPresetDir(), "*.sfs", true);
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(flags,
                              [this](const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File{})
                                  {
                                      return;
                                  }
                                  juce::String err;
                                  if (!processor_.loadPresetFromFile(file.getFullPathName(), err))
                                  {
                                      juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                                                       .withIconType(
                                                                           juce::MessageBoxIconType::WarningIcon)
                                                                       .withTitle("Preset load failed")
                                                                       .withMessage(err)
                                                                       .withButton("OK"),
                                                                   nullptr);
                                  }
                              });
}

void HeaderBar::onSaveAs()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Save SFS preset",
                                                       defaultPresetDir().getChildFile(processor_.currentPresetName() +
                                                                                       ".sfs"),
                                                       "*.sfs",
                                                       true);
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser_->launchAsync(flags,
                              [this](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File{})
                                  {
                                      return;
                                  }
                                  if (!file.hasFileExtension("sfs"))
                                  {
                                      file = file.withFileExtension("sfs");
                                  }
                                  juce::String err;
                                  if (!processor_.savePresetToFile(file.getFullPathName(),
                                                                   file.getFileNameWithoutExtension(),
                                                                   /*author*/ "User",
                                                                   /*category*/ "User",
                                                                   /*tags*/ {},
                                                                   /*description*/ "",
                                                                   err))
                                  {
                                      juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                                                       .withIconType(
                                                                           juce::MessageBoxIconType::WarningIcon)
                                                                       .withTitle("Preset save failed")
                                                                       .withMessage(err)
                                                                       .withButton("OK"),
                                                                   nullptr);
                                  }
                              });
}

void HeaderBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(15, 18, 24));
    g.setColour(juce::Colour::fromRGB(45, 55, 70));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced(8, 6);
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
    {
        return;
    }
    productLabel_.setBounds(area.removeFromLeft(50));
    saveButton_.setBounds(area.removeFromRight(96));
    area.removeFromRight(6);
    browseButton_.setBounds(area.removeFromRight(80));
    area.removeFromRight(8);
    presetLabel_.setBounds(area);
}

} // namespace sfs::plugin
