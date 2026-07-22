/*
    PitchBendFader.h
    ----------------
    Pitch-bend fader slider (63C-18: moved out of LegacyTunerPage so the
    always-visible meter sidebar can host it). After releasing the mouse it
    returns to centre after a short delay, like a hardware pitch wheel.
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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
