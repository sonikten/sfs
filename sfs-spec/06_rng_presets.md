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

The Random123 Philox-4×32-10 primitive takes a **64-bit key** (two 32-bit words) and a **128-bit counter** (four 32-bit words), and returns 128 bits of random output (four 32-bit words). SFS uses this API directly; an earlier draft of this document mistakenly described a 128-bit key.

SFS encodes its stream identity in the counter's high words and its sample/draw position in the counter's low words:

```
key64       = preset_seed_64                                    // identifies the preset
counter[3]  = voice_index_16  | (agent_index_16 << 16)          // who is drawing
counter[2]  = stream_id_16    | (reserved_16 << 16)             // why
counter[1]  = sample_index_high                                 // when (high 32 bits)
counter[0]  = sample_index_low                                  // when (low 32 bits)
```

For initialisation streams (`stream_id ∈ {0…7}` below), `sample_index = 0` and the four output words give 128 bits of fixed entropy keyed by the (preset, voice, agent, stream) tuple. For per-sample streams (e.g., migration noise), `sample_index` increments by 1 per audio sample. For Gaussian draws that need two uniforms, the implementation calls Philox once per Gaussian — not once per uniform — and treats the four output words as two independent Gaussian seeds (Box–Muller without cache; see §1.4).

Stream IDs:

| Stream ID | Purpose | Sample-indexed? |
|---|---|---|
| `0` | Per-voice ignition burst | no (init) |
| `1` | Per-voice agent position initialisation | no (init) |
| `2` | Per-voice agent shape selection | no (init) |
| `3` | Per-voice agent harmonic ratio selection | no (init) |
| `4` | Per-agent migration noise | yes |
| `5` | Per-agent sample-and-hold noise (for `noise` waveform) | yes (one draw per phase wrap) |
| `6` | Per-voice random-walk LFO state | yes |
| `7` | Per-voice envelope time-randomisation | no (init) |
| `8` | Per-voice modulation `RANDOM` source (one value per note-on) | no (init) |
| `9` | Per-voice MPE_RANDOM expression source (one value per note-on) | no (init) |
| `10` | Per-voice harvester orbit (when shape = random_walk) | yes |
| `11` | Plug-in dice button — preset randomisation | no (init, draws from a curated distribution) |
| `12–31` | Reserved | — |

Stream IDs 8–11 cover features that earlier drafts of this document omitted; the wider table is the canonical inventory.

### 1.3 Reference implementation

Philox-4×32-10 takes a **64-bit key** (two 32-bit words) and a **128-bit counter** (four 32-bit words) and returns a 128-bit result (interpreted as four 32-bit unsigned ints). This matches the Random123 library's API exactly; do not rename the parameters or repack them.

