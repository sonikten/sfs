// tests/integration/audio_hash_validation_test.cpp
//
// Phase 4 §A3 — `_audio_hash` validation. Each factory preset's
// `_audio_hash` field stores the SHA-256 of its canonical 30-second
// render, computed at preset-creation time per `sfs-spec/06_rng_presets.md`
// §3.3. This test re-renders every factory preset under the same canonical
// protocol and asserts the live hash matches the stored value, byte for
// byte.
//
// Canonical render protocol (Doc 06 §3.3):
//   * 48 kHz / block 256 / stereo float32 / 30 s / C4 (note 60)
//   * velocity 100/127 (≈ 0.787402)
//   * gate-on at sample 0
//   * gate-off at sample 24000 (0.5 s)
//   * channel-interleaved float32 PCM
//
// This protocol is deliberately a representative *musical playing*
// pattern: gate-off at 0.5 s captures the release tail (substrate
// ringing under γ damping) which is where most engine-drift regressions
// will surface. A sustained-only render misses that path entirely.
//
// Mirrors `tools/sfs_preset_render`'s default flags. If those defaults
// change, this test will fail and the change must be a deliberate
// preset-format-version bump.
//
// Hashing is done via `shasum -a 256` on the rendered .raw file —
// matches the exact bytes that `tools/sfs_preset_hash.sh` consumed
// when generating the stored hash. Avoids an in-process SHA-256
// vendor / divergence risk.

#include "engine/voice_manager.h"
#include "preset/preset.h"
#include "preset/preset_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 16;
constexpr int kMidiNote = 60;
constexpr float kVelocity = 100.0f / 127.0f; // Doc 06 §3.3
constexpr int kGateOffSample = 24000;        // 0.5 s @ 48 kHz — Doc 06 §3.3
constexpr double kRenderSeconds = 30.0;

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

[[nodiscard]] std::vector<std::filesystem::path> listAllFactoryPresets()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const auto root = repoRoot() / "presets" / "factory";
    if (!fs::is_directory(root))
    {
        return out;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".sfs")
        {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Render the canonical PCM to disk (mirrors `tools/sfs_preset_render`).
// Returns true on success; false on render failure (NaN/Inf or I/O).
[[nodiscard]] bool renderCanonicalToFile(const sfs::preset::Preset& preset, const std::filesystem::path& outPath)
{
    sfs::engine::VoiceManager vm(kSubstrateCells, kAgentCount, static_cast<float>(kSampleRate));
    sfs::preset::applyToEngine(preset, vm);
    vm.noteOn(kMidiNote, kVelocity);

    const int totalSamples = static_cast<int>(kRenderSeconds * kSampleRate);
    std::vector<float> bufL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> bufR(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> interleaved(static_cast<std::size_t>(totalSamples) * 2u, 0.0f);

    int written = 0;
    bool gateOffFired = false;
    while (written < totalSamples)
    {
        if (!gateOffFired && written >= kGateOffSample)
        {
            vm.noteOff(kMidiNote);
            gateOffFired = true;
        }
        const int n = std::min(kBlockSize, totalSamples - written);
        vm.renderBlockStereo(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float l = bufL[static_cast<std::size_t>(i)];
            const float r = bufR[static_cast<std::size_t>(i)];
            if (!std::isfinite(l) || !std::isfinite(r))
            {
                return false;
            }
            const std::size_t idx = static_cast<std::size_t>(written + i) * 2u;
            interleaved[idx + 0u] = l;
            interleaved[idx + 1u] = r;
        }
        written += n;
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out.write(reinterpret_cast<const char*>(interleaved.data()),
              static_cast<std::streamsize>(interleaved.size() * sizeof(float)));
    return static_cast<bool>(out);
}

// popen / pclose are POSIX. Windows / MSVC has the same API under
// _popen / _pclose. Alias here so the body below stays clean.
#ifdef _WIN32
#define SFS_POPEN _popen
#define SFS_PCLOSE _pclose
#else
#define SFS_POPEN popen
#define SFS_PCLOSE pclose
#endif

// Compute SHA-256 of a file via the platform's hashing utility — same
// path the hash tool uses. Returns lowercase hex digest, or empty
// string on error.
//
// macOS / Linux: `shasum -a 256 <path>` → "<hash>  <path>\n"
// Windows:       `certutil -hashfile <path> SHA256` → multi-line output
//                with the hash on the second line (no path prefix), in
//                uppercase. We lowercase before returning.
[[nodiscard]] std::string shasum256(const std::filesystem::path& filePath)
{
#ifdef _WIN32
    // certutil's output format on Win is:
    //   "SHA256 hash of <path>:\r\n"
    //   "<HEX_HASH>\r\n"
    //   "CertUtil: -hashfile command completed successfully.\r\n"
    const std::string cmd = "certutil -hashfile \"" + filePath.string() + "\" SHA256";
#else
    const std::string cmd = "shasum -a 256 \"" + filePath.string() + "\"";
#endif

    FILE* p = SFS_POPEN(cmd.c_str(), "r");
    if (p == nullptr)
    {
        return {};
    }

    std::array<char, 512> buf{};
    std::vector<std::string> lines;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), p) != nullptr)
    {
        lines.emplace_back(buf.data());
    }
    SFS_PCLOSE(p);

    auto trim = [](std::string s)
    {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        {
            s.pop_back();
        }
        return s;
    };

#ifdef _WIN32
    // The hash is the first whitespace-only-or-hex line.
    for (const auto& raw : lines)
    {
        std::string line = trim(raw);
        if (line.empty())
        {
            continue;
        }
        // Skip the human-readable header / footer lines.
        bool allHex = !line.empty();
        for (char c : line)
        {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ' ';
            if (!ok)
            {
                allHex = false;
                break;
            }
        }
        if (!allHex)
        {
            continue;
        }
        // Strip any embedded spaces and lowercase.
        std::string hex;
        hex.reserve(line.size());
        for (char c : line)
        {
            if (c == ' ')
            {
                continue;
            }
            hex.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c));
        }
        if (hex.size() == 64)
        {
            return hex;
        }
    }
    return {};
