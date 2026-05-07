// src/engine/voice.cpp
//
// Per-voice engine glue. See voice.h for the contract.

#include "voice.h"

#include "dsp/denormal_flush.h"
#include "engine/rng/philox.h"

#include <algorithm>
#include <cmath>

namespace sfs::engine
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDcCutoffHz = 5.0f;

// Phase 1 output-stage saturator. The full sfs-spec/04 §4 output chain
// (master gain + dm_tanh saturator + DC block + optional limiter) lands
// in Phase 2 / Phase 4; for Phase 1 a rational soft-clip keeps the
// substrate's natural amplitude swings inside (-1, 1) without needing
// dm_tanh. x / (1 + |x|) is C¹, monotonic, and bounded.
//
//   x = 0.0 → 0.0   (linear at small signals)
//   x = 0.5 → 0.333 (gentle compression)
//   x = 1.0 → 0.5
//   x = 2.0 → 0.667
//   x = 5.0 → 0.833 (asymptote toward ±1)
//
// Pre-gain is intentionally < 1 so the typical Phase 1 substrate
// amplitude (RMS ≈ 0.6 raw) lands at perceived RMS ≈ 0.3 after clip.
constexpr float kOutputPreGain = 0.5f;

[[nodiscard]] inline float softClip(float x) noexcept
{
    return x / (1.0f + std::fabs(x));
}

} // namespace

// Run the mod matrix once per block, returning the macro values modulated
// by all active slots. The return-by-value is deliberate — callers want a
// snapshot for the single fan-out call below; we don't want to mutate the
// host-parameter state stored in macros_.
sfs::engine::macros::MacroValues Voice::applyModMatrix(const sfs::engine::macros::MacroValues& base) const noexcept
{
    using namespace sfs::engine::mod_matrix;

    std::array<float, ModMatrix::kNumSources> sources{};
    sources[static_cast<std::size_t>(Source::Lfo1)] = lfoValues_[0];
    sources[static_cast<std::size_t>(Source::Lfo2)] = lfoValues_[1];
    sources[static_cast<std::size_t>(Source::Lfo3)] = lfoValues_[2];
    sources[static_cast<std::size_t>(Source::Lfo4)] = lfoValues_[3];
    sources[static_cast<std::size_t>(Source::Env1)] = ampEnv_.value();
    sources[static_cast<std::size_t>(Source::KeyVelocity)] = keyVelocity_;
    sources[static_cast<std::size_t>(Source::MidiCc1)] = midiCc1_;
    sources[static_cast<std::size_t>(Source::Random)] = randomPerNote_;

    std::array<float, ModMatrix::kNumDestinations> deltas{};
    modMatrix_.evaluate(sources, deltas);

    sfs::engine::macros::MacroValues out = base;
    out.tension += deltas[static_cast<std::size_t>(Destination::Tension)];
    out.damping += deltas[static_cast<std::size_t>(Destination::Damping)];
    out.density += deltas[static_cast<std::size_t>(Destination::Density)];
    out.migration += deltas[static_cast<std::size_t>(Destination::Migration)];
    out.coherence += deltas[static_cast<std::size_t>(Destination::Coherence)];
    out.excitation += deltas[static_cast<std::size_t>(Destination::Excitation)];
    out.clampInPlace();
    return out;
}

