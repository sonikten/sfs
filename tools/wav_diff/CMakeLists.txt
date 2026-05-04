# tools/wav_diff/CMakeLists.txt
#
# Minimal WAV diff tool. Used by the determinism CI compare job to attach a
# useful diagnostic when two platforms produce divergent renders.

add_executable (wav_diff
    main.cpp
)

target_link_libraries (wav_diff
    PRIVATE
        SFS::DeterminismFlags
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions (wav_diff PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

set_target_properties (wav_diff PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
