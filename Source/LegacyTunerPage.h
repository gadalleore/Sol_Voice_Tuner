/*
    LegacyTunerPage.h
    -----------------
    The pre-paging Sol Voice Tuner UI, moved verbatim out of PluginEditor into
    a page component (63C-6): title header, Pitch In / Out, Robotic + Formant,
    bottom strip: scale / MIDI + bend range.
    Hosted inside TuningWindowPage until the real Tuning window (63C-16) lands.
    63C-18: pitch bend + volume moved to the always-visible MeterSidebar
    (PitchBendFaderSlider now lives in PitchBendFader.h).
*/

#pragma once

#include <array>

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SolLookAndFeel.h"
#include "OscilloscopeComponent.h"

class LegacyTunerPage final : public juce::Component,
                              private juce::Timer
{
public:
    explicit LegacyTunerPage (PitchCorrectorAudioProcessor&);
    ~LegacyTunerPage() override;

    void paint  (juce::Graphics&) override;
    void resized()                override;

private:
    void timerCallback() override;

    void stylePitchReadout (juce::Label& title, juce::Label& value, const juce::String& titleText);
    void refreshKeyNoteSelection();

    PitchCorrectorAudioProcessor& processorRef;

    juce::Label    productTitle;
    juce::ToggleButton bypassBtn { "Bypass" };

    juce::Label pitchInTitle,  pitchInValue;
    juce::Label pitchOutTitle, pitchOutValue;

    juce::Slider roboticKnob  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider subKnob      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider formantKnob  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  roboticLbl, subLbl, formantLbl;

    // Centre-panel tabs: 0 = Knobs, 1 = Scope.
    juce::TextButton    tabKnobsBtn { "Knobs" };
    juce::TextButton    tabScopeBtn { "Scope" };
    OscilloscopeComponent oscilloscope;
    int                 currentCentreTab { 0 };
    void                setCentreTab (int t);

    juce::Slider bendRangeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  bendRangeLbl;

    juce::ComboBox scaleBox;
    juce::Label    scaleLbl;

    std::array<juce::TextButton, 12> keyNoteBtns {};
    juce::Label     keyLbl;

    juce::ToggleButton midiFollowBtn { "MIDI Follow" };
    juce::Label     midiStatusLbl;

    using SAPVTS = juce::AudioProcessorValueTreeState;
    using SAtt   = SAPVTS::SliderAttachment;
    using BAtt   = SAPVTS::ButtonAttachment;
    using CAtt   = SAPVTS::ComboBoxAttachment;

    std::unique_ptr<SAtt> roboticAtt, subAtt, formantAtt, bendRangeAtt;
    std::unique_ptr<BAtt> bypassAtt, midiFollowAtt;
    std::unique_ptr<CAtt> scaleAtt;
    std::unique_ptr<juce::ParameterAttachment> keyRootAtt;

    juce::Rectangle<int> pitchInPanelBounds, pitchOutPanelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LegacyTunerPage)
};
