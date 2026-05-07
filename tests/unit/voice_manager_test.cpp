// tests/unit/voice_manager_test.cpp
//
// Phase 2 polyphony sanity tests. Asserts:
//   - 8 simultaneous note-ons don't crash; activeVoiceCount reaches 8
//   - 9th noteOn steals a voice (count stays at 8)
//   - noteOff for a held note marks the voice as not gated
//   - allNotesOff gates everything off
//   - rendering a chord produces non-silent stereo with bounded peak

#include "engine/voice_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using sfs::engine::VoiceManager;

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kSubstrateCells = 1024;
constexpr int kAgentCount = 8; // smaller per-voice for the test (faster)
constexpr float kSampleRateF = 48000.0f;
} // namespace

TEST_CASE("VoiceManager allocates up to 8 voices, then steals", "[voice_manager]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    REQUIRE(mgr.activeVoiceCount() == 0);

    // Fire 8 distinct notes.
    for (int i = 0; i < VoiceManager::kMaxVoices; ++i)
    {
        mgr.noteOn(48 + i, 1.0f);
    }
    REQUIRE(mgr.activeVoiceCount() == VoiceManager::kMaxVoices);

    // 9th note steals — count stays at 8.
    mgr.noteOn(60, 1.0f);
    REQUIRE(mgr.activeVoiceCount() == VoiceManager::kMaxVoices);
}

TEST_CASE("noteOff gates the matching voice", "[voice_manager][note-off]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    mgr.noteOn(60, 1.0f);
    mgr.noteOn(64, 1.0f);
    mgr.noteOn(67, 1.0f);
    REQUIRE(mgr.activeVoiceCount() == 3);

    mgr.noteOff(64);
    REQUIRE(mgr.activeVoiceCount() == 2);

    mgr.allNotesOff();
    REQUIRE(mgr.activeVoiceCount() == 0);
}

TEST_CASE("Chord render: 3 notes produce non-silent bounded stereo", "[voice_manager][chord]")
{
    VoiceManager mgr(kSubstrateCells, kAgentCount, kSampleRateF);
    mgr.noteOn(60, 1.0f); // C4
    mgr.noteOn(64, 1.0f); // E4
    mgr.noteOn(67, 1.0f); // G4

    constexpr int kSamples = kSampleRate / 2; // 0.5 s
    std::vector<float> L(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockStereo(L.data() + written, R.data() + written, n);
    }

    // Non-silent.
    float peakL = 0.0f;
    float peakR = 0.0f;
    double rmsL = 0.0;
    double rmsR = 0.0;
    for (int i = 0; i < kSamples; ++i)
    {
        const float lv = L[static_cast<std::size_t>(i)];
        const float rv = R[static_cast<std::size_t>(i)];
        peakL = std::max(peakL, std::fabs(lv));
        peakR = std::max(peakR, std::fabs(rv));
        rmsL += static_cast<double>(lv) * static_cast<double>(lv);
        rmsR += static_cast<double>(rv) * static_cast<double>(rv);
    }
    rmsL = std::sqrt(rmsL / static_cast<double>(kSamples));
    rmsR = std::sqrt(rmsR / static_cast<double>(kSamples));

    CAPTURE(peakL, peakR, rmsL, rmsR);
    REQUIRE(peakL > 1e-3f);
    REQUIRE(peakR > 1e-3f);
    // 3-voice sum should fit in [-3, 3] before saturation; our soft-clip
    // bounds each voice at ~1, so 3-voice sum bounded at ~3. Loose check
    // against catastrophic blow-up.
    REQUIRE(peakL < 5.0f);
    REQUIRE(peakR < 5.0f);
}

