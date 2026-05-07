// tests/integration/preset_playback_smoke_test.cpp
//
// Phase 4 regression coverage. Walks every `presets/factory/**/*.sfs`,
// loads it via SfsAudioProcessor::loadPresetFromFile (the real host-
// param load path the GUI uses), sends a MIDI noteOn at C4, runs
// processBlock for ~0.5 s, and asserts non-silent stereo output.
//
// Catches the "preset loads but no audio plays" failure mode: prior
// tests exercised applyToEngine (VoiceManager direct setters) only;
// this test exercises the AudioProcessor parameter pipeline.
//
// The test runs as a Catch2 binary under tests/integration/. It links
// SFS::PluginCore (the AudioProcessor implementation) so it can spin
// the full plug-in pipeline without needing a host.

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace
{

[[nodiscard]] std::filesystem::path repoRoot()
{
    namespace fs = std::filesystem;
    for (const auto& cand : {fs::path("."), fs::path(".."), fs::path("../..")})
    {
        if (fs::exists(cand / "presets" / "factory"))
        {
            return fs::canonical(cand);
        }
    }
    return {};
}

[[nodiscard]] std::vector<std::filesystem::path> listFactoryPresets()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const auto root = repoRoot();
    if (root.empty())
    {
        return out;
    }
    const auto factory = root / "presets" / "factory";
    if (!fs::is_directory(factory))
    {
        return out;
    }
    for (const auto& entry : fs::recursive_directory_iterator(factory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sfs")
        {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct RenderResult
{
    float peakAbs = 0.0f;
    float rms = 0.0f;
    bool hasNan = false;
};

[[nodiscard]] RenderResult renderViaProcessor(const std::filesystem::path& presetPath)
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 256;
    constexpr int kHoldSeconds = 1; // keep test wall-clock tight; the gate is non-silence

    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::String err;
    if (!processor.loadPresetFromFile(juce::String(presetPath.string()), err))
    {
        // Loading failure is a different kind of bug — caller flags as failure.
        return {};
    }

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);

    const int totalSamples = static_cast<int>(kHoldSeconds * static_cast<int>(kSampleRate));
    int written = 0;

    RenderResult res;
    double sumSq = 0.0;
    int sampleCount = 0;
    while (written < totalSamples)
    {
        const int n = std::min(kBlockSize, totalSamples - written);
        buffer.clear();
        processor.processBlock(buffer, midi);
        midi.clear(); // noteOn fires once at the start
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* p = buffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const float v = p[i];
                if (!std::isfinite(v))
                {
                    res.hasNan = true;
                }
                const float a = std::fabs(v);
                if (a > res.peakAbs)
                {
                    res.peakAbs = a;
                }
                sumSq += static_cast<double>(v) * static_cast<double>(v);
                ++sampleCount;
            }
        }
        written += n;
    }
    res.rms = (sampleCount > 0) ? static_cast<float>(std::sqrt(sumSq / sampleCount)) : 0.0f;
    return res;
}

} // namespace

TEST_CASE("Factory presets exist and are discoverable", "[integration][preset][playback]")
{
    const auto presets = listFactoryPresets();
    INFO("repo root: " << repoRoot().string());
    REQUIRE_FALSE(presets.empty());
    REQUIRE(presets.size() >= 128); // Phase 4 gate criterion
}

TEST_CASE("Every factory preset produces non-silent audio when loaded + played", "[integration][preset][playback]")
{
    const auto presets = listFactoryPresets();
    REQUIRE_FALSE(presets.empty());

    int silent = 0;
    int nans = 0;
    int passed = 0;
    constexpr float kPeakThreshold = 1e-3f; // ~-60 dBFS — anything quieter is "silent"

    for (const auto& path : presets)
    {
        const auto r = renderViaProcessor(path);
        const std::string name = path.stem().string();
        if (r.hasNan)
        {
            ++nans;
            UNSCOPED_INFO("NaN/Inf in render: " << name);
            continue;
        }
        if (r.peakAbs < kPeakThreshold)
        {
            ++silent;
            UNSCOPED_INFO("silent preset: " << name << " peak=" << r.peakAbs << " rms=" << r.rms);
            continue;
        }
        ++passed;
    }

    INFO("passed=" << passed << " silent=" << silent << " nan=" << nans);
    REQUIRE(nans == 0);
    REQUIRE(silent == 0);
}

TEST_CASE("Default state (no preset loaded) produces non-silent audio", "[integration][preset][playback]")
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 256;

    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);

    float peak = 0.0f;
    constexpr int kHoldSamples = 24000;
    int written = 0;
    while (written < kHoldSamples)
    {
        const int n = std::min(kBlockSize, kHoldSamples - written);
        buffer.clear();
        processor.processBlock(buffer, midi);
        midi.clear();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* p = buffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                peak = std::max(peak, std::fabs(p[i]));
            }
        }
        written += n;
    }
    REQUIRE(peak > 1e-3f);
}