// Apply the block-rate macro fan-out to substrate + every active agent.
// Wires every InternalField that maps cleanly onto Phase 2 state:
//   TENSION    → substrate c²
//   DAMPING    → substrate γ
//   DENSITY    → activeCount (live resize) + per-agent deposit weight
//   MIGRATION  → per-agent r_i drift + ε_i noise scale
//   COHERENCE  → per-agent detuned frequency from baseFrequency
//   EXCITATION → per-agent modSensitivity
// Plus the SHAPE selector (not a macro, but applied here for symmetry).
void Voice::applyMacroFanOut(const sfs::engine::macros::InternalFields& fields) noexcept
{
    // Substrate κ from the spec's viscosity fan-out (sfs-spec/05 §3.1 +
    // §3.6): κ = clamp(viscosityFloor + viscosityOffset, 0, κ_max).
    constexpr float kKappaMax = 0.225f;
    const float kappa = std::clamp(fields.substrateViscosityFloor + fields.substrateViscosityOffset, 0.0f, kKappaMax);
    // Both substrates always receive the macro-driven coefficients so
    // switching topology mid-render gets the right behaviour
    // immediately — the inactive substrate is just unread, not stale.
    // The 2D CFL clamp (c² + κ ≤ 0.225) is enforced internally by
    // Substrate2D::setCoefficients.
    substrate_.setCoefficients(fields.substrateC2, kappa, fields.substrateGamma);
    substrate2D_.setCoefficients(fields.substrateC2, kappa, fields.substrateGamma);

    // DENSITY: live resize the active subset BEFORE we walk active agents.
    agents_.setActiveCount(fields.agentActiveCount);

    constexpr float kRDirectionUnit = 0.001f * 1024.0f;
    constexpr float kEpsNoiseUnit = 0.0005f * 1024.0f;
    constexpr float kCentsToRatio = 1.0f / 1200.0f;
    const float driftScale = fields.agentDriftScale;
    const float noiseScale = fields.agentMigrationNoiseScale;
    const float detuneScale = fields.agentDetuneScale; // (1 - C)²
    for (int i = 0; i < agents_.activeCount(); ++i)
    {
        auto& a = agents_.mutableAgent(i);
        a.shape = uniformShape_;
        a.modSensitivity = fields.agentModSensitivityScale;
        a.depositWeight = a.baseDepositWeight * fields.agentDepositWeightScale;
        a.migrationRate = a.migrationDirection * driftScale * kRDirectionUnit;
        a.migrationNoiseScale = noiseScale * kEpsNoiseUnit;
        const float cents = a.detuneCents * detuneScale;
        a.frequency = a.baseFrequency * (1.0f + cents * kCentsToRatio);
    }
}

void Voice::snapshotSubstrate(float* dst, int dstSize) const noexcept
{
    if (dst == nullptr || dstSize <= 0)
    {
        return;
    }
    if (topology_ == Topology::Torus2D)
    {
        substrate2D_.snapshot(dst, static_cast<std::size_t>(dstSize));
    }
    else
    {
        substrate_.snapshot(dst, static_cast<std::size_t>(dstSize));
    }
}

