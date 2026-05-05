// tests/unit/denormal_flush_test.cpp
//
// Verifies the FTZ/DAZ enablement helper actually sets the platform's
// floating-point status register bits. Per the determinism contract
// (CLAUDE.md / sfs-spec/06 §2.3) the audio thread must run with denormal
// flush on; this is the per-platform unit test that asserts our helper
// does the right thing on whichever platform CI happens to run on.

#include "dsp/denormal_flush.h"

#include <catch2/catch_test_macros.hpp>

using sfs::dsp::enableFlushToZero;
using sfs::dsp::getFpStatusRegister;
using sfs::dsp::isFlushToZeroSet;
using sfs::dsp::ScopedFlushToZero;
using sfs::dsp::setFpStatusRegister;

TEST_CASE("enableFlushToZero turns on the platform's FTZ/DAZ bits", "[dsp][denormal-flush]")
{
    // Save current state so we don't leak FTZ across tests.
    const auto prev = getFpStatusRegister();

    // Clear: write back the saved value MINUS the FTZ/DAZ mask (so we know
    // the next assertion measures the helper, not the inherited state).
#if defined(SFS_DENORMAL_FLUSH_X86)
    setFpStatusRegister(prev & ~0x8040u);
#elif defined(SFS_DENORMAL_FLUSH_AARCH64)
    setFpStatusRegister(prev & ~(1ULL << 24));
#endif

#if defined(SFS_DENORMAL_FLUSH_X86) || defined(SFS_DENORMAL_FLUSH_AARCH64)
    REQUIRE_FALSE(isFlushToZeroSet()); // sanity: cleared

    enableFlushToZero();
    REQUIRE(isFlushToZeroSet()); // helper set the bit

    // Restore.
    setFpStatusRegister(prev);
#else
    // Unsupported platform — helper is a no-op; the test merely confirms it
    // doesn't blow up.
    enableFlushToZero();
    SUCCEED("denormal flush is a no-op on this platform");
#endif
}

TEST_CASE("ScopedFlushToZero enables on entry and restores on exit", "[dsp][denormal-flush][raii]")
{
#if defined(SFS_DENORMAL_FLUSH_X86) || defined(SFS_DENORMAL_FLUSH_AARCH64)
    const auto prev = getFpStatusRegister();
#if defined(SFS_DENORMAL_FLUSH_X86)
    setFpStatusRegister(prev & ~0x8040u);
#else
    setFpStatusRegister(prev & ~(1ULL << 24));
#endif
    REQUIRE_FALSE(isFlushToZeroSet());

    {
        ScopedFlushToZero scope;
        REQUIRE(isFlushToZeroSet());
    }

    // After scope exit, prior state restored.
    REQUIRE_FALSE(isFlushToZeroSet());

    setFpStatusRegister(prev);
#else
    SUCCEED("denormal flush is a no-op on this platform");
#endif
}
