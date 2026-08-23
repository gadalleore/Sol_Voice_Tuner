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

#include "EdgeMeters.h"
#include "LegacyTunerPage.h"
#include "SolPage.h"
#include "VolumeArc.h"

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
        // Keep clear of the plate's right-hand column.
        //
        // Volume, Input Mono, the meters and the brand mark are fixtures of
        // the WINDOW, not of the page — MeteredPlate hangs them off the plate
        // as siblings drawn over whatever content is showing. Every other page
        // happens to dodge them (the wheels bulge leftward), but the legacy
        // tuner lays out across the full width it is handed, so Bypass landed
        // under the volume dial and the bend-range knob under the 63C mark.
        // Reserving the column here is what stops the two from stacking.
        legacy.setBounds (area.withTrimmedRight (kRightColumn));
    }

    /** Widest thing in that column (the dial), plus the air the plate keeps
        outboard of it. */
    static constexpr int kRightColumn = VolumeArc::kWidth + EdgeMeters::kBarWidth;

    LegacyTunerPage legacy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningWindowPage)
};
