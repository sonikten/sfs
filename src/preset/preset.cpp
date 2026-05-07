// src/preset/preset.cpp

#include "preset.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace sfs::preset
{

namespace
{

using nlohmann::json;

// ---------------------------------------------------------------------------
// JSON helpers — read with default fallback. Doc 06 §3.4: future engine
// versions accept presets with missing-but-not-yet-introduced fields by
// applying the default. This helper centralises that policy.
// ---------------------------------------------------------------------------
template <class T> T optAt(const json& j, const char* key, const T& fallback)
{
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
    {
        return fallback;
    }
    return it->get<T>();
}

// Strict variant — raises a parse error if missing. Used for required
// schema fields like `_magic`.
template <class T> T requireAt(const json& j, const char* key)
{
    auto it = j.find(key);
    if (it == j.end())
    {
        throw PresetParseError(std::string("preset: required field missing: ") + key);
    }
    return it->get<T>();
}

PresetMetadata metadataFromJson(const json& j)
{
    PresetMetadata m;
    m.name = optAt<std::string>(j, "name", "Untitled");
    m.author = optAt<std::string>(j, "author", "");
    m.category = optAt<std::string>(j, "category", "User");
    m.tags = optAt<std::vector<std::string>>(j, "tags", {});
    m.created = optAt<std::string>(j, "created", "");
    m.modified = optAt<std::string>(j, "modified", "");
    m.description = optAt<std::string>(j, "description", "");
    return m;
}

json metadataToJson(const PresetMetadata& m)
{
    return json{
        {"name", m.name},
        {"author", m.author},
        {"category", m.category},
        {"tags", m.tags},
        {"created", m.created},
        {"modified", m.modified},
        {"description", m.description},
    };
}

PresetMacros macrosFromJson(const json& j)
{
    PresetMacros r;
    r.tension = optAt<float>(j, "tension", r.tension);
    r.damping = optAt<float>(j, "damping", r.damping);
    r.density = optAt<float>(j, "density", r.density);
    r.migration = optAt<float>(j, "migration", r.migration);
    r.coherence = optAt<float>(j, "coherence", r.coherence);
    r.excitation = optAt<float>(j, "excitation", r.excitation);
    return r;
}

json macrosToJson(const PresetMacros& m)
{
    return json{
        {"tension", m.tension},
        {"damping", m.damping},
        {"density", m.density},
        {"migration", m.migration},
        {"coherence", m.coherence},
        {"excitation", m.excitation},
    };
}

PresetStructural structuralFromJson(const json& j)
{
    PresetStructural r;
    r.topology = optAt<std::string>(j, "topology", r.topology);
    r.aspect = optAt<float>(j, "aspect", r.aspect);
    r.substrate_size = optAt<int>(j, "substrate_size", r.substrate_size);
    r.deposit_kernel = optAt<std::string>(j, "deposit_kernel", r.deposit_kernel);
    r.read_kernel = optAt<std::string>(j, "read_kernel", r.read_kernel);
    return r;
}

json structuralToJson(const PresetStructural& s)
{
    return json{
        {"topology", s.topology},
        {"aspect", s.aspect},
        {"substrate_size", s.substrate_size},
        {"deposit_kernel", s.deposit_kernel},
        {"read_kernel", s.read_kernel},
    };
}

PresetShapeDistribution shapeDistFromJson(const json& j)
{
    PresetShapeDistribution r;
    r.sine = optAt<float>(j, "sine", r.sine);
    r.saw = optAt<float>(j, "saw", r.saw);
    r.square = optAt<float>(j, "square", r.square);
    r.fmpair = optAt<float>(j, "fmpair", r.fmpair);
    r.noise = optAt<float>(j, "noise", r.noise);
    return r;
}

json shapeDistToJson(const PresetShapeDistribution& s)
{
    return json{
        {"sine", s.sine},
        {"saw", s.saw},
        {"square", s.square},
        {"fmpair", s.fmpair},
        {"noise", s.noise},
    };
}

PresetAgents agentsFromJson(const json& j)
{
    PresetAgents r;
    r.max_active = optAt<int>(j, "max_active", r.max_active);
    r.harmonic_set = optAt<std::vector<float>>(j, "harmonic_set", r.harmonic_set);
    auto sd = j.find("shape_distribution");
    if (sd != j.end())
    {
        r.shape_distribution = shapeDistFromJson(*sd);
    }
    auto sp = j.find("shape_param_default");
    if (sp != j.end())
    {
        r.fm_ratio_index = optAt<float>(*sp, "fm_ratio_index", r.fm_ratio_index);
        r.fm_index_index = optAt<float>(*sp, "fm_index_index", r.fm_index_index);
    }
    r.detune_scale = optAt<float>(j, "detune_scale", r.detune_scale);
    return r;
}

json agentsToJson(const PresetAgents& a)
{
    return json{
        {"max_active", a.max_active},
        {"harmonic_set", a.harmonic_set},
        {"shape_distribution", shapeDistToJson(a.shape_distribution)},
        {"shape_param_default",
         json{
             {"fm_ratio_index", a.fm_ratio_index},
             {"fm_index_index", a.fm_index_index},
         }},
        {"detune_scale", a.detune_scale},
    };
}

PresetEnvelope envFromJson(const json& j, const PresetEnvelope& defaults)
{
    PresetEnvelope r = defaults;
    r.attack = optAt<float>(j, "attack", r.attack);
    r.decay = optAt<float>(j, "decay", r.decay);
    r.sustain = optAt<float>(j, "sustain", r.sustain);
    r.release = optAt<float>(j, "release", r.release);
    return r;
}

json envToJson(const PresetEnvelope& e)
{
    return json{{"attack", e.attack}, {"decay", e.decay}, {"sustain", e.sustain}, {"release", e.release}};
}

PresetLfo lfoFromJson(const json& j)
{
    PresetLfo r;
    r.rate_hz = optAt<float>(j, "rate_hz", r.rate_hz);
    r.depth = optAt<float>(j, "depth", r.depth);
    r.shape = optAt<std::string>(j, "shape", r.shape);
    r.sync = optAt<std::string>(j, "sync", r.sync);
    r.phase = optAt<float>(j, "phase", r.phase);
    r.reset_on_note = optAt<bool>(j, "reset_on_note", r.reset_on_note);
    return r;
}

json lfoToJson(const PresetLfo& l)
{
    return json{{"rate_hz", l.rate_hz},
                {"depth", l.depth},
                {"shape", l.shape},
                {"sync", l.sync},
                {"phase", l.phase},
                {"reset_on_note", l.reset_on_note}};
}

PresetModSlot modSlotFromJson(const json& j)
{
    PresetModSlot r;
    r.active = optAt<bool>(j, "active", r.active);
    r.source = optAt<std::string>(j, "source", r.source);
    r.destination = optAt<std::string>(j, "destination", r.destination);
    r.depth = optAt<float>(j, "depth", r.depth);
    r.curve = optAt<std::string>(j, "curve", r.curve);
    return r;
}

json modSlotToJson(const PresetModSlot& m)
{
    return json{{"active", m.active},
                {"source", m.source},
                {"destination", m.destination},
                {"depth", m.depth},
                {"curve", m.curve}};
}

PresetHarvesters harvestersFromJson(const json& j)
{
    PresetHarvesters r;
    r.spacing = optAt<float>(j, "spacing", r.spacing);
    r.orbit_depth = optAt<float>(j, "orbit_depth", r.orbit_depth);
    r.orbit_speed_hz = optAt<float>(j, "orbit_speed_hz", r.orbit_speed_hz);
    r.orbit_shape = optAt<std::string>(j, "orbit_shape", r.orbit_shape);
    return r;
}

json harvestersToJson(const PresetHarvesters& h)
{
    return json{
        {"spacing", h.spacing},
        {"orbit_depth", h.orbit_depth},
        {"orbit_speed_hz", h.orbit_speed_hz},
        {"orbit_shape", h.orbit_shape},
    };
}

PresetOutput outputFromJson(const json& j)
{
    PresetOutput r;
    r.master_gain_db = optAt<float>(j, "master_gain_db", r.master_gain_db);
    r.pan = optAt<float>(j, "pan", r.pan);
    r.limiter_active = optAt<bool>(j, "limiter_active", r.limiter_active);
    return r;
}

json outputToJson(const PresetOutput& o)
{
    return json{{"master_gain_db", o.master_gain_db}, {"pan", o.pan}, {"limiter_active", o.limiter_active}};
}

// Hex-string round-trip for the 64-bit seed (Doc 06 §3.2 example shows
// "0xAB12CD34EF567890" form).
std::string seedToHex(std::uint64_t seed)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(seed));
    return std::string(buf);
}

