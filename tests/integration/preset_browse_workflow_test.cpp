// tests/integration/preset_browse_workflow_test.cpp
//
// Phase 4 regression coverage for the actual preset-browsing user workflow,
// which the fresh-instance smoke test cannot exercise.
//
// Failure modes covered:
//   * Switching presets mid-note leaves stuck voices that subsequent noteOn
//     events can't dislodge — a fresh noteOn on channel 1 must always
//     produce audio.
//   * MPE channel side-table leaks across preset boundaries: a stale
//     channel-pressure / pitch-bend / CC74 value held high under a routing
//     that inverts density would silence the next note. Loading a preset
//     should not amplify these stale values.
//   * Idle gap before noteOn (long silence between preset load and first
//     note) doesn't desync the voice manager — a noteOn after several
//     blocks of silence still allocates a voice and produces audio.
//
// Each test runs the processor as a single instance across multiple
// preset loads + MIDI events, mirroring how a user actually browses.

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr float kPeakThreshold = 1e-3f;

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

[[nodiscard]] std::filesystem::path findFactoryPreset(const std::string& stemHint)
{
    namespace fs = std::filesystem;
    const auto factory = repoRoot() / "presets" / "factory";
    if (!fs::is_directory(factory))
    {
        return {};
    }
    for (const auto& entry : fs::recursive_directory_iterator(factory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sfs" &&
            entry.path().stem().string().find(stemHint) != std::string::npos)
        {
            return entry.path();
        }
    }
    return {};
}

struct Stats
{
    float peak = 0.0f;
    bool hasNan = false;
};

Stats renderBlock(sfs::plugin::SfsAudioProcessor& processor, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    Stats s;
    buffer.clear();
    processor.processBlock(buffer, midi);
    midi.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* p = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float v = p[i];
            if (!std::isfinite(v))
            {
                s.hasNan = true;
            }
            s.peak = std::max(s.peak, std::fabs(v));
        }
    }
    return s;
}

float renderHold(sfs::plugin::SfsAudioProcessor& processor, int totalSamples, juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> buf(2, kBlockSize);
    float peak = 0.0f;
    int written = 0;
    while (written < totalSamples)
    {
        const auto s = renderBlock(processor, buf, midi);
        REQUIRE_FALSE(s.hasNan);
        peak = std::max(peak, s.peak);
        written += kBlockSize;
    }
    return peak;
}

} // namespace

TEST_CASE("Switching presets mid-note then triggering a fresh note still plays", "[integration][preset][workflow]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    const auto presetA = findFactoryPreset("init_drone");
    const auto presetB = findFactoryPreset("init_pitched");
    REQUIRE_FALSE(presetA.empty());
    REQUIRE_FALSE(presetB.empty());

    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(presetA.string()), err));

    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);

    // Hold note for ~250 ms.
    {
        const float peak = renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
        INFO("preset A held: peak=" << peak);
        REQUIRE(peak > kPeakThreshold);
    }

    // Switch presets MID-NOTE. (User-realistic — they preview many presets
    // while a sustain is ringing.)
    REQUIRE(processor.loadPresetFromFile(juce::String(presetB.string()), err));

    // Continue rendering for ~250 ms — voices should adapt or release.
    {
        renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
    }

    // Release the held note.
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    renderHold(processor, static_cast<int>(0.05 * kSampleRate), midi);

    // Now the regression check: a fresh noteOn after the preset switch +
    // release must produce audio. If voice state is stuck, this fails.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(120)), 0);
    const float peakAfter = renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
    INFO("post-switch fresh note: peak=" << peakAfter);
    REQUIRE(peakAfter > kPeakThreshold);
}

TEST_CASE("Channel pressure on MPE channel does not silence subsequent C1 noteOn",
          "[integration][preset][workflow][mpe]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::String err;
    const auto preset = findFactoryPreset("init_pitched");
    REQUIRE_FALSE(preset.empty());
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    // Simulate an MPE keyboard sending high pressure on channel 2 — the
    // VoiceManager caches it in the per-channel side table. If a later
    // preset load doesn't reset this table, a slot-mapped depth could
    // cancel out density and silence channel-1 notes.
    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::channelPressureChange(2, 127), 0);
    midi.addEvent(juce::MidiMessage::pitchWheel(2, 16383), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(2, 74, 127), 0);
    renderBlock(processor, buf, midi);

    // Load a different preset (the user picks something new).
    const auto preset2 = findFactoryPreset("init_glitch");
    REQUIRE_FALSE(preset2.empty());
    REQUIRE(processor.loadPresetFromFile(juce::String(preset2.string()), err));

    // Play a fresh note on channel 1 — must produce audio regardless of
    // what was cached on channel 2.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);
    const float peak = renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
    INFO("post-MPE-leak channel-1 note: peak=" << peak);
    REQUIRE(peak > kPeakThreshold);
}

TEST_CASE("Long idle gap between preset load and first note still plays", "[integration][preset][workflow][idle]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::String err;
    const auto preset = findFactoryPreset("init_drone");
    REQUIRE_FALSE(preset.empty());
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    // Render 2 seconds of silence (no MIDI) — voice manager should remain
    // ready to allocate.
    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer empty;
    int written = 0;
    while (written < static_cast<int>(2.0 * kSampleRate))
    {
        renderBlock(processor, buf, empty);
        written += kBlockSize;
    }

    // Now play a note — must produce audio.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);
    const float peak = renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
    INFO("post-idle note: peak=" << peak);
    REQUIRE(peak > kPeakThreshold);
}

TEST_CASE("Rapid preset cycling without notes does not poison engine state", "[integration][preset][workflow]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer empty;

    // Browse rapidly — load each kind of corner preset 3 times back-to-back
    // with a single processBlock between loads.
    juce::String err;
    const std::vector<std::string> stems{"init_drone", "init_organic", "init_pitched", "init_glitch"};
    for (int round = 0; round < 3; ++round)
    {
        for (const auto& stem : stems)
        {
            const auto p = findFactoryPreset(stem);
            REQUIRE_FALSE(p.empty());
            REQUIRE(processor.loadPresetFromFile(juce::String(p.string()), err));
            renderBlock(processor, buf, empty);
        }
    }

    // After heavy preset churn, a fresh note must still play.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);
    const float peak = renderHold(processor, static_cast<int>(0.25 * kSampleRate), midi);
    INFO("post-rapid-browse note: peak=" << peak);
    REQUIRE(peak > kPeakThreshold);
}
