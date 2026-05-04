// tools/sfs_render/main.cpp
//
// Headless render rig. Phase 0 step 7. Loads the SFS AudioProcessor (Phase 0
// skeleton: continuous polynomial 440 Hz sine), drives it for a fixed
// duration, and writes a WAV file. No DAW host, no GUI.
//
// This is the substrate for every later integration test: contract tests,
// determinism hash, factory-preset audio hashing (Phase 4), etc. Keep it
// minimal and dependency-free beyond JUCE + sfs_plugin_core.
//
// Usage:
//   sfs_render [--sr 48000] [--block 256] [--seconds 1.0] [--channels 2]
//              [--out output.wav]
//
// Exit codes:
//   0  success
//   1  invocation / I/O error
//   2  AudioProcessor reported an unsupported bus layout

#include "PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace
{

struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 256;
    double durationSec = 1.0;
    int numChannels = 2;
    std::string outputPath = "sine_skeleton_48k_256.wav";
    std::string rawOutputPath; // when non-empty, also write raw channel-interleaved float32 PCM

    // MIDI gate. <0 disables (renders silence — useful for engine self-test).
    // Default matches the Phase 0 era when there was no engine; Phase 1 uses
    // --note 60 (C4) to actually play through the engine.
    int midiNote = -1;
    float midiVelocity = 1.0f;
    // Gate on/off as sample indices. -1 means "at start" / "never (gate held)".
    juce::int64 gateOnSample = 0;
    juce::int64 gateOffSample = -1;
};

void printUsage()
{
    std::fprintf(stderr,
                 "usage: sfs_render [--sr <hz>] [--block <n>] [--seconds <s>]\n"
                 "                  [--channels <n>] [--out <path>] [--raw <path>]\n"
                 "                  [--note <midi>] [--velocity <0-1>]\n"
                 "                  [--gate-on-sample <N>] [--gate-off-sample <N>]\n"
                 "\n"
                 "Headless render of the SFS plug-in. Defaults:\n"
                 "  --sr 48000  --block 256  --seconds 1.0  --channels 2\n"
                 "  --out sine_skeleton_48k_256.wav\n"
                 "  --raw <none>      when set, also write channel-interleaved float32\n"
                 "                    PCM (no header) to <path> for hashing per spec\n"
                 "                    sfs-spec/06 §2.2.\n"
                 "  --note <none>     when set, send a MIDI note-on at gate-on-sample\n"
                 "                    and a note-off at gate-off-sample. With no\n"
                 "                    --note, the engine plays silence (no MIDI).\n"
                 "  --velocity 1.0\n"
                 "  --gate-on-sample 0\n"
                 "  --gate-off-sample <duration/2 in samples; -1 holds gate>\n");
}

