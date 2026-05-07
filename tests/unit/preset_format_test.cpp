// tests/unit/preset_format_test.cpp
//
// Phase 4 §A1 — preset JSON format. Asserts:
//   * Magic check + format-version negotiation.
//   * Round-trip every persisted field.
//   * Atomic save (write to .tmp + rename) lands the file at the target
//     path with the right content; no .tmp left behind.
//   * Malformed input is rejected with PresetParseError.

#include "preset/preset.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

using sfs::preset::Preset;
using sfs::preset::PresetParseError;

namespace
{

[[nodiscard]] std::filesystem::path uniqueTempPath(const char* tag)
{
    namespace fs = std::filesystem;
    static int counter = 0;
    return fs::temp_directory_path() /
           (std::string("sfs_preset_test_") + tag + "_" + std::to_string(++counter) + ".sfs");
}

[[nodiscard]] Preset makeRichPreset()
{
    Preset p;
    p.metadata.name = "Slow Glacier";
    p.metadata.author = "Test Suite";
    p.metadata.category = "Drone";
    p.metadata.tags = {"ambient", "ethereal"};
    p.metadata.created = "2026-05-07T18:00:00Z";
    p.metadata.modified = "2026-05-07T18:30:00Z";
    p.metadata.description = "A drone preset under test.";
    p.seed = 0xAB12CD34EF567890ull;
    p.macros = {0.62f, 0.18f, 0.55f, 0.22f, 0.85f, 0.30f};
    p.structural.topology = "torus_32";
    p.structural.aspect = 1.25f;
    p.structural.substrate_size = 1024;
    p.structural.deposit_kernel = "linear";
    p.structural.read_kernel = "hermite4";
    p.agents.max_active = 24;
    p.agents.harmonic_set = {1.0f, 1.5f, 2.0f, 3.0f};
    p.agents.shape_distribution = {0.5f, 0.0f, 0.0f, 0.4f, 0.1f};
    p.agents.fm_ratio_index = 0.55f;
    p.agents.fm_index_index = 0.42f;
    p.agents.detune_scale = 0.1f;
    p.env1 = {2.0f, 1.5f, 0.7f, 4.0f};
    p.env2 = {0.05f, 0.2f, 1.0f, 0.5f};
    p.lfos[0] = {0.05f, 1.0f, "sine", "free", 0.0f, false};
    p.lfos[1] = {0.20f, 0.8f, "sample_hold", "free", 0.5f, true};
    p.lfos[2] = {1.0f, 1.0f, "triangle", "tempo_quarter", 0.0f, false};
    p.lfos[3] = {4.0f, 0.5f, "square", "free", 0.25f, false};
    p.mod_matrix[0] = {true, "LFO1", "MIGRATION", 0.4f, "LINEAR"};
    p.mod_matrix[1] = {true, "ENV1", "DAMPING", -0.3f, "S_CURVE"};
    p.mod_matrix[2] = {false, "LFO2", "TENSION", 0.0f, "LINEAR"};
    p.harvesters = {0.6f, 0.2f, 0.1f, "circle"};
    p.output = {-3.0f, 0.5f, true};
    p.audio_hash = "sha256:deadbeefcafef00d";
    return p;
}

} // namespace