Voice::Voice(int substrateCells, int agentCount, float sampleRate)
    : substrate_(substrateCells, sampleRate),
      // 2D substrate sized for the same total cell count (32×32 = 1024
      // matches the 1D default). Phase 4 preset format will let the user
      // pick 64×64 or 128×128 explicitly.
      substrate2D_(32, 32, sampleRate),
      agents_(agentCount),
      sampleRate_(sampleRate)
{
    // Phase 2 amplitude envelope. Defaults are middle-of-road
    // pad/lead values; preset format will overwrite in Phase 4.
    ampEnv_.setSampleRate(sampleRate);
    ampEnv_.setAttackMs(10.0f);
    ampEnv_.setDecayMs(120.0f);
    ampEnv_.setSustainLevel(0.75f);
    ampEnv_.setReleaseMs(250.0f);

    // Phase 2 LFO bank (sfs-spec/05 §5). Four LFOs per voice; default
    // shapes/rates form a useful starting palette before the preset
    // format lands. The mod matrix wires them to destinations.
    constexpr float kDefaultLfoRates[kLfoCount] = {0.5f, 2.0f, 5.0f, 7.0f};
    constexpr sfs::engine::lfo::LfoShape kDefaultLfoShapes[kLfoCount] = {
        sfs::engine::lfo::LfoShape::Sine,
        sfs::engine::lfo::LfoShape::Triangle,
        sfs::engine::lfo::LfoShape::Sine,
        sfs::engine::lfo::LfoShape::SampleHold,
    };
    for (int i = 0; i < kLfoCount; ++i)
    {
        lfos_[static_cast<std::size_t>(i)].setSampleRate(sampleRate);
        lfos_[static_cast<std::size_t>(i)].setRateHz(kDefaultLfoRates[i]);
        lfos_[static_cast<std::size_t>(i)].setShape(kDefaultLfoShapes[i]);
    }

    // Phase 2 default mod-matrix wiring. The preset format (Phase 4) takes
    // over once it lands; until then this gives the stock plug-in audible
    // movement and a working mod-wheel response. Slots 4-15 are inactive.
    using namespace sfs::engine::mod_matrix;
    modMatrix_.setSlot(0, Source::MidiCc1, Destination::Migration, 0.5f);
    modMatrix_.setSlot(1, Source::Lfo1, Destination::Tension, 0.10f);
    modMatrix_.setSlot(2, Source::Lfo2, Destination::Coherence, -0.08f);
    modMatrix_.setSlot(3, Source::KeyVelocity, Destination::Excitation, 0.30f);

    // Default coefficients chosen for an audible "alive" feel out of the box,
    // close to sfs-spec/09 §3.7 internal defaults. Phase 2's macro fan-out
    // will set these from TENSION/DAMPING/etc.
    substrate_.setCoefficients(0.30f, 0.05f, 0.005f);
    // 2D substrate uses the tighter CFL bound (0.225); pick coefficients
    // that satisfy it and feel similar in character to the 1D defaults.
    substrate2D_.setCoefficients(0.15f, 0.04f, 0.005f);
    agents_.layoutEvenly(substrateCells);

    // DC blocker α from the host sample rate (sfs-spec/02 §6).
    dcBlockerAlpha_ = 1.0f - 2.0f * kPi * kDcCutoffHz / sampleRate_;

    // Mono harvester at the midpoint of the ring — maximally separated from
    // any single-agent deposit at position 0 so the substrate has to actually
    // propagate the wave to be heard.
    harvesterPosition_ = static_cast<float>(substrateCells) * 0.5f;

    // Stereo: two harvesters at positions 0 and N/2 (sfs-spec/04 §3.2).
    // The ring's wave propagation between them produces natural inter-channel
    // decorrelation.
    harvesterPositionStereoL_ = 0.0f;
    harvesterPositionStereoR_ = static_cast<float>(substrateCells) * 0.5f;
}

void Voice::noteOn(int midiNote, float velocity)
{
    agents_.noteOn(midiNote, velocity);
    ampEnv_.noteOn();
    keyVelocity_ = std::clamp(velocity, 0.0f, 1.0f);
    // Phase 2 LFOs retrigger from phase 0 at noteOn (free-running becomes
    // a host-parameter choice when the preset format lands). Seed each
    // LFO's S&H stream from the canonical (preset, voice, lfo) tuple.
    constexpr std::uint64_t kPhase2PresetSeed = 0x5F5'5F5'5F5'5F5ull;
    constexpr std::uint16_t kPhase2VoiceIndex = 0;
    for (int i = 0; i < kLfoCount; ++i)
    {
        lfos_[static_cast<std::size_t>(i)].reset();
        lfos_[static_cast<std::size_t>(i)].seedStream(kPhase2PresetSeed,
                                                      kPhase2VoiceIndex,
                                                      static_cast<std::uint16_t>(i));
    }

    // RANDOM source (sfs-spec/06 §1.2 stream 8 ModRandomSource): one draw
    // per noteOn, mapped to [-1, 1).
    sfs::engine::rng::Philox4x32Stream randomStream;
    randomStream.seed(kPhase2PresetSeed, kPhase2VoiceIndex, 0, sfs::engine::rng::StreamId::ModRandomSource);
    const float u01 = randomStream.nextFloat01();
    randomPerNote_ = u01 * 2.0f - 1.0f;

    gated_ = true;
}

void Voice::noteOff()
{
    agents_.noteOff();
    ampEnv_.noteOff();
    gated_ = false;
}

