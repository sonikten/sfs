// tools/list_params/main.cpp
//
// Instantiates SfsAudioProcessor and dumps every host parameter (ID,
// name, range, default). Used both as a local debug aid ("does Live see
// my new ADSR knob?") and as a CI smoke check that the param surface
// hasn't regressed unintentionally.
//
// This intentionally does NOT instantiate the VST3 wrapper — it links
// against sfs_plugin_core directly, the same way sfs_render does. So
// running this binary doesn't require a host or any VST infrastructure.

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

int main()
{
    sfs::plugin::SfsAudioProcessor proc;

    const auto& params = proc.getParameters();
    std::printf("Plug-in name: %s\n", proc.getName().toRawUTF8());
    std::printf("Parameter count: %d\n", params.size());
    std::printf("\n%-30s  %-25s  %-10s  %s\n", "ID", "NAME", "DEFAULT", "RANGE");
    std::printf("%-30s  %-25s  %-10s  %s\n",
                "------------------------------",
                "-------------------------",
                "----------",
                "----------");

    for (auto* p : params)
    {
        const auto id = p->getName(64);
        const auto defaultValue = p->getDefaultValue();
        const auto* asRanged = dynamic_cast<juce::RangedAudioParameter*>(p);
        const auto* asChoice = dynamic_cast<juce::AudioParameterChoice*>(p);

        juce::String paramId;
        if (asRanged != nullptr)
        {
            paramId = asRanged->paramID;
        }

        if (asChoice != nullptr)
        {
            std::printf("%-30s  %-25s  %-10d  choices=[%s]\n",
                        paramId.toRawUTF8(),
                        id.toRawUTF8(),
                        asChoice->getIndex(),
                        asChoice->choices.joinIntoString(", ").toRawUTF8());
        }
        else if (asRanged != nullptr)
        {
            const auto& range = asRanged->getNormalisableRange();
            std::printf("%-30s  %-25s  %-10.4f  [%.4f, %.4f] skew=%.3f\n",
                        paramId.toRawUTF8(),
                        id.toRawUTF8(),
                        asRanged->convertFrom0to1(defaultValue),
                        range.start,
                        range.end,
                        range.skew);
        }
        else
        {
            std::printf("%-30s  %-25s  %-10.4f  (raw)\n", paramId.toRawUTF8(), id.toRawUTF8(), defaultValue);
        }
    }

    return 0;
}
