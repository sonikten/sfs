// tools/render_presets/main.cpp
//
// Multi-preset audio regression harness. For each known preset
// configuration (default, drone, pitched, organic, glitch), renders 5 s
// of audio at 48 kHz / 256-block / stereo, and prints analytic
// fingerprints: peak, RMS, DC offset, zero-crossing rate, RMS-by-second
// (decay envelope), substrate peak.
//
// Output:
//   <out_dir>/<preset>.wav  human-listenable
//   <out_dir>/<preset>.raw  channel-interleaved float32 PCM (hash target)
//   <out_dir>/report.txt    side-by-side analytics, also stdout
//
// Used to catch silent audio regressions ("everything still compiles but
// the default preset now sounds wrong") between commits. Complements the
// contract tests, which only assert the four-corner thresholds; this
// tool stores the WAVs so a human can audition + a-b them.

#include "engine/voice.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 256;
constexpr float kHoldSeconds = 5.0f;
constexpr int kSubstrateN = 1024;
constexpr int kAgentCount = 16;

struct PresetCfg
{
    const char* name;
    float tension;
    float damping;
    float density;
    float migration;
    float coherence;
    float excitation;
    bool clearMatrix;
    int midiNote;
    float velocity;
};

const PresetCfg kPresets[] = {
    // name      T    D    Den  Mig  Coh  Exc   clearMx  note vel
    {"default", 0.5f, 0.3f, 0.6f, 0.2f, 0.8f, 0.3f, false, 60, 1.0f},
    {"drone", 0.5f, 0.3f, 0.6f, 0.0f, 1.0f, 0.0f, true, 60, 1.0f},
    {"pitched", 0.5f, 0.3f, 0.6f, 0.0f, 1.0f, 0.0f, true, 64, 1.0f},
    {"organic", 0.5f, 0.4f, 0.6f, 0.7f, 0.2f, 0.4f, true, 60, 1.0f},
    {"glitch", 0.6f, 0.2f, 0.5f, 0.0f, 0.5f, 0.5f, false, 60, 1.0f},
};

struct Stats
{
    double meanL;
    double meanR;
    float peak;
    float rms;
    int zeroCrossingsPerSec;
    std::vector<float> rmsBySecond; // length = totalSec
    float substratePeak;
    float substrateMean;
};

Stats analyse(const std::vector<float>& interleaved, int sampleRate, float substratePeak, float substrateMean)
{
    Stats s{};
    const std::size_t total = interleaved.size();
    if (total == 0)
    {
        return s;
    }

    double sumL = 0.0, sumR = 0.0;
    double sumSq = 0.0;
    float peak = 0.0f;
    int zeroCross = 0;
    float prev = 0.0f;
    for (std::size_t i = 0; i < total; ++i)
    {
        const float v = interleaved[i];
        const float a = std::fabs(v);
        if (a > peak)
        {
            peak = a;
        }
        sumSq += static_cast<double>(v) * static_cast<double>(v);
        if ((i & 1) == 0)
        {
            sumL += static_cast<double>(v);
            // Count L-channel zero crossings only.
            if (i > 0 && (prev < 0.0f) != (v < 0.0f))
            {
                ++zeroCross;
            }
            prev = v;
        }
        else
        {
            sumR += static_cast<double>(v);
        }
    }
    const std::size_t perCh = total / 2;
    s.meanL = sumL / static_cast<double>(perCh);
    s.meanR = sumR / static_cast<double>(perCh);
    s.peak = peak;
    s.rms = std::sqrt(static_cast<float>(sumSq / static_cast<double>(total)));
    s.zeroCrossingsPerSec = zeroCross * sampleRate / static_cast<int>(perCh);

    // RMS by second (L channel).
    const int sec = static_cast<int>(perCh / static_cast<std::size_t>(sampleRate));
    for (int t = 0; t < sec; ++t)
    {
        double sq = 0.0;
        for (int i = 0; i < sampleRate; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(2 * (t * sampleRate + i));
            const double v = static_cast<double>(interleaved[idx]);
            sq += v * v;
        }
        s.rmsBySecond.push_back(static_cast<float>(std::sqrt(sq / static_cast<double>(sampleRate))));
    }

    s.substratePeak = substratePeak;
    s.substrateMean = substrateMean;
    return s;
}

void writeWav(const juce::File& dest, const std::vector<float>& interleaved, int sampleRate, int channels)
{
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream>(dest);
    if (!stream->openedOk())
    {
        std::fprintf(stderr, "WAV open failed: %s\n", dest.getFullPathName().toRawUTF8());
        return;
    }
    auto* writer = wav.createWriterFor(stream.get(), sampleRate, channels, 32, {}, 0);
    if (writer == nullptr)
    {
        std::fprintf(stderr, "WAV writer create failed\n");
        return;
    }
    stream.release(); // writer takes ownership
    const int numFrames = static_cast<int>(interleaved.size() / channels);
    juce::AudioBuffer<float> buffer(channels, numFrames);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* dst = buffer.getWritePointer(ch);
        for (int f = 0; f < numFrames; ++f)
        {
            dst[f] = interleaved[static_cast<std::size_t>(f * channels + ch)];
        }
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
    delete writer;
}