std::uint64_t seedFromHex(const std::string& s)
{
    // Tolerate the optional "0x" prefix; reject everything else as malformed.
    // std::from_chars is portable + locale-free + MSVC-safe (sscanf trips
    // C4996 deprecation under /WX on MSVC).
    const char* first = s.data();
    const char* last = first + s.size();
    if (s.size() >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X'))
    {
        first += 2;
    }
    if (first == last)
    {
        return 0;
    }
    std::uint64_t v = 0;
    auto result = std::from_chars(first, last, v, 16);
    if (result.ec != std::errc{} || result.ptr != last)
    {
        throw PresetParseError("preset: malformed seed hex string: " + s);
    }
    return v;
}

} // namespace

Preset Preset::fromJsonString(const std::string& jsonText)
{
    json j;
    try
    {
        j = json::parse(jsonText);
    }
    catch (const json::parse_error& e)
    {
        throw PresetParseError(std::string("preset: JSON parse error: ") + e.what());
    }
    if (!j.is_object())
    {
        throw PresetParseError("preset: top-level value is not an object");
    }
    const auto magic = optAt<std::string>(j, "_magic", "");
    if (magic != kFormatMagic)
    {
        throw PresetParseError("preset: missing or wrong _magic (expected SFS-1.00, got '" + magic + "')");
    }
    const auto formatVersion = optAt<std::string>(j, "format_version", kFormatVersion);
    // v1.0 only at v1.0; future v1.x preserves backwards compat by
    // missing-field-default-policy. v2.x is rejected as outside this
    // engine's understood range (Doc 06 §3.4).
    if (!formatVersion.empty() && formatVersion[0] != '1')
    {
        throw PresetParseError("preset: unsupported format_version '" + formatVersion + "' (this engine speaks 1.x)");
    }

    Preset p;
    p.format_version = formatVersion.empty() ? kFormatVersion : formatVersion;
    p.engine_version = optAt<std::string>(j, "engine_version", p.engine_version);
    if (auto it = j.find("metadata"); it != j.end())
    {
        p.metadata = metadataFromJson(*it);
    }
    if (auto it = j.find("seed"); it != j.end() && it->is_string())
    {
        p.seed = seedFromHex(it->get<std::string>());
    }
    if (auto it = j.find("macros"); it != j.end())
    {
        p.macros = macrosFromJson(*it);
    }
    if (auto it = j.find("structural"); it != j.end())
    {
        p.structural = structuralFromJson(*it);
    }
    if (auto it = j.find("agents"); it != j.end())
    {
        p.agents = agentsFromJson(*it);
    }
    if (auto it = j.find("envelopes"); it != j.end())
    {
        if (auto e1 = it->find("env1"); e1 != it->end())
        {
            p.env1 = envFromJson(*e1, p.env1);
        }
        if (auto e2 = it->find("env2"); e2 != it->end())
        {
            p.env2 = envFromJson(*e2, p.env2);
        }
    }
    if (auto it = j.find("lfos"); it != j.end() && it->is_array())
    {
        const std::size_t n = std::min(p.lfos.size(), it->size());
        for (std::size_t i = 0; i < n; ++i)
        {
            p.lfos[i] = lfoFromJson(it->at(i));
        }
    }
    if (auto it = j.find("modulation_matrix"); it != j.end())
    {
        if (auto slots = it->find("slots"); slots != it->end() && slots->is_array())
        {
            const std::size_t n = std::min(p.mod_matrix.size(), slots->size());
            for (std::size_t i = 0; i < n; ++i)
            {
                p.mod_matrix[i] = modSlotFromJson(slots->at(i));
            }
        }
    }
    if (auto it = j.find("harvesters"); it != j.end())
    {
        p.harvesters = harvestersFromJson(*it);
    }
    if (auto it = j.find("output"); it != j.end())
    {
        p.output = outputFromJson(*it);
    }
    p.audio_hash = optAt<std::string>(j, "_audio_hash", "");
    return p;
}

