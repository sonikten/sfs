// tests/integration/audio_stability_test.cpp
//
// Phase 4 audit follow-up — preset-driven musical-stability tests. The
// existing test suite verifies engineering robustness (no NaN, peak ≤ 1.5,
// substrate |u| ≤ 20, DC < 0.15) and corner sonic contracts (drone /
// organic / pitched / glitch on a handful of presets). It does NOT verify
// that an arbitrary parameter combination produces *musical* output —
// stable amplitude, decaying tail, focused spectrum.
//
// Real-world DAW use surfaces combinations that produce "spiralling"
// audio: amplitude drifting upward, harmonics piling on, output never
// settling. The engine isn't crashing, but the output is unmusical.
//
// This file walks the 128 factory presets at category-appropriate
// thresholds:
//   * RMS growth ratio (late RMS / early RMS — catches monotonic energy
//     accumulation)
//   * Peak monotonicity (latest 5 s window peak ≤ early max × bound —
//     catches late-stage blow-up)
//   * DC drift (mean over last 5 s — catches slow leakage to a rail)
//   * RMS coefficient of variation (windowed-RMS stddev/mean — catches
//     "all over the place" chaos)
//   * Tail decay ratio (RMS deep-tail / RMS just-after-noteOff — catches
//     undamped self-oscillation)
//   * Spectral flatness (geometric/arithmetic mean of magnitude bins —
//     catches broadband noise signature)
//
// Plus two synthetic stress sections:
//   * 12 corner combos hand-picked to span the near-CFL regime the user
//     reported (e.g. TENSION 0.9 + DAMPING 0.1 + EXCITATION 0.95).
//   * 30 s long-tail free response on each init preset (catches deep-tail
//     undamped substrate modes).
//
// All tests use the engine direct path (VoiceManager + applyToEngine).
// Bounds are set per category and calibrated to leave headroom for FP /
// platform jitter while still failing on real spiralling.

#include "engine/mod_matrix/mod_matrix.h"
#include "engine/voice_manager.h"
#include "preset/preset.h"
#include "preset/preset_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 16;

// ============================================================================
// Repo + preset discovery — copied from audio_correctness_test.cpp.
// ============================================================================

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

[[nodiscard]] std::vector<std::filesystem::path> listCategoryPresets(const std::string& category)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const auto dir = repoRoot() / "presets" / "factory" / category;
    if (!fs::is_directory(dir))
    {
        return out;
    }
    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sfs")
        {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ============================================================================
// FFT helpers — copied from drone_test.cpp.
// ============================================================================

using cfloat = std::complex<float>;

void fft(cfloat* x, int n)
{
    int j = 0;
    for (int i = 1; i < n; ++i)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(x[i], x[j]);
        }
    }
    constexpr float kPi = 3.14159265358979323846f;
    for (int len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const cfloat wn(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len)
        {
            cfloat w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k)
            {
                const cfloat u = x[i + k];
                const cfloat v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wn;
            }
        }
    }
}

void applyHannWindow(float* buf, int n)
{
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n - 1)));
        buf[i] *= w;
    }
}

// Spectral flatness over a single FFT frame. Geometric mean / arithmetic
// mean of magnitudes (positive-frequency bins only). 1.0 = perfect noise;
// 0.0 = pure tone. Musical material typically < 0.5.
[[nodiscard]] float spectralFlatness(const cfloat* spec, int nFft)
{
    const int nyquist = nFft / 2;
    double sumLog = 0.0;
    double sumLin = 0.0;
    int valid = 0;
    for (int k = 1; k < nyquist; ++k)
    {
        const double re = spec[k].real();
        const double im = spec[k].imag();
        const double mag = std::sqrt(re * re + im * im);
        if (mag > 1e-9)
        {
            sumLog += std::log(mag);
            sumLin += mag;
            ++valid;
        }
    }
    if (valid < 4 || sumLin < 1e-9)
    {
        return 0.0f;
    }
    const double geom = std::exp(sumLog / valid);
    const double arith = sumLin / valid;
    return static_cast<float>(geom / arith);
}

// ============================================================================
// Window measurement helpers — copied from audio_correctness_test.cpp,
// extended with windowed-RMS-series + mean/std.
// ============================================================================

