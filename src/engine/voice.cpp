// src/engine/voice.cpp
//
// Per-voice engine glue. See voice.h for the contract.

#include "voice.h"

#include "dsp/denormal_flush.h"
#include "dsp/dm_pow2.h"
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
    sources[static_cast<std::size_t>(Source::MpePressure)] = mpePressure_;
    sources[static_cast<std::size_t>(Source::MpeTimbre)] = mpeTimbre_;

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
    // MPE pitch-bend ratio: block-rate 2^(semitones/12). Bypassed (= 1.0)
    // when no bend is active so non-MPE renders stay bit-exact with Phase 2.
    const float pitchBendRatio = (pitchBendSemitones_ != 0.0f) ? sfs::dsp::dm_pow2(pitchBendSemitones_ * (1.0f / 12.0f))
                                                               : 1.0f;
    for (int i = 0; i < agents_.activeCount(); ++i)
    {
        auto& a = agents_.mutableAgent(i);
        a.shape = uniformShape_;
        a.modSensitivity = fields.agentModSensitivityScale;
        a.depositWeight = a.baseDepositWeight * fields.agentDepositWeightScale;
        a.migrationRate = a.migrationDirection * driftScale * kRDirectionUnit;
        a.migrationNoiseScale = noiseScale * kEpsNoiseUnit;
        const float cents = a.detuneCents * detuneScale;
        a.frequency = a.baseFrequency * (1.0f + cents * kCentsToRatio) * pitchBendRatio;
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

    // LFE 120 Hz first-order LPF: α = 1 - exp(-2π·fc/fs). Padé approximation
    // (std::exp forbidden in src/engine).
    constexpr float kLfeCutoffHz = 120.0f;
    constexpr float kTwoPi = 6.2831853f;
    const float x = kTwoPi * kLfeCutoffHz / sampleRate;
    lfeLpfAlpha_ = (2.0f * x) / (2.0f + x);
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

void Voice::renderBlockSurround51(
    float* outL, float* outR, float* outC, float* outLfe, float* outLs, float* outRs, int numSamples) noexcept
{
    if (outL == nullptr || outR == nullptr || outC == nullptr || outLfe == nullptr || outLs == nullptr ||
        outRs == nullptr || numSamples <= 0)
    {
        return;
    }

    // 1D fallback: spec sfs-spec/04 §3.4 — "5.1 layouts downmix to
    // stereo at the output stage" when topology is 1D. Render the
    // existing stereo path into L/R, copy L/R into Ls/Rs (a flat
    // backwards image since 1D has no spatial back), C = (L+R)/2,
    // LFE = LPF(L+R).
    if (topology_ != Topology::Torus2D)
    {
        renderBlockStereo(outL, outR, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            outC[i] = 0.5f * (outL[i] + outR[i]);
            outLs[i] = outL[i];
            outRs[i] = outR[i];
            const float sum = outL[i] + outR[i];
            // 1D-mode 5.1 LFE downmix: 2nd-order LR (see 5.1 path).
            lfeLpfStage1_ += lfeLpfAlpha_ * (sum - lfeLpfStage1_);
            lfeLpfStage2_ += lfeLpfAlpha_ * (lfeLpfStage1_ - lfeLpfStage2_);
            outLfe[i] = lfeLpfStage2_;
        }
        return;
    }

    const sfs::dsp::ScopedFlushToZero scopedFtz;

    macros_.clampInPlace();
    const sfs::engine::macros::MacroValues modulated = applyModMatrix(macros_);
    const sfs::engine::macros::InternalFields fields = sfs::engine::macros::fanOut(modulated);
    applyMacroFanOut(fields);

    // Five harvester positions at ITU-R BS.775 angles around a circle of
    // radius 0.4·min(Nx, Ny) centred at (0.5·Nx, 0.5·Ny). Angles use
    // standard counterclockwise from front (positive X = front, positive
    // Y = left, in audio convention).
    const float nx = static_cast<float>(substrate2D_.cellsX());
    const float ny = static_cast<float>(substrate2D_.cellsY());
    const float cx = 0.5f * nx;
    const float cy = 0.5f * ny;
    const float r = 0.4f * std::min(nx, ny);

    // 5 angles in degrees: L=-30, R=+30, C=0, Ls=-110, Rs=+110.
    // Pre-computed unit-circle (cos, sin) values to avoid runtime
    // transcendentals (the spec's std::cos/sin are forbidden in src/engine).
    constexpr float kCosNeg30 = 0.8660254f;
    constexpr float kSinNeg30 = -0.5f;
    constexpr float kCosPos30 = 0.8660254f;
    constexpr float kSinPos30 = 0.5f;
    constexpr float kCosZero = 1.0f;
    constexpr float kSinZero = 0.0f;
    constexpr float kCosNeg110 = -0.34202014f;
    constexpr float kSinNeg110 = -0.93969262f;
    constexpr float kCosPos110 = -0.34202014f;
    constexpr float kSinPos110 = 0.93969262f;

    // Positions per channel (substrate-cell coordinates, Y-axis flipped
    // because grid coordinates increase downward).
    const float posLx = cx + r * kCosNeg30;
    const float posLy = cy - r * kSinNeg30;
    const float posRx = cx + r * kCosPos30;
    const float posRy = cy - r * kSinPos30;
    const float posCx = cx + r * kCosZero;
    const float posCy = cy - r * kSinZero;
    const float posLsx = cx + r * kCosNeg110;
    const float posLsy = cy - r * kSinNeg110;
    const float posRsx = cx + r * kCosPos110;
    const float posRsy = cy - r * kSinPos110;

    const float a = dcBlockerAlpha_;
    auto prevIn = dcBlockerLastInput51_;
    auto prevOut = dcBlockerLastOutput51_;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int li = 0; li < kLfoCount; ++li)
        {
            lfoValues_[static_cast<std::size_t>(li)] = lfos_[static_cast<std::size_t>(li)].tick();
        }
        agents_.setVoiceGain(ampEnv_.tick());

        agents_.processOneSample(substrate2D_, sampleRate_);
        substrate2D_.step();

        const float rL = substrate2D_.read(posLx, posLy);
        const float rR = substrate2D_.read(posRx, posRy);
        const float rC = substrate2D_.read(posCx, posCy);
        const float rLs = substrate2D_.read(posLsx, posLsy);
        const float rRs = substrate2D_.read(posRsx, posRsy);
        const float lfeRaw = rL + rR + rC + rLs + rRs;
        // 5.1 2D LFE: 2nd-order Linkwitz-Riley (sfs-spec/04 §3.4).
        lfeLpfStage1_ += lfeLpfAlpha_ * (lfeRaw - lfeLpfStage1_);
        lfeLpfStage2_ += lfeLpfAlpha_ * (lfeLpfStage1_ - lfeLpfStage2_);
        const float rLfe = lfeLpfStage2_;

        // Per-channel DC block + soft clip + steal ramp.
        const std::array<float, 6> raw = {rL, rR, rC, rLfe, rLs, rRs};
        std::array<float, 6> out{};
        for (int c = 0; c < 6; ++c)
        {
            const float blocked = raw[static_cast<std::size_t>(c)] - prevIn[static_cast<std::size_t>(c)] +
                                  a * prevOut[static_cast<std::size_t>(c)];
            prevIn[static_cast<std::size_t>(c)] = raw[static_cast<std::size_t>(c)];
            prevOut[static_cast<std::size_t>(c)] = blocked;
            out[static_cast<std::size_t>(c)] = softClip(kOutputPreGain * blocked) * stealRampGain_;
        }
        outL[i] = out[0];
        outR[i] = out[1];
        outC[i] = out[2];
        outLfe[i] = out[3];
        outLs[i] = out[4];
        outRs[i] = out[5];

        if (stealRampGain_ < 1.0f)
        {
            stealRampGain_ = std::min(1.0f, stealRampGain_ + stealRampInc_);
        }
    }

    dcBlockerLastInput51_ = prevIn;
    dcBlockerLastOutput51_ = prevOut;
}

