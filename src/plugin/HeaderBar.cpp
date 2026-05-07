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

    presetButton_.setButtonText(processor_.currentPresetName());
    presetButton_.onClick = [this] { onPresetMenu(); };
    presetButton_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(28, 35, 45));
    presetButton_.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 215, 235));
    addAndMakeVisible(presetButton_);

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
    if (presetButton_.getButtonText() != name)
    {
        presetButton_.setButtonText(name);
    }
}

void HeaderBar::rescanFactory()
{
    factoryByCategory_.clear();
    auto root = defaultPresetDir();
    // If defaultPresetDir landed on the user home (no factory found), bail.
    if (root.getFileName() != "factory")
    {
        // Try one of the known relative locations explicitly.
        for (const auto& cand : {juce::File::getCurrentWorkingDirectory().getChildFile("presets/factory"),
                                 juce::File::getCurrentWorkingDirectory().getChildFile("../presets/factory"),
                                 juce::File::getCurrentWorkingDirectory().getChildFile("../../presets/factory")})
        {
            if (cand.isDirectory())
            {
                root = cand;
                break;
            }
        }
    }
    if (!root.isDirectory())
    {
        factoryScanned_ = true;
        return;
    }
    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true, "*.sfs");
    for (const auto& f : files)
    {
        const auto category = f.getParentDirectory().getFileName(); // "drone", "init", etc.
        if (!factoryByCategory_.contains(category))
        {
            factoryByCategory_.set(category, {});
        }
        auto& list = factoryByCategory_.getReference(category);
        FactoryEntry entry;
        entry.path = f;
        entry.displayName = f.getFileNameWithoutExtension();
        list.add(entry);
    }
    factoryScanned_ = true;
}

void HeaderBar::onPresetMenu()
{
    if (!factoryScanned_)
    {
        rescanFactory();
    }

    juce::PopupMenu menu;
    juce::Array<juce::File> allFiles; // running list for prev/next future use

    // Order categories deterministically: init first (Doc 09 §4 Init
    // is the documentation set), then alphabetical.
    juce::StringArray categories;
    for (juce::HashMap<juce::String, juce::Array<FactoryEntry>>::Iterator it(factoryByCategory_); it.next();)
    {
        categories.add(it.getKey());
    }
    categories.sort(true /*ignoreCase*/);
    if (auto initIdx = categories.indexOf("init", true); initIdx > 0)
    {
        categories.move(initIdx, 0);
    }

    int nextItemId = 1000;
    juce::Array<juce::File> idToFile; // index = (id - 1000)
    for (const auto& cat : categories)
    {
        const auto& entries = factoryByCategory_[cat];
        if (entries.isEmpty())
        {
            continue;
        }
        juce::PopupMenu sub;
        // Sort entries by displayName.
        auto sorted = entries;
        std::sort(sorted.begin(),
                  sorted.end(),
                  [](const FactoryEntry& a, const FactoryEntry& b) { return a.displayName < b.displayName; });
        for (const auto& e : sorted)
        {
            sub.addItem(nextItemId, e.displayName);
            idToFile.add(e.path);
            ++nextItemId;
        }
        menu.addSubMenu(cat.toUpperCase(), sub);
    }
    if (categories.isEmpty())
    {
        menu.addItem(1, "(no factory presets found)", false);
        menu.addSeparator();
        menu.addItem(2, "Rescan...");
    }
    else
    {
        menu.addSeparator();
        menu.addItem(2, "Rescan factory directory");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetButton_),
                       [this, idToFile = std::move(idToFile)](int result) mutable
                       {
                           if (result == 0)
                           {
                               return;
                           }
                           if (result == 2)
                           {
                               factoryScanned_ = false;
                               return;
                           }
                           const int idx = result - 1000;
                           if (idx < 0 || idx >= idToFile.size())
                           {
                               return;
                           }
                           juce::String err;
                           if (!processor_.loadPresetFromFile(idToFile[idx].getFullPathName(), err))
                           {
                               juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                                .withTitle("Preset load failed")
                                                                .withMessage(err)
                                                                .withButton("OK"),
                                                            nullptr);
                           }
                       });
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
    // Preset button takes the remaining space; clicking it opens the
    // factory chooser popup.
    presetButton_.setBounds(area);
}

} // namespace sfs::plugin
