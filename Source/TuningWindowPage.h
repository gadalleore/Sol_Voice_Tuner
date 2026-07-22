/*
    TuningWindowPage.h
    ------------------
    Window 3 of the paging UI (63C-6 placeholder for 63C-16): hosts the full
    legacy tuner UI so every existing control stays reachable. The real Tuning
    window (key wheel + voice effects: Autotune / Roboto / Phonecall) replaces
    the legacy layout in 63C-16.
*/

#pragma once

#include <JuceHeader.h>

#include "LegacyTunerPage.h"
#include "SolPage.h"

class TuningWindowPage final : public SolPage
{
public:
    TuningWindowPage (PitchCorrectorAudioProcessor& p, PageStack& stackToUse)
        : SolPage (stackToUse, "Tuning"),
          legacy (p)
    {
        addAndMakeVisible (legacy);
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        legacy.setBounds (area);
    }

    LegacyTunerPage legacy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningWindowPage)
};