void Voice::renderBlockSurround714(float* const* outs, int numSamples) noexcept
{
    if (outs == nullptr || numSamples <= 0)
    {
        return;
    }
    for (int c = 0; c < 12; ++c)
    {
        if (outs[c] == nullptr)
        {
            return;
        }
    }

    // 1D fallback: spec downmix-to-stereo. Floor channels mirror the 5.1
    // shape; height channels are silent (1D has no height information).
    if (topology_ != Topology::Torus2D)
    {
        renderBlockStereo(outs[0], outs[1], numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = outs[0][i];
            const float r = outs[1][i];
            outs[2][i] = 0.5f * (l + r); // C
            const float sum = l + r;
            // 7.1.4 LFE: 2nd-order Linkwitz-Riley (cascaded 1st-order
            // Butterworth-1 stages) at 120 Hz. -6 dB at fc, -12 dB/oct.
            lfeLpfStage1_ += lfeLpfAlpha_ * (sum - lfeLpfStage1_);
            lfeLpfStage2_ += lfeLpfAlpha_ * (lfeLpfStage1_ - lfeLpfStage2_);
            outs[3][i] = lfeLpfStage2_; // LFE
            outs[4][i] = l;             // Ls
            outs[5][i] = r;             // Rs
            outs[6][i] = l;             // Lr
            outs[7][i] = r;             // Rr
            outs[8][i] = 0.0f;          // Tfl
            outs[9][i] = 0.0f;          // Tfr
            outs[10][i] = 0.0f;         // Trl
            outs[11][i] = 0.0f;         // Trr
        }
        return;
    }

    const sfs::dsp::ScopedFlushToZero scopedFtz;

    macros_.clampInPlace();
    const sfs::engine::macros::MacroValues modulated = applyModMatrix(macros_);
    const sfs::engine::macros::InternalFields fields = sfs::engine::macros::fanOut(modulated);
    applyMacroFanOut(fields);

    const float nx = static_cast<float>(substrate2D_.cellsX());
    const float ny = static_cast<float>(substrate2D_.cellsY());
    const float minDim = std::min(nx, ny);

    // Floor (7 ch): centre (0.5·Nx, 0.2·Ny), radius 0.15·minDim — keeps
    // y in [0.05·Ny, 0.35·Ny] which respects the spec's "y < 0.3·Ny"
    // floor band with a small margin.
    const float floorCx = 0.5f * nx;
    const float floorCy = 0.2f * ny;
    const float floorR = 0.15f * minDim;

    // Height (4 ch): centre (0.5·Nx, 0.8·Ny), same radius.
    const float topCx = 0.5f * nx;
    const float topCy = 0.8f * ny;
    const float topR = 0.15f * minDim;

    // Pre-computed unit-circle (cos, sin) for every angle used:
    //   floor: 0, ±30, ±90, ±150
    //   height: ±45, ±135
    constexpr float kCos0 = 1.0f, kSin0 = 0.0f;
    constexpr float kCosP30 = 0.8660254f, kSinP30 = 0.5f;
    constexpr float kCosN30 = 0.8660254f, kSinN30 = -0.5f;
    constexpr float kCosP90 = 0.0f, kSinP90 = 1.0f;
    constexpr float kCosN90 = 0.0f, kSinN90 = -1.0f;
    constexpr float kCosP150 = -0.8660254f, kSinP150 = 0.5f;
    constexpr float kCosN150 = -0.8660254f, kSinN150 = -0.5f;
    constexpr float kCosP45 = 0.7071068f, kSinP45 = 0.7071068f;
    constexpr float kCosN45 = 0.7071068f, kSinN45 = -0.7071068f;
    constexpr float kCosP135 = -0.7071068f, kSinP135 = 0.7071068f;
    constexpr float kCosN135 = -0.7071068f, kSinN135 = -0.7071068f;

    // Y axis points down in grid coords; angle convention is CCW from
    // front (audio convention positive = left ear). Position formula:
    //   x = cx + r·cos(angle), y = cy - r·sin(angle).
    // Floor positions (channels 0..7 except LFE at 3):
    const float pL[2] = {floorCx + floorR * kCosN30, floorCy - floorR * kSinN30};
    const float pR[2] = {floorCx + floorR * kCosP30, floorCy - floorR * kSinP30};
    const float pC[2] = {floorCx + floorR * kCos0, floorCy - floorR * kSin0};
    const float pLs[2] = {floorCx + floorR * kCosN90, floorCy - floorR * kSinN90};
    const float pRs[2] = {floorCx + floorR * kCosP90, floorCy - floorR * kSinP90};
    const float pLr[2] = {floorCx + floorR * kCosN150, floorCy - floorR * kSinN150};
    const float pRr[2] = {floorCx + floorR * kCosP150, floorCy - floorR * kSinP150};
    // Height positions (channels 8..11):
    const float pTfl[2] = {topCx + topR * kCosN45, topCy - topR * kSinN45};
    const float pTfr[2] = {topCx + topR * kCosP45, topCy - topR * kSinP45};
    const float pTrl[2] = {topCx + topR * kCosN135, topCy - topR * kSinN135};
    const float pTrr[2] = {topCx + topR * kCosP135, topCy - topR * kSinP135};

    const float a = dcBlockerAlpha_;
    auto prevIn = dcBlockerLastInput714_;
    auto prevOut = dcBlockerLastOutput714_;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int li = 0; li < kLfoCount; ++li)
        {
            lfoValues_[static_cast<std::size_t>(li)] = lfos_[static_cast<std::size_t>(li)].tick();
        }
        agents_.setVoiceGain(ampEnv_.tick());

        agents_.processOneSample(substrate2D_, sampleRate_);
        substrate2D_.step();

        const float rL = substrate2D_.read(pL[0], pL[1]);
        const float rR = substrate2D_.read(pR[0], pR[1]);
        const float rC = substrate2D_.read(pC[0], pC[1]);
        const float rLs = substrate2D_.read(pLs[0], pLs[1]);
        const float rRs = substrate2D_.read(pRs[0], pRs[1]);
        const float rLr = substrate2D_.read(pLr[0], pLr[1]);
        const float rRr = substrate2D_.read(pRr[0], pRr[1]);
        const float rTfl = substrate2D_.read(pTfl[0], pTfl[1]);
        const float rTfr = substrate2D_.read(pTfr[0], pTfr[1]);
        const float rTrl = substrate2D_.read(pTrl[0], pTrl[1]);
        const float rTrr = substrate2D_.read(pTrr[0], pTrr[1]);

        // 7.1.4 2D LFE: 2nd-order Linkwitz-Riley of the 11-channel sum
        // (sfs-spec/04 §3.5).
        const float lfeRaw = rL + rR + rC + rLs + rRs + rLr + rRr + rTfl + rTfr + rTrl + rTrr;
        lfeLpfStage1_ += lfeLpfAlpha_ * (lfeRaw - lfeLpfStage1_);
        lfeLpfStage2_ += lfeLpfAlpha_ * (lfeLpfStage1_ - lfeLpfStage2_);
        const float rLfe = lfeLpfStage2_;

        // Per-channel DC block + soft clip + steal ramp. JUCE channel
        // order: L, R, C, LFE, Ls, Rs, Lr, Rr, Tfl, Tfr, Trl, Trr.
        const std::array<float, 12> raw = {rL, rR, rC, rLfe, rLs, rRs, rLr, rRr, rTfl, rTfr, rTrl, rTrr};
        for (int c = 0; c < 12; ++c)
        {
            const float blocked = raw[static_cast<std::size_t>(c)] - prevIn[static_cast<std::size_t>(c)] +
                                  a * prevOut[static_cast<std::size_t>(c)];
            prevIn[static_cast<std::size_t>(c)] = raw[static_cast<std::size_t>(c)];
            prevOut[static_cast<std::size_t>(c)] = blocked;
            outs[c][i] = softClip(kOutputPreGain * blocked) * stealRampGain_;
        }

        if (stealRampGain_ < 1.0f)
        {
            stealRampGain_ = std::min(1.0f, stealRampGain_ + stealRampInc_);
        }
    }

    dcBlockerLastInput714_ = prevIn;
    dcBlockerLastOutput714_ = prevOut;
}

