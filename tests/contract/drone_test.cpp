// tests/contract/drone_test.cpp
//
// Sonic-corner contract test: drone-degraded (Phase 1 form per spec
// sfs-spec/08 §2.3). Renders the engine sustaining a held note and
// asserts the spectral centroid is stable over time:
//
//     stddev(centroid) / mean(centroid) < 0.05  (5%)
//
// Phase 1 uses a 5-second sustain (the spec's full criterion uses 60 s
// with a 5-55 s window; we trim for test wall-clock) and analyses a
// 4-second window starting 1 second after gate-on so the substrate has
// reached its quasi-steady state. The full 60 s criterion gets enabled
// when the contract harness lands as a separate workflow.
//
// FFT inline because we don't want to vendor KissFFT just for offline
// analysis. Naive radix-2 Cooley-Tukey is fine for tens of frames; the
// FFT cost is tiny next to the engine render.

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

// In-place radix-2 Cooley-Tukey FFT. n MUST be a power of two.
void fft(cfloat* x, int n)
{
    // Bit-reversal permutation.
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

    // Butterflies.
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

// Hann window for STFT framing.
void applyHannWindow(float* buf, int n)
{
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n - 1)));
        buf[i] *= w;
    }
}

// Spectral centroid of a single FFT frame, in Hz.
//   centroid = Σ(f_k · |X_k|) / Σ|X_k|   over the positive-frequency bins
[[nodiscard]] float spectralCentroidHz(const cfloat* spec, int nFft, float sampleRate)
{
    double weightedSum = 0.0;
    double magSum = 0.0;
    const int nyquistBin = nFft / 2;
    for (int k = 1; k < nyquistBin; ++k) // skip DC (k=0); ignore Nyquist (k=N/2) symmetry
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

TEST_CASE("Drone contract (degraded Phase 1 form): spectral centroid is stable", "[contract][drone]")
{
    using sfs::engine::Voice;

    constexpr int kSampleRate = 48000;
    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;
    constexpr int kRenderSeconds = 5;
    constexpr int kAnalysisStart = 1; // skip first 1 s (transient)
    constexpr int kAnalysisEnd = 5;   // analyse seconds [1, 5)
    constexpr int kNFft = 4096;
    constexpr int kHop = 1024;

    Voice voice(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    voice.noteOn(60, 1.0f); // C4

    // Configure for a Phase 1 "drone-degraded" preset: agents are static
    // (no migration drift) and don't bend their pitch from the substrate
    // (modSensitivity = 0). The substrate still rings (shaping the timbre)
    // but its spectral cross-section seen by the harvester doesn't wander.
    // Phase 2's MIGRATION + EXCITATION macros expose these to users.
    auto& agents = voice.agents();
    for (int i = 0; i < agents.activeCount(); ++i)
    {
        agents.mutableAgent(i).migrationRate = 0.0f;
        agents.mutableAgent(i).modSensitivity = 0.0f;
    }

    // Render mono into a contiguous buffer.
    const int totalSamples = kRenderSeconds * kSampleRate;
    std::vector<float> mono(static_cast<std::size_t>(totalSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < totalSamples; written += kBlock)
    {
        const int n = std::min(kBlock, totalSamples - written);
        voice.renderBlock(mono.data() + written, n);
    }

    // STFT over the analysis window.
    const int analysisStartSample = kAnalysisStart * kSampleRate;
    const int analysisEndSample = kAnalysisEnd * kSampleRate;
    REQUIRE(analysisStartSample + kNFft <= analysisEndSample);

    std::vector<float> windowed(static_cast<std::size_t>(kNFft), 0.0f);
    std::vector<cfloat> spec(static_cast<std::size_t>(kNFft), {0.0f, 0.0f});
    std::vector<float> centroids;
    centroids.reserve(64);

    for (int frameStart = analysisStartSample; frameStart + kNFft <= analysisEndSample; frameStart += kHop)
    {
        // Copy + window.
        for (int i = 0; i < kNFft; ++i)
        {
            windowed[static_cast<std::size_t>(i)] = mono[static_cast<std::size_t>(frameStart + i)];
        }
        applyHannWindow(windowed.data(), kNFft);

        // To complex.
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

    REQUIRE(!centroids.empty());

    // Compute mean and stddev of the centroid time series.
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

    const double cv = (mean > 0.0) ? (stddev / mean) : 1.0;

    std::printf("\n[drone contract] frames=%zu  mean centroid=%.1f Hz  stddev=%.1f Hz  cv=%.4f\n",
                centroids.size(),
                mean,
                stddev,
                cv);

    // Spec criterion: stddev/mean < 0.05 (5%). Drone IS a stable timbre.
    REQUIRE(cv < 0.05);
}