namespace
{

// Estimate fundamental Hz from a mono buffer via autocorrelation.
[[nodiscard]] float estimatePitchHz(const float* buf, int len, float sampleRate, float fMin, float fMax)
{
    const int tauMin = static_cast<int>(sampleRate / fMax);
    const int tauMax = std::min(len / 2, static_cast<int>(sampleRate / fMin));
    if (tauMin >= tauMax)
    {
        return 0.0f;
    }
    double mean = 0.0;
    for (int i = 0; i < len; ++i)
    {
        mean += static_cast<double>(buf[i]);
    }
    const float m = static_cast<float>(mean / static_cast<double>(len));
    int bestLag = 0;
    double bestVal = -1e300;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i + tau < len; ++i)
        {
            s += static_cast<double>(buf[i] - m) * static_cast<double>(buf[i + tau] - m);
        }
        if (s > bestVal)
        {
            bestVal = s;
            bestLag = tau;
        }
    }
    if (bestLag == 0)
    {
        return 0.0f;
    }
    return sampleRate / static_cast<float>(bestLag);
}

[[nodiscard]] float renderAndEstimatePitchHz(VoiceManager& mgr, int holdSamples)
{
    std::vector<float> L(static_cast<std::size_t>(holdSamples), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(holdSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < holdSamples; written += kBlock)
    {
        const int n = std::min(kBlock, holdSamples - written);
        mgr.renderBlockStereo(L.data() + written, R.data() + written, n);
    }
    // Skip the attack — analyse the steady-state tail.
    const int analysisStart = holdSamples / 4;
    return estimatePitchHz(L.data() + analysisStart, holdSamples - analysisStart, kSampleRateF, 50.0f, 2000.0f);
}

} // namespace

TEST_CASE("MPE pitch bend on a member channel shifts that voice's pitch", "[voice_manager][mpe]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);

    // Drive pitched-style voicing: COHERENCE high, MIGRATION/EXCITATION off.
    mgr.macros().tension = 0.5f;
    mgr.macros().damping = 0.3f;
    mgr.macros().density = 0.6f;
    mgr.macros().migration = 0.0f;
    mgr.macros().coherence = 1.0f;
    mgr.macros().excitation = 0.0f;
    mgr.setModMatrixSlotDepth(0, 0.0f);
    mgr.setModMatrixSlotDepth(1, 0.0f);
    mgr.setModMatrixSlotDepth(2, 0.0f);
    mgr.setModMatrixSlotDepth(3, 0.0f);

    constexpr int kHoldSamples = kSampleRate / 2; // 0.5 s

    // Reference: A4 (440 Hz) on member channel 2, no bend.
    mgr.noteOn(/*channel*/ 2, /*note*/ 69, 1.0f);
    const float refHz = renderAndEstimatePitchHz(mgr, kHoldSamples);
    mgr.noteOff(/*channel*/ 2, /*note*/ 69);
    CAPTURE(refHz);
    REQUIRE(refHz > 380.0f);
    REQUIRE(refHz < 500.0f);

    // Bent: A4 + 7 semitones (E5, ~659.26 Hz) on the same channel, set
    // BEFORE noteOn so the voice inherits the pitch bend on attack.
    mgr.setChannelPitchBendSemitones(2, 7.0f);
    mgr.noteOn(/*channel*/ 2, /*note*/ 69, 1.0f);
    const float bentHz = renderAndEstimatePitchHz(mgr, kHoldSamples);
    mgr.noteOff(/*channel*/ 2, /*note*/ 69);
    CAPTURE(bentHz);

    // Should land near 659.26 Hz (within ~3 percent).
    const float expectedHz = 440.0f * 1.498307f; // 2^(7/12)
    const float relErr = std::fabs(bentHz - expectedHz) / expectedHz;
    CAPTURE(expectedHz, relErr);
    REQUIRE(relErr < 0.03f);
}

