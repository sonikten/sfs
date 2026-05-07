# src/preset/CMakeLists.txt
#
# Phase 4 §A1 — preset JSON format. Pure data + nlohmann::json; no engine
# dependency at this layer. The engine binding (Preset → VoiceManager and
# back) lives alongside the plug-in shell because that's where the
# AudioProcessorParameter pointers live.

add_library(sfs_preset STATIC
    preset.cpp
)

add_library(SFS::Preset ALIAS sfs_preset)

target_include_directories(sfs_preset
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..   # src/
)

target_link_libraries(sfs_preset
    PUBLIC
        nlohmann_json::nlohmann_json
        SFS::DeterminismFlags
    PRIVATE
        SFS::StrictWarnings
)

set_target_properties(sfs_preset PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
