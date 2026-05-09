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
    is2D_ = (vm->topology() == sfs::engine::Topology::Torus2D);
    const bool gotData = vm->snapshotPrimaryVoiceSubstrate(snapshot_.data(), kCells);

    // Agent snapshot. The two arrays decouple — substrate may have data
    // even after the voice is fully released (decaying tail) while
    // agents stop emitting new content.
    sfs::engine::VoiceManager::AgentSnapshot agentBuf[kMaxAgentDots];
    int actual = 0;
    vm->snapshotPrimaryVoiceAgents(agentBuf, kMaxAgentDots, actual);
    agentCount_ = actual;
    for (int i = 0; i < actual; ++i)
    {
        agents_[static_cast<std::size_t>(i)].position = agentBuf[i].position;
        agents_[static_cast<std::size_t>(i)].positionY = agentBuf[i].positionY;
        agents_[static_cast<std::size_t>(i)].shape = agentBuf[i].shape;
        agents_[static_cast<std::size_t>(i)].amplitude = agentBuf[i].amplitude;
    }
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

    if (is2D_)
    {
        paint2D(g, bounds);
    }
    else
    {
        paint1D(g, bounds);
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
        label = juce::String(is2D_ ? "SUBSTRATE 2D  ±" : "SUBSTRATE  ±") + juce::String(displayScale_, 2);
    }
    g.drawText(label, bounds.reduced(8.0f), juce::Justification::topLeft);
}

namespace
{

// Doc 07 §4.1.1: agent waveform → colour (sine = blue, saw = orange,
// square = red, fmpair = purple, noise = gray).
juce::Colour agentColour(int shape)
{
    switch (shape)
    {
    case 1:
        return juce::Colour::fromRGB(220, 150, 50); // saw — orange
    case 2:
        return juce::Colour::fromRGB(220, 80, 80); // square — red
    case 3:
        return juce::Colour::fromRGB(180, 120, 220); // fmpair — purple
    case 4:
        return juce::Colour::fromRGB(150, 150, 160); // noise — gray
    default:
        return juce::Colour::fromRGB(100, 180, 230); // sine — blue
    }
}

} // namespace

void SubstrateView::paintAgents1D(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    if (agentCount_ <= 0)
    {
        return;
    }
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    // Y row reserved for agents — sit them just above the harvester row.
    const float dotRowY = bounds.getBottom() - h * 0.25f;
    const float dxPerCell = w / static_cast<float>(kCells);
    for (int i = 0; i < agentCount_; ++i)
    {
        const auto& a = agents_[static_cast<std::size_t>(i)];
        // Wrap position into [0, kCells) just in case migration drifted it.
        float p = a.position;
        while (p < 0.0f)
        {
            p += static_cast<float>(kCells);
        }
        while (p >= static_cast<float>(kCells))
        {
            p -= static_cast<float>(kCells);
        }
        const float x = bounds.getX() + p * dxPerCell;
        const float radius = 1.5f + 2.0f * juce::jlimit(0.0f, 1.0f, a.amplitude);
        g.setColour(agentColour(a.shape).withAlpha(0.85f));
        g.fillEllipse(x - radius, dotRowY - radius, radius * 2.0f, radius * 2.0f);
    }
}

void SubstrateView::paintHarvesters1D(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    g.setColour(juce::Colour::fromRGB(220, 140, 120));
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float dx = w / static_cast<float>(kCells - 1);
    const float markerH = h * 0.12f;
    for (int pos : {0, kCells / 2})
    {
        const float x = bounds.getX() + dx * static_cast<float>(pos);
        g.fillRect(x - 1.0f, bounds.getBottom() - markerH, 2.0f, markerH);
    }
}