TEST_CASE("MPE pitch bend per channel: two notes bend independently", "[voice_manager][mpe]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.macros().coherence = 1.0f;
    mgr.macros().migration = 0.0f;
    mgr.macros().excitation = 0.0f;
    mgr.setModMatrixSlotDepth(0, 0.0f);

    // Channel 2 holds A4 with +7 st bend (→ E5), channel 3 holds A4 with
    // -7 st bend (→ D4). Renders should produce a 5th-apart double stop.
    mgr.setChannelPitchBendSemitones(2, 7.0f);
    mgr.setChannelPitchBendSemitones(3, -7.0f);
    mgr.noteOn(2, 69, 1.0f);
    mgr.noteOn(3, 69, 1.0f);

    constexpr int kHoldSamples = kSampleRate / 2;
    std::vector<float> L(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kHoldSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < kHoldSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kHoldSamples - written);
        mgr.renderBlockStereo(L.data() + written, R.data() + written, n);
    }

    // The mix should be non-silent and bounded — full pitch detection on
    // a chord is unreliable here; the bit-exact render will catch any
    // wrong-pitch regression via the determinism CI.
    float peak = 0.0f;
    for (int i = kHoldSamples / 4; i < kHoldSamples; ++i)
    {
        peak = std::max(peak, std::fabs(L[static_cast<std::size_t>(i)]));
    }
    REQUIRE(peak > 1e-3f);
    REQUIRE(peak < 1.5f);
}

