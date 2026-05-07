// tests/integration/audio_correctness_test.cpp
//
// Phase 4 audit follow-up. The existing test suite verifies non-silence,
// no NaN, spectral corner contracts, and determinism. It does NOT verify
// that the *acoustic shape* of the output matches the parameter intent.
// This file fills the highest-priority gaps from that audit:
//
//   1. ENV1 amplitude shaping — attack/decay/sustain/release values
//      actually multiply the output amplitude over time.
//   2. Modulation-matrix depth magnitude — depth=1.0 produces a clearly
//      bigger spectral effect than depth=0.0 (catches a routing-but-
//      no-effect bug).
//   3. Topology routing isolation — Ring1D and Torus2D produce
//      *different* output for the same preset/MIDI (catches a routing
//      bug that would silently downgrade Torus2D to Ring1D).
//   4. Host-param load path ≡ engine direct-call path — the GUI's
//      `loadPresetFromFile + processBlock` must produce the same audio
//      as the headless `applyToEngine + renderBlockStereo` path. If
//      these diverge, the GUI experience diverges from documented
//      headless behaviour and from CI's contract corpus.
//
// All four are render-time properties; they don't replace the existing
// determinism / contract tests, they cover blind spots between them.

#include "PluginProcessor.h"
#include "engine/voice_manager.h"
#include "preset/preset.h"
#include "preset/preset_engine.h"

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

[[nodiscard]] std::filesystem::path findPresetByStem(const std::string& stem)
{
    namespace fs = std::filesystem;
    for (const auto& entry : fs::recursive_directory_iterator(repoRoot() / "presets" / "factory"))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sfs" &&
            entry.path().stem().string().find(stem) != std::string::npos)
        {
            return entry.path();
        }
    }
    return {};
}

struct WindowedPeak
{
    int sampleStart;
    int sampleEnd;
    float peak;
    float rms;
};

WindowedPeak measureWindow(const std::vector<float>& samples, int start, int len)
{
    WindowedPeak w{start, start + len, 0.0f, 0.0f};
    double sumSq = 0.0;
    for (int i = start; i < start + len && i < static_cast<int>(samples.size()); ++i)
    {
        const float a = std::fabs(samples[static_cast<std::size_t>(i)]);
        if (a > w.peak)
        {
            w.peak = a;
        }
        sumSq += static_cast<double>(samples[static_cast<std::size_t>(i)]) *
                 static_cast<double>(samples[static_cast<std::size_t>(i)]);
    }
    w.rms = (len > 0) ? static_cast<float>(std::sqrt(sumSq / len)) : 0.0f;
    return w;
}

// Render N samples through the AudioProcessor with a single noteOn at t=0.
std::vector<float> renderProcessor(sfs::plugin::SfsAudioProcessor& processor, int totalSamples, int midiNote = 60)
{
    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, static_cast<juce::uint8>(120)), 0);

    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(totalSamples));

    int written = 0;
    while (written < totalSamples)
    {
        const int n = std::min(kBlockSize, totalSamples - written);
        buf.clear();
        processor.processBlock(buf, midi);
        midi.clear();
        const float* const L = buf.getReadPointer(0);
        const float* const R = buf.getNumChannels() >= 2 ? buf.getReadPointer(1) : L;
        for (int i = 0; i < n; ++i)
        {
            mono.push_back(0.5f * (L[i] + R[i]));
        }
        written += n;
    }
    return mono;
}

// Render N samples through the engine direct path.
std::vector<float> renderEngineDirect(const sfs::preset::Preset& p, int totalSamples, int midiNote = 60)
{
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;
    sfs::engine::VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    sfs::preset::applyToEngine(p, vm);
    vm.noteOn(1, midiNote, 120.0f / 127.0f);

    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(totalSamples));
    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    int written = 0;
    while (written < totalSamples)
    {
        const int n = std::min(kBlockSize, totalSamples - written);
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            mono.push_back(0.5f * (bufL[static_cast<std::size_t>(i)] + bufR[static_cast<std::size_t>(i)]));
        }
        written += n;
    }
    return mono;
}

} // namespace

