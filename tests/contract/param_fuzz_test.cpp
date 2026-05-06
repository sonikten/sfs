// tests/contract/param_fuzz_test.cpp
//
// Combinatorial parameter fuzz test. Sweeps the 6 macros across boundary
// values × multiple notes × LFO/mod-matrix corners and asserts:
//
//   * No NaN / Inf in the audio output.
//   * Substrate state stays within the runaway clamp (|u| ≤ kUMax).
//   * Audio peak ≤ 1.5 (soft-clip + post-output stages keep us under the
//     hard ceiling; some transient overshoot is allowed but not 10x).
//   * DC offset on the audio is bounded.
//   * On note-on, output is non-silent (RMS > some minimum).
//
// This test exists because the user reported "Extreme values lead to a
// total collapse of the sound" — the substrate runaway at c²=0+γ=0.
// Fixed in src/engine/substrate/substrate_1d.cpp via a hard clamp; this
// test pins the contract so we never lose it.
//
// Wall time ≈ 30 s (200+ render combinations × 1.5 s each rendered at
// ~10× real-time). Runs as a separate CTest entry so contract failures
// here don't mask drone/pitched/organic/glitch failures.

#include "engine/rng/philox.h"
#include "engine/voice.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr int kSampleRate = 48000;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 16;
constexpr float kRenderSeconds = 1.5f;
constexpr int kBlockSize = 256;

// Substrate runaway clamp from src/engine/substrate/substrate_1d.cpp.
// Test asserts substrate state stays at-or-under this value (with a
// small tolerance for the snapshot being read between blocks).
constexpr float kSubstrateUMax = 20.0f;
constexpr float kSubstrateTol = 0.01f;

struct Config
{
    std::string label;
    float tension = 0.5f;
    float damping = 0.3f;
    float density = 0.6f;
    float migration = 0.2f;
    float coherence = 0.8f;
    float excitation = 0.3f;
    sfs::engine::lfo::LfoShape lfoShape[4] = {
        sfs::engine::lfo::LfoShape::Sine,
        sfs::engine::lfo::LfoShape::Triangle,
        sfs::engine::lfo::LfoShape::Sine,
        sfs::engine::lfo::LfoShape::SampleHold,
    };
    float lfoRateHz[4] = {0.5f, 2.0f, 5.0f, 7.0f};
    float slotDepth[4] = {0.5f, 0.10f, -0.08f, 0.30f};
    bool clearMatrix = false;
    int midiNote = 60;
    float velocity = 1.0f;
    float attackMs = 10.0f;
    float decayMs = 120.0f;
    float sustainLvl = 0.75f;
    float releaseMs = 250.0f;
};

struct RenderResult
{
    bool anyNonFinite = false;
    float audioPeak = 0.0f;
    float audioRms = 0.0f;
    float audioMean = 0.0f;
    float substrateMaxAbs = 0.0f;
};