#else
    if (lines.empty())
    {
        return {};
    }
    const std::string& line = lines.front();
    const auto sp = line.find(' ');
    return sp == std::string::npos ? std::string{} : line.substr(0, sp);
#endif
}

} // namespace

TEST_CASE("Every factory preset's _audio_hash matches a live canonical render", "[integration][audio][hash][long]")
{
    const auto presets = listAllFactoryPresets();
    REQUIRE(presets.size() >= 128);

    namespace fs = std::filesystem;
    const auto tmpRoot = fs::temp_directory_path() / "sfs_hash_validation";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    int matched = 0;
    int mismatched = 0;
    int missing = 0;

    for (const auto& path : presets)
    {
        sfs::preset::Preset p;
        try
        {
            p = sfs::preset::Preset::loadFromFile(path.string());
        }
        catch (const std::exception& e)
        {
            UNSCOPED_INFO("preset load failed: " << path.filename() << " — " << e.what());
            FAIL();
        }

        const std::string stored = p.audio_hash;
        if (stored.empty())
        {
            UNSCOPED_INFO("preset missing _audio_hash: " << path.filename());
            ++missing;
            continue;
        }
        if (stored.find("sha256:") != 0)
        {
            UNSCOPED_INFO("preset _audio_hash missing sha256: prefix: " << path.filename() << " = " << stored);
            FAIL();
        }
        const std::string storedHex = stored.substr(7);

        const auto rawPath = tmpRoot / (path.stem().string() + ".raw");
        if (!renderCanonicalToFile(p, rawPath))
        {
            UNSCOPED_INFO("render failed: " << path.filename());
            FAIL();
        }
        const std::string liveHex = shasum256(rawPath);
        fs::remove(rawPath);

        if (liveHex.empty())
        {
            UNSCOPED_INFO("shasum returned empty for: " << path.filename());
            FAIL();
        }
        if (liveHex != storedHex)
        {
            UNSCOPED_INFO(path.stem().string() << ": stored=sha256:" << storedHex << " live=sha256:" << liveHex);
            ++mismatched;
        }
        else
        {
            ++matched;
        }
    }

    fs::remove_all(tmpRoot);

    INFO("matched=" << matched << " mismatched=" << mismatched << " missing=" << missing);
    REQUIRE(mismatched == 0);
    REQUIRE(missing == 0);
    REQUIRE(matched >= 128);
}