void SubstrateView::paintAgents2D(
    juce::Graphics& g, juce::Rectangle<float> /*bounds*/, float originX, float originY, float gridSize) const
{
    if (agentCount_ <= 0)
    {
        return;
    }
    constexpr int kNx = 32;
    constexpr int kNy = 32;
    const float cellW = gridSize / static_cast<float>(kNx);
    const float cellH = gridSize / static_cast<float>(kNy);
    for (int i = 0; i < agentCount_; ++i)
    {
        const auto& a = agents_[static_cast<std::size_t>(i)];
        float px = a.position;
        float py = a.positionY;
        while (px < 0.0f)
        {
            px += static_cast<float>(kNx);
        }
        while (px >= static_cast<float>(kNx))
        {
            px -= static_cast<float>(kNx);
        }
        while (py < 0.0f)
        {
            py += static_cast<float>(kNy);
        }
        while (py >= static_cast<float>(kNy))
        {
            py -= static_cast<float>(kNy);
        }
        const float x = originX + px * cellW;
        const float y = originY + py * cellH;
        const float radius = 1.5f + 2.0f * juce::jlimit(0.0f, 1.0f, a.amplitude);
        g.setColour(agentColour(a.shape).withAlpha(0.95f));
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void SubstrateView::paint1D(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour::fromRGB(40, 42, 55));
    const float midY = bounds.getCentreY();
    g.drawHorizontalLine(static_cast<int>(midY), bounds.getX(), bounds.getRight());

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

    // Agent dots + harvester position markers (Doc 07 §4.1.1).
    paintAgents1D(g, bounds);
    paintHarvesters1D(g, bounds);
}

void SubstrateView::paint2D(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Render the 32×32 grid as a heatmap. Substrate2D::snapshot stores
    // u in row-major order (x fastest), so cell (x, y) is at index y*Nx + x.
    constexpr int kNx = 32;
    constexpr int kNy = 32;
    static_assert(kNx * kNy == kCells, "snapshot size mismatches 2D grid");

    const float invScale = (displayScale_ > 1e-6f) ? (1.0f / displayScale_) : 1.0f;

    // Centre the grid in the viewport, square aspect (smaller dim wins).
    const float available = std::min(bounds.getWidth(), bounds.getHeight() - 24.0f);
    const float gridSize = std::max(64.0f, available);
    const float cellW = gridSize / static_cast<float>(kNx);
    const float cellH = gridSize / static_cast<float>(kNy);
    const float originX = bounds.getX() + (bounds.getWidth() - gridSize) * 0.5f;
    const float originY = bounds.getY() + (bounds.getHeight() - gridSize) * 0.5f;

    for (int y = 0; y < kNy; ++y)
    {
        for (int x = 0; x < kNx; ++x)
        {
            const auto idx = static_cast<std::size_t>(y * kNx + x);
            const float normalised = juce::jlimit(-1.0f, 1.0f, snapshot_[idx] * invScale);
            // Map [-1, 1] to a teal/orange divergent palette:
            //   negative → teal (low channel = blue-green)
            //   positive → orange (high channel = warm)
            //   zero     → black
            juce::Colour c;
            if (normalised >= 0.0f)
            {
                const float k = normalised;
                c = juce::Colour::fromFloatRGBA(0.86f * k, 0.55f * k, 0.30f * k, 1.0f);
            }
            else
            {
                const float k = -normalised;
                c = juce::Colour::fromFloatRGBA(0.30f * k, 0.78f * k, 0.78f * k, 1.0f);
            }
            g.setColour(c);
            g.fillRect(originX + cellW * static_cast<float>(x),
                       originY + cellH * static_cast<float>(y),
                       cellW + 0.5f,
                       cellH + 0.5f);
        }
    }

    // Agent dots overlay (Doc 07 §4.1.2).
    paintAgents2D(g, bounds, originX, originY, gridSize);

    // Harvester L/R position markers (stereo positions 0 and N/2 along
    // the X axis at midY — sfs-spec/04 §3.6 stereo default).
    g.setColour(juce::Colour::fromRGB(220, 140, 120));
    const float midRowY = originY + cellH * (static_cast<float>(kNy) * 0.5f);
    for (int xCell : {0, kNx / 2})
    {
        const float xPx = originX + cellW * static_cast<float>(xCell);
        g.drawEllipse(xPx - 4.0f, midRowY - 4.0f, 8.0f, 8.0f, 1.5f);
    }
}

// ----- SfsEditor -------------------------------------------------------------

SfsEditor::SfsEditor(SfsAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor),
      headerBar_(processor),
      substrateView_(processor),
      macroPanel_(processor),
      advancedPanel_(processor),
      footer_(processor)
{
    addAndMakeVisible(headerBar_);
    addAndMakeVisible(substrateView_);
    addAndMakeVisible(macroPanel_);
    addAndMakeVisible(advancedPanel_);
    addAndMakeVisible(footer_);
    setResizable(true, true);
    setResizeLimits(720, 560, 1280, 800);
    setSize(960, 740);
}

void SfsEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 28, 36));
}

void SfsEditor::resized()
{
    auto area = getLocalBounds();
    // Header strip: 44 px tall (Doc 07 §3 — was 48; trimmed for vertical budget).
    headerBar_.setBounds(area.removeFromTop(44));
    // Footer strip: 22 px tall (Doc 07 §8 status bar).
    footer_.setBounds(area.removeFromBottom(22));

    // Three-panel stack at default 740 px window (after header+footer = 674):
    //   substrate visualiser:  ~200 px (28 %)
    //   macro panel:            ~190 px (28 %) — generous for square knobs
    //   advanced panel:         remainder, ~284 px (3 sub-rows of square knobs)
    // The macro/advanced panels need square cells to render the rotary knobs
    // at usable sizes (no text boxes any more). The substrate keeps a tall
    // strip for the waveform/heatmap.
    const int totalH = area.getHeight();
    const int substrateH = juce::jlimit(160, 320, static_cast<int>(totalH * 0.30f));
    const int macroH = juce::jlimit(160, 220, static_cast<int>(totalH * 0.28f));
    substrateView_.setBounds(area.removeFromTop(substrateH));
    macroPanel_.setBounds(area.removeFromTop(macroH));
    advancedPanel_.setBounds(area);
}

} // namespace sfs::plugin
