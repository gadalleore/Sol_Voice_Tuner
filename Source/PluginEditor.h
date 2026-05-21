/*
    PluginEditor.h
    --------------
    Sol Voice Tuner UI: title header, Pitch In / Out, Robotic + Formant,
    bottom strip: scale / MIDI + bend range beside vertical pitch-bend fader.
*/

#pragma once

#include <array>

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SolLookAndFeel.h"

/** Pitch-bend fader: after releasing the mouse, returns to centre after a short delay (hardware wheel behaviour). */
class PitchBendFaderSlider final : public juce::Slider,
                                   private juce::Timer
{
public:
    static constexpr int snapDelayMs = 100;

    explicit PitchBendFaderSlider (PitchCorrectorAudioProcessor& p) noexcept : processor (p) {}

    ~PitchBendFaderSlider() override { stopTimer(); }

private:
    PitchCorrectorAudioProcessor& processor;

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        juce::Slider::mouseDown (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp (e);

        if (! e.source.isMouse())
            return;

        if (e.mods.isPopupMenu())
            return;

        if (juce::approximatelyEqual (getValue(), 0.0))
            return;

        startTimer (snapDelayMs);
    }

    void timerCallback() override
    {
        stopTimer();

        if (juce::approximatelyEqual (getValue(), 0.0))
            return;

        if (auto* param = processor.getAPVTS().getParameter (PitchCorrectorAudioProcessor::PID_PITCH_BEND))
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                ranged->beginChangeGesture();
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (0.0f));
                ranged->endChangeGesture();
            }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendFaderSlider)
};

class PitchCorrectorAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit PitchCorrectorAudioProcessorEditor (PitchCorrectorAudioProcessor&);
    ~PitchCorrectorAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized()                override;

private:
    void timerCallback() override;

    void stylePitchReadout (juce::Label& title, juce::Label& value, const juce::String& titleText);
    void refreshKeyNoteSelection();

    PitchCorrectorAudioProcessor& processorRef;
    SolLookAndFeel lookAndFeel;

    juce::Label    productTitle;
    juce::ToggleButton bypassBtn { "Bypass" };

    juce::Label pitchInTitle,  pitchInValue;
    juce::Label pitchOutTitle, pitchOutValue;

    juce::Slider roboticKnob  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider formantKnob  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  roboticLbl, formantLbl;

    PitchBendFaderSlider pitchBendSlider { processorRef };
    juce::Label  pitchBendLbl;
    juce::Slider bendRangeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  bendRangeLbl;
    juce::Slider volumeSlider  { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label  volumeLbl;

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

    std::unique_ptr<SAtt> roboticAtt, formantAtt, bendAtt, bendRangeAtt, volumeAtt;
    std::unique_ptr<BAtt> bypassAtt, midiFollowAtt;
    std::unique_ptr<CAtt> scaleAtt;
    std::unique_ptr<juce::ParameterAttachment> keyRootAtt;

    juce::Rectangle<int> pitchInPanelBounds, pitchOutPanelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCorrectorAudioProcessorEditor)
};
