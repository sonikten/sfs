# src/dsp/CMakeLists.txt
#
# Pure-DSP primitives: deterministic math (dm_sin, dm_cos, dm_tanh, ...),
# polynomial / LUT helpers, SIMD scalar fallbacks. No JUCE / no plug-in
# coupling — this library must remain audio-thread-safe and unit-testable
# in isolation.

add_library(sfs_dsp STATIC
    dm_sin.cpp
    dm_log.cpp
    # dm_cos.h, dm_sqrt.h, denormal_flush.h are header-only — listed here
    # so they show up in IDEs.
)

add_library(SFS::Dsp ALIAS sfs_dsp)

target_include_directories(sfs_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..   # src/   so callers do #include "dsp/dm_sin.h"
)

target_link_libraries(sfs_dsp
    PUBLIC
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties(sfs_dsp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
