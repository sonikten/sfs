# src/engine/CMakeLists.txt
#
# Top-level engine glue: ties Substrate1D + AgentPool + the harvester read
# into a per-voice render loop. Phase 1 ships exactly one Voice per plug-in;
# Phase 2 introduces the VoiceManager + 8-voice polyphony.

add_library (sfs_engine STATIC
    voice.cpp
)

add_library (SFS::Engine ALIAS sfs_engine)

target_include_directories (sfs_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..   # src/
)

target_link_libraries (sfs_engine
    PUBLIC
        SFS::Engine::Agents
        SFS::Engine::Substrate
        SFS::Engine::Macros              # MacroValues + fanOut
        SFS::Dsp                         # for ScopedFlushToZero in Voice
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties (sfs_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
