// tools/sfs_preset_render/main.cpp
//
// Phase 4 §A3 — audio-hash render harness. Loads a `.sfs` preset, renders
// the canonical "30 s C4 at velocity 1.0, 48 kHz, 256-block stereo" clip
// per Doc 06 §3.3, and writes channel-interleaved float32 PCM to a `.raw`
// file. Pair with `tools/hash_pcm.sh` to produce the SHA-256 that goes
// into the preset's `_audio_hash` field.
//
// Usage:
//   sfs_preset_render <input.sfs> <output.raw> [--seconds 30.0] [--note 60]
//                      [--velocity 1.0] [--sr 48000] [--block 256]
//
// Exit codes:
//   0  success
//   1  invocation / I/O error (missing args, malformed preset, etc.)
//   2  preset failed engine apply (e.g. NaN in render — should never happen)

#include "engine/voice_manager.h"
#include "preset/preset.h"
#include "preset/preset_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string inputPath;
    std::string outputPath;
    double sampleRate = 48000.0;
    int blockSize = 256;
    double seconds = 30.0;
    int midiNote = 60;                // C4 — Doc 06 §3.3
    float velocity = 100.0f / 127.0f; // velocity 100 (normalised) — Doc 06 §3.3
    int gateOffSample = 24000;        // gate-off at 0.5 s @ 48 kHz — Doc 06 §3.3 (0 = no gate-off)
};

void printUsage()
{
    std::fprintf(stderr,
                 "usage: sfs_preset_render <input.sfs> <output.raw> [--seconds 30.0]\n"
                 "                          [--note 60] [--velocity 0.787]\n"
                 "                          [--gate-off-sample 24000]\n"
                 "                          [--sr 48000] [--block 256]\n"
                 "\n"
                 "Renders the canonical preset audio-hash input per Doc 06 §3.3:\n"
                 "  velocity 100/127, gate-on at sample 0, gate-off at sample 24000\n"
                 "  (0.5 s @ 48 kHz), render until 30 s. Pass --gate-off-sample 0 to\n"
                 "  suppress gate-off and render fully sustained.\n");
}

[[nodiscard]] bool parseArgs(int argc, char** argv, Options& opts)
{
    if (argc < 3)
    {
        return false;
    }
    opts.inputPath = argv[1];
    opts.outputPath = argv[2];
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if ((a == "--seconds") && i + 1 < argc)
        {
            opts.seconds = std::stod(argv[++i]);
        }
        else if ((a == "--note") && i + 1 < argc)
        {
            opts.midiNote = std::atoi(argv[++i]);
        }
        else if ((a == "--velocity") && i + 1 < argc)
        {
            opts.velocity = std::stof(argv[++i]);
        }
        else if ((a == "--gate-off-sample") && i + 1 < argc)
        {
            opts.gateOffSample = std::atoi(argv[++i]);
        }
        else if ((a == "--sr") && i + 1 < argc)
        {
            opts.sampleRate = std::stod(argv[++i]);
        }
        else if ((a == "--block") && i + 1 < argc)
        {
            opts.blockSize = std::atoi(argv[++i]);
        }
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options opts;
    if (!parseArgs(argc, argv, opts))
    {
        printUsage();
        return 1;
    }

    sfs::preset::Preset preset;
    try
    {
        preset = sfs::preset::Preset::loadFromFile(opts.inputPath);
    }
    catch (const sfs::preset::PresetParseError& e)
    {
        std::fprintf(stderr, "preset load failed: %s\n", e.what());
        return 1;
    }

    constexpr int kSubstrateCells = 1024;
    constexpr int kAgentCount = 16;
    sfs::engine::VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(opts.sampleRate));
    sfs::preset::applyToEngine(preset, vm);
    vm.noteOn(opts.midiNote, opts.velocity);

    const int totalSamples = static_cast<int>(opts.seconds * opts.sampleRate);
    std::vector<float> bufL(static_cast<std::size_t>(opts.blockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(opts.blockSize), 0.0f);
    std::vector<float> interleaved(static_cast<std::size_t>(totalSamples * 2), 0.0f);

    int written = 0;
    bool gateOffFired = false;
    while (written < totalSamples)
    {
        // Fire gate-off at the configured sample. Doc 06 §3.3 specifies
        // sample 24000 (0.5 s @ 48 kHz) so the canonical render captures
        // the release path + substrate ringing tail. opts.gateOffSample = 0
        // suppresses gate-off entirely (sustained variant).
        if (!gateOffFired && opts.gateOffSample > 0 && written >= opts.gateOffSample)
        {
            vm.noteOff(opts.midiNote);
            gateOffFired = true;
        }
        const int n = std::min(opts.blockSize, totalSamples - written);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            interleaved[static_cast<std::size_t>(2 * (written + i) + 0)] = bufL[static_cast<std::size_t>(i)];
            interleaved[static_cast<std::size_t>(2 * (written + i) + 1)] = bufR[static_cast<std::size_t>(i)];
        }
        // Defensive NaN check — if a malformed preset slipped through and
        // the render explodes, fail loudly rather than emit a NaN .raw
        // that hashes deterministically without the user noticing.
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(bufL[static_cast<std::size_t>(i)]) || !std::isfinite(bufR[static_cast<std::size_t>(i)]))
            {
                std::fprintf(stderr, "NaN/Inf in render at sample %d — preset is unstable\n", written + i);
                return 2;
            }
        }
        written += n;
    }

    std::ofstream out(opts.outputPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::fprintf(stderr, "cannot open output: %s\n", opts.outputPath.c_str());
        return 1;
    }
    out.write(reinterpret_cast<const char*>(interleaved.data()),
              static_cast<std::streamsize>(interleaved.size() * sizeof(float)));
    if (!out)
    {
        std::fprintf(stderr, "write failed: %s\n", opts.outputPath.c_str());
        return 1;
    }
    out.close();

    std::printf("rendered %.1f s of %s -> %s (%d samples stereo float32)\n",
                opts.seconds,
                opts.inputPath.c_str(),
                opts.outputPath.c_str(),
                totalSamples);
    return 0;
}
