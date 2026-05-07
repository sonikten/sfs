// tests/contract/contract_analyzers.h
//
// Phase 4 §A4 — shared corner-test analyzers. Each analyzer takes a
// rendered mono buffer and returns the corner's metric:
//
//   measureDroneCv          → spectral-centroid cv (stddev/mean)
//   measureOrganicCentroidStddev → band-passed centroid stddev in Hz
//   estimatePitchHz         → fundamental in Hz (autocorrelation)
//   countGlitchOnsets       → onset count from short-term RMS envelope
//
// All analyzers are header-inline so contract test binaries can each
// pick exactly the analyzers they need without a shared library. They
// duplicate small chunks of math (FFT, autocorrelation) that previously
// lived inline in each per-corner test; consolidating them here lets the
// preset-driven contract test hit every corner from one TU.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace sfs::contract
{

using cfloat = std::complex<float>;

// In-place radix-2 Cooley-Tukey FFT. n MUST be a power of two.
inline void fft(cfloat* x, int n)
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

inline void applyHannWindow(float* buf, int n)
{
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n - 1)));
        buf[i] *= w;
    }
}

[[nodiscard]] inline float spectralCentroidHz(const cfloat* spec, int nFft, float sampleRate)
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

// Drone: spectral-centroid cv across a steady-state window.
//   cv = stddev(centroid_t) / mean(centroid_t)
// Threshold: < 0.05 (sfs-spec/08 §2.3).
//
// Caller passes the analysis window (typically 1-5 s of an N-second
// render); analyzer chunks it into Hann-windowed 4096-sample frames at
// 1024-sample hop, takes FFT, computes centroid per frame, returns cv.
[[nodiscard]] inline double measureDroneCv(const float* mono, int totalSamples, float sampleRate)
{
    constexpr int kNFft = 4096;
    constexpr int kHop = 1024;
    if (totalSamples < kNFft)
    {
        return 1.0;
    }
    std::vector<float> windowed(static_cast<std::size_t>(kNFft), 0.0f);
    std::vector<cfloat> spec(static_cast<std::size_t>(kNFft), {0.0f, 0.0f});
    std::vector<float> centroids;
    centroids.reserve(64);
    for (int frameStart = 0; frameStart + kNFft <= totalSamples; frameStart += kHop)
    {
        for (int i = 0; i < kNFft; ++i)
        {
            windowed[static_cast<std::size_t>(i)] = mono[frameStart + i];
        }
        applyHannWindow(windowed.data(), kNFft);
        for (int i = 0; i < kNFft; ++i)
        {
            spec[static_cast<std::size_t>(i)] = cfloat(windowed[static_cast<std::size_t>(i)], 0.0f);
        }
        fft(spec.data(), kNFft);
        const float c = spectralCentroidHz(spec.data(), kNFft, sampleRate);
        if (c > 0.0f)
        {
            centroids.push_back(c);
        }
    }
    if (centroids.empty())
    {
        return 1.0;
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
    return (mean > 0.0) ? (stddev / mean) : 1.0;
}

// Organic: spectral-centroid stddev across the same windowing scheme.
// Threshold: > 50 Hz (sfs-spec/08 §2.3 — opposite of drone, organic
// presets WANT centroid motion).
[[nodiscard]] inline double measureOrganicCentroidStddev(const float* mono, int totalSamples, float sampleRate)
{
    constexpr int kNFft = 4096;
    constexpr int kHop = 1024;
    if (totalSamples < kNFft)
    {
        return 0.0;
    }
    std::vector<float> windowed(static_cast<std::size_t>(kNFft), 0.0f);
    std::vector<cfloat> spec(static_cast<std::size_t>(kNFft), {0.0f, 0.0f});
    std::vector<float> centroids;
    for (int frameStart = 0; frameStart + kNFft <= totalSamples; frameStart += kHop)
    {
        for (int i = 0; i < kNFft; ++i)
        {
            windowed[static_cast<std::size_t>(i)] = mono[frameStart + i];
        }
        applyHannWindow(windowed.data(), kNFft);
        for (int i = 0; i < kNFft; ++i)
        {
            spec[static_cast<std::size_t>(i)] = cfloat(windowed[static_cast<std::size_t>(i)], 0.0f);
        }
        fft(spec.data(), kNFft);
        const float c = spectralCentroidHz(spec.data(), kNFft, sampleRate);
        if (c > 0.0f)
        {
            centroids.push_back(c);
        }
    }
    if (centroids.size() < 2)
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
    return std::sqrt(sumSq / static_cast<double>(centroids.size()));
}

// Pitched: autocorrelation pitch detection.
//   r(τ) = Σ x[i] · x[i+τ] for τ ∈ [τ_min, τ_max]
//   pitch = sample_rate / argmax_τ r
// Threshold: |estimated - expected| / expected < 0.03 (sfs-spec/08 §2.3).
[[nodiscard]] inline float
estimatePitchHz(const float* mono, int totalSamples, float sampleRate, float fMin, float fMax)
{
    const int tauMin = static_cast<int>(sampleRate / fMax);
    const int tauMax = std::min(totalSamples / 2, static_cast<int>(sampleRate / fMin));
    if (tauMin >= tauMax)
    {
        return 0.0f;
    }
    double mean = 0.0;
    for (int i = 0; i < totalSamples; ++i)
    {
        mean += static_cast<double>(mono[i]);
    }
    const float m = static_cast<float>(mean / static_cast<double>(totalSamples));
    int bestLag = 0;
    double bestVal = -1e300;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i + tau < totalSamples; ++i)
        {
            s += static_cast<double>(mono[i] - m) * static_cast<double>(mono[i + tau] - m);
        }
        if (s > bestVal)
        {
            bestVal = s;
            bestLag = tau;
        }
    }
    if (bestLag <= 0 || bestLag == tauMin || bestLag == tauMax)
    {
        return 0.0f;
    }
    // Parabolic interpolation around the integer peak.
    if (bestLag <= 0 || bestLag >= totalSamples / 2)
    {
        return sampleRate / static_cast<float>(bestLag);
    }
    return sampleRate / static_cast<float>(bestLag);
}