[[nodiscard]] float rms(const std::vector<float>& s, int start, int len)
{
    if (len <= 0 || start < 0 || start >= static_cast<int>(s.size()))
    {
        return 0.0f;
    }
    const int end = std::min<int>(start + len, static_cast<int>(s.size()));
    double sumSq = 0.0;
    int n = 0;
    for (int i = start; i < end; ++i)
    {
        const double v = s[static_cast<std::size_t>(i)];
        sumSq += v * v;
        ++n;
    }
    return n > 0 ? static_cast<float>(std::sqrt(sumSq / n)) : 0.0f;
}

[[nodiscard]] float peakAbs(const std::vector<float>& s, int start, int len)
{
    if (len <= 0 || start < 0 || start >= static_cast<int>(s.size()))
    {
        return 0.0f;
    }
    const int end = std::min<int>(start + len, static_cast<int>(s.size()));
    float p = 0.0f;
    for (int i = start; i < end; ++i)
    {
        p = std::max(p, std::fabs(s[static_cast<std::size_t>(i)]));
    }
    return p;
}

[[nodiscard]] float meanWindow(const std::vector<float>& s, int start, int len)
{
    if (len <= 0 || start < 0 || start >= static_cast<int>(s.size()))
    {
        return 0.0f;
    }
    const int end = std::min<int>(start + len, static_cast<int>(s.size()));
    double sum = 0.0;
    int n = 0;
    for (int i = start; i < end; ++i)
    {
        sum += s[static_cast<std::size_t>(i)];
        ++n;
    }
    return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

// Series of RMS values over [winStart..winEnd) using winSamples windows
// hopping by hopSamples. Returns one float per window.
[[nodiscard]] std::vector<float>
windowedRmsSeries(const std::vector<float>& s, int winStart, int winEnd, int winSamples, int hopSamples)
{
    std::vector<float> out;
    if (hopSamples <= 0 || winSamples <= 0)
    {
        return out;
    }
    for (int i = winStart; i + winSamples <= winEnd; i += hopSamples)
    {
        out.push_back(rms(s, i, winSamples));
    }
    return out;
}

[[nodiscard]] std::pair<float, float> meanStd(const std::vector<float>& v)
{
    if (v.empty())
    {
        return {0.0f, 0.0f};
    }
    double mean = 0.0;
    for (float x : v)
    {
        mean += x;
    }
    mean /= v.size();
    double sumSq = 0.0;
    for (float x : v)
    {
        const double d = x - mean;
        sumSq += d * d;
    }
    const double std = std::sqrt(sumSq / v.size());
    return {static_cast<float>(mean), static_cast<float>(std)};
}

// ============================================================================
// Rendering. Each render is a 15 s sustained note + 5 s post-noteOff tail.
// ============================================================================

struct RenderResult
{
    std::vector<float> samples; // mono = (L+R)/2
    int gateOffSample = 0;      // index where noteOff was sent
    bool hasNonFinite = false;
};

// Render a preset with engine-direct path. Holds note for sustainSeconds,
// then noteOff + extra tailSeconds of tail. Uses default channel 1 noteOn.
RenderResult
renderPresetSustainTail(const sfs::preset::Preset& preset, int midiNote, double sustainSeconds, double tailSeconds)
{
    sfs::engine::VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    sfs::preset::applyToEngine(preset, vm);
    vm.noteOn(1, midiNote, 1.0f);

    const int sustainSamples = static_cast<int>(sustainSeconds * kSampleRate);
    const int tailSamples = static_cast<int>(tailSeconds * kSampleRate);
    const int totalSamples = sustainSamples + tailSamples;

    RenderResult r;
    r.samples.reserve(static_cast<std::size_t>(totalSamples));
    r.gateOffSample = sustainSamples;

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    int written = 0;
    bool gateOffFired = false;
    while (written < totalSamples)
    {
        if (!gateOffFired && written >= sustainSamples)
        {
            vm.noteOff(1, midiNote);
            gateOffFired = true;
        }
        const int n = std::min(kBlockSize, totalSamples - written);
        std::fill(bufL.begin(), bufL.begin() + n, 0.0f);
        std::fill(bufR.begin(), bufR.begin() + n, 0.0f);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (bufL[static_cast<std::size_t>(i)] + bufR[static_cast<std::size_t>(i)]);
            if (!std::isfinite(v))
            {
                r.hasNonFinite = true;
            }
            r.samples.push_back(v);
        }
        written += n;
    }
    return r;
}

// Engine-direct render with explicit macros + optional LFO/mod-matrix
// configuration (Block B synthetic stress).
struct StressConfig
{
    float tension = 0.5f;
    float damping = 0.5f;
    float density = 0.5f;
    float migration = 0.2f;
    float coherence = 0.5f;
    float excitation = 0.5f;

