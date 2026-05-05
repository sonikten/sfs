// src/dsp/denormal_flush.h
//
// FTZ/DAZ enablement at audio-thread entry. Per CLAUDE.md "Hard invariants"
// + sfs-spec/06 §2.3: "Denormal-flush settings (FTZ/DAZ) are explicitly
// enabled at the start of every audio thread invocation so denormals don't
// make platforms differ."
//
// JUCE's ScopedNoDenormals does the same thing; we have our own helper so
// the engine isn't transitively dependent on JUCE for the determinism
// contract — and so unit tests can verify the bits without dragging JUCE
// into the test target.
//
// Implementation by ISA:
//   x86_64 (SSE):   MXCSR bits  6 (DAZ) and 15 (FTZ)
//   AArch64 (NEON): FPCR  bit  24 (FZ — flush-to-zero)
//   else:           no-op (unsupported platform; substrate's clamps handle it)

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define SFS_DENORMAL_FLUSH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SFS_DENORMAL_FLUSH_AARCH64 1
#else
#define SFS_DENORMAL_FLUSH_NOOP 1
#endif

namespace sfs::dsp
{

// Read the floating-point status register. The shape is platform-specific;
// callers should treat the return value as opaque except via
// `isFlushToZeroSet`.
[[nodiscard]] inline std::uint64_t getFpStatusRegister() noexcept
{
#if defined(SFS_DENORMAL_FLUSH_X86)
    return static_cast<std::uint64_t>(_mm_getcsr());
#elif defined(SFS_DENORMAL_FLUSH_AARCH64)
    std::uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
#else
    return 0;
#endif
}

// Write the floating-point status register. Used by the RAII scope below;
// also useful in tests that need to clear FTZ to verify the helper sets it.
inline void setFpStatusRegister(std::uint64_t fpsr) noexcept
{
#if defined(SFS_DENORMAL_FLUSH_X86)
    _mm_setcsr(static_cast<unsigned int>(fpsr));
#elif defined(SFS_DENORMAL_FLUSH_AARCH64)
    __asm__ volatile("msr fpcr, %0" : : "r"(fpsr));
#else
    (void)fpsr;
#endif
}

// True iff the FP status indicates flush-to-zero is on.
[[nodiscard]] inline bool isFlushToZeroSet() noexcept
{
#if defined(SFS_DENORMAL_FLUSH_X86)
    // MXCSR bit 15 (FTZ) and bit 6 (DAZ).
    constexpr std::uint64_t kMask = 0x8040u;
    return (getFpStatusRegister() & kMask) == kMask;
#elif defined(SFS_DENORMAL_FLUSH_AARCH64)
    // FPCR bit 24 (FZ).
    constexpr std::uint64_t kMask = (1ULL << 24);
    return (getFpStatusRegister() & kMask) != 0;
#else
    return false;
#endif
}

// Force FTZ/DAZ on. Idempotent.
inline void enableFlushToZero() noexcept
{
#if defined(SFS_DENORMAL_FLUSH_X86)
    _mm_setcsr(_mm_getcsr() | 0x8040u);
#elif defined(SFS_DENORMAL_FLUSH_AARCH64)
    setFpStatusRegister(getFpStatusRegister() | (1ULL << 24));
#endif
}

// RAII: enable FTZ/DAZ for a scope; restore the prior state on exit.
// Call at the top of every audio-thread render scope.
class ScopedFlushToZero
{
public:
    ScopedFlushToZero() noexcept : prev_(getFpStatusRegister()) { enableFlushToZero(); }
    ~ScopedFlushToZero() noexcept { setFpStatusRegister(prev_); }

    ScopedFlushToZero(const ScopedFlushToZero&) = delete;
    ScopedFlushToZero& operator=(const ScopedFlushToZero&) = delete;

private:
    std::uint64_t prev_;
};

} // namespace sfs::dsp
