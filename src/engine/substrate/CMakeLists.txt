# src/engine/substrate/CMakeLists.txt
#
# 1D ring substrate (Phase 1) and 2D toroidal substrate (Phase 3, not yet here).
# Per-voice; pure scalar reference until the CPU profile justifies SIMD.
# See sfs-spec/02.

add_library (sfs_engine_substrate STATIC
    substrate_1d.cpp
    substrate_2d.cpp
)

add_library (SFS::Engine::Substrate ALIAS sfs_engine_substrate)

target_include_directories (sfs_engine_substrate
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..   # src/   so callers do #include "engine/substrate/substrate_1d.h"
)

target_link_libraries (sfs_engine_substrate
    PUBLIC
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties (sfs_engine_substrate PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