std::string Preset::toJsonString(int indent) const
{
    json lfoArr = json::array();
    for (const auto& l : lfos)
    {
        lfoArr.push_back(lfoToJson(l));
    }
    json slotArr = json::array();
    for (const auto& s : mod_matrix)
    {
        slotArr.push_back(modSlotToJson(s));
    }

    json j;
    // Insertion order is preserved by nlohmann::json on serialise; the
    // canonical layout puts _magic first per Doc 06 §3.1.
    j["_magic"] = kFormatMagic;
    j["format_version"] = format_version;
    j["engine_version"] = engine_version;
    j["metadata"] = metadataToJson(metadata);
    j["seed"] = seedToHex(seed);
    j["macros"] = macrosToJson(macros);
    j["structural"] = structuralToJson(structural);
    j["agents"] = agentsToJson(agents);
    j["envelopes"] = json{{"env1", envToJson(env1)}, {"env2", envToJson(env2)}};
    j["lfos"] = lfoArr;
    j["modulation_matrix"] = json{{"slots", slotArr}};
    j["harvesters"] = harvestersToJson(harvesters);
    j["output"] = outputToJson(output);
    if (!audio_hash.empty())
    {
        j["_audio_hash"] = audio_hash;
    }
    return j.dump(indent);
}

Preset Preset::loadFromFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw PresetParseError("preset: cannot open file: " + path);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return fromJsonString(ss.str());
}

void Preset::saveToFile(const std::string& path) const
{
    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path tmp(target.string() + ".tmp");

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw PresetParseError("preset: cannot open temp file for write: " + tmp.string());
        }
        const auto text = toJsonString(2);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
        {
            throw PresetParseError("preset: write failed to temp file: " + tmp.string());
        }
        out.flush();
    } // out dtor closes the handle before rename

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec)
    {
        // Cleanup attempt; ignore error.
        std::error_code rmEc;
        fs::remove(tmp, rmEc);
        throw PresetParseError("preset: rename failed: " + ec.message());
    }
}

} // namespace sfs::preset