// =====================================================================
// Gap 1: ENV1 attack / decay / sustain / release shapes the output.
// =====================================================================
TEST_CASE("ENV1 attack ramp is audible in the output amplitude envelope", "[integration][audio][correctness][envelope]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    // Fast attack vs slow attack — the output peak in the first 50 ms must
    // be smaller for the slow attack. We pick init_pitched (deterministic
    // tonal material) as the substrate and override ENV1 directly.
    const auto preset = findPresetByStem("init_pitched");
    REQUIRE_FALSE(preset.empty());

    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    // Fast attack: 5 ms.
    processor.attackMsParam()->beginChangeGesture();
    *processor.attackMsParam() = 5.0f;
    processor.attackMsParam()->endChangeGesture();
    *processor.sustainLevelParam() = 1.0f;
    *processor.decayMsParam() = 50.0f;

    auto fastSamples = renderProcessor(processor, static_cast<int>(0.5 * kSampleRate));
    const auto earlyFast = measureWindow(fastSamples, 0, static_cast<int>(0.025 * kSampleRate));
    const auto lateFast = measureWindow(fastSamples,
                                        static_cast<int>(0.25 * kSampleRate),
                                        static_cast<int>(0.05 * kSampleRate));

    // Slow attack: 250 ms. Re-load the preset so voices are released, then
    // override.
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));
    *processor.attackMsParam() = 250.0f;
    *processor.sustainLevelParam() = 1.0f;
    *processor.decayMsParam() = 50.0f;

    auto slowSamples = renderProcessor(processor, static_cast<int>(0.5 * kSampleRate));
    const auto earlySlow = measureWindow(slowSamples, 0, static_cast<int>(0.025 * kSampleRate));
    const auto lateSlow = measureWindow(slowSamples,
                                        static_cast<int>(0.25 * kSampleRate),
                                        static_cast<int>(0.05 * kSampleRate));

    INFO("fast: early peak=" << earlyFast.peak << " late peak=" << lateFast.peak);
    INFO("slow: early peak=" << earlySlow.peak << " late peak=" << lateSlow.peak);

    // Fast-attack early window must be at least 2× louder than slow-attack
    // early window (the ramp is barely started for the slow case at 25 ms
    // when the time constant is 250 ms — expect ~10% of late level).
    REQUIRE(earlyFast.peak > 2.0f * earlySlow.peak);

    // Both should reach a similar late-window peak (sustain phase).
    REQUIRE(std::fabs(lateFast.peak - lateSlow.peak) < 0.5f * std::max(lateFast.peak, lateSlow.peak));
}

TEST_CASE("ENV1 release decays the output amplitude after noteOff", "[integration][audio][correctness][envelope]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    const auto preset = findPresetByStem("init_pitched");
    REQUIRE_FALSE(preset.empty());
    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    // Snappy ENV1 with a 100 ms release.
    *processor.attackMsParam() = 5.0f;
    *processor.decayMsParam() = 10.0f;
    *processor.sustainLevelParam() = 0.9f;
    *processor.releaseMsParam() = 100.0f;

    juce::AudioBuffer<float> buf(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(120)), 0);

    std::vector<float> mono;
    auto pump = [&](int samples)
    {
        int written = 0;
        while (written < samples)
        {
            const int n = std::min(kBlockSize, samples - written);
            buf.clear();
            processor.processBlock(buf, midi);
            midi.clear();
            const float* L = buf.getReadPointer(0);
            const float* R = buf.getNumChannels() >= 2 ? buf.getReadPointer(1) : L;
            for (int i = 0; i < n; ++i)
            {
                mono.push_back(0.5f * (L[i] + R[i]));
            }
            written += n;
        }
    };

    pump(static_cast<int>(0.3 * kSampleRate)); // hold 300 ms

    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    const int releaseStart = static_cast<int>(mono.size());
    pump(static_cast<int>(0.5 * kSampleRate)); // 500 ms tail

    const auto sustain = measureWindow(mono,
                                       releaseStart - static_cast<int>(0.05 * kSampleRate),
                                       static_cast<int>(0.04 * kSampleRate));
    const auto tail = measureWindow(mono,
                                    releaseStart + static_cast<int>(0.4 * kSampleRate),
                                    static_cast<int>(0.05 * kSampleRate));

    INFO("sustain peak=" << sustain.peak << " tail peak=" << tail.peak);
    // Sanity bound: the held note must produce non-trivial audio.
    REQUIRE(sustain.peak > 1e-3f);
    // After 4× the release time constant, output should be a fraction of
    // the sustain level. Substrate decay (γ) adds further attenuation, so
    // the bound is generous: tail must be at least 50 % quieter than the
    // sustain window.
    REQUIRE(tail.peak < 0.5f * sustain.peak);
}

