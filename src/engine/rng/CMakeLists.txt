# src/engine/rng/CMakeLists.txt
#
# Counter-based RNG wrapper around Random123's Philox-4×32-10. The single
# legal RNG in the audio path. See sfs-spec/06 §1.

add_library (sfs_engine_rng STATIC
    philox.cpp
)

add_library (SFS::Engine::Rng ALIAS sfs_engine_rng)

target_include_directories (sfs_engine_rng
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..   # src/   so callers do #include "engine/rng/philox.h"
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/Random123/include
)

target_link_libraries (sfs_engine_rng
    PUBLIC
        SFS::Dsp                  # dm_log + dm_cos + dm_sqrt for nextGaussian
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties (sfs_engine_rng PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