void writeRaw(const juce::File& dest, const std::vector<float>& interleaved)
{
    auto stream = std::make_unique<juce::FileOutputStream>(dest);
    if (!stream->openedOk())
    {
        std::fprintf(stderr, "raw open failed: %s\n", dest.getFullPathName().toRawUTF8());
        return;
    }
    stream->setPosition(0);
    stream->truncate();
    stream->write(interleaved.data(), interleaved.size() * sizeof(float));
}

} // namespace

int main(int argc, char** argv)
{
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile("build/preset_renders");
    if (argc > 1)
    {
        outDir = juce::File(juce::String::fromUTF8(argv[1]));
    }
    outDir.createDirectory();

    std::printf("Rendering %d presets -> %s\n",
                static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])),
                outDir.getFullPathName().toRawUTF8());

    std::string report;
    report += "Preset    Peak    RMS     DC(L)     DC(R)     ZC/sec  Sub|peak  Sub|mean  RMS by second\n";
    report +=
        "------    ------  ------  --------  --------  ------  --------  --------  ----------------------------\n";

    for (const auto& preset : kPresets)
    {
        sfs::engine::Voice voice(kSubstrateN, kAgentCount, static_cast<float>(kSampleRate));
        voice.macros().tension = preset.tension;
        voice.macros().damping = preset.damping;
        voice.macros().density = preset.density;
        voice.macros().migration = preset.migration;
        voice.macros().coherence = preset.coherence;
        voice.macros().excitation = preset.excitation;
        if (preset.clearMatrix)
        {
            voice.modMatrix().clearAllSlots();
        }
        voice.noteOn(preset.midiNote, preset.velocity);

        const int totalSamples = static_cast<int>(kHoldSeconds * static_cast<float>(kSampleRate));
        std::vector<float> interleaved(static_cast<std::size_t>(totalSamples * 2), 0.0f);
        std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);

        // Track substrate peak/mean over the full render to catch drift.
        float substratePeak = 0.0f;
        double substrateMeanSum = 0.0;
        int substrateMeanCount = 0;
        std::vector<float> snap(static_cast<std::size_t>(kSubstrateN), 0.0f);

        int written = 0;
        while (written < totalSamples)
        {
            const int n = std::min(kBlockSize, totalSamples - written);
            voice.renderBlockStereo(bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i)
            {
                interleaved[static_cast<std::size_t>(2 * (written + i) + 0)] = bufL[static_cast<std::size_t>(i)];
                interleaved[static_cast<std::size_t>(2 * (written + i) + 1)] = bufR[static_cast<std::size_t>(i)];
            }
            written += n;

            // Sample the substrate state every ~100 ms.
            if ((written / kBlockSize) % 16 == 0)
            {
                voice.snapshotSubstrate(snap.data(), kSubstrateN);
                double m = 0.0;
                float p = 0.0f;
                for (float v : snap)
                {
                    m += static_cast<double>(v);
                    const float a = std::fabs(v);
                    if (a > p)
                    {
                        p = a;
                    }
                }
                if (p > substratePeak)
                {
                    substratePeak = p;
                }
                substrateMeanSum += m / static_cast<double>(kSubstrateN);
                ++substrateMeanCount;
            }
        }

        const float substrateMean = (substrateMeanCount > 0)
                                        ? static_cast<float>(substrateMeanSum / static_cast<double>(substrateMeanCount))
                                        : 0.0f;

        Stats s = analyse(interleaved, kSampleRate, substratePeak, substrateMean);

        // Files.
        writeWav(outDir.getChildFile(juce::String(preset.name) + ".wav"), interleaved, kSampleRate, 2);
        writeRaw(outDir.getChildFile(juce::String(preset.name) + ".raw"), interleaved);

        // Report row.
        char row[512];
        std::string rmsByLine;
        for (float r : s.rmsBySecond)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.4f ", r);
            rmsByLine += buf;
        }
        std::snprintf(row,
                      sizeof(row),
                      "%-8s  %.4f  %.4f  %+.5f  %+.5f  %5d   %.4f    %+.5f   %s",
                      preset.name,
                      static_cast<double>(s.peak),
                      static_cast<double>(s.rms),
                      s.meanL,
                      s.meanR,
                      s.zeroCrossingsPerSec,
                      static_cast<double>(s.substratePeak),
                      static_cast<double>(s.substrateMean),
                      rmsByLine.c_str());
        report += row;
        report += "\n";
    }

    // Stdout + report file.
    std::printf("\n%s", report.c_str());
    juce::File reportFile = outDir.getChildFile("report.txt");
    reportFile.replaceWithText(report);
    std::printf("\nReport: %s\n", reportFile.getFullPathName().toRawUTF8());
    return 0;
}
