// tools/wav_diff/main.cpp
//
// Find the first sample where two WAV files diverge, and summarise all
// divergences. Used by the determinism CI compare job (`.github/workflows/
// determinism.yml`) to attach a useful diagnostic to a failing run.
//
// Usage:
//   wav_diff <a.wav> <b.wav> [--epsilon N]
//
//     --epsilon N   Treat samples as equal if |a - b| <= N (default 0).
//                   For float32 WAV that's exact; only relax when investigating
//                   a known-tolerable drift before deciding to fix the root cause.
//
// Exit codes:
//   0  files match within epsilon
//   1  files differ
//   2  invocation problem (missing file, format mismatch)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{

void printUsage()
{
    std::fprintf(stderr,
                 "usage: wav_diff <a.wav> <b.wav> [--epsilon N]\n"
                 "\n"
                 "Compare two WAV files sample-by-sample. Exit 0 on match, 1 on diff.\n");
}

struct LoadedWav
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
    int numChannels = 0;
    juce::int64 numSamples = 0;
    int bitsPerSample = 0;
    juce::String format;
};

bool loadWav(const juce::File& file, LoadedWav& out, juce::AudioFormatManager& mgr)
{
    if (!file.existsAsFile())
    {
        std::fprintf(stderr, "wav_diff: file not found: %s\n", file.getFullPathName().toRawUTF8());
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader{mgr.createReaderFor(file)};
    if (reader == nullptr)
    {
        std::fprintf(stderr, "wav_diff: unsupported audio format: %s\n", file.getFullPathName().toRawUTF8());
        return false;
    }

    out.sampleRate = reader->sampleRate;
    out.numChannels = static_cast<int>(reader->numChannels);
    out.numSamples = reader->lengthInSamples;
    out.bitsPerSample = static_cast<int>(reader->bitsPerSample);
    out.format = reader->getFormatName();

    out.buffer.setSize(out.numChannels, static_cast<int>(out.numSamples));
    reader->read(&out.buffer, 0, static_cast<int>(out.numSamples), 0, true, true);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printUsage();
        return 2;
    }

    juce::File aFile(argv[1]);
    juce::File bFile(argv[2]);
    float epsilon = 0.0f;

    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if (arg == "--epsilon" && i + 1 < argc)
        {
            epsilon = static_cast<float>(std::atof(argv[++i]));
        }
        else
        {
            std::fprintf(stderr, "wav_diff: unknown argument '%s'\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    LoadedWav a, b;
    if (!loadWav(aFile, a, mgr) || !loadWav(bFile, b, mgr))
    {
        return 2;
    }

    if (a.sampleRate != b.sampleRate || a.numChannels != b.numChannels || a.numSamples != b.numSamples ||
        a.bitsPerSample != b.bitsPerSample)
    {
        std::fprintf(stderr,
                     "wav_diff: format mismatch — refusing to diff:\n"
                     "  a: %.0f Hz, %d ch, %lld samples, %d-bit %s\n"
                     "  b: %.0f Hz, %d ch, %lld samples, %d-bit %s\n",
                     a.sampleRate,
                     a.numChannels,
                     static_cast<long long>(a.numSamples),
                     a.bitsPerSample,
                     a.format.toRawUTF8(),
                     b.sampleRate,
                     b.numChannels,
                     static_cast<long long>(b.numSamples),
                     b.bitsPerSample,
                     b.format.toRawUTF8());
        return 2;
    }

    std::fprintf(stdout,
                 "comparing %lld samples × %d ch @ %.0f Hz (epsilon=%g)\n",
                 static_cast<long long>(a.numSamples),
                 a.numChannels,
                 a.sampleRate,
                 epsilon);

    juce::int64 firstDiffSample = -1;
    int firstDiffChan = -1;
    juce::int64 numDiffSamples = 0;
    float maxAbsDiff = 0.0f;
    juce::int64 maxDiffSample = -1;
    int maxDiffChan = -1;
    double sumSqDiff = 0.0;

    for (int ch = 0; ch < a.numChannels; ++ch)
    {
        const float* ap = a.buffer.getReadPointer(ch);
        const float* bp = b.buffer.getReadPointer(ch);
        for (juce::int64 i = 0; i < a.numSamples; ++i)
        {
            const float d = std::fabs(ap[i] - bp[i]);
            if (d > epsilon)
            {
                if (firstDiffSample < 0)
                {
                    firstDiffSample = i;
                    firstDiffChan = ch;
                }
                ++numDiffSamples;
                if (d > maxAbsDiff)
                {
                    maxAbsDiff = d;
                    maxDiffSample = i;
                    maxDiffChan = ch;
                }
                sumSqDiff += static_cast<double>(d) * d;
            }
        }
    }

    if (numDiffSamples == 0)
    {
        std::fprintf(stdout, "MATCH (within epsilon=%g)\n", epsilon);
        return 0;
    }

    const double rmse = std::sqrt(sumSqDiff / static_cast<double>(numDiffSamples));
    std::fprintf(stdout,
                 "DIFF\n"
                 "  first divergence:  sample=%lld  channel=%d  block=%lld\n"
                 "                     a=%.9g  b=%.9g  |a-b|=%.9g\n"
                 "  max divergence:    sample=%lld  channel=%d  |a-b|=%.9g\n"
                 "  total divergent:   %lld of %lld samples\n"
                 "  RMSE over diffs:   %.9g\n",
                 static_cast<long long>(firstDiffSample),
                 firstDiffChan,
                 static_cast<long long>(firstDiffSample / 256),
                 a.buffer.getSample(firstDiffChan, static_cast<int>(firstDiffSample)),
                 b.buffer.getSample(firstDiffChan, static_cast<int>(firstDiffSample)),
                 std::fabs(a.buffer.getSample(firstDiffChan, static_cast<int>(firstDiffSample)) -
                           b.buffer.getSample(firstDiffChan, static_cast<int>(firstDiffSample))),
                 static_cast<long long>(maxDiffSample),
                 maxDiffChan,
                 maxAbsDiff,
                 static_cast<long long>(numDiffSamples),
                 static_cast<long long>(a.numSamples * a.numChannels),
                 rmse);
    return 1;
}
