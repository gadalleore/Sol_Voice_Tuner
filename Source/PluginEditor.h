/*
    PluginEditor.h
    --------------
    Sol uses a plugin-owned floating window (63C-50), so this editor is only a
    small stub: the host gets a name plate, and the real UI lives in
    `FloatingShell` — a layered desktop window where alpha works and the plate
    can have a non-rectangular silhouette.

    The v3 paging UI (63C-6) is still compiled but detached while the interface
    is rebuilt panel by panel; see the commented block in the constructor.
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "SolLookAndFeel.h"
#include "ChamferPanel.h"
#include "FloatingShell.h"
#include "LissajousDisplay.h"
#include "PageStack.h"
#include "HomePage.h"
#include "EffectsWindowPage.h"
#include "HarmoniesWindowPage.h"
#include "TuningWindowPage.h"
#include "MeterSidebar.h"

class PitchCorrectorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit PitchCorrectorAudioProcessorEditor (PitchCorrectorAudioProcessor&);
    ~PitchCorrectorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** The stub is an artefact of the floating-window technique, not UI —
        kept as small as hosts will tolerate. */
    static constexpr int kStubWidth  = 170;
    static constexpr int kStubHeight = 36;

    /** Opening size of the floating UI. */
    static constexpr int kShellWidth  = 750;
    static constexpr int kShellHeight = 450;

    /** Size of the sliced corner, in px. Zero gives a square plate. The close
        X is half this, so the cut has to be generous for the X to clear the
        diagonal and float free of the plate. */
    static constexpr float kChamfer = 88.0f;

    /** Refresh rate for the hub's Lissajous, in Hz. */
    static constexpr int kScopeFps = 30;

    /** Only used when the shell first appears or is re-shown — never while the
        host moves its plugin window. */
    void placeShellNearStub();

    void timerCallback() override;

    PitchCorrectorAudioProcessor& processorRef;
    SolLookAndFeel lookAndFeel;

    // The real window, the plate that fills it, and the wheel on the plate.
    FloatingShell  shell;
    ChamferPanel     basePlate;
    WheelComponent   wheel;
    LissajousDisplay hubScope;

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
    HarmoniesWindowPage harmoniesPage { processorRef.getAPVTS(), pageStack };
    TuningWindowPage    tuningPage    { processorRef, pageStack };

    // 63C-18: always-visible metering column, outside the PageStack.
    MeterSidebar meterSidebar { processorRef };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCorrectorAudioProcessorEditor)
};
