# src/plugin/CMakeLists.txt
#
# Two targets:
#
#   sfs_plugin_core  — static library with the AudioProcessor implementation.
#                      Linked by both the VST3 wrapper (sfs_plugin) and the
#                      headless render rig (tools/sfs_render).
#
#   sfs_plugin       — JUCE-generated VST3 wrapper around sfs_plugin_core.
#                      Produces SFS.vst3.
#
# Identity placeholders below MUST be replaced before public release.
# See README.md "Licensing".

# ----------------------------------------------------------------------------
# sfs_plugin_core: AudioProcessor implementation, no VST3 plumbing.
# ----------------------------------------------------------------------------
add_library (sfs_plugin_core STATIC
    PluginProcessor.cpp
    PluginEditor.cpp
    MacroPanel.cpp
    AdvancedPanel.cpp
    HeaderBar.cpp
    Footer.cpp
)

add_library (SFS::PluginCore ALIAS sfs_plugin_core)

target_include_directories (sfs_plugin_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries (sfs_plugin_core
    PUBLIC
        SFS::Dsp
        SFS::Engine
        SFS::Preset
        SFS::DeterminismFlags
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_graphics
)

# Suppress JUCE's default splash + curl + browser to keep the build slim and
# the determinism story narrow.
target_compile_definitions (sfs_plugin_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ----------------------------------------------------------------------------
# sfs_plugin: VST3 wrapper.
# ----------------------------------------------------------------------------
juce_add_plugin (sfs_plugin
    PRODUCT_NAME                 "SFS"
    COMPANY_NAME                 "SFS"                       # TODO: legal entity before release
    BUNDLE_ID                    "com.sfs.synth.sfs"         # TODO: replace with owned domain
    PLUGIN_MANUFACTURER_CODE     "Stig"                      # 4 chars, first uppercase
    PLUGIN_CODE                  "Sfs0"                      # 4 chars exactly
    FORMATS                      VST3
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             TRUE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      ${SFS_INSTALL_PLUGIN_AFTER_BUILD}
    DESCRIPTION                  "Stigmergic Field Synthesis — Phase 0 skeleton"
    VST3_CATEGORIES              Instrument Synth
)

# The VST3 wrapper needs at least one source even when delegating to the
# static lib; an empty TU keeps juce_add_plugin happy.
target_sources (sfs_plugin PRIVATE
    PluginEntry.cpp
)

target_link_libraries (sfs_plugin
    PRIVATE
        SFS::PluginCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