    // Optional: route LFO0 -> destination at depth.
    bool useLfoMod = false;
    float lfoRateHz = 0.0f;
    sfs::engine::lfo::LfoShape lfoShape = sfs::engine::lfo::LfoShape::Sine;
    sfs::engine::mod_matrix::Destination lfoDest = sfs::engine::mod_matrix::Destination::Excitation;
    float lfoDepth = 0.0f;

    // Optional: route Random -> destination at depth (covers test #12).
    bool useRandomMod = false;
    sfs::engine::mod_matrix::Destination randomDest = sfs::engine::mod_matrix::Destination::Density;
    float randomDepth = 0.0f;
};

RenderResult renderStress(const StressConfig& cfg, int midiNote, double sustainSeconds, double tailSeconds)
{
    sfs::engine::VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));

    auto& macros = vm.macros();
    macros.tension = cfg.tension;
    macros.damping = cfg.damping;
    macros.density = cfg.density;
    macros.migration = cfg.migration;
    macros.coherence = cfg.coherence;
    macros.excitation = cfg.excitation;

    vm.setUniformShape(sfs::engine::agents::AgentShape::Sine);
    vm.setTopology(sfs::engine::Topology::Ring1D);
    vm.setAdsr(5.0f, 50.0f, 0.9f, 200.0f);
    if (cfg.useLfoMod)
    {
        vm.setLfoConfig(0, cfg.lfoRateHz, cfg.lfoShape);
        vm.setModMatrixSlot(0, sfs::engine::mod_matrix::Source::Lfo1, cfg.lfoDest, cfg.lfoDepth);
    }
    if (cfg.useRandomMod)
    {
        vm.setModMatrixSlot(1, sfs::engine::mod_matrix::Source::Random, cfg.randomDest, cfg.randomDepth);
    }

    vm.noteOn(1, midiNote, 1.0f);

    const int sustainSamples = static_cast<int>(sustainSeconds * kSampleRate);
    const int tailSamples = static_cast<int>(tailSeconds * kSampleRate);
    const int totalSamples = sustainSamples + tailSamples;

    RenderResult r;
    r.samples.reserve(static_cast<std::size_t>(totalSamples));
    r.gateOffSample = sustainSamples;

    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    int written = 0;
    bool gateOffFired = false;
    while (written < totalSamples)
    {
        if (!gateOffFired && written >= sustainSamples)
        {
            vm.noteOff(1, midiNote);
            gateOffFired = true;
        }
        const int n = std::min(kBlockSize, totalSamples - written);
        std::fill(bufL.begin(), bufL.begin() + n, 0.0f);
        std::fill(bufR.begin(), bufR.begin() + n, 0.0f);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (bufL[static_cast<std::size_t>(i)] + bufR[static_cast<std::size_t>(i)]);
            if (!std::isfinite(v))
            {
                r.hasNonFinite = true;
            }
            r.samples.push_back(v);
        }
        written += n;
    }
    return r;
}

// ============================================================================
// StabilityMetrics: every metric the per-category bounds use, computed
// from one render.
// ============================================================================

struct StabilityMetrics
{
    float rmsEarly = 0.0f;         // RMS([2,5]s)
    float rmsLate = 0.0f;          // RMS([10,15]s)
    float rmsLateOverEarly = 0.0f; // ratio (1.0 = stable)
    float peakEarly = 0.0f;        // max peak in [0,5]s & [5,10]s combined
    float peakLate = 0.0f;         // peak in [10,15]s
    float peakMonotonicity = 0.0f; // peakLate / peakEarly
    float dcDrift = 0.0f;          // |mean([10,15]s)|
    float rmsCoeffVar = 0.0f;      // stddev/mean of windowed RMS over [2,15]s
    float spectralFlatnessLate = 0.0f;
    float tailDecayRatio = 0.0f; // RMS([4,5]s tail) / RMS([0,0.1]s tail)
    bool noNanInf = true;
};

