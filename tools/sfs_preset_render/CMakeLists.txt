# tools/sfs_preset_render/CMakeLists.txt
#
# Phase 4 §A3 — audio-hash render harness for `.sfs` presets. JUCE-free
# (no plug-in shell, no host-param wiring); operates directly on the
# engine + preset modules. The companion shasum step lives in
# tools/sfs_preset_hash.sh.

add_executable(sfs_preset_render
    main.cpp
)

target_link_libraries(sfs_preset_render
    PRIVATE
        SFS::Preset
        SFS::PresetEngine
        SFS::Engine
        SFS::DeterminismFlags
        SFS::StrictWarnings
)

set_target_properties(sfs_preset_render PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