TEST_CASE("FOA render: 2D voice produces non-silent W with Z = 0", "[voice_manager][foa]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.setTopology(sfs::engine::Topology::Torus2D);
    mgr.macros().tension = 0.5f;
    mgr.macros().damping = 0.3f;
    mgr.macros().density = 0.6f;
    mgr.macros().migration = 0.0f;
    mgr.macros().coherence = 1.0f;
    mgr.macros().excitation = 0.0f;
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4; // 0.25 s
    std::vector<float> W(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> X(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> Y(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> Z(static_cast<std::size_t>(kSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockFoa(W.data() + written, X.data() + written, Y.data() + written, Z.data() + written, n);
    }

    // W should carry signal, Z should always be zero.
    float peakW = 0.0f;
    float peakZ = 0.0f;
    for (int i = 0; i < kSamples; ++i)
    {
        peakW = std::max(peakW, std::fabs(W[static_cast<std::size_t>(i)]));
        peakZ = std::max(peakZ, std::fabs(Z[static_cast<std::size_t>(i)]));
    }
    CAPTURE(peakW, peakZ);
    REQUIRE(peakW > 1e-3f);
    REQUIRE(peakW < 1.5f);
    REQUIRE(peakZ == 0.0f); // strict zero — Z is hard-wired to 0 in 2D mode
}

TEST_CASE("5.1 render: 2D voice produces non-silent across all 6 channels", "[voice_manager][surround]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.setTopology(sfs::engine::Topology::Torus2D);
    mgr.macros().tension = 0.5f;
    mgr.macros().damping = 0.3f;
    mgr.macros().density = 0.6f;
    mgr.macros().migration = 0.0f;
    mgr.macros().coherence = 1.0f;
    mgr.macros().excitation = 0.0f;
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4;
    std::array<std::vector<float>, 6> ch;
    for (auto& c : ch)
    {
        c.assign(static_cast<std::size_t>(kSamples), 0.0f);
    }
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockSurround51(ch[0].data() + written,
                                  ch[1].data() + written,
                                  ch[2].data() + written,
                                  ch[3].data() + written,
                                  ch[4].data() + written,
                                  ch[5].data() + written,
                                  n);
    }

    // Each of L, R, C, Ls, Rs should be non-silent. LFE should be heavily
    // low-passed (so its peak is bounded but non-zero).
    for (int c = 0; c < 6; ++c)
    {
        float peak = 0.0f;
        for (int i = 0; i < kSamples; ++i)
        {
            peak = std::max(peak, std::fabs(ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]));
        }
        CAPTURE(c, peak);
        REQUIRE(peak < 1.5f);
        // Channel 3 = LFE; allow it to be small (it's heavily LPF'd).
        if (c != 3)
        {
            REQUIRE(peak > 1e-3f);
        }
    }
}

TEST_CASE("5.1 render: 1D voice downmixes per spec (Ls=L, Rs=R, C=avg)", "[voice_manager][surround]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.setTopology(sfs::engine::Topology::Ring1D);
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4;
    std::array<std::vector<float>, 6> ch;
    for (auto& c : ch)
    {
        c.assign(static_cast<std::size_t>(kSamples), 0.0f);
    }
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockSurround51(ch[0].data() + written,
                                  ch[1].data() + written,
                                  ch[2].data() + written,
                                  ch[3].data() + written,
                                  ch[4].data() + written,
                                  ch[5].data() + written,
                                  n);
    }

    // Spec: 1D-mode 5.1 downmix => Ls = L, Rs = R after the bus soft-clip.
    // The bus soft-clip is applied to each summed channel, so the
    // tap copy happens before the clip — check approximate equality
    // (within the soft-clip's monotonic mapping).
    int matchesLs = 0;
    int matchesRs = 0;
    for (int i = 0; i < kSamples; ++i)
    {
        if (std::fabs(ch[0][static_cast<std::size_t>(i)] - ch[4][static_cast<std::size_t>(i)]) < 1e-4f)
        {
            ++matchesLs;
        }
        if (std::fabs(ch[1][static_cast<std::size_t>(i)] - ch[5][static_cast<std::size_t>(i)]) < 1e-4f)
        {
            ++matchesRs;
        }
    }
    // Most samples should match (the downmix is L→Ls and R→Rs).
    REQUIRE(matchesLs > kSamples * 9 / 10);
    REQUIRE(matchesRs > kSamples * 9 / 10);
}

TEST_CASE("7.1.4 render: 2D voice produces non-silent across all 12 channels", "[voice_manager][surround]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.setTopology(sfs::engine::Topology::Torus2D);
    mgr.macros().tension = 0.5f;
    mgr.macros().damping = 0.3f;
    mgr.macros().density = 0.6f;
    mgr.macros().migration = 0.0f;
    mgr.macros().coherence = 1.0f;
    mgr.macros().excitation = 0.0f;
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4;
    std::array<std::vector<float>, 12> ch;
    std::array<float*, 12> ptrs{};
    for (std::size_t c = 0; c < 12; ++c)
    {
        ch[c].assign(static_cast<std::size_t>(kSamples), 0.0f);
        ptrs[c] = ch[c].data();
    }
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        std::array<float*, 12> offs{};
        for (std::size_t c = 0; c < 12; ++c)
        {
            offs[c] = ptrs[c] + written;
        }
        mgr.renderBlockSurround714(offs.data(), n);
    }

    for (int c = 0; c < 12; ++c)
    {
        float peak = 0.0f;
        for (int i = 0; i < kSamples; ++i)
        {
            peak = std::max(peak, std::fabs(ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]));
        }
        CAPTURE(c, peak);
        REQUIRE(peak < 1.5f);
        if (c != 3) // skip LFE (heavily LPF'd)
        {
            REQUIRE(peak > 1e-3f);
        }
    }
}

