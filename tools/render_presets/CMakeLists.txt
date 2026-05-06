# tools/render_presets/CMakeLists.txt
#
# Multi-preset audio regression harness. Renders each known-preset config
# to WAV + raw + an analytic report so changes that subtly break the audio
# (DC drift, RMS collapse, peak overshoot) are visible before shipping.

add_executable (sfs_render_presets
    main.cpp
)

target_link_libraries (sfs_render_presets
    PRIVATE
        SFS::Engine
        SFS::DeterminismFlags
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions (sfs_render_presets PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

set_target_properties (sfs_render_presets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
