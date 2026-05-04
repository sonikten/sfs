# tools/sfs_render/CMakeLists.txt
#
# Headless render rig. Links the AudioProcessor (sfs_plugin_core) without the
# VST3 wrapper, so it runs on a CI host with no DAW. Output: WAV file.

add_executable (sfs_render
    main.cpp
)

target_link_libraries (sfs_render
    PRIVATE
        SFS::PluginCore
        SFS::DeterminismFlags
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions (sfs_render PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

set_target_properties (sfs_render PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