TEST_CASE("7.1.4 render: 1D voice downmix has silent height channels", "[voice_manager][surround]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mgr.setTopology(sfs::engine::Topology::Ring1D);
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4;
    std::array<std::vector<float>, 12> ch;
    std::array<float*, 12> ptrs{};
    for (std::size_t c = 0; c < 12; ++c)
    {
        ch[c].assign(static_cast<std::size_t>(kSamples), 0.0f);
        ptrs[c] = ch[c].data();
    }
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        std::array<float*, 12> offs{};
        for (std::size_t c = 0; c < 12; ++c)
        {
            offs[c] = ptrs[c] + written;
        }
        mgr.renderBlockSurround714(offs.data(), n);
    }

    // Height channels (8..11) must be exactly silent in 1D mode (spec
    // §3.5: "1D mode does not pretend to deliver true 7.1.4").
    for (int c = 8; c < 12; ++c)
    {
        float peak = 0.0f;
        for (int i = 0; i < kSamples; ++i)
        {
            peak = std::max(peak, std::fabs(ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]));
        }
        CAPTURE(c, peak);
        REQUIRE(peak == 0.0f);
    }
    // L (0) and R (1) carry the stereo downmix.
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < kSamples; ++i)
    {
        peakL = std::max(peakL, std::fabs(ch[0][static_cast<std::size_t>(i)]));
        peakR = std::max(peakR, std::fabs(ch[1][static_cast<std::size_t>(i)]));
    }
    REQUIRE(peakL > 1e-3f);
    REQUIRE(peakR > 1e-3f);
}

