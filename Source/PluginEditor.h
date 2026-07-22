/*
    PluginEditor.h
    --------------
    Thin host for the Xbox-style paging UI (63C-6): owns the LookAndFeel, the
    PageStack, and one instance of every window. Navigation:

        Home ─┬─ Input Global Effects   (EffectsWindowPage, input chain)
              ├─ Harmonies ── Tuning    (legacy tuner UI lives here for now)
              └─ Output Global Effects  (EffectsWindowPage, output chain)
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "SolLookAndFeel.h"
#include "PageStack.h"
#include "HomePage.h"
#include "EffectsWindowPage.h"
#include "HarmoniesWindowPage.h"
#include "TuningWindowPage.h"
#include "MeterSidebar.h"

class PitchCorrectorAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PitchCorrectorAudioProcessorEditor (PitchCorrectorAudioProcessor&);
    ~PitchCorrectorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PitchCorrectorAudioProcessor& processorRef;
    SolLookAndFeel lookAndFeel;

    // Declared before the pages: pages remove themselves from the stack's
    // child list on destruction, so the stack must outlive them.
    PageStack pageStack;

    HomePage            homePage;
    EffectsWindowPage   inputFxPage  { processorRef.getAPVTS(),
                                       PitchCorrectorAudioProcessor::fxChainInput,
                                       "Input Global Effects", pageStack };
    EffectsWindowPage   outputFxPage { processorRef.getAPVTS(),
                                       PitchCorrectorAudioProcessor::fxChainOutput,
                                       "Output Global Effects", pageStack };
    HarmoniesWindowPage harmoniesPage { pageStack };
    TuningWindowPage    tuningPage    { processorRef, pageStack };

    // 63C-18: always-visible metering column, outside the PageStack.
    MeterSidebar meterSidebar { processorRef };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCorrectorAudioProcessorEditor)
};