bool parseArgs(int argc, char** argv, RenderOptions& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};
        const auto next = [&](double* dst, int* dstI = nullptr) -> bool
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "sfs_render: missing value for %.*s\n", static_cast<int>(arg.size()), arg.data());
                return false;
            }
            if (dstI != nullptr)
            {
                *dstI = std::atoi(argv[i]);
            }
            else
            {
                *dst = std::atof(argv[i]);
            }
            return true;
        };

        if (arg == "--sr")
        {
            if (!next(&opts.sampleRate))
                return false;
        }
        else if (arg == "--block")
        {
            double tmp = 0;
            if (!next(&tmp, &opts.blockSize))
                return false;
        }
        else if (arg == "--seconds")
        {
            if (!next(&opts.durationSec))
                return false;
        }
        else if (arg == "--channels")
        {
            double tmp = 0;
            if (!next(&tmp, &opts.numChannels))
                return false;
        }
        else if (arg == "--out")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "sfs_render: missing value for --out\n");
                return false;
            }
            opts.outputPath = argv[i];
        }
        else if (arg == "--raw")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "sfs_render: missing value for --raw\n");
                return false;
            }
            opts.rawOutputPath = argv[i];
        }
        else if (arg == "--note")
        {
            double tmp = 0;
            if (!next(&tmp, &opts.midiNote))
                return false;
        }
        else if (arg == "--velocity")
        {
            double v = 0;
            if (!next(&v))
                return false;
            opts.midiVelocity = static_cast<float>(v);
        }
        else if (arg == "--gate-on-sample")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "sfs_render: missing value for --gate-on-sample\n");
                return false;
            }
            opts.gateOnSample = static_cast<juce::int64>(std::atoll(argv[i]));
        }
        else if (arg == "--gate-off-sample")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "sfs_render: missing value for --gate-off-sample\n");
                return false;
            }
            opts.gateOffSample = static_cast<juce::int64>(std::atoll(argv[i]));
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage();
            std::exit(0);
        }
        else
        {
            std::fprintf(stderr, "sfs_render: unknown argument '%.*s'\n", static_cast<int>(arg.size()), arg.data());
            printUsage();
            return false;
        }
    }

    if (opts.sampleRate <= 0.0 || opts.blockSize <= 0 || opts.durationSec <= 0.0 || opts.numChannels <= 0)
    {
        std::fprintf(stderr, "sfs_render: invalid argument values\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    RenderOptions opts;
    if (!parseArgs(argc, argv, opts))
    {
        return 1;
    }

    sfs::plugin::SfsAudioProcessor processor;

    // The plug-in advertises stereo only by default; ask for the requested
    // layout, fall back to stereo if unsupported (Phase 0 skeleton supports
    // mono and stereo).
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(opts.numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    if (!processor.checkBusesLayoutSupported(layout))
    {
        std::fprintf(stderr, "sfs_render: AudioProcessor does not support %d-channel output\n", opts.numChannels);
        return 2;
    }
    processor.setBusesLayout(layout);

    processor.prepareToPlay(opts.sampleRate, opts.blockSize);

    // Output WAV: 32-bit float, little-endian. Single source of truth for the
    // determinism harness; do NOT change format without bumping reference hashes.
    juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(opts.outputPath));
    if (juce::File::isAbsolutePath(juce::String(opts.outputPath)))
    {
        outFile = juce::File(opts.outputPath);
    }
    outFile.deleteFile();
    outFile.getParentDirectory().createDirectory();

    auto fileStream = std::make_unique<juce::FileOutputStream>(outFile);
    if (!fileStream->openedOk())
    {
        std::fprintf(stderr,
                     "sfs_render: cannot open output '%s' for writing\n",
                     outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::WavAudioFormat wavFormat;
    constexpr int kBitsPerSample = 32; // 32-bit float
    juce::StringPairArray emptyMetadata;
    std::unique_ptr<juce::AudioFormatWriter> writer{
        wavFormat.createWriterFor(fileStream.release(),
                                  opts.sampleRate,
                                  static_cast<unsigned int>(opts.numChannels),
                                  kBitsPerSample,
                                  emptyMetadata,
                                  /*qualityOptionIndex=*/0)};

    if (writer == nullptr)
    {
        std::fprintf(stderr, "sfs_render: cannot create WAV writer\n");
        return 1;
    }

    // Optional raw PCM stream: channel-interleaved float32, no header.
    // Per sfs-spec/06 §2.2, the determinism hash MUST be over raw PCM frames,
    // not over the .wav file (whose header may differ across platforms for
    // non-audio reasons — BWF timestamps, INFO chunk ordering, etc.).
    std::unique_ptr<juce::FileOutputStream> rawStream;
    if (!opts.rawOutputPath.empty())
    {
        juce::File rawFile(juce::File::isAbsolutePath(juce::String(opts.rawOutputPath))
                               ? juce::File(opts.rawOutputPath)
                               : juce::File::getCurrentWorkingDirectory().getChildFile(opts.rawOutputPath));
        rawFile.deleteFile();
        rawFile.getParentDirectory().createDirectory();
        rawStream = std::make_unique<juce::FileOutputStream>(rawFile);
        if (!rawStream->openedOk())
        {
            std::fprintf(stderr,
                         "sfs_render: cannot open raw output '%s' for writing\n",
                         rawFile.getFullPathName().toRawUTF8());
            return 1;
        }
    }

    juce::AudioBuffer<float> buffer(opts.numChannels, opts.blockSize);

    const auto totalSamples = static_cast<juce::int64>(opts.durationSec * opts.sampleRate + 0.5);

    // Default gate-off to halfway through, so we get 50% sustain + 50% release tail.
    if (opts.midiNote >= 0 && opts.gateOffSample < 0)
    {
        opts.gateOffSample = totalSamples / 2;
    }

    juce::int64 written = 0;

    while (written < totalSamples)
    {
        const int n = static_cast<int>(std::min<juce::int64>(opts.blockSize, totalSamples - written));
        buffer.clear();

        // Build the MIDI buffer for this block: note-on / note-off events that
        // fall within [written, written + n) become offsets within this block.
        juce::MidiBuffer midi;
        if (opts.midiNote >= 0)
        {
            const juce::int64 onAbs = opts.gateOnSample;
            const juce::int64 offAbs = opts.gateOffSample;
            if (onAbs >= written && onAbs < written + n)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, opts.midiNote, opts.midiVelocity),
                              static_cast<int>(onAbs - written));
            }
            if (offAbs >= 0 && offAbs >= written && offAbs < written + n)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, opts.midiNote), static_cast<int>(offAbs - written));
            }
        }

        processor.processBlock(buffer, midi);
        writer->writeFromAudioSampleBuffer(buffer, 0, n);

        if (rawStream != nullptr)
        {
            // Channel-interleaved float32. All v1.0 platforms are little-endian
            // (x86_64 + arm64) so we can write host-order bytes directly.
            for (int s = 0; s < n; ++s)
            {
                for (int ch = 0; ch < opts.numChannels; ++ch)
                {
                    const float v = buffer.getSample(ch, s);
                    rawStream->write(&v, sizeof(float));
                }
            }
        }

        written += n;
    }

    writer.reset();    // flushes header + data
    rawStream.reset(); // flushes raw bytes
    processor.releaseResources();

    std::fprintf(stdout,
                 "wrote %lld samples (%.3f s @ %.0f Hz, %d ch) -> %s\n",
                 static_cast<long long>(written),
                 static_cast<double>(written) / opts.sampleRate,
                 opts.sampleRate,
                 opts.numChannels,
                 outFile.getFullPathName().toRawUTF8());
    return 0;
}