void Voice::setTopology(Topology t) noexcept
{
    if (topology_ == t)
    {
        return;
    }
    topology_ = t;
    // Reset both substrates so the prior topology's residual energy
    // doesn't bleed through. Re-lay the agents for the new topology.
    substrate_.reset();
    substrate2D_.reset();
    if (t == Topology::Torus2D)
    {
        agents_.layoutEvenly2D(substrate2D_.cellsX(), substrate2D_.cellsY());
    }
    else
    {
        agents_.layoutEvenly(substrate_.size());
    }
    // Reset DC-blocker filter state too — the new substrate's harvester
    // reads start from zero.
    dcBlockerLastInput_ = 0.0f;
    dcBlockerLastOutput_ = 0.0f;
    dcBlockerLastInputL_ = 0.0f;
    dcBlockerLastOutputL_ = 0.0f;
    dcBlockerLastInputR_ = 0.0f;
    dcBlockerLastOutputR_ = 0.0f;
}

void Voice::noteOnAfterSteal(int midiNote, float velocity)
{
    // Reset both substrate states so the prior note's residual energy
    // doesn't bleed through the new note (independent of which topology
    // is currently active — switching topology + stealing in the same
    // window leaves no residual). 5 ms output-gain ramp 0 → 1 covers the
    // substrate's wake-up transient.
    substrate_.reset();
    substrate2D_.reset();
    dcBlockerLastInput_ = 0.0f;
    dcBlockerLastOutput_ = 0.0f;
    dcBlockerLastInputL_ = 0.0f;
    dcBlockerLastOutputL_ = 0.0f;
    dcBlockerLastInputR_ = 0.0f;
    dcBlockerLastOutputR_ = 0.0f;

    constexpr float kStealRampMs = 5.0f;
    const float rampSamples = std::max(1.0f, kStealRampMs * 0.001f * sampleRate_);
    stealRampGain_ = 0.0f;
    stealRampInc_ = 1.0f / rampSamples;

    noteOn(midiNote, velocity);
}

void Voice::renderBlock(float* out, int numSamples) noexcept
{
    if (out == nullptr || numSamples <= 0)
    {
        return;
    }

    // Self-defend FTZ/DAZ even when called outside a JUCE plug-in context
    // (tests, sfs_render, sfs_profile). The plug-in path also sets
    // juce::ScopedNoDenormals at processBlock entry; both nest cleanly
    // because both are RAII scopes that save/restore.
    const sfs::dsp::ScopedFlushToZero scopedFtz;

    // Apply macro fan-out at block boundaries (sfs-spec/05 §4 — block-rate
    // evaluation, sample-rate smoothing on the macro outputs is a Phase 2
    // follow-up). Currently wired:
    //   TENSION    → substrate.c2
    //   DAMPING    → substrate.gamma
    //   EXCITATION → agent.modSensitivity (per-agent)
    // DENSITY / MIGRATION / COHERENCE outputs are computed but not yet
    // applied to live state; they land alongside the agent re-allocator
    // (DENSITY), live re-seed of migration noise scales (MIGRATION), and
    // harmonic_set logic (COHERENCE).
    macros_.clampInPlace();
    const sfs::engine::macros::MacroValues modulated = applyModMatrix(macros_);
    const sfs::engine::macros::InternalFields fields = sfs::engine::macros::fanOut(modulated);
    applyMacroFanOut(fields);

    const float pos = harvesterPosition_;
    const float pos2DX = static_cast<float>(substrate2D_.cellsX()) * 0.5f;
    const float pos2DY = static_cast<float>(substrate2D_.cellsY()) * 0.5f;
    const float a = dcBlockerAlpha_;
    float prevIn = dcBlockerLastInput_;
    float prevOut = dcBlockerLastOutput_;
    const bool use2D = (topology_ == Topology::Torus2D);
    for (int i = 0; i < numSamples; ++i)
    {
        for (int li = 0; li < kLfoCount; ++li)
        {
            lfoValues_[static_cast<std::size_t>(li)] = lfos_[static_cast<std::size_t>(li)].tick();
        }
        agents_.setVoiceGain(ampEnv_.tick());

        float raw = 0.0f;
        if (use2D)
        {
            agents_.processOneSample(substrate2D_, sampleRate_);
            substrate2D_.step();
            raw = substrate2D_.read(pos2DX, pos2DY);
        }
        else
        {
            agents_.processOneSample(substrate_, sampleRate_);
            substrate_.step();
            raw = substrate_.read(pos);
        }

        const float blocked = raw - prevIn + a * prevOut; // first-order DC block
        prevIn = raw;
        prevOut = blocked;
        out[i] = softClip(kOutputPreGain * blocked) * stealRampGain_;
        if (stealRampGain_ < 1.0f)
        {
            stealRampGain_ = std::min(1.0f, stealRampGain_ + stealRampInc_);
        }
    }
    dcBlockerLastInput_ = prevIn;
    dcBlockerLastOutput_ = prevOut;
}

