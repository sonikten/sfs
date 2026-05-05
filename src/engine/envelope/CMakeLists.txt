# src/engine/envelope/CMakeLists.txt
#
# Per-voice envelopes (Phase 2 §9 step 9). Header-only — linear-piecewise
# ADSR is small enough to inline. Tests link directly against this target.

add_library (sfs_engine_envelope INTERFACE)
add_library (SFS::Engine::Envelope ALIAS sfs_engine_envelope)

target_include_directories (sfs_engine_envelope INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries (sfs_engine_envelope
    INTERFACE
        SFS::DeterminismFlags
)
