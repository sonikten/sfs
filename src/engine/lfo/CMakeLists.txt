# src/engine/lfo/CMakeLists.txt
#
# Per-voice LFO bank (Phase 2 §9 step 9). Header-only — the LFO is small
# enough to inline and links against dm_sin + the Philox RNG.

add_library (sfs_engine_lfo INTERFACE)
add_library (SFS::Engine::Lfo ALIAS sfs_engine_lfo)

target_include_directories (sfs_engine_lfo INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries (sfs_engine_lfo
    INTERFACE
        SFS::Dsp
        SFS::Engine::Rng
        SFS::DeterminismFlags
)
