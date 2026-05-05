// tools/sfs_profile/main.cpp
//
// Phase 1 §9 step 14 — CPU profile micro-benchmark. Times the per-sample
// engine inner loop (substrate.step + agent processOneSample + harvester
// read + DC block + soft clip) so we can compare against the spec's
// `08 §2.5` budget table as engine work progresses.
//
// Usage:
//   sfs_profile [--substrate-cells N] [--agents N] [--seconds S] [--repeat R]
//
// Reports wall-clock per render, samples/sec throughput, real-time factor
// (1.0 = exactly real-time at 48 kHz, > 1.0 = headroom).

#include "engine/voice.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace
{

struct Options
{
    int substrateCells = 1024;
    int agentCount = 16;
    double seconds = 1.0;
    int repeat = 5;
    int midiNote = 60;
};

void printUsage()
{
    std::fprintf(stderr,
                 "usage: sfs_profile [--substrate-cells N] [--agents N]\n"
                 "                   [--seconds S] [--repeat R] [--note M]\n"
                 "\n"
                 "Defaults: 1024-cell substrate, 16 agents, 1.0 s render, 5 repetitions, MIDI 60.\n");
}

bool parseArgs(int argc, char** argv, Options& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a{argv[i]};
        const auto needArg = [&](const char* name) -> char*
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "sfs_profile: missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--substrate-cells")
        {
            char* v = needArg("--substrate-cells");
            if (!v)
                return false;
            opts.substrateCells = std::atoi(v);
        }
        else if (a == "--agents")
        {
            char* v = needArg("--agents");
            if (!v)
                return false;
            opts.agentCount = std::atoi(v);
        }
        else if (a == "--seconds")
        {
            char* v = needArg("--seconds");
            if (!v)
                return false;
            opts.seconds = std::atof(v);
        }
        else if (a == "--repeat")
        {
            char* v = needArg("--repeat");
            if (!v)
                return false;
            opts.repeat = std::atoi(v);
        }
        else if (a == "--note")
        {
            char* v = needArg("--note");
            if (!v)
                return false;
            opts.midiNote = std::atoi(v);
        }
        else if (a == "-h" || a == "--help")
        {
            printUsage();
            std::exit(0);
        }
        else
        {
            std::fprintf(stderr, "sfs_profile: unknown argument '%.*s'\n", static_cast<int>(a.size()), a.data());
            printUsage();
            return false;
        }
    }
    return opts.substrateCells > 0 && opts.agentCount > 0 && opts.seconds > 0.0 && opts.repeat > 0;
}

} // namespace

int main(int argc, char** argv)
{
    Options opts;
    if (!parseArgs(argc, argv, opts))
    {
        return 1;
    }

    constexpr float kSampleRate = 48000.0f;
    const int blockSize = 256;
    const int totalSamples = static_cast<int>(opts.seconds * static_cast<double>(kSampleRate));
    const int numBlocks = (totalSamples + blockSize - 1) / blockSize;

    std::printf("Configuration:\n");
    std::printf("  substrate cells:   %d\n", opts.substrateCells);
    std::printf("  agent count:       %d\n", opts.agentCount);
    std::printf("  sample rate:       %.0f Hz\n", static_cast<double>(kSampleRate));
    std::printf("  block size:        %d samples\n", blockSize);
    std::printf("  duration / pass:   %.3f s (%d samples / %d blocks)\n", opts.seconds, totalSamples, numBlocks);
    std::printf("  MIDI note:         %d\n", opts.midiNote);
    std::printf("  repetitions:       %d\n\n", opts.repeat);

    using Clock = std::chrono::steady_clock;

    std::vector<double> wallTimesMs;
    wallTimesMs.reserve(static_cast<std::size_t>(opts.repeat));

    for (int r = 0; r < opts.repeat; ++r)
    {
        sfs::engine::Voice voice(opts.substrateCells, opts.agentCount, kSampleRate);
        std::vector<float> out(static_cast<std::size_t>(blockSize), 0.0f);

        voice.noteOn(opts.midiNote, 1.0f);

        const auto t0 = Clock::now();
        int written = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            const int n = std::min(blockSize, totalSamples - written);
            voice.renderBlock(out.data(), n);
            written += n;
        }
        const auto t1 = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        wallTimesMs.push_back(ms);
    }

    // Report stats — min, median, mean. Min is the most useful for cold-cache
    // best-case throughput; median for typical; mean lets you eyeball variance.
    std::sort(wallTimesMs.begin(), wallTimesMs.end());
    const double minMs = wallTimesMs.front();
    const double medianMs = wallTimesMs[wallTimesMs.size() / 2];
    double sum = 0.0;
    for (double t : wallTimesMs)
    {
        sum += t;
    }
    const double meanMs = sum / static_cast<double>(wallTimesMs.size());

    const double realTimeMs = opts.seconds * 1000.0;
    auto report = [&](const char* label, double ms)
    {
        const double rtFactor = realTimeMs / ms;
        const double samplesPerSec = static_cast<double>(totalSamples) / (ms / 1000.0);
        const double cpuPercent = (ms / realTimeMs) * 100.0;
        std::printf("  %-7s  %7.2f ms   %.2fx real-time   %.0f samples/s   ~%.1f%% one core\n",
                    label,
                    ms,
                    rtFactor,
                    samplesPerSec,
                    cpuPercent);
    };

    std::printf("Wall time per pass (n=%d):\n", opts.repeat);
    report("min", minMs);
    report("median", medianMs);
    report("mean", meanMs);

    return 0;
}
