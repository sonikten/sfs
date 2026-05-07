// src/plugin/PluginProcessor.cpp
//
// Phase 1 implementation. Per audio sample: agents → substrate → harvester
// read → output. MIDI note-on/off drives the gate envelope.

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "preset/preset.h"

#include <ctime>
#include <string>

namespace sfs::plugin
{

juce::AudioProcessorEditor* SfsAudioProcessor::createEditor()
{
    return new SfsEditor(*this);
}

namespace
{

// Stable header bytes so future loaders can detect / version the blob.
// 'S','F','S','2' = magic + Phase 2 schema. Phase 4 preset format ('S','F','S','4')
// will gain dedicated handling alongside the JSON preset.
constexpr juce::uint32 kStateMagic = 0x53465332; // 'SFS2'
constexpr juce::uint32 kStateVersion = 1;

} // namespace

void SfsAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream stream(dest, true);
    stream.writeInt(static_cast<juce::int32>(kStateMagic));
    stream.writeInt(static_cast<juce::int32>(kStateVersion));

    const auto& params = getParameters();
    stream.writeInt(params.size());
    for (auto* p : params)
    {
        // We persist the normalised value [0, 1] so the blob is stable
        // under future range / skew tweaks — setValueNotifyingHost takes
        // a normalised value too.
        stream.writeFloat(p->getValue());
    }
}

void SfsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 12)
    {
        return;
    }
    juce::MemoryInputStream stream(data, static_cast<std::size_t>(sizeInBytes), false);
    const auto magic = static_cast<juce::uint32>(stream.readInt());
    const auto version = static_cast<juce::uint32>(stream.readInt());
    if (magic != kStateMagic || version != kStateVersion)
    {
        // Unknown blob — leave parameters at their current values.
        return;
    }
    const auto count = stream.readInt();
    const auto& params = getParameters();
    const auto loadN = juce::jmin(count, params.size());
    for (int i = 0; i < loadN; ++i)
    {
        if (stream.getNumBytesRemaining() < 4)
        {
            break;
        }
        const float normalised = stream.readFloat();
        params[i]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
    }
}

