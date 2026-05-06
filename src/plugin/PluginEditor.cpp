// src/plugin/PluginEditor.cpp

#include "PluginEditor.h"

namespace sfs::plugin
{

// ----- SubstrateView ---------------------------------------------------------

SubstrateView::SubstrateView(SfsAudioProcessor& processor) : processor_(processor)
{
    snapshot_.fill(0.0f);
    setOpaque(true);
    startTimerHz(30);
}

void SubstrateView::timerCallback()
{
    auto* vm = processor_.voiceManager();
    if (vm == nullptr)
    {
        if (hasSignal_)
        {
            snapshot_.fill(0.0f);
            hasSignal_ = false;
            repaint();
        }
        return;
    }
    const bool gotData = vm->snapshotPrimaryVoiceSubstrate(snapshot_.data(), kCells);
    if (!gotData)
    {
        // No active voice — fade snapshot to zero gradually so the line
        // settles to the centre instead of jumping.
        for (auto& v : snapshot_)
        {
            v *= 0.85f;
        }
    }
    hasSignal_ = gotData;
    repaint();
}

void SubstrateView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Background.
    g.fillAll(juce::Colour::fromRGB(15, 15, 22));

    // Centre line.
    g.setColour(juce::Colour::fromRGB(40, 42, 55));
    const float midY = bounds.getCentreY();
    g.drawHorizontalLine(static_cast<int>(midY), bounds.getX(), bounds.getRight());

    // Substrate trace.
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float halfH = h * 0.45f;
    const float dx = w / static_cast<float>(kCells - 1);

    juce::Path trace;
    trace.preallocateSpace(kCells * 3);
    for (int i = 0; i < kCells; ++i)
    {
        const float x = bounds.getX() + dx * static_cast<float>(i);
        const float y = midY - juce::jlimit(-1.0f, 1.0f, snapshot_[static_cast<std::size_t>(i)]) * halfH;
        if (i == 0)
        {
            trace.startNewSubPath(x, y);
        }
        else
        {
            trace.lineTo(x, y);
        }
    }
    g.setColour(hasSignal_ ? juce::Colour::fromRGB(120, 220, 200) : juce::Colour::fromRGB(60, 90, 80));
    g.strokePath(trace, juce::PathStrokeType(1.4f));

    // Harvester position markers (stereo: positions 0 and N/2).
    g.setColour(juce::Colour::fromRGB(220, 140, 120));
    const float markerH = h * 0.12f;
    for (int pos : {0, kCells / 2})
    {
        const float x = bounds.getX() + dx * static_cast<float>(pos);
        g.fillRect(x - 1.0f, bounds.getBottom() - markerH, 2.0f, markerH);
    }

    // Label.
    g.setColour(juce::Colour::fromRGB(80, 90, 110));
    g.setFont(11.0f);
    g.drawText(hasSignal_ ? "SUBSTRATE" : "SUBSTRATE (idle)", bounds.reduced(8.0f), juce::Justification::topLeft);
}

// ----- SfsEditor -------------------------------------------------------------

SfsEditor::SfsEditor(SfsAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor), substrateView_(processor), knobs_(processor)
{
    addAndMakeVisible(substrateView_);
    addAndMakeVisible(knobs_);
    setResizable(true, true);
    setResizeLimits(640, 480, 1600, 1200);
    setSize(900, 700);
}

void SfsEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 28, 36));
}

void SfsEditor::resized()
{
    auto area = getLocalBounds();
    substrateView_.setBounds(area.removeFromTop(area.getHeight() / 3));
    knobs_.setBounds(area);
}

} // namespace sfs::plugin
