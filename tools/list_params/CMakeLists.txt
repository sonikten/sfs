# tools/list_params/CMakeLists.txt
#
# Diagnostic tool: prints the plug-in's full host parameter surface.
# Built only when SFS_BUILD_TOOLS is on.

add_executable (sfs_list_params
    main.cpp
)

target_link_libraries (sfs_list_params
    PRIVATE
        SFS::PluginCore
        SFS::DeterminismFlags
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions (sfs_list_params PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

set_target_properties (sfs_list_params PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
