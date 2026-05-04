// src/engine/rng/philox.cpp
//
// Wraps the Random123 reference Philox-4×32-10 implementation behind the
// API specified in sfs-spec/06 §1.3. The underlying 10-round transform is
// bit-identical across all supported platforms (DE Shaw Research's
// reference C code from third_party/Random123).

#include "philox.h"

// Random123's Philox header uses old-style C casts internally (uint64_t /
// __uint128_t multiplications) that trip our `-Wold-style-cast -Werror`.
// The macros are correct; we just locally suspend the strict diagnostics
// for the include so OUR translation unit stays under SFS::StrictWarnings.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4456)
#endif

#include <Random123/philox.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace sfs::engine::rng
{

namespace
{

// 1 / 2^24 — the divisor for the 24-bit fractional uniform.
// Constexpr float; bit-exact across platforms.
constexpr float kInvTwoPow24 = 1.0f / 16777216.0f;

inline ::philox4x32_ctr_t toCtr(const std::uint32_t in[4]) noexcept
{
    ::philox4x32_ctr_t out;
    out.v[0] = in[0];
    out.v[1] = in[1];
    out.v[2] = in[2];
    out.v[3] = in[3];
    return out;
}

inline ::philox4x32_key_t toKey(const std::uint32_t in[2]) noexcept
{
    ::philox4x32_key_t out;
    out.v[0] = in[0];
    out.v[1] = in[1];
    return out;
}

} // namespace

void philox4x32_10(std::uint32_t out[4], const std::uint32_t ctr[4], const std::uint32_t key[2]) noexcept
{
    const ::philox4x32_ctr_t outCtr = ::philox4x32(toCtr(ctr), toKey(key));
    out[0] = outCtr.v[0];
    out[1] = outCtr.v[1];
    out[2] = outCtr.v[2];
    out[3] = outCtr.v[3];
}

void Philox4x32Stream::seed(std::uint64_t presetSeed,
                            std::uint16_t voice,
                            std::uint16_t agent,
                            StreamId stream) noexcept
{
    // Key: 64-bit preset seed, low word first (sfs-spec/06 §1.2).
    key_[0] = static_cast<std::uint32_t>(presetSeed & 0xFFFFFFFFu);
    key_[1] = static_cast<std::uint32_t>((presetSeed >> 32) & 0xFFFFFFFFu);

    // Counter packing per spec:
    //   counter[3]  = voice_index_16  | (agent_index_16 << 16)
    //   counter[2]  = stream_id_16    | (reserved_16 << 16)
    //   counter[1]  = sample_index_high
    //   counter[0]  = sample_index_low
    counter_[3] = static_cast<std::uint32_t>(voice) | (static_cast<std::uint32_t>(agent) << 16);
    counter_[2] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(stream));
    counter_[1] = 0;
    counter_[0] = 0;

    lastOutCursor_ = 4; // forces a philox call on first next32()
}

void Philox4x32Stream::setSampleIndex(std::uint64_t sampleIndex) noexcept
{
    counter_[0] = static_cast<std::uint32_t>(sampleIndex & 0xFFFFFFFFu);
    counter_[1] = static_cast<std::uint32_t>((sampleIndex >> 32) & 0xFFFFFFFFu);
    lastOutCursor_ = 4; // invalidate the cache; next draw re-philoxes
}

std::uint32_t Philox4x32Stream::next32() noexcept
{
    if (lastOutCursor_ >= 4)
    {
        philox4x32_10(lastOut_, counter_, key_);

        // Increment the low counter word for back-to-back draws within the
        // same sample. Carries up into counter[1] (sample_index_high) on
        // overflow — matches the spec §1.3 reference. Per-sample streams
        // should call setSampleIndex() before each sample to override.
        if (++counter_[0] == 0u)
        {
            ++counter_[1];
        }
        lastOutCursor_ = 0;
    }
    return lastOut_[lastOutCursor_++];
}

float Philox4x32Stream::nextFloat01() noexcept
{
    // Drop the low 8 bits — keep the 24 bits that fit in a float mantissa.
    return static_cast<float>(next32() >> 8) * kInvTwoPow24;
}

} // namespace sfs::engine::rng
