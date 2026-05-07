# tests/integration/CMakeLists.txt
#
# Integration tests that exercise the SfsAudioProcessor (the host-facing
# AudioProcessor wrapper). Lives separately from unit tests because it
# needs the JUCE-linked sfs_plugin_core target.

if (NOT SFS_BUILD_PLUGIN)
    return()
endif()

add_executable (sfs_integration_tests
    preset_playback_smoke_test.cpp
    editor_layout_test.cpp
)

target_link_libraries (sfs_integration_tests
    PRIVATE
        SFS::PluginCore
        SFS::Preset
        SFS::DeterminismFlags
        Catch2::Catch2WithMain
)

set_target_properties (sfs_integration_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test (NAME sfs_integration_tests
          COMMAND sfs_integration_tests --reporter compact
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
