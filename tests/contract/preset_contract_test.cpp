// tests/contract/preset_contract_test.cpp
//
// Phase 4 §A4 — contract-driven test rig.
//
// Walks `presets/contract/*.sfs`, loads each, dispatches by `tags`:
//   "contract:drone"   → measure cv → assert < 0.05
//   "contract:organic" → measure centroid stddev → assert > 50 Hz
//   "contract:pitched" → estimate pitch → assert |est - expected| / exp < 3%
//   "contract:glitch"  → count onsets → assert ≥ 5 in 5 s
//
// This is the primary Phase 4 contract gate: each authored preset that
// claims a corner via its `contract:<corner>` tag must meet that
// corner's threshold. The Phase 3 hardcoded inline tests stay green as
// regression hedges.

#include "contract_analyzers.h"

#include "engine/voice_manager.h"
#include "preset/preset.h"
#include "preset/preset_engine.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using sfs::engine::VoiceManager;
using sfs::preset::Preset;

namespace
{

constexpr int kSampleRate = 48000;
constexpr float kSampleRateF = 48000.0f;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 16;
constexpr int kBlock = 256;

[[nodiscard]] std::vector<std::filesystem::path> presetRoots()
{
    // Probe candidate locations of the preset roots. Tests may be invoked
    // from build/, repo root, or CTest's WORKING_DIRECTORY. We walk both
    // `presets/contract/` (small in-tree fixtures) and `presets/factory/`
    // (the shipping palette, including the 8 Init presets from Doc 09 §4).
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    for (const auto& sub : {fs::path("contract"), fs::path("factory")})
    {
        for (const auto& base : {fs::path("presets"), fs::path("../presets"), fs::path("../../presets")})
        {
            const auto candidate = base / sub;
            if (fs::exists(candidate) && fs::is_directory(candidate))
            {
                out.push_back(fs::canonical(candidate));
                break;
            }
        }
    }
    return out;
}

[[nodiscard]] bool hasTag(const Preset& p, const std::string& tag)
{
    for (const auto& t : p.metadata.tags)
    {
        if (t == tag)
        {
            return true;
        }
    }
    return false;
}

// Render `seconds` of mono (downmix L+R) audio with `vm` and return the
// PCM. Caller has already applied a preset + called noteOn.
[[nodiscard]] std::vector<float> renderMono(VoiceManager& vm, int seconds)
{
    const int totalSamples = seconds * kSampleRate;
    std::vector<float> bufL(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
    for (int written = 0; written < totalSamples; written += kBlock)
    {
        const int n = std::min(kBlock, totalSamples - written);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            mono[static_cast<std::size_t>(written + i)] = 0.5f * (bufL[static_cast<std::size_t>(i)] +
                                                                  bufR[static_cast<std::size_t>(i)]);
        }
    }
    return mono;
}

[[nodiscard]] std::vector<std::filesystem::path> listPresets()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    for (const auto& root : presetRoots())
    {
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".sfs")
            {
                out.push_back(entry.path());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("Contract presets exist (presets/{contract,factory}/ is populated)", "[contract][preset]")
{
    const auto presets = listPresets();
    REQUIRE_FALSE(presets.empty());
}

TEST_CASE("Drone-tagged presets pass cv < 0.05", "[contract][preset][drone]")
{
    const auto presets = listPresets();
    int seen = 0;
    for (const auto& path : presets)
    {
        Preset p;
        try
        {
            p = Preset::loadFromFile(path.string());
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (!hasTag(p, "contract:drone"))
        {
            continue;
        }
        ++seen;
        VoiceManager vm(kSubstrateCells, kAgentCount, kSampleRateF);
        sfs::preset::applyToEngine(p, vm);
        vm.noteOn(60, 1.0f);
        const auto mono = renderMono(vm, /*seconds*/ 5);

        // Skip the first second (attack); analyse 1-5 s window.
        const float* analysis = mono.data() + kSampleRate;
        const int analysisLen = 4 * kSampleRate;
        const double cv = sfs::contract::measureDroneCv(analysis, analysisLen, kSampleRateF);
        std::printf("[preset-contract drone] %s cv=%.4f\n", path.filename().string().c_str(), cv);
        CAPTURE(path.string(), cv);
        REQUIRE(cv < 0.05);
    }
    // No drone-tagged presets is silently fine at Phase 4 start (we have
    // one fixture); flips to REQUIRE(seen >= 4) at gate time per phase-4.md.
    INFO("drone-tagged preset count: " << seen);
}

TEST_CASE("Organic-tagged presets pass centroid stddev > 50 Hz", "[contract][preset][organic]")
{
    const auto presets = listPresets();
    int seen = 0;
    for (const auto& path : presets)
    {
        Preset p;
        try
        {
            p = Preset::loadFromFile(path.string());
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (!hasTag(p, "contract:organic"))
        {
            continue;
        }
        ++seen;
        VoiceManager vm(kSubstrateCells, kAgentCount, kSampleRateF);
        sfs::preset::applyToEngine(p, vm);
        vm.noteOn(60, 1.0f);
        const auto mono = renderMono(vm, /*seconds*/ 6);
        const double stddev = sfs::contract::measureOrganicCentroidStddev(mono.data(),
                                                                          static_cast<int>(mono.size()),
                                                                          kSampleRateF);
        std::printf("[preset-contract organic] %s stddev=%.1f Hz\n", path.filename().string().c_str(), stddev);
        CAPTURE(path.string(), stddev);
        REQUIRE(stddev > 50.0);
    }
    INFO("organic-tagged preset count: " << seen);
}

TEST_CASE("Pitched-tagged presets land within 3% of MIDI 60 (C4 = ~261.6 Hz)", "[contract][preset][pitched]")
{
    const auto presets = listPresets();
    int seen = 0;
    constexpr float kExpectedHz = 261.6256f; // C4 (MIDI 60)
    for (const auto& path : presets)
    {
        Preset p;
        try
        {
            p = Preset::loadFromFile(path.string());
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (!hasTag(p, "contract:pitched"))
        {
            continue;
        }
        ++seen;
        VoiceManager vm(kSubstrateCells, kAgentCount, kSampleRateF);
        sfs::preset::applyToEngine(p, vm);
        vm.noteOn(60, 1.0f);
        const auto mono = renderMono(vm, /*seconds*/ 1);
        // Skip 0.2 s of attack.
        const float* analysis = mono.data() + (kSampleRate / 5);
        const int analysisLen = static_cast<int>(mono.size()) - (kSampleRate / 5);
        const float estimated =
            sfs::contract::estimatePitchHz(analysis, analysisLen, kSampleRateF, /*fMin*/ 50.0f, /*fMax*/ 2000.0f);
        const float relErr = std::fabs(estimated - kExpectedHz) / kExpectedHz;
        std::printf("[preset-contract pitched] %s estimated=%.2f Hz err=%.3f%%\n",
                    path.filename().string().c_str(),
                    static_cast<double>(estimated),
                    static_cast<double>(relErr) * 100.0);
        CAPTURE(path.string(), estimated, relErr);
        REQUIRE(estimated > 0.0f);
        REQUIRE(relErr < 0.03f);
    }
    INFO("pitched-tagged preset count: " << seen);
}

TEST_CASE("Glitch-tagged presets pass >= 5 onsets in 5 s", "[contract][preset][glitch]")
{
    const auto presets = listPresets();
    int seen = 0;
    for (const auto& path : presets)
    {
        Preset p;
        try
        {
            p = Preset::loadFromFile(path.string());
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (!hasTag(p, "contract:glitch"))
        {
            continue;
        }
        ++seen;
        VoiceManager vm(kSubstrateCells, kAgentCount, kSampleRateF);
        sfs::preset::applyToEngine(p, vm);
        vm.noteOn(60, 1.0f);
        const auto mono = renderMono(vm, /*seconds*/ 5);
        const int onsets = sfs::contract::countGlitchOnsets(mono.data(), static_cast<int>(mono.size()), kSampleRate);
        std::printf("[preset-contract glitch] %s onsets=%d\n", path.filename().string().c_str(), onsets);
        CAPTURE(path.string(), onsets);
        REQUIRE(onsets >= 5);
    }
    INFO("glitch-tagged preset count: " << seen);
}