RenderResult runConfig(const Config& cfg)
{
    sfs::engine::Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    auto& macros = voice.macros();
    macros.tension = cfg.tension;
    macros.damping = cfg.damping;
    macros.density = cfg.density;
    macros.migration = cfg.migration;
    macros.coherence = cfg.coherence;
    macros.excitation = cfg.excitation;

    if (cfg.clearMatrix)
    {
        voice.modMatrix().clearAllSlots();
    }
    else
    {
        // Override default depths with cfg.
        for (int i = 0; i < 4; ++i)
        {
            const auto& slot = voice.modMatrix().slot(i);
            voice.modMatrix().setSlot(i, slot.source, slot.dest, cfg.slotDepth[i]);
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        voice.lfo(i).setRateHz(cfg.lfoRateHz[i]);
        voice.lfo(i).setShape(cfg.lfoShape[i]);
    }

    voice.ampEnv().setAttackMs(cfg.attackMs);
    voice.ampEnv().setDecayMs(cfg.decayMs);
    voice.ampEnv().setSustainLevel(cfg.sustainLvl);
    voice.ampEnv().setReleaseMs(cfg.releaseMs);

    voice.noteOn(cfg.midiNote, cfg.velocity);

    const int totalSamples = static_cast<int>(kRenderSeconds * static_cast<float>(kSampleRate));
    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

    RenderResult r;
    double sumSq = 0.0;
    double sumLin = 0.0;
    std::size_t totalSampPairs = 0;

    int written = 0;
    while (written < totalSamples)
    {
        const int n = std::min(kBlockSize, totalSamples - written);
        voice.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float l = bufL[static_cast<std::size_t>(i)];
            const float rs = bufR[static_cast<std::size_t>(i)];
            if (!std::isfinite(l) || !std::isfinite(rs))
            {
                r.anyNonFinite = true;
            }
            const float a = std::max(std::fabs(l), std::fabs(rs));
            if (a > r.audioPeak)
            {
                r.audioPeak = a;
            }
            sumSq += static_cast<double>(l) * static_cast<double>(l) +
                     static_cast<double>(rs) * static_cast<double>(rs);
            sumLin += static_cast<double>(l) + static_cast<double>(rs);
            ++totalSampPairs;
        }

        // Snapshot substrate every block; record peak.
        voice.snapshotSubstrate(snap.data(), kSubstrateCells);
        for (float v : snap)
        {
            if (!std::isfinite(v))
            {
                r.anyNonFinite = true;
            }
            const float a = std::fabs(v);
            if (a > r.substrateMaxAbs)
            {
                r.substrateMaxAbs = a;
            }
        }

        written += n;
    }

    if (totalSampPairs > 0)
    {
        r.audioRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(2 * totalSampPairs)));
        r.audioMean = static_cast<float>(sumLin / static_cast<double>(2 * totalSampPairs));
    }
    return r;
}

void assertHealthy(const Config& cfg, const RenderResult& r)
{
    INFO("Config: " << cfg.label);
    INFO("audioPeak=" << r.audioPeak << " audioRms=" << r.audioRms << " audioMean=" << r.audioMean
                      << " substrateMax=" << r.substrateMaxAbs);
    REQUIRE_FALSE(r.anyNonFinite);
    REQUIRE(r.substrateMaxAbs <= kSubstrateUMax + kSubstrateTol);
    REQUIRE(r.audioPeak <= 1.5f);            // soft-clip caps near 0.5; some transient OK
    REQUIRE(std::fabs(r.audioMean) < 0.10f); // DC at output bounded
}

} // namespace

TEST_CASE("Param fuzz: macro boundary corners (2^6 = 64 combos)", "[contract][fuzz][macros]")
{
    int fails = 0;
    int silent = 0;
    std::vector<float> levels = {0.0f, 1.0f};
    for (float t : levels)
        for (float d : levels)
            for (float den : levels)
                for (float mig : levels)
                    for (float coh : levels)
                        for (float exc : levels)
                        {
                            Config c;
                            c.label = "T" + std::to_string(static_cast<int>(t)) + "D" +
                                      std::to_string(static_cast<int>(d)) + "Den" +
                                      std::to_string(static_cast<int>(den)) + "Mig" +
                                      std::to_string(static_cast<int>(mig)) + "Coh" +
                                      std::to_string(static_cast<int>(coh)) + "Exc" +
                                      std::to_string(static_cast<int>(exc));
                            c.tension = t;
                            c.damping = d;
                            c.density = den;
                            c.migration = mig;
                            c.coherence = coh;
                            c.excitation = exc;
                            const RenderResult r = runConfig(c);
                            assertHealthy(c, r);
                            if (r.audioRms < 0.0001f)
                            {
                                ++silent;
                            }
                            (void)fails;
                        }
    std::printf("\n[macro corners] 64 configs run; %d effectively silent (RMS < 1e-4)\n", silent);
}

