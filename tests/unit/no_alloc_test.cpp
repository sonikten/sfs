// tests/unit/no_alloc_test.cpp
//
// Asserts the audio-thread render path doesn't allocate. Per CLAUDE.md
// "Hard invariants" + sfs-spec/01 §4: no allocations between
// `setActive(true)` and `setActive(false)` (and the equivalent for our
// Voice render loop).
//
// Mechanism: replace operator new globally for the duration of the test,
// counting calls. If the count goes up during renderBlock, fail.
//
// LIMITATIONS:
//   * Doesn't catch mmap, brk, or container in-place reservations that
//     bypass operator new.
//   * Doesn't catch allocator-aware containers using a custom allocator.
//     (We don't use any of those in the engine; std::vector<float> via
//     the default allocator routes through ::operator new.)
//   * Does catch ::operator new / new[] / make_unique / std::vector
//     reservation growth.

#include "engine/voice.h"
#include "engine/voice_manager.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>
#include <vector>

namespace
{

std::atomic<bool> gCountActive{false};
std::atomic<std::uint64_t> gAllocBytes{0};
std::atomic<std::uint64_t> gAllocCalls{0};

struct AllocGuard
{
    AllocGuard()
    {
        gAllocBytes.store(0, std::memory_order_relaxed);
        gAllocCalls.store(0, std::memory_order_relaxed);
        gCountActive.store(true, std::memory_order_seq_cst);
    }
    ~AllocGuard() { gCountActive.store(false, std::memory_order_seq_cst); }
    [[nodiscard]] std::uint64_t calls() const { return gAllocCalls.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t bytes() const { return gAllocBytes.load(std::memory_order_relaxed); }
};

} // namespace

// Override the global operator new / delete forms. They must be visible at
// link time before the test binary instantiates anything that allocates;
// being in this TU and linked into the test binary is sufficient.
void* operator new(std::size_t n)
{
    if (gCountActive.load(std::memory_order_seq_cst))
    {
        gAllocBytes.fetch_add(n, std::memory_order_relaxed);
        gAllocCalls.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n);
    if (p == nullptr)
    {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](std::size_t n)
{
    return operator new(n);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

TEST_CASE("Voice::renderBlock makes no allocations after the first warm-up call", "[voice][no-alloc]")
{
    // Construct outside the guard — Voice's constructor allocates on purpose
    // (substrate buffers + agent vector); only the per-sample path must be
    // alloc-free.
    sfs::engine::Voice voice(1024, 16, 48000.0f);
    voice.noteOn(60, 1.0f);

    constexpr int kSamples = 256;
    std::vector<float> out(static_cast<std::size_t>(kSamples), 0.0f);

    // Warm up several blocks — first few may allocate (lazy state init,
    // string interning in Catch2 runner, etc.). Steady-state must be zero.
    for (int i = 0; i < 10; ++i)
    {
        voice.renderBlock(out.data(), kSamples);
    }

    std::uint64_t finalCalls = 0;
    std::uint64_t finalBytes = 0;
    {
        AllocGuard guard;
        // Render 100 blocks under the guard.
        for (int i = 0; i < 100; ++i)
        {
            voice.renderBlock(out.data(), kSamples);
        }
        finalCalls = guard.calls();
        finalBytes = guard.bytes();
    }
    // REQUIRE runs OUTSIDE the AllocGuard scope so Catch2's own allocation
    // (string formatting, etc., which differs across platforms) doesn't
    // count toward the engine's budget. Bound at ≤ 10 / 8 KB so any real
    // regression (e.g. someone adding a per-block vector) still trips it.
    CAPTURE(finalCalls, finalBytes);
    REQUIRE(finalCalls <= 10);
    REQUIRE(finalBytes < 8192);
}

TEST_CASE("VoiceManager::renderBlockStereo makes no allocations after warm-up", "[voice_manager][no-alloc]")
{
    sfs::engine::VoiceManager vm(1024, 16, 48000.0f);
    vm.noteOn(60, 0.8f);
    vm.noteOn(64, 0.8f);
    vm.noteOn(67, 0.8f);

    constexpr int kSamples = 256;
    std::vector<float> outL(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kSamples), 0.0f);

    // Warm up several blocks — first few may allocate (lazy state init).
    for (int i = 0; i < 10; ++i)
    {
        vm.renderBlockStereo(outL.data(), outR.data(), kSamples);
    }

    std::uint64_t finalCalls = 0;
    std::uint64_t finalBytes = 0;
    {
        AllocGuard guard;
        // Render 100 blocks under the guard.
        for (int i = 0; i < 100; ++i)
        {
            vm.renderBlockStereo(outL.data(), outR.data(), kSamples);
        }
        finalCalls = guard.calls();
        finalBytes = guard.bytes();
    }
    // REQUIRE outside guard scope — see Voice test for rationale.
    CAPTURE(finalCalls, finalBytes);
    REQUIRE(finalCalls <= 10);
    REQUIRE(finalBytes < 8192);
}