void Voice::renderBlockStereo(float* outL, float* outR, int numSamples) noexcept
{
    if (outL == nullptr || outR == nullptr || numSamples <= 0)
    {
        return;
    }

    const sfs::dsp::ScopedFlushToZero scopedFtz;

    macros_.clampInPlace();
    const sfs::engine::macros::MacroValues modulated = applyModMatrix(macros_);
    const sfs::engine::macros::InternalFields fields = sfs::engine::macros::fanOut(modulated);
    applyMacroFanOut(fields);

    const float a = dcBlockerAlpha_;
    float prevInL = dcBlockerLastInputL_;
    float prevOutL = dcBlockerLastOutputL_;
    float prevInR = dcBlockerLastInputR_;
    float prevOutR = dcBlockerLastOutputR_;

    const float posL = harvesterPositionStereoL_;
    const float posR = harvesterPositionStereoR_;
    // 2D harvester layout: L at (0, midY), R at (midX, midY) — same
    // X-axis spread as 1D (positions 0 and N/2 along the ring) with
    // Y centred. Phase 4 multichannel will let the harvester ring
    // around the torus in a polygonal pattern.
    const float pos2DLx = 0.0f;
    const float pos2DLy = static_cast<float>(substrate2D_.cellsY()) * 0.5f;
    const float pos2DRx = static_cast<float>(substrate2D_.cellsX()) * 0.5f;
    const float pos2DRy = pos2DLy;
    const bool use2D = (topology_ == Topology::Torus2D);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int li = 0; li < kLfoCount; ++li)
        {
            lfoValues_[static_cast<std::size_t>(li)] = lfos_[static_cast<std::size_t>(li)].tick();
        }
        agents_.setVoiceGain(ampEnv_.tick());

        float rawL = 0.0f;
        float rawR = 0.0f;
        if (use2D)
        {
            agents_.processOneSample(substrate2D_, sampleRate_);
            substrate2D_.step();
            rawL = substrate2D_.read(pos2DLx, pos2DLy);
            rawR = substrate2D_.read(pos2DRx, pos2DRy);
        }
        else
        {
            agents_.processOneSample(substrate_, sampleRate_);
            substrate_.step();
            rawL = substrate_.read(posL);
            rawR = substrate_.read(posR);
        }

        const float blockedL = rawL - prevInL + a * prevOutL;
        prevInL = rawL;
        prevOutL = blockedL;
        outL[i] = softClip(kOutputPreGain * blockedL) * stealRampGain_;

        const float blockedR = rawR - prevInR + a * prevOutR;
        prevInR = rawR;
        prevOutR = blockedR;
        outR[i] = softClip(kOutputPreGain * blockedR) * stealRampGain_;

        if (stealRampGain_ < 1.0f)
        {
            stealRampGain_ = std::min(1.0f, stealRampGain_ + stealRampInc_);
        }
    }

    dcBlockerLastInputL_ = prevInL;
    dcBlockerLastOutputL_ = prevOutL;
    dcBlockerLastInputR_ = prevInR;
    dcBlockerLastOutputR_ = prevOutR;
}

} // namespace sfs::engine