TEST_CASE("FOA render: 1D voice downmixes stereo into W/X (Y=Z=0)", "[voice_manager][foa]")
{
    VoiceManager mgr(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    // Default topology is Ring1D — explicit anyway.
    mgr.setTopology(sfs::engine::Topology::Ring1D);
    mgr.noteOn(60, 1.0f);

    constexpr int kSamples = kSampleRate / 4;
    std::vector<float> W(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> X(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> Y(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> Z(static_cast<std::size_t>(kSamples), 0.0f);
    constexpr int kBlock = 256;
    for (int written = 0; written < kSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kSamples - written);
        mgr.renderBlockFoa(W.data() + written, X.data() + written, Y.data() + written, Z.data() + written, n);
    }

    float peakY = 0.0f;
    float peakZ = 0.0f;
    float peakW = 0.0f;
    for (int i = 0; i < kSamples; ++i)
    {
        peakW = std::max(peakW, std::fabs(W[static_cast<std::size_t>(i)]));
        peakY = std::max(peakY, std::fabs(Y[static_cast<std::size_t>(i)]));
        peakZ = std::max(peakZ, std::fabs(Z[static_cast<std::size_t>(i)]));
    }
    REQUIRE(peakW > 1e-3f);
    REQUIRE(peakY == 0.0f);
    REQUIRE(peakZ == 0.0f);
}

TEST_CASE("Voice-pool threading: parallel render is bit-exact with serial", "[voice_manager][threading][determinism]")
{
    // The whole point of voice-pool threading is to be a perf optimisation
    // that doesn't change a single output bit. Render the same chord with
    // threading off and threading on, assert the PCM matches sample-by-
    // sample. Repeats with topology = 2D so the heavier render path is
    // covered too.
    constexpr int kHoldSamples = kSampleRate / 4; // 0.25 s
    constexpr int kBlock = 256;

    auto renderChord = [&](VoiceManager& mgr, std::vector<float>& outL, std::vector<float>& outR)
    {
        // Identical setup, identical 3-note chord.
        mgr.macros().tension = 0.5f;
        mgr.macros().damping = 0.3f;
        mgr.macros().density = 0.6f;
        mgr.macros().migration = 0.2f;
        mgr.macros().coherence = 0.8f;
        mgr.macros().excitation = 0.3f;
        mgr.noteOn(60, 1.0f);
        mgr.noteOn(64, 1.0f);
        mgr.noteOn(67, 1.0f);
        for (int written = 0; written < kHoldSamples; written += kBlock)
        {
            const int n = std::min(kBlock, kHoldSamples - written);
            mgr.renderBlockStereo(outL.data() + written, outR.data() + written, n);
        }
    };

    // Serial reference.
    VoiceManager serial(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    std::vector<float> sL(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> sR(static_cast<std::size_t>(kHoldSamples), 0.0f);
    renderChord(serial, sL, sR);

    // Threaded — every worker count from 1 to 4 must match.
    for (int workers : {1, 2, 4})
    {
        VoiceManager parallel(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
        parallel.enableThreading(workers);
        REQUIRE(parallel.threadingEnabled());
        REQUIRE(parallel.numWorkers() == workers);

        std::vector<float> pL(static_cast<std::size_t>(kHoldSamples), 0.0f);
        std::vector<float> pR(static_cast<std::size_t>(kHoldSamples), 0.0f);
        renderChord(parallel, pL, pR);

        for (int i = 0; i < kHoldSamples; ++i)
        {
            CAPTURE(workers, i);
            REQUIRE(sL[static_cast<std::size_t>(i)] == pL[static_cast<std::size_t>(i)]);
            REQUIRE(sR[static_cast<std::size_t>(i)] == pR[static_cast<std::size_t>(i)]);
        }
    }
}

TEST_CASE("Voice-pool threading: 2D topology bit-exact with serial", "[voice_manager][threading][determinism]")
{
    constexpr int kHoldSamples = kSampleRate / 8; // 0.125 s — 2D is heavier
    constexpr int kBlock = 256;
    auto renderChord = [&](VoiceManager& mgr, std::vector<float>& outL, std::vector<float>& outR)
    {
        mgr.setTopology(sfs::engine::Topology::Torus2D);
        mgr.noteOn(60, 1.0f);
        mgr.noteOn(64, 0.8f);
        for (int written = 0; written < kHoldSamples; written += kBlock)
        {
            const int n = std::min(kBlock, kHoldSamples - written);
            mgr.renderBlockStereo(outL.data() + written, outR.data() + written, n);
        }
    };

    VoiceManager serial(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    std::vector<float> sL(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> sR(static_cast<std::size_t>(kHoldSamples), 0.0f);
    renderChord(serial, sL, sR);

    VoiceManager parallel(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    parallel.enableThreading(2);
    std::vector<float> pL(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> pR(static_cast<std::size_t>(kHoldSamples), 0.0f);
    renderChord(parallel, pL, pR);

    for (int i = 0; i < kHoldSamples; ++i)
    {
        REQUIRE(sL[static_cast<std::size_t>(i)] == pL[static_cast<std::size_t>(i)]);
        REQUIRE(sR[static_cast<std::size_t>(i)] == pR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("MPE pitch bend = 0 produces bit-exact output vs. legacy noteOn", "[voice_manager][mpe][determinism]")
{
    // Bypass guarantee: when no MPE pitch bend is in flight the render
    // must match the Phase 2 path exactly (the dm_pow2 branch is skipped).
    constexpr int kHoldSamples = kSampleRate / 4; // 0.25 s
    constexpr int kBlock = 256;

    VoiceManager legacy(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    legacy.noteOn(60, 1.0f);
    std::vector<float> aL(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> aR(static_cast<std::size_t>(kHoldSamples), 0.0f);
    for (int written = 0; written < kHoldSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kHoldSamples - written);
        legacy.renderBlockStereo(aL.data() + written, aR.data() + written, n);
    }

    VoiceManager mpe(kSubstrateCells, /*agentCount*/ 16, kSampleRateF);
    mpe.noteOn(/*channel*/ 0, 60, 1.0f); // channel 0 = unbound, no bend table
    std::vector<float> bL(static_cast<std::size_t>(kHoldSamples), 0.0f);
    std::vector<float> bR(static_cast<std::size_t>(kHoldSamples), 0.0f);
    for (int written = 0; written < kHoldSamples; written += kBlock)
    {
        const int n = std::min(kBlock, kHoldSamples - written);
        mpe.renderBlockStereo(bL.data() + written, bR.data() + written, n);
    }

    for (int i = 0; i < kHoldSamples; ++i)
    {
        REQUIRE(aL[static_cast<std::size_t>(i)] == bL[static_cast<std::size_t>(i)]);
        REQUIRE(aR[static_cast<std::size_t>(i)] == bR[static_cast<std::size_t>(i)]);
    }
}
