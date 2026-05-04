# src/engine/agents/CMakeLists.txt
#
# Per-voice agent pool. Reads from + deposits into the substrate; never
# observes other agents directly (stigmergy contract — sfs-spec/01 §1).
# Phase 1: sine waveform only, no migration noise.

add_library (sfs_engine_agents STATIC
    agent_pool.cpp
)

add_library (SFS::Engine::Agents ALIAS sfs_engine_agents)

target_include_directories (sfs_engine_agents
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..   # src/   so callers do #include "engine/agents/..."
)

target_link_libraries (sfs_engine_agents
    PUBLIC
        SFS::Dsp
        SFS::Engine::Substrate
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties (sfs_engine_agents PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