void Voice::renderBlockFoa(float* outW, float* outX, float* outY, float* outZ, int numSamples) noexcept
{
    if (outW == nullptr || outX == nullptr || outY == nullptr || outZ == nullptr || numSamples <= 0)
    {
        return;
    }

    // 1D fallback: spec sfs-spec/04 §3.6 mandates downmix-to-stereo when
    // the substrate is 1D. Route the stereo path into W/X and zero Y/Z so
    // a downstream FOA decoder still produces a meaningful stereo image.
    if (topology_ != Topology::Torus2D)
    {
        renderBlockStereo(outW, outX, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            outY[i] = 0.0f;
            outZ[i] = 0.0f;
        }
        return;
    }

    const sfs::dsp::ScopedFlushToZero scopedFtz;

    macros_.clampInPlace();
    const sfs::engine::macros::MacroValues modulated = applyModMatrix(macros_);
    const sfs::engine::macros::InternalFields fields = sfs::engine::macros::fanOut(modulated);
    applyMacroFanOut(fields);

    // Quadrant-centre harvester positions on the 2D torus per
    // sfs-spec/04 §3.6: (0.25, 0.25), (0.25, 0.75), (0.75, 0.25),
    // (0.75, 0.75) of the (Nx, Ny) grid.
    const float nx = static_cast<float>(substrate2D_.cellsX());
    const float ny = static_cast<float>(substrate2D_.cellsY());
    const float h0x = 0.25f * nx;
    const float h0y = 0.25f * ny;
    const float h1x = 0.25f * nx;
    const float h1y = 0.75f * ny;
    const float h2x = 0.75f * nx;
    const float h2y = 0.25f * ny;
    const float h3x = 0.75f * nx;
    const float h3y = 0.75f * ny;

    const float a = dcBlockerAlpha_;
    auto prevIn = dcBlockerLastInputFoa_;
    auto prevOut = dcBlockerLastOutputFoa_;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int li = 0; li < kLfoCount; ++li)
        {
            lfoValues_[static_cast<std::size_t>(li)] = lfos_[static_cast<std::size_t>(li)].tick();
        }
        agents_.setVoiceGain(ampEnv_.tick());

        agents_.processOneSample(substrate2D_, sampleRate_);
        substrate2D_.step();

        const float h0 = substrate2D_.read(h0x, h0y);
        const float h1 = substrate2D_.read(h1x, h1y);
        const float h2 = substrate2D_.read(h2x, h2y);
        const float h3 = substrate2D_.read(h3x, h3y);

        // SN3D / ACN encoding (spec §3.6). Z = 0 by construction.
        const float rawW = 0.5f * (h0 + h1 + h2 + h3);
        const float rawX = 0.5f * (h2 + h3 - h0 - h1);
        const float rawY = 0.5f * (h1 + h3 - h0 - h2);
        const float rawZ = 0.0f;

        // Per-channel DC blocker + soft clip + steal ramp. We deliberately
        // DC-block AFTER encoding: the W channel sums all 4 harvester DCs
        // so its DC offset is the largest of the four; X/Y get cancellation
        // but a blocker is harmless. Z stays at 0 throughout.
        const std::array<float, 4> raw = {rawW, rawX, rawY, rawZ};
        std::array<float, 4> out{};
        for (int c = 0; c < 4; ++c)
        {
            const float blocked = raw[static_cast<std::size_t>(c)] - prevIn[static_cast<std::size_t>(c)] +
                                  a * prevOut[static_cast<std::size_t>(c)];
            prevIn[static_cast<std::size_t>(c)] = raw[static_cast<std::size_t>(c)];
            prevOut[static_cast<std::size_t>(c)] = blocked;
            out[static_cast<std::size_t>(c)] = softClip(kOutputPreGain * blocked) * stealRampGain_;
        }
        outW[i] = out[0];
        outX[i] = out[1];
        outY[i] = out[2];
        outZ[i] = out[3];

        if (stealRampGain_ < 1.0f)
        {
            stealRampGain_ = std::min(1.0f, stealRampGain_ + stealRampInc_);
        }
    }

    dcBlockerLastInputFoa_ = prevIn;
    dcBlockerLastOutputFoa_ = prevOut;
}

} // namespace sfs::engine