The reference C implementation is ~50 lines from [DE Shaw Research Random123](https://github.com/DEShawResearch/random123). SFS wraps it as:

```c++
struct Philox4x32Stream {
    // Key: 64 bits derived from preset seed.
    uint32_t key[2];

    // Counter: 128 bits encoding (sample_index, stream_id, voice, agent).
    // Layout matches §1.2 packing.
    uint32_t counter[4];

    // Last draw: cached output of the last philox call (4 × 32 bits).
    uint32_t lastOut[4];
    uint8_t  lastOutCursor;       // 0..3, which word of lastOut is the next read

    void seed(uint64_t presetSeed,
              uint16_t voice, uint16_t agent, uint16_t stream) {
        key[0] = static_cast<uint32_t>(presetSeed);
        key[1] = static_cast<uint32_t>(presetSeed >> 32);
        counter[3] = (static_cast<uint32_t>(voice) << 16) | agent;
        counter[2] = (static_cast<uint32_t>(stream) << 0)  | (0u << 16);  // reserved high
        counter[1] = 0;            // sample_index high
        counter[0] = 0;            // sample_index low
        lastOutCursor = 4;         // forces philox call on first draw
    }

    void setSampleIndex(uint64_t sampleIndex) {
        counter[0] = static_cast<uint32_t>(sampleIndex);
        counter[1] = static_cast<uint32_t>(sampleIndex >> 32);
        lastOutCursor = 4;         // invalidate cache; next draw re-philoxes
    }

    uint32_t next32() {
        if (lastOutCursor >= 4) {
            philox4x32_R10(lastOut, counter, key);
            // For init streams sample_index is fixed; for per-sample streams the
            // caller advances via setSampleIndex() before each draw, so we only
            // increment counter[0] for back-to-back draws within the same sample.
            // Increment of counter[0] handles overflow into counter[1] correctly.
            if (++counter[0] == 0u) { ++counter[1]; }
            lastOutCursor = 0;
        }
        return lastOut[lastOutCursor++];
    }

    float nextFloat01() {
        return (next32() >> 8) * (1.0f / 16777216.0f);   // 24-bit precision
    }
};
```

`philox4x32_R10(out, counter, key)` is the reference 10-round implementation from the Random123 library — bit-exact across platforms. The wrapper exposes 4 random 32-bit words per Philox call; consumers call `next32()` to draw uniformly.

### 1.4 Gaussian draws (deterministic, no Box–Muller cache)

Gaussian noise is essential for migration. SFS uses Box–Muller **without** the second-value cache, because the cache makes draw order matter for determinism under interleaved consumption (one agent's two-draw Gaussian could split across another agent's draw). Each Gaussian costs one Philox call and consumes two of its four output words, ignoring the other two:

```c++
float nextGaussian(Philox4x32Stream& s) {
    // One philox draw gives 4 × 32-bit; we use the first two for one Gaussian.
    if (s.lastOutCursor >= 4) {
        philox4x32_R10(s.lastOut, s.counter, s.key);
        if (++s.counter[0] == 0u) { ++s.counter[1]; }
        s.lastOutCursor = 4;        // mark consumed; next call re-philoxes
    }
    uint32_t u1_bits = s.lastOut[0];
    uint32_t u2_bits = s.lastOut[1];
    s.lastOutCursor = 4;            // discard remaining 2 words
    float u1 = (u1_bits >> 8) * (1.0f / 16777216.0f);
    float u2 = (u2_bits >> 8) * (1.0f / 16777216.0f);
    if (u1 < 1e-7f) u1 = 1e-7f;
    float r  = dm_sqrt(-2.0f * dm_log(u1));
    return r * dm_cos(u2);          // dm_cos: deterministic primitive
}
```

This costs more Philox calls than a cached Box–Muller (4× more, since we drop two of every four output words), but it makes Gaussians purely a function of the (voice, agent, stream, sample_index) tuple — no inter-call ordering matters.

`dm_sqrt`, `dm_log`, `dm_cos` are deterministic primitives (see §1.6).

### 1.5 Vectorised Gaussian batches

The dominant per-sample RNG cost is migration-noise Gaussians. A vectorised Philox kernel computes 4 streams at once (one per agent in a SIMD lane). Each lane is independently keyed by `(voice, agent_lane_index, stream=4, sample_index)`, so the SIMD output is bit-identical to four independent `nextGaussian` calls run sequentially. This is what makes the determinism contract survive vectorisation.

A small per-voice ring buffer can prefill 64 Gaussians at a time outside the hot per-sample agent loop, but the prefill MUST be keyed by sample-index ranges so the consumer reads exactly the value that the determinism contract specifies for each (agent, sample_index) pair. The prefill is a CPU optimisation; it cannot reorder the deterministic stream.

### 1.6 Deterministic math primitives

The audio path uses only the following primitives, all polynomial or LUT approximations whose output is bit-identical across all supported platforms. The table maps each primitive to its accuracy bound and the library equivalent it replaces:

| Primitive | Replaces | Accuracy | Use site |
|---|---|---|---|
| `dm_sin(x)` | `sinf`, `std::sin` | < 1e-6 | agent waveforms; ambisonic encoder |
| `dm_cos(x)` | `cosf`, `std::cos` | < 1e-6 | Gaussian, harvester orbits |
| `dm_tanh(x)` | `tanhf`, `std::tanh` | < 1e-3 | output saturator |
| `dm_exp(x)` | `expf`, `std::exp` | < 1e-5 | smoothing coefficients (init only), envelope curves |
| `dm_log(x)` | `logf`, `std::log` | < 1e-5 | Gaussian (Box–Muller) |
| `dm_sqrt(x)` | `sqrtf`, `std::sqrt` | exact (HW) | Gaussian, normalisation; uses platform-IEEE-conformant `sqrtf` which IS deterministic in IEEE 754 round-to-nearest mode |
| `dm_pow(x, y)` | `powf`, `std::pow` | < 1e-4 | FM ratio (init only) |
| `dm_pow2(x)` | `exp2f`, `std::exp2` | < 1e-4 | MPE pitch-bend ratio (block-rate); 5th-degree minimax + `ldexp` |
| `dm_floor(x)` | `floorf`, `std::floor` | exact | wrap, fract |
| `dm_fract(x)` | `x - floor(x)` | exact | phase wrap |
| `dm_fmod(x, y)` | `fmodf`, `std::fmod` | derived from `dm_floor` | not used in v1.0 audio path |

`sqrtf` is on most platforms a single hardware instruction with IEEE 754 correct rounding; we treat it as deterministic when the engine sets round-to-nearest mode at thread entry. `dm_sqrt` may simply call `sqrtf`. All other primitives are pure polynomial/LUT and ship with the engine.

The set is closed: any audio-path computation that needs a transcendental MUST use one of the above. Library calls (`std::sin`, `tanhf`, etc.) are forbidden anywhere reachable from `engineStep()` (01 §5.4). They remain allowed in init/non-audio code paths.

### 1.7 RNG cost summary

* Initialisation (once per voice): ~64 calls = ~1 µs.
* Per-sample agent migration: ~64 Gaussians per voice = ~1 µs vectorised (per §1.5), ~3 µs scalar.

Negligible compared to substrate updates.

## 2. Determinism contract

The contract: given identical preset, identical MIDI input, identical sample rate, identical block size, and identical channel layout, the SFS engine produces bit-identical audio output across platforms.

### 2.1 The canonical render

The test rig (08) renders presets under one canonical configuration:

* **Sample rate**: 48 000 Hz.
* **Block size**: 256 samples.
* **Channel layout**: stereo (2 channels), unless the preset explicitly requires another layout (e.g., the 5.1 factory preset renders in 5.1).
* **Sample format**: 32-bit float PCM.
* **MIDI input**: a fixed sequence per preset, stored alongside the preset as `<preset>.midi`. The default sequence is "C4 at velocity 100, gate-on at sample 0, gate-off at sample 24000 (0.5 s), render until sample 1440000 (30 s)."
* **Tail handling**: render continues until either 30 s elapsed or RMS over the last 100 ms drops below −96 dBFS.
* **Dither**: none. Output is stored as raw 32-bit float; conversion to 16/24-bit is the host's responsibility.
* **Engine settings**: `engine.oversample_mode = STD`, `engine.max_voices = 8`, `engine.theme` and other GUI-only preferences ignored.

### 2.2 The hash and diff target

The audio hash and bit-exact diff operate on **raw PCM frames** — not the WAV file as written to disk. The WAV file's RIFF header, BWF metadata, INFO chunks, and any ID3 tags are NOT part of the determinism surface. Specifically:

* Compute a SHA-256 hash over the concatenated channel-interleaved 32-bit float samples produced by the engine for the canonical render.
* Store this hash as the preset's `_audio_hash` (§3.3).
* CI re-renders, recomputes the hash, and compares.

This hash-of-PCM-frames approach makes the determinism contract independent of WAV writer implementation, host metadata insertion, or BWF timestamp differences across platforms.

### 2.3 Sources of non-determinism explicitly excluded

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

`.sfs` is the file extension. The plug-in registers `application/x-sfs` as a MIME type for drag-and-drop import.

The file is **pure JSON** (no binary header, no length prefix). Identification uses the first JSON key:

* The first key in the top-level object MUST be `"_magic"` and its value MUST be `"SFS-1.00"`.
* By convention the JSON object opens at byte 0 with `{"_magic": "SFS-1.00",` so `grep -l '"_magic":"SFS' *.sfs` (with whitespace tolerance) is a quick file-detection check.
* The plug-in's preset loader parses the file as JSON and validates the `_magic` field; the loader does not perform a byte-offset comparison.

Earlier drafts of this document described a fixed 8-byte ASCII prefix; that was inconsistent with the JSON-first design. Pure JSON wins because it is diff-friendly under version control, easy to inspect, and survives copy-paste in forums and Git.

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

### 3.3b Complete persisted-field reference

The example in §3.2 is illustrative; for v1.0 the **canonical** persisted-field set is exactly the union of:

* All host-exposed parameters in 09 §3.1–3.8 with `Host-visible: yes` or `no` (every row of those tables persists in the preset, not only host-visible ones; `no` only means the parameter doesn't appear in the host's automation list).
* The reserved fields catalogued in 09 §"Reserved persisted fields" (which currently include `shared_substrate`, `substrate.laplacian_order`, and `substrate.nonlinear_beta`).
* The metadata fields in §3.3a.
* The modulation matrix and LFO/envelope state per §3.2.

A preset MUST include every host-exposed parameter from 09 §3.1–3.8 (so that loading is deterministic), every reserved field from 09 §"Reserved persisted fields" (which fall back to documented defaults when omitted in older presets), and the metadata fields. The example JSON in §3.2 shows the structure but elides routine fields; document 09 is authoritative for the complete field list. A preset that omits a host-exposed field loads with that field set to its 09-documented default.

Engine preferences (`engine.oversample_mode`, `engine.max_voices`, `engine.gui_refresh_hz`, `engine.theme`) are NOT preset-persisted — they live in user preferences (09 §5).

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