SfsAudioProcessor::SfsAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Six host-automatable macros (sfs-spec/05 §2 + §3). Defaults match
    // sfs-spec/09 §3.1.
    auto add = [&](juce::String id, juce::String name, float defaultValue)
    {
        auto* p = new juce::AudioParameterFloat(juce::ParameterID(std::move(id), 1),
                                                std::move(name),
                                                juce::NormalisableRange<float>{0.0f, 1.0f, 0.0001f},
                                                defaultValue);
        addParameter(p);
        return p;
    };
    tensionParam_ = add("tension", "TENSION", 0.5f);
    dampingParam_ = add("damping", "DAMPING", 0.3f);
    densityParam_ = add("density", "DENSITY", 0.6f);
    migrationParam_ = add("migration", "MIGRATION", 0.2f);
    coherenceParam_ = add("coherence", "COHERENCE", 0.8f);
    excitationParam_ = add("excitation", "EXCITATION", 0.3f);

    // Phase 2 uniform-shape selector: all agents in all voices use this
    // waveform. Phase 3 replaces with per-agent shape draws from
    // shape_distribution (sfs-spec/09 §3.3).
    shapeParam_ = new juce::AudioParameterChoice(juce::ParameterID("shape", 1),
                                                 "SHAPE",
                                                 juce::StringArray{"Sine", "Saw", "Square", "FmPair", "Noise"},
                                                 0); // default Sine
    addParameter(shapeParam_);

    // Phase 3 substrate topology selector. Default Ring 1D = Phase 2
    // bit-exact behaviour. Torus 2D switches every voice to the 2D
    // substrate (32×32 cells, von Neumann Laplacian).
    topologyParam_ = new juce::AudioParameterChoice(juce::ParameterID("topology", 1),
                                                    "TOPOLOGY",
                                                    juce::StringArray{"Ring 1D", "Torus 2D"},
                                                    0);
    addParameter(topologyParam_);

    // Amp ADSR (Phase 2 §9 step 9). Times are millis on a skewed range so
    // the typical musical sweet spot (1-500 ms) gets dial resolution; range
    // tops at 5/10 s for pad-style ramps. Defaults match Voice constructor.
    auto addMs = [&](const char* id, const char* name, float minMs, float maxMs, float defaultMs)
    {
        juce::NormalisableRange<float> range(minMs, maxMs, 0.01f);
        range.setSkewForCentre(juce::jmax(minMs, maxMs * 0.05f));
        auto* p = new juce::AudioParameterFloat(juce::ParameterID(id, 1), juce::String(name), range, defaultMs);
        addParameter(p);
        return p;
    };
    attackMsParam_ = addMs("attack", "ATTACK", 0.0f, 5000.0f, 10.0f);
    decayMsParam_ = addMs("decay", "DECAY", 0.0f, 5000.0f, 120.0f);
    sustainLevelParam_ = add("sustain", "SUSTAIN", 0.75f);
    releaseMsParam_ = addMs("release", "RELEASE", 0.0f, 10000.0f, 250.0f);

    // Per-LFO controls. Rate is log-skewed so 0.5-5 Hz gets dial precision;
    // top-end 20 Hz is plenty for tremolo. Shape mirrors LfoShape enum.
    constexpr float kLfoDefaultRates[kLfoCount] = {0.5f, 2.0f, 5.0f, 7.0f};
    constexpr int kLfoDefaultShapeIdx[kLfoCount] = {
        static_cast<int>(sfs::engine::lfo::LfoShape::Sine),
        static_cast<int>(sfs::engine::lfo::LfoShape::Triangle),
        static_cast<int>(sfs::engine::lfo::LfoShape::Sine),
        static_cast<int>(sfs::engine::lfo::LfoShape::SampleHold),
    };
    for (int i = 0; i < kLfoCount; ++i)
    {
        const juce::String idRate = "lfo" + juce::String(i + 1) + "_rate";
        const juce::String idShape = "lfo" + juce::String(i + 1) + "_shape";
        const juce::String nmRate = "LFO" + juce::String(i + 1) + " RATE";
        const juce::String nmShape = "LFO" + juce::String(i + 1) + " SHAPE";

        juce::NormalisableRange<float> rateRange(0.05f, 20.0f, 0.001f);
        rateRange.setSkewForCentre(2.0f);
        auto* rateP =
            new juce::AudioParameterFloat(juce::ParameterID(idRate, 1), nmRate, rateRange, kLfoDefaultRates[i]);
        addParameter(rateP);
        lfoRateParams_[static_cast<std::size_t>(i)] = rateP;

        auto* shapeP = new juce::AudioParameterChoice(juce::ParameterID(idShape, 1),
                                                      nmShape,
                                                      juce::StringArray{"Sine", "Triangle", "Saw", "Square", "S&H"},
                                                      kLfoDefaultShapeIdx[i]);
        addParameter(shapeP);
        lfoShapeParams_[static_cast<std::size_t>(i)] = shapeP;
    }

    // Mod matrix slot depths (4 active default slots). Range -1..1; the
    // remaining 12 slots are inactive at start and not exposed.
    auto addDepth = [&](const char* id, const char* name, float defaultValue)
    {
        juce::NormalisableRange<float> range(-1.0f, 1.0f, 0.001f);
        auto* p = new juce::AudioParameterFloat(juce::ParameterID(id, 1), juce::String(name), range, defaultValue);
        addParameter(p);
        return p;
    };
    slot0DepthParam_ = addDepth("mod_cc1_migration", "CC1>MIGRATION", 0.5f);
    slot1DepthParam_ = addDepth("mod_lfo1_tension", "LFO1>TENSION", 0.10f);
    slot2DepthParam_ = addDepth("mod_lfo2_coherence", "LFO2>COHERENCE", -0.08f);
    slot3DepthParam_ = addDepth("mod_vel_excitation", "VEL>EXCITATION", 0.30f);
}

void SfsAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    voiceManager_ = std::make_unique<sfs::engine::VoiceManager>(kSubstrateCells,
                                                                kAgentCount,
                                                                static_cast<float>(sampleRate));
}

void SfsAudioProcessor::releaseResources()
{
    voiceManager_.reset();
}

bool SfsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    // Phase 3 supported layouts:
    //   mono / stereo         — Phase 2 default paths
    //   ambisonic(1) / 4ch    — first-order ambisonic (sfs-spec/04 §3.6)
    if (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
    {
        return true;
    }
    if (out == juce::AudioChannelSet::ambisonic(1) || (out.size() == 4 && out == juce::AudioChannelSet::quadraphonic()))
    {
        return true;
    }
    if (out == juce::AudioChannelSet::create5point1())
    {
        return true;
    }
    if (out == juce::AudioChannelSet::create7point1point4())
    {
        return true;
    }
    return false;
}

void SfsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Set FTZ/DAZ for the duration of this block. Mandatory per the
    // determinism contract (CLAUDE.md "Hard invariants" + sfs-spec/01 §9).
    const juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    // Clear any host-supplied input that may have been mistakenly routed in.
    for (int ch = getTotalNumInputChannels(); ch < numChannels; ++ch)
    {
        buffer.clear(ch, 0, numSamples);
    }

    if (voiceManager_ == nullptr || numChannels == 0 || numSamples == 0)
    {
        return;
    }

    // Block-rate macro update: read host parameter snapshots into the
    // VoiceManager's MacroValues; per-voice copies happen inside
    // VoiceManager::renderBlockStereo. Spec §5.7 sample-accurate
    // automation lands as a Phase 2 follow-up.
    auto& macros = voiceManager_->macros();
    macros.tension = tensionParam_->get();
    macros.damping = dampingParam_->get();
    macros.density = densityParam_->get();
    macros.migration = migrationParam_->get();
    macros.coherence = coherenceParam_->get();
    macros.excitation = excitationParam_->get();

    // Read the SHAPE selector and push to all voices.
    voiceManager_->setUniformShape(static_cast<sfs::engine::agents::AgentShape>(shapeParam_->getIndex()));
    voiceManager_->setTopology(static_cast<sfs::engine::Topology>(topologyParam_->getIndex()));

    // ADSR + LFO + mod-matrix slot depths fan out to every voice. The
    // calls are O(voiceCount) and well under budget.
    voiceManager_->setAdsr(attackMsParam_->get(),
                           decayMsParam_->get(),
                           sustainLevelParam_->get(),
                           releaseMsParam_->get());
    for (int i = 0; i < kLfoCount; ++i)
    {
        voiceManager_->setLfoConfig(i,
                                    lfoRateParams_[static_cast<std::size_t>(i)]->get(),
                                    static_cast<sfs::engine::lfo::LfoShape>(
                                        lfoShapeParams_[static_cast<std::size_t>(i)]->getIndex()));
    }
    voiceManager_->setModMatrixSlotDepth(0, slot0DepthParam_->get());
    voiceManager_->setModMatrixSlotDepth(1, slot1DepthParam_->get());
    voiceManager_->setModMatrixSlotDepth(2, slot2DepthParam_->get());
    voiceManager_->setModMatrixSlotDepth(3, slot3DepthParam_->get());

    // MIDI dispatch — VoiceManager handles allocation + stealing.
    // Phase 2 simplification: events apply at block boundaries (5 ms
    // granularity at 256-sample blocks @ 48 kHz). Sample-accurate
    // dispatch lands as part of step 12.
    //
    // Phase 3 §9 step 8 — MPE Note Expression. We don't formally toggle
    // MPE zones (the spec's MPE 1.0 RPN messages); instead any incoming
    // pitch-bend / channel pressure / CC74 message is routed through the
    // VoiceManager's per-channel side table so MPE keyboards (typically
    // sending one note per member channel 2-15) get per-note expression
    // automatically. Non-MPE keyboards send everything on channel 1, in
    // which case the channel side table behaves as global state.
    constexpr float kMpePitchBendRangeSemis = 48.0f; // MPE default member-zone range
    for (const auto meta : midiMessages)
    {
        const auto& msg = meta.getMessage();
        const int ch = msg.getChannel(); // 1-16, or 0 for sysex/non-channel
        if (msg.isNoteOn())
        {
            voiceManager_->noteOn(ch, msg.getNoteNumber(), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            voiceManager_->noteOff(ch, msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voiceManager_->allNotesOff();
        }
        else if (msg.isPitchWheel())
        {
            // 14-bit pitch bend [0, 16383] centred at 8192. Convert to
            // semitones at the MPE default ±48 range; non-MPE hosts that
            // configure narrower ranges can bend less than the full ±48
            // — the audible effect is identical, just less throw.
            const int raw = msg.getPitchWheelValue();
            const float normalised = (static_cast<float>(raw) - 8192.0f) / 8192.0f;
            voiceManager_->setChannelPitchBendSemitones(ch, normalised * kMpePitchBendRangeSemis);
        }
        else if (msg.isChannelPressure())
        {
            voiceManager_->setChannelPressure(ch, static_cast<float>(msg.getChannelPressureValue()) / 127.0f);
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const float v01 = static_cast<float>(msg.getControllerValue()) / 127.0f;
            if (cc == 1)
            {
                // Mod wheel (CC1) — global mod-matrix source.
                voiceManager_->setMidiCc1(v01);
            }
            else if (cc == 74)
            {
                // CC74 — MPE timbre / Y axis on Roli-style keyboards.
                voiceManager_->setChannelTimbre(ch, v01);
            }
        }
    }

    // Phase 3: dispatch by output bus layout.
    //   1 ch        — mono downmix of stereo render
    //   2 ch        — stereo (Phase 2 default)
    //   4 ch        — first-order ambisonic / quadraphonic (W, X, Y, Z)
    //                 ACN/SN3D order; sfs-spec/04 §3.6
    if (numChannels == 12)
    {
        // 7.1.4 — JUCE channel order: L, R, C, LFE, Ls, Rs, Lr, Rr,
        // Tfl, Tfr, Trl, Trr.
        float* outs[12];
        for (int c = 0; c < 12; ++c)
        {
            outs[c] = buffer.getWritePointer(c);
        }
        voiceManager_->renderBlockSurround714(outs, numSamples);
    }
    else if (numChannels == 6)
    {
        // 5.1 — JUCE channel order: L, R, C, LFE, Ls, Rs.
        auto* const outL = buffer.getWritePointer(0);
        auto* const outR = buffer.getWritePointer(1);
        auto* const outC = buffer.getWritePointer(2);
        auto* const outLfe = buffer.getWritePointer(3);
        auto* const outLs = buffer.getWritePointer(4);
        auto* const outRs = buffer.getWritePointer(5);
        voiceManager_->renderBlockSurround51(outL, outR, outC, outLfe, outLs, outRs, numSamples);
    }
    else if (numChannels == 4)
    {
        auto* const outW = buffer.getWritePointer(0);
        auto* const outX = buffer.getWritePointer(1);
        auto* const outY = buffer.getWritePointer(2);
        auto* const outZ = buffer.getWritePointer(3);
        voiceManager_->renderBlockFoa(outW, outX, outY, outZ, numSamples);
    }
    else if (numChannels >= 2)
    {
        auto* const outL = buffer.getWritePointer(0);
        auto* const outR = buffer.getWritePointer(1);
        voiceManager_->renderBlockStereo(outL, outR, numSamples);
        for (int ch = 2; ch < numChannels; ++ch)
        {
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
        }
    }
    else
    {
        // No mono path on the manager (Phase 2 stereo-default); render
        // stereo and downmix.
        std::vector<float> tmpR(static_cast<std::size_t>(numSamples), 0.0f);
        auto* const left = buffer.getWritePointer(0);
        voiceManager_->renderBlockStereo(left, tmpR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = 0.5f * (left[i] + tmpR[static_cast<std::size_t>(i)]);
        }
    }
}

// ----- Preset I/O ---------------------------------------------------------

namespace
{

[[nodiscard]] juce::String iso8601Now()
{
    const std::time_t now = std::time(nullptr);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &now);
#else
    gmtime_r(&now, &gmt);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    return juce::String(buf);
}

[[nodiscard]] const char* topologyToString(int idx)
{
    return (idx == 1) ? "torus_32" : "ring";
}

[[nodiscard]] const char* shapeIdxToString(int idx)
{
    static const char* kNames[5] = {"sine", "saw", "square", "fmpair", "noise"};
    return (idx >= 0 && idx < 5) ? kNames[idx] : "sine";
}

[[nodiscard]] sfs::preset::PresetShapeDistribution shapeIdxToDistribution(int idx)
{
    sfs::preset::PresetShapeDistribution d{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    switch (idx)
    {
    case 1:
        d.saw = 1.0f;
        break;
    case 2:
        d.square = 1.0f;
        break;
    case 3:
        d.fmpair = 1.0f;
        break;
    case 4:
        d.noise = 1.0f;
        break;
    default:
        d.sine = 1.0f;
        break;
    }
    return d;
}

[[nodiscard]] int shapeStringToIdx(const std::string& s)
{
    if (s == "saw")
    {
        return 1;
    }
    if (s == "square")
    {
        return 2;
    }
    if (s == "fmpair")
    {
        return 3;
    }
    if (s == "noise")
    {
        return 4;
    }
    return 0;
}

[[nodiscard]] int dominantShapeIdx(const sfs::preset::PresetShapeDistribution& d)
{
    const float v[5] = {d.sine, d.saw, d.square, d.fmpair, d.noise};
    int best = 0;
    float bestVal = v[0];
    for (int i = 1; i < 5; ++i)
    {
        if (v[i] > bestVal)
        {
            bestVal = v[i];
            best = i;
        }
    }
    return best;
}

[[nodiscard]] int topologyStringToIdx(const std::string& s)
{
    return (s == "ring") ? 0 : 1;
}

} // namespace

bool SfsAudioProcessor::loadPresetFromFile(const juce::String& path, juce::String& errorOut)
{
    sfs::preset::Preset p;
    try
    {
        p = sfs::preset::Preset::loadFromFile(path.toStdString());
    }
    catch (const sfs::preset::PresetParseError& e)
    {
        errorOut = juce::String(e.what());
        return false;
    }

    // Write into the AudioParameters; processBlock picks them up next.
    *tensionParam_ = p.macros.tension;
    *dampingParam_ = p.macros.damping;
    *densityParam_ = p.macros.density;
    *migrationParam_ = p.macros.migration;
    *coherenceParam_ = p.macros.coherence;
    *excitationParam_ = p.macros.excitation;
    *topologyParam_ = topologyStringToIdx(p.structural.topology);
    *shapeParam_ = dominantShapeIdx(p.agents.shape_distribution);

    // ENV1 — preset times are seconds, host params are ms (sfs-spec/09 §3.4
    // log-scaled range, attached to the same AudioParameterFloat).
    *attackMsParam_ = p.env1.attack * 1000.0f;
    *decayMsParam_ = p.env1.decay * 1000.0f;
    *sustainLevelParam_ = juce::jlimit(0.0f, 1.0f, p.env1.sustain);
    *releaseMsParam_ = p.env1.release * 1000.0f;

    // LFOs (4): rate Hz + shape index.
    static const juce::StringArray kLfoShapes{"sine", "triangle", "saw", "square", "sample_hold"};
    for (int i = 0; i < kLfoCount; ++i)
    {
        const auto& l = p.lfos[static_cast<std::size_t>(i)];
        *lfoRateParams_[static_cast<std::size_t>(i)] = juce::jlimit(0.05f, 20.0f, l.rate_hz);
        const int shapeIdx = std::max(0, kLfoShapes.indexOf(juce::String(l.shape), false, false));
        *lfoShapeParams_[static_cast<std::size_t>(i)] = shapeIdx;
    }

    // Mod-matrix slot depths (4 active slots) — slot index 0..3 maps to
    // the existing fixed source/dest pairs in the AudioParameter list.
    auto slotDepth = [&](int idx) -> float
    {
        const auto& s = p.mod_matrix[static_cast<std::size_t>(idx)];
        return s.active ? juce::jlimit(-1.0f, 1.0f, s.depth) : 0.0f;
    };
    *slot0DepthParam_ = slotDepth(0);
    *slot1DepthParam_ = slotDepth(1);
    *slot2DepthParam_ = slotDepth(2);
    *slot3DepthParam_ = slotDepth(3);

    currentPresetName_ = juce::String(p.metadata.name);
    return true;
}

bool SfsAudioProcessor::savePresetToFile(const juce::String& path,
                                         const juce::String& name,
                                         const juce::String& author,
                                         const juce::String& category,
                                         const juce::StringArray& tags,
                                         const juce::String& description,
                                         juce::String& errorOut)
{
    sfs::preset::Preset p;
    p.metadata.name = name.toStdString();
    p.metadata.author = author.toStdString();
    p.metadata.category = category.toStdString();
    for (const auto& t : tags)
    {
        p.metadata.tags.push_back(t.toStdString());
    }
    p.metadata.created = iso8601Now().toStdString();
    p.metadata.modified = p.metadata.created;
    p.metadata.description = description.toStdString();
    p.macros.tension = tensionParam_->get();
    p.macros.damping = dampingParam_->get();
    p.macros.density = densityParam_->get();
    p.macros.migration = migrationParam_->get();
    p.macros.coherence = coherenceParam_->get();
    p.macros.excitation = excitationParam_->get();
    p.structural.topology = topologyToString(topologyParam_->getIndex());
    p.agents.shape_distribution = shapeIdxToDistribution(shapeParam_->getIndex());
    p.env1.attack = attackMsParam_->get() / 1000.0f;
    p.env1.decay = decayMsParam_->get() / 1000.0f;
    p.env1.sustain = sustainLevelParam_->get();
    p.env1.release = releaseMsParam_->get() / 1000.0f;
    static const char* kLfoShapeStrings[5] = {"sine", "triangle", "saw", "square", "sample_hold"};
    for (int i = 0; i < kLfoCount; ++i)
    {
        auto& l = p.lfos[static_cast<std::size_t>(i)];
        l.rate_hz = lfoRateParams_[static_cast<std::size_t>(i)]->get();
        const int s = lfoShapeParams_[static_cast<std::size_t>(i)]->getIndex();
        l.shape = kLfoShapeStrings[std::clamp(s, 0, 4)];
    }
    // 4 active mod-matrix slots map to the 4 fixed source/dest pairs.
    auto setSlot = [&](int idx, const char* src, const char* dst, float depth)
    {
        auto& slot = p.mod_matrix[static_cast<std::size_t>(idx)];
        slot.active = (depth != 0.0f);
        slot.source = src;
        slot.destination = dst;
        slot.depth = depth;
    };
    setSlot(0, "MIDI_CC1", "MIGRATION", slot0DepthParam_->get());
    setSlot(1, "LFO1", "TENSION", slot1DepthParam_->get());
    setSlot(2, "LFO2", "COHERENCE", slot2DepthParam_->get());
    setSlot(3, "KEY_VELOCITY", "EXCITATION", slot3DepthParam_->get());

    try
    {
        p.saveToFile(path.toStdString());
    }
    catch (const sfs::preset::PresetParseError& e)
    {
        errorOut = juce::String(e.what());
        return false;
    }
    currentPresetName_ = name;
    return true;
}

} // namespace sfs::plugin
// (createPluginFilter() lives in PluginEntry.cpp so this TU can be linked
// into the headless render rig without dragging the VST3 wrapper symbols.)