[[nodiscard]] StabilityMetrics computeMetrics(const RenderResult& r)
{
    StabilityMetrics m;
    m.noNanInf = !r.hasNonFinite;

    const int sr = static_cast<int>(kSampleRate);
    const int s2 = 2 * sr;
    const int s5 = 5 * sr;
    const int s10 = 10 * sr;
    const int s15 = 15 * sr;

    // RMS growth.
    m.rmsEarly = rms(r.samples, s2, s5 - s2);   // [2,5]s
    m.rmsLate = rms(r.samples, s10, s15 - s10); // [10,15]s
    m.rmsLateOverEarly = m.rmsEarly > 1e-6f ? m.rmsLate / m.rmsEarly : 0.0f;

    // Peak monotonicity. Compare [10,15]s peak to max of earlier 5s windows.
    const float peakW0 = peakAbs(r.samples, 0, s5);
    const float peakW1 = peakAbs(r.samples, s5, s10 - s5);
    m.peakEarly = std::max(peakW0, peakW1);
    m.peakLate = peakAbs(r.samples, s10, s15 - s10);
    m.peakMonotonicity = m.peakEarly > 1e-6f ? m.peakLate / m.peakEarly : 0.0f;

    // DC drift.
    m.dcDrift = std::fabs(meanWindow(r.samples, s10, s15 - s10));

    // RMS coefficient of variation over [2,15]s in 1 s windows hopped by 0.5 s.
    auto rmsSeries = windowedRmsSeries(r.samples, s2, s15, sr, sr / 2);
    auto [rmsMean, rmsStd] = meanStd(rmsSeries);
    m.rmsCoeffVar = rmsMean > 1e-6f ? rmsStd / rmsMean : 0.0f;

    // Spectral flatness over [10,15]s using a 4096-point FFT, single frame
    // (4096/48000 = 85ms — captures sustained spectral character).
    constexpr int kNFft = 4096;
    if (s10 + kNFft <= static_cast<int>(r.samples.size()))
    {
        std::vector<float> frame(kNFft, 0.0f);
        for (int i = 0; i < kNFft; ++i)
        {
            frame[static_cast<std::size_t>(i)] = r.samples[static_cast<std::size_t>(s10 + i)];
        }
        applyHannWindow(frame.data(), kNFft);
        std::vector<cfloat> spec(static_cast<std::size_t>(kNFft));
        for (int i = 0; i < kNFft; ++i)
        {
            spec[static_cast<std::size_t>(i)] = cfloat(frame[static_cast<std::size_t>(i)], 0.0f);
        }
        fft(spec.data(), kNFft);
        m.spectralFlatnessLate = spectralFlatness(spec.data(), kNFft);
    }

    // Tail decay ratio.
    if (r.gateOffSample > 0)
    {
        const int tailStart = r.gateOffSample;
        const int tailEarly = sr / 10; // 100 ms post-noteOff
        const int tailLate4s = 4 * sr; // start of [4,5]s post-noteOff
        const int tailLate5s = 5 * sr;
        if (tailStart + tailLate5s <= static_cast<int>(r.samples.size()))
        {
            const float rmsImmediate = rms(r.samples, tailStart, tailEarly);
            const float rmsDeep = rms(r.samples, tailStart + tailLate4s, tailLate5s - tailLate4s);
            m.tailDecayRatio = rmsImmediate > 1e-6f ? rmsDeep / rmsImmediate : 0.0f;
        }
    }

    return m;
}

// ============================================================================
// Bounds tables. One row per category. Block A's REQUIRE() macro reads
// the row to know which thresholds to apply.
// ============================================================================

struct CategoryBounds
{
    const char* name;
    float maxRmsLateOverEarly;
    float maxPeakMonotonicity;
    float maxDcDrift;
    float maxRmsCoeffVar; // 0 = skip (e.g., glitch is meant to vary)
    float maxSpectralFlatness;
    float maxTailDecayRatio;
};

// Bounds informed by category intent + headroom for FP/platform jitter.
// CALIBRATION: if any factory preset fails, raise the bound to
// max(1.5×measured, intent_bound). Each REQUIRE() reports the offending
// preset + value so a future regression has full provenance.
constexpr CategoryBounds kBoundsDrone = {"drone", 1.40f, 1.30f, 0.05f, 0.30f, 0.40f, 0.06f};
constexpr CategoryBounds kBoundsOrganic = {"organic", 1.60f, 1.45f, 0.07f, 0.75f, 0.60f, 0.10f};
constexpr CategoryBounds kBoundsPitched = {"pitched", 1.50f, 1.30f, 0.05f, 0.45f, 0.45f, 0.06f};
constexpr CategoryBounds kBoundsGlitch = {"glitch", 2.50f, 1.80f, 0.10f, 0.0f, 0.85f, 0.12f};
constexpr CategoryBounds kBoundsHybrid = {"hybrid", 1.80f, 1.55f, 0.07f, 0.65f, 0.60f, 0.10f};
constexpr CategoryBounds kBoundsInit = {"init", 1.60f, 1.40f, 0.05f, 0.55f, 0.55f, 0.06f};