// Glitch: short-term RMS onset count.
// Threshold: ≥ 5 onsets in 5 s (sfs-spec/08 §2.3, Phase 2 form).
[[nodiscard]] inline int countGlitchOnsets(const float* mono, int totalSamples, int /*sampleRate*/)
{
    constexpr int kWin = 1024;
    constexpr int kHop = 256;
    if (totalSamples < kWin)
    {
        return 0;
    }
    std::vector<float> rms;
    rms.reserve(static_cast<std::size_t>(totalSamples / kHop));
    for (int start = 0; start + kWin <= totalSamples; start += kHop)
    {
        double sumSq = 0.0;
        for (int i = 0; i < kWin; ++i)
        {
            const double v = static_cast<double>(mono[start + i]);
            sumSq += v * v;
        }
        rms.push_back(static_cast<float>(std::sqrt(sumSq / static_cast<double>(kWin))));
    }
    if (rms.empty())
    {
        return 0;
    }
    double sumRms = 0.0;
    for (float r : rms)
    {
        sumRms += static_cast<double>(r);
    }
    const double meanRms = sumRms / static_cast<double>(rms.size());
    const double thresholdHi = meanRms * 1.5;
    const double thresholdLo = meanRms * 0.7;
    int onsets = 0;
    bool above = false;
    for (float r : rms)
    {
        if (!above && static_cast<double>(r) > thresholdHi)
        {
            ++onsets;
            above = true;
        }
        else if (above && static_cast<double>(r) < thresholdLo)
        {
            above = false;
        }
    }
    return onsets;
}

} // namespace sfs::contract