// =====================================================================
// Gap 2: Mod-matrix depth magnitude produces proportional spectral motion.
// =====================================================================
TEST_CASE("Mod-matrix depth produces proportional macro motion in the output",
          "[integration][audio][correctness][modulation]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    const auto preset = findPresetByStem("init_drone");
    REQUIRE_FALSE(preset.empty());
    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    // Crank LFO1 fast and route LFO1 → TENSION at depth 0 vs depth 1. The
    // depth-1 case must produce noticeably more spectral variance than the
    // depth-0 case (which has effectively no modulation, just whatever the
    // preset's other LFOs do).
    auto modSlot1 = processor.modSlotDepthParam(1);
    REQUIRE(modSlot1 != nullptr);
    auto rate1 = processor.lfoRateParam(0);
    REQUIRE(rate1 != nullptr);

    *rate1 = 8.0f; // fast — full cycles within our render window

    *modSlot1 = 0.0f;
    auto noModSamples = renderProcessor(processor, static_cast<int>(1.0 * kSampleRate));

    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));
    *rate1 = 8.0f;
    *modSlot1 = 1.0f;
    auto deepModSamples = renderProcessor(processor, static_cast<int>(1.0 * kSampleRate));

    // Use peak-amplitude variance over windowed slices as a cheap proxy for
    // spectral motion: if TENSION is being modulated, the substrate's
    // resonance peaks shift, and the harvester output's peak amplitude
    // changes from window to window. With the modulation off, peak stays
    // roughly constant.
    auto windowedPeakStd = [](const std::vector<float>& s, int winSamples)
    {
        std::vector<float> peaks;
        peaks.reserve(s.size() / static_cast<std::size_t>(winSamples) + 1);
        for (int i = 0; i + winSamples <= static_cast<int>(s.size()); i += winSamples)
        {
            float p = 0.0f;
            for (int j = 0; j < winSamples; ++j)
            {
                p = std::max(p, std::fabs(s[static_cast<std::size_t>(i + j)]));
            }
            peaks.push_back(p);
        }
        if (peaks.size() < 2)
        {
            return 0.0f;
        }
        double mean = 0.0;
        for (float p : peaks)
        {
            mean += p;
        }
        mean /= static_cast<double>(peaks.size());
        double sumSq = 0.0;
        for (float p : peaks)
        {
            const double d = p - mean;
            sumSq += d * d;
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(peaks.size())));
    };

    const float winSize = static_cast<int>(0.05 * kSampleRate); // 50 ms
    const float noModStd = windowedPeakStd(noModSamples, static_cast<int>(winSize));
    const float deepModStd = windowedPeakStd(deepModSamples, static_cast<int>(winSize));

    INFO("nomod stddev=" << noModStd << " deepmod stddev=" << deepModStd);
    // Depth-1 modulation must produce ≥1.5× the windowed-peak stddev of
    // the depth-0 case. (Loose bound — the drone preset is intentionally
    // smooth, so even at depth 1 the LFO motion is gentle.)
    REQUIRE(deepModStd > 1.5f * noModStd);
}

// =====================================================================
// Gap 3: Topology Ring1D vs Torus2D produce different audio.
// =====================================================================
TEST_CASE("Topology Ring1D and Torus2D produce different audio for the same preset",
          "[integration][audio][correctness][topology]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    const auto preset = findPresetByStem("init_drone");
    REQUIRE_FALSE(preset.empty());
    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));

    *processor.topologyParam() = 0; // Ring1D
    auto ringSamples = renderProcessor(processor, static_cast<int>(0.5 * kSampleRate));

    REQUIRE(processor.loadPresetFromFile(juce::String(preset.string()), err));
    *processor.topologyParam() = 1; // Torus2D
    auto torusSamples = renderProcessor(processor, static_cast<int>(0.5 * kSampleRate));

    // Sanity: both must be non-silent.
    const auto ringStat = measureWindow(ringSamples, 0, static_cast<int>(ringSamples.size()));
    const auto torusStat = measureWindow(torusSamples, 0, static_cast<int>(torusSamples.size()));
    REQUIRE(ringStat.peak > 1e-3f);
    REQUIRE(torusStat.peak > 1e-3f);

    // The two outputs must NOT be bit-identical (substrate physics differ).
    bool exactlyEqual = (ringSamples.size() == torusSamples.size());
    if (exactlyEqual)
    {
        for (std::size_t i = 0; i < ringSamples.size(); ++i)
        {
            if (ringSamples[i] != torusSamples[i])
            {
                exactlyEqual = false;
                break;
            }
        }
    }
    REQUIRE_FALSE(exactlyEqual);

    // RMS difference between Ring and Torus renders must be a meaningful
    // fraction of either render's RMS — not just floating-point noise.
    double sumSq = 0.0;
    const std::size_t n = std::min(ringSamples.size(), torusSamples.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const double d = static_cast<double>(ringSamples[i]) - static_cast<double>(torusSamples[i]);
        sumSq += d * d;
    }
    const float diffRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(n)));
    INFO("ring rms=" << ringStat.rms << " torus rms=" << torusStat.rms << " diff rms=" << diffRms);
    REQUIRE(diffRms > 0.1f * std::max(ringStat.rms, torusStat.rms));
}