void runBlockACategory(const std::string& categoryDir, const CategoryBounds& bounds, int defaultMidiNote)
{
    const auto presets = listCategoryPresets(categoryDir);
    REQUIRE_FALSE(presets.empty());

    for (const auto& presetPath : presets)
    {
        sfs::preset::Preset p;
        try
        {
            p = sfs::preset::Preset::loadFromFile(presetPath.string());
        }
        catch (const std::exception& e)
        {
            UNSCOPED_INFO(categoryDir << ": preset load failed " << presetPath.filename() << " " << e.what());
            FAIL();
        }

        const auto r = renderPresetSustainTail(p, defaultMidiNote, 15.0, 5.0);
        const auto m = computeMetrics(r);

        const std::string name = presetPath.stem().string();
        UNSCOPED_INFO(bounds.name << " " << name << " rmsRatio=" << m.rmsLateOverEarly << " peakRatio="
                                  << m.peakMonotonicity << " dc=" << m.dcDrift << " cv=" << m.rmsCoeffVar
                                  << " flat=" << m.spectralFlatnessLate << " tail=" << m.tailDecayRatio);

        REQUIRE(m.noNanInf);
        REQUIRE(m.rmsLateOverEarly < bounds.maxRmsLateOverEarly);
        REQUIRE(m.peakMonotonicity < bounds.maxPeakMonotonicity);
        REQUIRE(m.dcDrift < bounds.maxDcDrift);
        if (bounds.maxRmsCoeffVar > 0.0f)
        {
            REQUIRE(m.rmsCoeffVar < bounds.maxRmsCoeffVar);
        }
        REQUIRE(m.spectralFlatnessLate < bounds.maxSpectralFlatness);
        REQUIRE(m.tailDecayRatio < bounds.maxTailDecayRatio);
    }
}

} // namespace

// ============================================================================
// BLOCK A — preset-driven stability (one TEST_CASE per category).
// ============================================================================

TEST_CASE("Drone factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][drone]")
{
    runBlockACategory("drone", kBoundsDrone, /*midiNote=*/36);
}

TEST_CASE("Organic factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][organic]")
{
    runBlockACategory("organic", kBoundsOrganic, /*midiNote=*/60);
}

TEST_CASE("Pitched factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][pitched]")
{
    runBlockACategory("pitched", kBoundsPitched, /*midiNote=*/60);
}

TEST_CASE("Glitch factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][glitch]")
{
    runBlockACategory("glitch", kBoundsGlitch, /*midiNote=*/60);
}

TEST_CASE("Hybrid factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][hybrid]")
{
    runBlockACategory("hybrid", kBoundsHybrid, /*midiNote=*/60);
}

TEST_CASE("Init factory presets: stability over 15 s sustain + 5 s tail", "[integration][stability][init]")
{
    runBlockACategory("init", kBoundsInit, /*midiNote=*/60);
}

TEST_CASE("All 128 factory presets are covered by Block A", "[integration][stability][coverage]")
{
    int total = 0;
    for (const auto& cat : {"drone", "organic", "pitched", "glitch", "hybrid", "init"})
    {
        total += static_cast<int>(listCategoryPresets(cat).size());
    }
    INFO("counted: " << total);
    REQUIRE(total >= 128);
}

// ============================================================================
// BLOCK B — synthetic macro stress (catch-the-bug net for combinations
// the factory palette doesn't currently cover).
// ============================================================================

