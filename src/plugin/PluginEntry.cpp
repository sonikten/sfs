// src/plugin/PluginEntry.cpp
//
// JUCE plug-in factory entry point. Lives here (not in PluginProcessor.cpp)
// so the AudioProcessor can be linked into both the VST3 wrapper and the
// headless render rig (tools/sfs_render) without duplicate symbol issues —
// the headless rig links sfs_plugin_core but not this file.

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sfs::plugin::SfsAudioProcessor();
}
