// tests/contract/organic_test.cpp
//
// Sonic-corner contract test: organic (Phase 2 form per sfs-spec/08 §2.3).
// Hold a note with an organic-style preset (high MIGRATION drift, low
// COHERENCE, slow LFO modulating TENSION) and verify the spectral
// centroid moves over time in the 0.1-5 Hz band — the audible signature
// of a "drifting, alive" texture.
//
// Spec criterion: band-passed centroid RMS > 50 Hz over the 5-55 s window.
// Phase 2 trim: 6-second render, analysis on the [1, 6) window. The "RMS"
// of the bandpassed signal is approximated by the centroid's stddev
// around its window mean — equivalent to detrending then RMS for slowly-
// varying signals. Full 60 s window enables at release-gate.

#include "engine/voice.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace
{

using cfloat = std::complex<float>;

// Inline radix-2 FFT (same shape as drone_test.cpp). n MUST be a power of 2.
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

[[nodiscard]] float spectralCentroidHz(const cfloat* spec, int nFft, float sampleRate)
{
    double weightedSum = 0.0;
    double magSum = 0.0;
    const int nyquistBin = nFft / 2;
    for (int k = 1; k < nyquistBin; ++k)
    {
        const double re = static_cast<double>(spec[k].real());
        const double im = static_cast<double>(spec[k].imag());
        const double mag = std::sqrt(re * re + im * im);
        const double f = static_cast<double>(k) * static_cast<double>(sampleRate) / static_cast<double>(nFft);
        weightedSum += f * mag;
        magSum += mag;
    }
    if (magSum < 1e-12)
    {
        return 0.0f;
    }
    return static_cast<float>(weightedSum / magSum);
}

} // namespace

namespace
{

double measureOrganicStddev(sfs::engine::Voice& voice)
{
    using namespace sfs::engine::mod_matrix;
    using sfs::engine::lfo::LfoShape;

    constexpr int kSampleRate = 48000;
    constexpr int kRenderSeconds = 6;
    constexpr int kAnalysisStart = 1;
    constexpr int kAnalysisEnd = 6;
    constexpr int kNFft = 4096;
    constexpr int kHop = 1024;
    constexpr int kBlock = 256;

    voice.macros().tension = 0.5f;
    voice.macros().damping = 0.4f;
    voice.macros().density = 0.6f;
    voice.macros().migration = 0.7f;
    voice.macros().coherence = 0.2f;
    voice.macros().excitation = 0.4f;

    voice.lfo(0).setShape(LfoShape::Sine);
    voice.lfo(0).setRateHz(0.3f);

    voice.modMatrix().clearAllSlots();
    voice.modMatrix().setSlot(0, Source::Lfo1, Destination::Tension, 0.4f);

    voice.noteOn(60, 1.0f);

    const int totalSamples = kRenderSeconds * kSampleRate;
    std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
    for (int written = 0; written < totalSamples; written += kBlock)
    {
        const int n = std::min(kBlock, totalSamples - written);
        voice.renderBlock(mono.data() + written, n);
    }

    const int analysisStartSample = kAnalysisStart * kSampleRate;
    const int analysisEndSample = kAnalysisEnd * kSampleRate;

    std::vector<float> windowed(static_cast<std::size_t>(kNFft), 0.0f);
    std::vector<cfloat> spec(static_cast<std::size_t>(kNFft), {0.0f, 0.0f});
    std::vector<float> centroids;
    centroids.reserve(64);

    for (int frameStart = analysisStartSample; frameStart + kNFft <= analysisEndSample; frameStart += kHop)
    {
        for (int i = 0; i < kNFft; ++i)
        {
            windowed[static_cast<std::size_t>(i)] = mono[static_cast<std::size_t>(frameStart + i)];
        }
        applyHannWindow(windowed.data(), kNFft);
        for (int i = 0; i < kNFft; ++i)
        {
            spec[static_cast<std::size_t>(i)] = cfloat(windowed[static_cast<std::size_t>(i)], 0.0f);
        }
        fft(spec.data(), kNFft);
        const float c = spectralCentroidHz(spec.data(), kNFft, static_cast<float>(kSampleRate));
        if (c > 0.0f)
        {
            centroids.push_back(c);
        }
    }
    if (centroids.empty())
    {
        return 0.0;
    }

    double sum = 0.0;
    for (float c : centroids)
    {
        sum += static_cast<double>(c);
    }
    const double mean = sum / static_cast<double>(centroids.size());

    double sumSq = 0.0;
    for (float c : centroids)
    {
        const double d = static_cast<double>(c) - mean;
        sumSq += d * d;
    }
    const double stddev = std::sqrt(sumSq / static_cast<double>(centroids.size()));
    std::printf("[organic contract] frames=%zu mean=%.1f Hz stddev=%.1f Hz\n", centroids.size(), mean, stddev);
    return stddev;
}

} // namespace

TEST_CASE("Organic contract (1D Phase 2 form): centroid drifts > 50 Hz", "[contract][organic][1d]")
{
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    const double stddev = measureOrganicStddev(voice);
    REQUIRE(stddev > 50.0);
}

TEST_CASE("Organic contract (2D torus): centroid drifts > 50 Hz", "[contract][organic][2d]")
{
    using sfs::engine::Topology;
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    voice.setTopology(Topology::Torus2D);
    const double stddev = measureOrganicStddev(voice);
    REQUIRE(stddev > 50.0);
}