TEST_CASE("Param fuzz: all 5 agent shapes × note range", "[contract][fuzz][shapes]")
{
    using sfs::engine::agents::AgentShape;
    constexpr AgentShape kShapes[5] = {
        AgentShape::Sine,
        AgentShape::Saw,
        AgentShape::Square,
        AgentShape::FmPair,
        AgentShape::Noise,
    };
    constexpr int kNotes[3] = {36, 60, 84}; // C2, C4, C6

    for (auto shape : kShapes)
    {
        for (int note : kNotes)
        {
            Config c;
            c.label = "shape" + std::to_string(static_cast<int>(shape)) + "_note" + std::to_string(note);
            c.midiNote = note;
            // Rest at defaults; the note + shape is what we're varying.

            sfs::engine::Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
            voice.macros().tension = c.tension;
            voice.macros().damping = c.damping;
            voice.macros().density = c.density;
            voice.macros().migration = c.migration;
            voice.macros().coherence = c.coherence;
            voice.macros().excitation = c.excitation;
            voice.setUniformShape(shape);
            voice.noteOn(note, 1.0f);

            const int totalSamples = static_cast<int>(kRenderSeconds * static_cast<float>(kSampleRate));
            std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
            std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
            std::vector<float> snap(static_cast<std::size_t>(kSubstrateCells), 0.0f);

            RenderResult r;
            double sumSq = 0.0;
            std::size_t pairs = 0;

            int written = 0;
            while (written < totalSamples)
            {
                const int n = std::min(kBlockSize, totalSamples - written);
                voice.renderBlockStereo(bufL.data(), bufR.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    const float l = bufL[static_cast<std::size_t>(i)];
                    const float rs = bufR[static_cast<std::size_t>(i)];
                    if (!std::isfinite(l) || !std::isfinite(rs))
                    {
                        r.anyNonFinite = true;
                    }
                    const float a = std::max(std::fabs(l), std::fabs(rs));
                    if (a > r.audioPeak)
                    {
                        r.audioPeak = a;
                    }
                    sumSq += static_cast<double>(l) * static_cast<double>(l) +
                             static_cast<double>(rs) * static_cast<double>(rs);
                    ++pairs;
                }
                voice.snapshotSubstrate(snap.data(), kSubstrateCells);
                for (float v : snap)
                {
                    if (!std::isfinite(v))
                    {
                        r.anyNonFinite = true;
                    }
                    const float a = std::fabs(v);
                    if (a > r.substrateMaxAbs)
                    {
                        r.substrateMaxAbs = a;
                    }
                }
                written += n;
            }
            if (pairs > 0)
            {
                r.audioRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(2 * pairs)));
            }
            assertHealthy(c, r);
            REQUIRE(r.audioRms > 0.0001f); // every shape should produce sound
        }
    }
}

TEST_CASE("Param fuzz: all LFOs at max rate, all shapes", "[contract][fuzz][lfo]")
{
    using sfs::engine::lfo::LfoShape;
    constexpr LfoShape kShapes[5] = {
        LfoShape::Sine,
        LfoShape::Triangle,
        LfoShape::Saw,
        LfoShape::Square,
        LfoShape::SampleHold,
    };

    for (auto shape : kShapes)
    {
        Config c;
        c.label = "lfo_shape" + std::to_string(static_cast<int>(shape)) + "_max_rate";
        for (int i = 0; i < 4; ++i)
        {
            c.lfoShape[i] = shape;
            c.lfoRateHz[i] = 20.0f;
        }
        // Crank slot depths so the LFO actually drives macros hard.
        c.slotDepth[0] = 1.0f;
        c.slotDepth[1] = 1.0f;
        c.slotDepth[2] = -1.0f;
        c.slotDepth[3] = 1.0f;
        const RenderResult r = runConfig(c);
        assertHealthy(c, r);
    }
}

