# 06 — RNG and preset format

The seeded RNG is what makes SFS presets shareable. Bit-exact reproducibility across platforms is a hard contract of v1.0. This document specifies the RNG architecture, the preset file format, the migration policy, and the preset-pack distribution mechanism.

---

## 1. RNG architecture

### 1.1 Why counter-based

Every standard RNG (`std::mt19937`, `std::default_random_engine`, BSD `random()`) has at least one of: platform-dependent state, sequential-only access, or non-trivial state size. None are suitable for a deterministic, parallel, splittable, reproducible audio synthesizer.

SFS uses [Philox-4×32-10](https://www.deshawresearch.com/resources_random123.html) as its core RNG primitive. Philox is a counter-based PRNG with these critical properties:

* **Stateless apart from the counter and key.** Two SFS instances given the same `(counter, key)` produce identical output, on any platform, in any thread, with no warmup.
* **Splittable by key.** Different `key` values give independent streams. We use `key` to encode `(preset_seed, voice_index, agent_index, stream_id)`.
* **Random access.** `philox(counter)` for any counter value, no need to step sequentially.
* **Vectorisable.** The internal computations are 4×32-bit operations that map directly to SIMD.

### 1.2 Stream structure

Every SFS RNG stream is keyed:

```
key128 = (preset_seed_64, voice_index_16, agent_index_16, stream_id_16, reserved_16)
```

Stream IDs:

| Stream ID | Purpose |
|---|---|
| `0` | Per-voice ignition burst |
| `1` | Per-voice agent position initialisation |
| `2` | Per-voice agent shape selection |
| `3` | Per-voice agent harmonic ratio selection |
| `4` | Per-agent migration noise |
| `5` | Per-agent sample-and-hold noise (for `noise` waveform) |
| `6` | Per-voice random-walk LFO state |
| `7` | Per-voice envelope time-randomisation (small jitter on attack/release) |

Counter usage:

* For per-sample stream draws (e.g., migration noise): counter increments by `1` per sample.
* For initialisation streams: counter is `0` and key fully determines the value (single read).

### 1.3 Reference implementation

Philox-4×32-10 takes a 128-bit key and 128-bit counter and returns a 128-bit result (interpreted as four 32-bit unsigned ints). The reference C implementation is ~50 lines and is deterministic across all platforms.

```c++
struct Philox4x32 {
    uint32_t key[2];
    uint32_t counter[4];

    void seed(uint64_t presetSeed, uint16_t voice, uint16_t agent, uint16_t stream) {
        key[0] = static_cast<uint32_t>(presetSeed);
        key[1] = static_cast<uint32_t>(presetSeed >> 32);
        counter[0] = (static_cast<uint32_t>(voice) << 16) | agent;
        counter[1] = stream;
        counter[2] = 0;
        counter[3] = 0;
    }

    uint32_t next32() {
        uint32_t out[4];
        philox4x32(out, counter, key);
        ++counter[2];
        return out[0];
    }

    float nextFloat01() {
        return (next32() >> 8) * (1.0f / 16777216.0f);   // 24-bit precision
    }

    float nextGaussian() {
        // Box–Muller; cache second draw
        if (haveCached) { haveCached = false; return cached; }
        float u1 = std::max(1e-7f, nextFloat01());
        float u2 = nextFloat01();
        float r  = sqrtf(-2.0f * logf(u1));
        cached      = r * sinf(2.0f * 3.14159265f * u2);
        haveCached  = true;
        return r * cosf(2.0f * 3.14159265f * u2);
    }

    bool haveCached = false;
    float cached = 0.0f;
};
```

The `philox4x32(out, counter, key)` function is the reference 10-round implementation from the [DE Shaw Research Random123 library](https://github.com/DEShawResearch/random123) — bit-exact across platforms.

### 1.4 Vectorisation

For the per-sample agent migration noise (the dominant RNG cost), a vectorised Philox kernel produces 4 random floats at a time, refilling a small per-voice ring buffer in 64-element batches outside the inner agent loop. This keeps RNG cost off the hot path.

### 1.5 RNG cost summary

* Initialisation (once per voice): ~64 calls = ~1 µs
* Per-sample agent migration: ~64 Gaussians per voice = ~1 µs vectorised, ~3 µs scalar

Negligible compared to substrate updates.

## 2. Determinism contract

The contract: given identical preset, identical MIDI input, identical sample rate, identical block size, and identical channel layout, the SFS engine produces bit-identical audio output across platforms.

To enforce this, the test rig (08) renders a fixed input on every supported platform and `diff`s the resulting WAV files. Any non-zero diff is a v1.0 release blocker.

Sources of non-determinism that are explicitly excluded:

* Wall-clock time (no `std::chrono::system_clock` calls anywhere in the audio path).
* Random seeds from system entropy (no `std::random_device`).
* `std::sin`, `std::cos`, `std::tanh` — replaced with our polynomial approximations whose precision is bit-exact.
* `std::exp`, `std::log` — same. Used only for envelope rate conversion (block-rate, deterministic input).
* Platform-specific SIMD math intrinsics with vendor-specific precision (the SIMD layer dispatches to a deterministic polynomial path).
* Compiler-specific floating-point optimisations (compiled with `-fno-fast-math` or `/fp:precise`).

Denormal-flush settings (FTZ/DAZ) are explicitly enabled at the start of every audio thread invocation so denormals don't make platforms differ.

## 3. The preset format

Presets are stored as JSON with a small binary blob for the modulation matrix. JSON was chosen over a binary format for diff-friendly version control; users sharing presets via Git or text-based forums benefit. The binary blob is base64-embedded in the JSON.

### 3.1 File extension and identification

`.sfs` is the file extension. The plug-in registers `application/x-sfs` as a MIME type for drag-and-drop import. The first 8 bytes of every preset must be the ASCII magic `"SFS-1.00"` to allow rapid identification (the magic appears as the first key in the JSON: `"_magic": "SFS-1.00"`).

### 3.2 Top-level schema

```json
{
  "_magic": "SFS-1.00",
  "format_version": "1.0",
  "engine_version": "1.0.0",
  "metadata": {
    "name": "Slow Glacier",
    "author": "Anthropic Demo Set",
    "category": "Drone",
    "tags": ["ambient", "ethereal", "meditative"],
    "created": "2026-05-01T14:23:00Z",
    "modified": "2026-05-01T14:23:00Z",
    "description": "60-second drone with slow harmonic motion."
  },
  "seed": "0xAB12CD34EF567890",
  "macros": {
    "tension":    0.62,
    "damping":    0.18,
    "density":    0.55,
    "migration":  0.22,
    "coherence":  0.85,
    "excitation": 0.30
  },
  "structural": {
    "topology": "ring",
    "aspect":   1.0,
    "substrate_size": 1024,
    "deposit_kernel": "linear",
    "read_kernel":    "linear"
  },
  "agents": {
    "max_active": 32,
    "harmonic_set": [1, 2, 3, 4, 5, 6, 7, 8, 1.5, 2.5],
    "shape_distribution": {
      "sine":    0.6,
      "saw":     0.0,
      "square":  0.0,
      "fmpair":  0.3,
      "noise":   0.1
    },
    "shape_param_default": {
      "fm_ratio_index": 0.5,
      "fm_index_index": 0.3
    }
  },
  "envelopes": {
    "env1": { "attack": 4.0, "decay": 1.0, "sustain": 0.8, "release": 6.0 },
    "env2": { "attack": 0.1, "decay": 0.3, "sustain": 1.0, "release": 0.5 }
  },
  "lfos": [
    { "rate_hz": 0.05, "depth": 1.0, "shape": "sine",   "sync": "free", "phase": 0.0 },
    { "rate_hz": 0.20, "depth": 1.0, "shape": "random", "sync": "free", "phase": 0.0 },
    { "rate_hz": 1.00, "depth": 1.0, "shape": "tri",    "sync": "tempo_quarter", "phase": 0.0 },
    { "rate_hz": 4.00, "depth": 1.0, "shape": "square", "sync": "free", "phase": 0.0 }
  ],
  "modulation_matrix": {
    "slots": [
      { "active": true, "source": "LFO1", "destination": "MIGRATION", "depth":  0.2, "curve": "LINEAR" },
      { "active": true, "source": "ENV1", "destination": "DAMPING",   "depth": -0.3, "curve": "LINEAR" },
      { "active": false, "source": "LFO2", "destination": "TENSION",  "depth":  0.0, "curve": "LINEAR" }
    ]
  },
  "harvesters": {
    "spacing": 0.5,
    "orbit_depth": 0.0,
    "orbit_shape": "circle",
    "orbit_speed_hz": 0.05
  },
  "output": {
    "master_gain_db": 0.0,
    "limiter_active": false
  },
  "_audio_hash": "sha256:1f2e3d4c..."
}
```

### 3.3 The audio hash

`_audio_hash` is a SHA-256 of a 30-second offline render of this preset playing C4 at velocity 100. This hash is generated at preset-creation time and verified by the test rig (08). Any preset whose hash mismatches its render is flagged as broken or platform-skewed.

The hash is informational only — it is not required for loading. A preset without a hash loads normally; only presets being submitted to the factory pack must include a verified hash.

### 3.3a Metadata fields

* `name`: user-editable string. The plug-in proposes a default at first save (e.g., "Untitled Drone 1") but never overwrites a user-entered name.
* `author`: user-editable string; defaults to the value in user preferences (set on first launch).
* `category`: user-selected from `{Drone, Organic, Pitched, Glitch, Hybrid, Init, User}`.
* `tags`: user-editable, free-form, max 16 tags per preset.
* `created`: ISO-8601 timestamp set automatically by the plug-in at the first save of a new preset; never modified afterwards.
* `modified`: ISO-8601 timestamp updated by the plug-in on every subsequent save.
* `description`: user-editable, multi-line text up to 512 characters.

Both timestamps use UTC (`Z` suffix) regardless of the user's local timezone, to keep preset packs portable.

### 3.4 Versioning and migration

The `format_version` field declares the schema version. Engine versions accept presets from older format versions and migrate them to the current schema:

* v1.0 → v1.1: any new fields default to v1.0 behaviour. No migration needed.
* v1.x → v2.0 (planned): structural changes (e.g., external excitation routing) require explicit migration. v2.0 ships with a `migrate_v1_to_v2()` function that maps old presets to new schema and emits warnings about any settings that don't translate.

Future engine versions MUST refuse to load presets whose `format_version` is greater than what they understand, with a clear error message in the GUI.

### 3.5 Preset packs

A **preset pack** is a `.sfsp` file: a ZIP archive containing:

```
preset-pack/
  pack.json                  # pack metadata
  presets/
    01_slow_glacier.sfs
    02_ember_field.sfs
    ...
  audio_previews/
    01_slow_glacier.ogg      # ~10-second preview
    02_ember_field.ogg
    ...
  cover.png                  # 512×512 cover art (optional)
```

`pack.json`:

```json
{
  "_magic": "SFSP-1.00",
  "name": "Anthropic Demo Pack",
  "author": "Anthropic",
  "version": "1.0",
  "preset_count": 32,
  "engine_min_version": "1.0.0",
  "engine_format_version": "1.0",
  "license": "CC-BY-SA-4.0"
}
```

The plug-in's preset browser reads `.sfsp` files directly; users drop them on the GUI to install.

## 4. The factory preset palette

v1.0 ships with **128 factory presets** distributed across categories:

| Category | Count | Notes |
|---|---|---|
| Drone | 32 | Slow evolving sustain pads |
| Organic | 32 | Breathing, evolving textures |
| Pitched | 24 | Polyphonic playable instruments |
| Glitch | 16 | Experimental, chaotic |
| Hybrid | 16 | Multi-character, modulation-driven |
| Init | 8 | Initialisation patches per topology + sonic-corner |

Each preset must have an audio hash and pass listening tests with the test rig (08).

The Init presets are the documentation set: each one demonstrates one engine feature in isolation (just the substrate; just one agent; just modulation; etc.).

## 5. The preset browser data model

The GUI's preset browser sees presets through this index:

```c++
struct PresetIndex {
    std::string name;
    std::string author;
    std::string category;
    std::vector<std::string> tags;
    std::filesystem::path path;
    std::string audioPreviewPath;   // optional
    int sortOrder;
};
```

The index is built at plug-in startup (or when packs are installed) by scanning the user's preset directory and any installed packs. Indexing is on the worker thread; the GUI shows a placeholder until indexing completes.

Search is across name, author, category, tags. Filter chips for category and tags. Sort by name, author, date created.

## 6. Reference behaviours

* **Loading a preset must complete in < 50 ms** with a cold cache. Tested in CI. If a preset takes longer, it's flagged.
* **Saving a preset must be atomic.** Write to `name.sfs.tmp`, fsync, rename. Never leave a partial file.
* **The audio hash must be computed in offline (non-realtime) rendering** to avoid contamination by realtime jitter. The plug-in shell exposes `getAudioHash(presetPath)` for use by tooling and CI.

## 7. Open questions

* **User preset cloud sync.** Reserved for a future service. The format is designed to be cloud-friendly (small JSON, optional preview audio).
* **Preset comparison.** A "diff this preset against the last loaded" feature is sketched in 07 but not implemented in v1.0.
* **Preset randomisation.** A "random preset" button is in 07 spec but uses a curated parameter-space distribution, not pure uniform — so as not to land in unmusical regions.