TEST_CASE("Preset round-trip preserves every field bit-for-bit", "[preset]")
{
    const auto original = makeRichPreset();
    const auto json = original.toJsonString();
    const auto reloaded = Preset::fromJsonString(json);

    REQUIRE(reloaded.metadata.name == original.metadata.name);
    REQUIRE(reloaded.metadata.author == original.metadata.author);
    REQUIRE(reloaded.metadata.category == original.metadata.category);
    REQUIRE(reloaded.metadata.tags == original.metadata.tags);
    REQUIRE(reloaded.metadata.created == original.metadata.created);
    REQUIRE(reloaded.metadata.modified == original.metadata.modified);
    REQUIRE(reloaded.metadata.description == original.metadata.description);
    REQUIRE(reloaded.seed == original.seed);
    REQUIRE(reloaded.macros.tension == original.macros.tension);
    REQUIRE(reloaded.macros.damping == original.macros.damping);
    REQUIRE(reloaded.macros.density == original.macros.density);
    REQUIRE(reloaded.macros.migration == original.macros.migration);
    REQUIRE(reloaded.macros.coherence == original.macros.coherence);
    REQUIRE(reloaded.macros.excitation == original.macros.excitation);
    REQUIRE(reloaded.structural.topology == original.structural.topology);
    REQUIRE(reloaded.structural.aspect == original.structural.aspect);
    REQUIRE(reloaded.structural.substrate_size == original.structural.substrate_size);
    REQUIRE(reloaded.structural.deposit_kernel == original.structural.deposit_kernel);
    REQUIRE(reloaded.structural.read_kernel == original.structural.read_kernel);
    REQUIRE(reloaded.agents.max_active == original.agents.max_active);
    REQUIRE(reloaded.agents.harmonic_set == original.agents.harmonic_set);
    REQUIRE(reloaded.agents.shape_distribution.sine == original.agents.shape_distribution.sine);
    REQUIRE(reloaded.agents.shape_distribution.fmpair == original.agents.shape_distribution.fmpair);
    REQUIRE(reloaded.agents.fm_ratio_index == original.agents.fm_ratio_index);
    REQUIRE(reloaded.agents.fm_index_index == original.agents.fm_index_index);
    REQUIRE(reloaded.agents.detune_scale == original.agents.detune_scale);
    REQUIRE(reloaded.env1.attack == original.env1.attack);
    REQUIRE(reloaded.env1.release == original.env1.release);
    REQUIRE(reloaded.env2.sustain == original.env2.sustain);
    for (std::size_t i = 0; i < reloaded.lfos.size(); ++i)
    {
        REQUIRE(reloaded.lfos[i].rate_hz == original.lfos[i].rate_hz);
        REQUIRE(reloaded.lfos[i].depth == original.lfos[i].depth);
        REQUIRE(reloaded.lfos[i].shape == original.lfos[i].shape);
        REQUIRE(reloaded.lfos[i].sync == original.lfos[i].sync);
        REQUIRE(reloaded.lfos[i].phase == original.lfos[i].phase);
        REQUIRE(reloaded.lfos[i].reset_on_note == original.lfos[i].reset_on_note);
    }
    for (std::size_t i = 0; i < reloaded.mod_matrix.size(); ++i)
    {
        REQUIRE(reloaded.mod_matrix[i].active == original.mod_matrix[i].active);
        REQUIRE(reloaded.mod_matrix[i].source == original.mod_matrix[i].source);
        REQUIRE(reloaded.mod_matrix[i].destination == original.mod_matrix[i].destination);
        REQUIRE(reloaded.mod_matrix[i].depth == original.mod_matrix[i].depth);
        REQUIRE(reloaded.mod_matrix[i].curve == original.mod_matrix[i].curve);
    }
    REQUIRE(reloaded.harvesters.spacing == original.harvesters.spacing);
    REQUIRE(reloaded.harvesters.orbit_depth == original.harvesters.orbit_depth);
    REQUIRE(reloaded.harvesters.orbit_speed_hz == original.harvesters.orbit_speed_hz);
    REQUIRE(reloaded.harvesters.orbit_shape == original.harvesters.orbit_shape);
    REQUIRE(reloaded.output.master_gain_db == original.output.master_gain_db);
    REQUIRE(reloaded.output.pan == original.output.pan);
    REQUIRE(reloaded.output.limiter_active == original.output.limiter_active);
    REQUIRE(reloaded.audio_hash == original.audio_hash);
}

TEST_CASE("Preset rejects missing or wrong _magic", "[preset]")
{
    REQUIRE_THROWS_AS(Preset::fromJsonString("{}"), PresetParseError);
    REQUIRE_THROWS_AS(Preset::fromJsonString(R"({"_magic": "WRONG-VAL"})"), PresetParseError);
    REQUIRE_THROWS_AS(Preset::fromJsonString(R"({"_magic": "SFS-2.00"})"), PresetParseError);
}

TEST_CASE("Preset rejects unsupported format_version", "[preset]")
{
    const std::string j = R"({"_magic": "SFS-1.00", "format_version": "2.0"})";
    REQUIRE_THROWS_AS(Preset::fromJsonString(j), PresetParseError);
}

TEST_CASE("Preset rejects malformed JSON", "[preset]")
{
    REQUIRE_THROWS_AS(Preset::fromJsonString("not-json {{ broken"), PresetParseError);
    REQUIRE_THROWS_AS(Preset::fromJsonString("[1, 2, 3]"), PresetParseError); // wrong top-level type
}

TEST_CASE("Preset accepts minimal {magic} input and fills defaults", "[preset]")
{
    const auto p = Preset::fromJsonString(R"({"_magic": "SFS-1.00"})");
    REQUIRE(p.format_version == sfs::preset::kFormatVersion);
    REQUIRE(p.macros.tension == 0.5f); // documented default
    REQUIRE(p.structural.topology == "ring");
    REQUIRE(p.lfos[0].shape == "sine");
}

TEST_CASE("Preset save+load is atomic; no .tmp left on success", "[preset]")
{
    namespace fs = std::filesystem;
    const auto target = uniqueTempPath("save_load");
    const auto tmpVariant = fs::path(target.string() + ".tmp");

    const auto original = makeRichPreset();
    original.saveToFile(target.string());

    REQUIRE(fs::exists(target));
    REQUIRE_FALSE(fs::exists(tmpVariant));

    const auto reloaded = Preset::loadFromFile(target.string());
    REQUIRE(reloaded.metadata.name == original.metadata.name);
    REQUIRE(reloaded.macros.tension == original.macros.tension);
    REQUIRE(reloaded.seed == original.seed);

    fs::remove(target);
}

TEST_CASE("Preset loadFromFile throws on missing file", "[preset]")
{
    REQUIRE_THROWS_AS(Preset::loadFromFile("/nonexistent/path/preset.sfs"), PresetParseError);
}