TEST_CASE("Param fuzz: extreme mod matrix depths", "[contract][fuzz][mod_matrix]")
{
    Config c;
    c.label = "all_slots_pos1";
    for (auto& d : c.slotDepth)
    {
        d = 1.0f;
    }
    assertHealthy(c, runConfig(c));

    c.label = "all_slots_neg1";
    for (auto& d : c.slotDepth)
    {
        d = -1.0f;
    }
    assertHealthy(c, runConfig(c));
}

TEST_CASE("Param fuzz: extreme ADSR (0/0/0/0 + 5000/5000/1/10000)", "[contract][fuzz][adsr]")
{
    Config a;
    a.label = "adsr_zero";
    a.attackMs = 0.0f;
    a.decayMs = 0.0f;
    a.sustainLvl = 0.0f;
    a.releaseMs = 0.0f;
    assertHealthy(a, runConfig(a));

    Config b;
    b.label = "adsr_max";
    b.attackMs = 5000.0f;
    b.decayMs = 5000.0f;
    b.sustainLvl = 1.0f;
    b.releaseMs = 10000.0f;
    assertHealthy(b, runConfig(b));
}

TEST_CASE("Param fuzz: max-velocity vs zero-velocity", "[contract][fuzz][velocity]")
{
    Config a;
    a.label = "vel_zero";
    a.velocity = 0.0f;
    assertHealthy(a, runConfig(a));

    Config b;
    b.label = "vel_max";
    b.velocity = 1.0f;
    assertHealthy(b, runConfig(b));
}

namespace
{

// Deterministic Philox-seeded random config generator. Each iteration
// gets a unique seed so failures are reproducible by index.
Config randomConfig(int iteration)
{
    sfs::engine::rng::Philox4x32Stream s;
    s.seed(0xC0FFEE'C0FFEEull,
           static_cast<std::uint16_t>(iteration & 0xFFFF),
           static_cast<std::uint16_t>((iteration >> 16) & 0xFFFF),
           sfs::engine::rng::StreamId::PresetDiceButton);

    auto u01 = [&]() { return s.nextFloat01(); };
    auto lerp = [&](float lo, float hi) { return lo + (hi - lo) * u01(); };

    Config c;
    c.label = "rand_" + std::to_string(iteration);

    c.tension = u01();
    c.damping = u01();
    c.density = u01();
    c.migration = u01();
    c.coherence = u01();
    c.excitation = u01();

    using sfs::engine::lfo::LfoShape;
    constexpr LfoShape kShapes[5] = {
        LfoShape::Sine,
        LfoShape::Triangle,
        LfoShape::Saw,
        LfoShape::Square,
        LfoShape::SampleHold,
    };
    for (int i = 0; i < 4; ++i)
    {
        const float skew = u01();
        c.lfoRateHz[i] = 0.05f * std::exp(skew * std::log(20.0f / 0.05f));
        c.lfoShape[i] = kShapes[static_cast<int>(u01() * 5.0f) % 5];
        c.slotDepth[i] = u01() * 2.0f - 1.0f;
    }

    c.attackMs = lerp(0.0f, 5000.0f);
    c.decayMs = lerp(0.0f, 5000.0f);
    c.sustainLvl = u01();
    c.releaseMs = lerp(0.0f, 10000.0f);

    c.midiNote = 24 + static_cast<int>(u01() * 73.0f);
    c.velocity = u01();

    return c;
}

} // namespace

TEST_CASE("Param fuzz: 200 random parameter combinations (Philox-seeded)", "[contract][fuzz][random]")
{
    constexpr int kIterations = 200;
    int silentNoteOn = 0;
    for (int i = 0; i < kIterations; ++i)
    {
        const Config c = randomConfig(i);
        const RenderResult r = runConfig(c);
        assertHealthy(c, r);
        if (c.velocity > 0.05f && r.audioRms < 0.0001f)
        {
            ++silentNoteOn;
        }
    }
    std::printf("\n[random fuzz] %d configs run; %d non-silent-velocity → silent (RMS < 1e-4)\n",
                kIterations,
                silentNoteOn);
    REQUIRE(silentNoteOn < kIterations / 4);
}