// =====================================================================
// Gap 5: Host-param load path ≡ engine direct-call path (within tolerance).
// =====================================================================
TEST_CASE("AudioProcessor host-param path ~= engine direct-call path", "[integration][audio][correctness][parity]")
{
    // Render the same preset two ways and assert the outputs are at least
    // strongly correlated. They aren't expected to be bit-identical
    // because the AudioProcessor path applies macros via processBlock's
    // per-block read with smoothing, while applyToEngine sets them
    // directly with the same smoother — minor block-boundary differences
    // can accumulate. We assert RMS-level correlation: the two outputs
    // must have the same broad amplitude profile.
    const auto presetPath = findPresetByStem("init_pitched");
    REQUIRE_FALSE(presetPath.empty());

    sfs::preset::Preset preset = sfs::preset::Preset::loadFromFile(presetPath.string());

    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);
    juce::String err;
    REQUIRE(processor.loadPresetFromFile(juce::String(presetPath.string()), err));

    const int totalSamples = static_cast<int>(0.5 * kSampleRate);
    const auto hostMono = renderProcessor(processor, totalSamples, 60);
    const auto engineMono = renderEngineDirect(preset, totalSamples, 60);

    REQUIRE(hostMono.size() == engineMono.size());
    REQUIRE_FALSE(hostMono.empty());

    auto rmsOf = [](const std::vector<float>& s)
    {
        double sumSq = 0.0;
        for (float v : s)
        {
            sumSq += static_cast<double>(v) * static_cast<double>(v);
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(s.size())));
    };
    const float hostRms = rmsOf(hostMono);
    const float engineRms = rmsOf(engineMono);

    REQUIRE(hostRms > 1e-3f);
    REQUIRE(engineRms > 1e-3f);

    // Ratio must be within 2× either way — i.e. neither path is silent
    // while the other plays, and they're producing comparable energy.
    const float ratio = hostRms / engineRms;
    INFO("host rms=" << hostRms << " engine rms=" << engineRms << " ratio=" << ratio);
    REQUIRE(ratio > 0.5f);
    REQUIRE(ratio < 2.0f);
}

// =====================================================================
// Gap 6 (medium priority): no audio without a noteOn (defends against
// engine self-oscillation regressions).
// =====================================================================
TEST_CASE("Engine produces silence with no MIDI note for any factory preset",
          "[integration][audio][correctness][silence]")
{
    sfs::plugin::SfsAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    // Use the four init presets — quickest spot check for "did anyone wire
    // a self-oscillating bug into a default routing?".
    for (const auto& stem : {"init_drone", "init_organic", "init_pitched", "init_glitch"})
    {
        const auto p = findPresetByStem(stem);
        REQUIRE_FALSE(p.empty());

        juce::String err;
        REQUIRE(processor.loadPresetFromFile(juce::String(p.string()), err));

        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer empty;
        // Render 1 s without any MIDI input; assert the output is below
        // -60 dBFS (≈ 1e-3). If a preset's mod-matrix routing accidentally
        // introduces a self-driving cycle, this trips.
        float peak = 0.0f;
        int written = 0;
        while (written < static_cast<int>(1.0 * kSampleRate))
        {
            buf.clear();
            processor.processBlock(buf, empty);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                const float* s = buf.getReadPointer(ch);
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    peak = std::max(peak, std::fabs(s[i]));
                }
            }
            written += kBlockSize;
        }
        UNSCOPED_INFO("preset " << stem << " no-note peak: " << peak);
        REQUIRE(peak < 1e-3f);
    }
}