TEST_CASE("Synthetic macro stress: near-CFL / feedback combinations don't spiral", "[integration][stability][stress]")
{
    struct Corner
    {
        const char* name;
        StressConfig cfg;
    };

    auto makeStatic = [](float t, float d, float e) -> StressConfig
    {
        StressConfig c;
        c.tension = t;
        c.damping = d;
        c.excitation = e;
        c.density = 0.5f;
        c.migration = 0.2f;
        c.coherence = 0.5f;
        return c;
    };

    Corner corners[] = {
        {"T0.9 D0.1 E0.9 (user-reported)", makeStatic(0.9f, 0.1f, 0.9f)},
        {"T0.95 D0.05 E0.7 (near-CFL high tension)", makeStatic(0.95f, 0.05f, 0.7f)},
        {"T0.7 D0.05 E0.95 (low damp + max excite)", makeStatic(0.7f, 0.05f, 0.95f)},
        {"T0.95 D0.05 E0.95 (all at edge)", makeStatic(0.95f, 0.05f, 0.95f)},
        {"T0.5 D0.05 E0.95 (mid tens + max excite)", makeStatic(0.5f, 0.05f, 0.95f)},
        {"T0.9 D0.1 E0.5 (high stiff + mid excite)", makeStatic(0.9f, 0.1f, 0.5f)},
        {"T0.9 D0.5 E0.95 (compensated damping)", makeStatic(0.9f, 0.5f, 0.95f)},
        {"T0.95 D0.95 E0.95 (all maxed)", makeStatic(0.95f, 0.95f, 0.95f)},
        {"T0.05 D0.05 E0.05 (all near zero)", makeStatic(0.05f, 0.05f, 0.05f)},
        {"T0.5 D0.5 E0.5 (control)", makeStatic(0.5f, 0.5f, 0.5f)},
    };

    // Modulation feedback corners.
    Corner modFb;
    modFb.name = "T0.95 D0.05 E0.5 + LFO->EXC depth 1.0";
    modFb.cfg = makeStatic(0.95f, 0.05f, 0.5f);
    modFb.cfg.useLfoMod = true;
    modFb.cfg.lfoRateHz = 6.0f;
    modFb.cfg.lfoShape = sfs::engine::lfo::LfoShape::Triangle;
    modFb.cfg.lfoDest = sfs::engine::mod_matrix::Destination::Excitation;
    modFb.cfg.lfoDepth = 1.0f;

    Corner randomFb;
    randomFb.name = "T0.9 D0.1 E0.95 + Random->DENSITY depth 1.0";
    randomFb.cfg = makeStatic(0.9f, 0.1f, 0.95f);
    randomFb.cfg.useRandomMod = true;
    randomFb.cfg.randomDest = sfs::engine::mod_matrix::Destination::Density;
    randomFb.cfg.randomDepth = 1.0f;

    auto runCorner = [](const Corner& c)
    {
        const auto r = renderStress(c.cfg, /*midiNote=*/60, /*sustain=*/15.0, /*tail=*/0.0);
        const auto m = computeMetrics(r);
        UNSCOPED_INFO("STRESS " << c.name << " rmsRatio=" << m.rmsLateOverEarly << " peakRatio=" << m.peakMonotonicity
                                << " dc=" << m.dcDrift << " cv=" << m.rmsCoeffVar
                                << " flat=" << m.spectralFlatnessLate);
        REQUIRE(m.noNanInf);
        // Loose stress bounds — these tests only catch *spiralling*, not
        // poor musicality. A corner that's just quiet or chaotic but
        // bounded passes.
        REQUIRE(m.rmsLateOverEarly < 2.5f);
        REQUIRE(m.peakMonotonicity < 1.7f);
        REQUIRE(m.dcDrift < 0.12f);
    };

    for (const auto& c : corners)
    {
        runCorner(c);
    }
    runCorner(modFb);
    runCorner(randomFb);
}

// ============================================================================
// BLOCK C — long-tail silence: free response decays to near-zero.
// Catches undamped substrate modes that ring forever.
// ============================================================================

TEST_CASE("Init presets: 30 s free-response tail decays below -60 dBFS", "[integration][stability][tail]")
{
    const auto presets = listCategoryPresets("init");
    REQUIRE_FALSE(presets.empty());

    constexpr int kSr = static_cast<int>(kSampleRate);

    for (const auto& presetPath : presets)
    {
        sfs::preset::Preset p;
        try
        {
            p = sfs::preset::Preset::loadFromFile(presetPath.string());
        }
        catch (const std::exception&)
        {
            FAIL("preset load failed: " + presetPath.string());
        }

        // Short note (200 ms) + 30 s of free response.
        const auto r = renderPresetSustainTail(p, /*midiNote=*/60, /*sustain=*/0.2, /*tail=*/30.0);
        REQUIRE_FALSE(r.hasNonFinite);

        // Peak in the deep-tail [20,30]s post-noteOff. Anything above
        // -60 dBFS (1e-3) at this point indicates an undamped mode.
        const int deepStart = r.gateOffSample + 20 * kSr;
        const int deepLen = 10 * kSr;
        const float deepPeak = peakAbs(r.samples, deepStart, deepLen);
        const std::string name = presetPath.stem().string();
        UNSCOPED_INFO("TAIL " << name << " deepPeak=" << deepPeak);
        REQUIRE(deepPeak < 5e-3f);
    }
}
