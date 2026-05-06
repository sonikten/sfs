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

    // The raw substrate state carries a DC offset that the harvester's
    // output-stage filter strips from the audio (sfs-spec/02 §6 — the
    // in-state DC blocker breaks energy conservation, so we don't apply it
    // to u). For visualisation, subtract the mean so the trace shows the
    // AC content the user actually cares about. Otherwise a held note's
    // flat-DC substrate would render as a flat line at full deflection.
    double meanD = 0.0;
    for (float v : snapshot_)
    {
        meanD += static_cast<double>(v);
    }
    const float mean = static_cast<float>(meanD / static_cast<double>(kCells));
    for (auto& v : snapshot_)
    {
        v -= mean;
    }

    // Auto-normalise on the AC peak. Substrate state isn't bounded to ±1;
    // peaks routinely hit several units. Fast attack, slow release.
    float peak = 0.0f;
    for (float v : snapshot_)
    {
        const float a = std::fabs(v);
        if (a > peak)
        {
            peak = a;
        }
    }
    constexpr float kFloor = 0.05f; // never normalise to truly tiny values
    const float targetScale = std::max(peak, kFloor);
    const float attackAlpha = 0.35f;
    const float releaseAlpha = 0.03f;
    const float alpha = (targetScale > displayScale_) ? attackAlpha : releaseAlpha;
    displayScale_ += (targetScale - displayScale_) * alpha;

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
    const float invScale = (displayScale_ > 1e-6f) ? (1.0f / displayScale_) : 1.0f;
    for (int i = 0; i < kCells; ++i)
    {
        const float x = bounds.getX() + dx * static_cast<float>(i);
        const float normalised = juce::jlimit(-1.0f, 1.0f, snapshot_[static_cast<std::size_t>(i)] * invScale);
        const float y = midY - normalised * halfH;
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

    // Label with auto-scale readout so the user knows the substrate's
    // current peak amplitude (raw, pre-DC-block / pre-clip).
    g.setColour(juce::Colour::fromRGB(80, 90, 110));
    g.setFont(11.0f);
    juce::String label;
    if (!hasSignal_)
    {
        label = "SUBSTRATE (idle)";
    }
    else
    {
        label = "SUBSTRATE  ±" + juce::String(displayScale_, 2);
    }
    g.drawText(label, bounds.reduced(8.0f), juce::Justification::topLeft);
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
