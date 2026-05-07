// src/preset/preset.h
//
// Phase 4 §A1 — `.sfs` preset format. Doc 06 §3 is canonical; this struct
// mirrors the JSON schema field-for-field and round-trips through
// nlohmann::json.
//
// Design choices:
//   * The struct holds raw values in their canonical types (float / int /
//     string / vector). It does NOT reference engine types directly —
//     keeps the preset module compilable in isolation and prevents
//     accidental engine-side mutation through preset access.
//   * Loading is two-step: parse JSON (`Preset::fromJson`) then apply to
//     a VoiceManager (`applyTo` lives in preset_io.h, which depends on
//     the engine).
//   * Magic check + format_version negotiation is in `fromJson`; future
//     v1.x→v2.0 migration is the migrator's job.
//   * Failure mode: parse errors throw `PresetParseError`. The plug-in
//     shell catches and surfaces them in the preset browser.

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sfs::preset
{

constexpr const char* kFormatMagic = "SFS-1.00";
constexpr const char* kFormatVersion = "1.0";

class PresetParseError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct PresetMetadata
{
    std::string name;
    std::string author;
    std::string category; // {Drone, Organic, Pitched, Glitch, Hybrid, Init, User}
    std::vector<std::string> tags;
    std::string created;     // ISO-8601 UTC, e.g. "2026-05-07T18:00:00Z"
    std::string modified;    // ISO-8601 UTC
    std::string description; // free-form, ≤ 512 chars
};

struct PresetMacros
{
    float tension = 0.5f;
    float damping = 0.3f;
    float density = 0.6f;
    float migration = 0.2f;
    float coherence = 0.8f;
    float excitation = 0.3f;
};

struct PresetStructural
{
    std::string topology = "ring"; // {ring, torus_32, torus_64, torus_128, torus_256}
    float aspect = 1.0f;
    int substrate_size = 1024;
    std::string deposit_kernel = "linear"; // {linear, lanczos4}
    std::string read_kernel = "linear";    // {linear, hermite4}
};

struct PresetShapeDistribution
{
    float sine = 0.6f;
    float saw = 0.0f;
    float square = 0.0f;
    float fmpair = 0.3f;
    float noise = 0.1f;
};

struct PresetAgents
{
    int max_active = 32;
    std::vector<float> harmonic_set{1, 2, 3, 4, 5, 6, 7, 8};
    PresetShapeDistribution shape_distribution{};
    float fm_ratio_index = 0.5f;
    float fm_index_index = 0.3f;
    float detune_scale = 0.0f;
};

struct PresetEnvelope
{
    float attack = 0.5f; // seconds (Doc 09 §3.4 log range)
    float decay = 1.0f;
    float sustain = 0.7f;
    float release = 2.0f;
};

struct PresetLfo
{
    float rate_hz = 1.0f;
    float depth = 1.0f;
    std::string shape = "sine"; // {sine, triangle, saw, square, sample_hold}
    std::string sync = "free";
    float phase = 0.0f;
    bool reset_on_note = false;
};

struct PresetModSlot
{
    bool active = false;
    std::string source = "LFO1"; // see Doc 05 §5.2 / 09 §"Modulation destinations"
    std::string destination = "TENSION";
    float depth = 0.0f;
    std::string curve = "LINEAR"; // {LINEAR, EXPONENTIAL, S_CURVE, INVERTED, BIPOLAR}
};

struct PresetHarvesters
{
    float spacing = 0.5f;
    float orbit_depth = 0.0f;
    float orbit_speed_hz = 0.05f;
    std::string orbit_shape = "circle"; // {circle, figure_eight, random_walk, off}
};

struct PresetOutput
{
    float master_gain_db = 0.0f;
    float pan = 0.0f;
    bool limiter_active = false;
};

struct Preset
{
    std::string format_version = kFormatVersion;
    std::string engine_version = "1.0.0";
    PresetMetadata metadata{};
    std::uint64_t seed = 0x5F5F5F5F5F5F5F5Full; // Phase 2 default constant
    PresetMacros macros{};
    PresetStructural structural{};
    PresetAgents agents{};
    PresetEnvelope env1{0.5f, 1.0f, 0.7f, 2.0f};
    PresetEnvelope env2{0.05f, 0.3f, 1.0f, 0.5f};
    std::array<PresetLfo, 4> lfos{};
    std::array<PresetModSlot, 16> mod_matrix{};
    PresetHarvesters harvesters{};
    PresetOutput output{};
    std::string audio_hash; // optional sha256:... ; empty when not yet hashed

    // JSON round-trip. `toJson`/`fromJson` live in preset_io.cpp (which
    // depends on nlohmann/json). They throw `PresetParseError` on
    // malformed input or magic / version mismatch.
    [[nodiscard]] static Preset fromJsonString(const std::string& jsonText);
    [[nodiscard]] std::string toJsonString(int indent = 2) const;

    // File I/O. Atomic save (write to .tmp + rename) per Doc 06 §6.
    // Throws on parse / I/O error.
    [[nodiscard]] static Preset loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;
};

} // namespace sfs::preset
