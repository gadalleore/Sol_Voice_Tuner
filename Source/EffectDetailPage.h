/*
    EffectDetailPage.h
    ------------------
    Minimal auto-generated drill-in page for one effect slot (63C-11): title =
    the effect's name, one big Amount knob attached to the slot's parameter.
    Reused by every effects wheel — rebind() points it at a different slot
    before each push. The real per-effect detail pages (extra parameters,
    63C-8) will grow from here.
*/

#pragma once

#include <JuceHeader.h>

#include "SolPage.h"

class EffectDetailPage final : public SolPage
{
public:
    EffectDetailPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Effect"), apvts (apvtsIn)
    {
        amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
        addAndMakeVisible (amount);

        amountLabel.setText ("Amount", juce::dontSendNotification);
        amountLabel.setJustificationType (juce::Justification::centred);
        amountLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        amountLabel.setColour (juce::Label::textColourId,
                               juce::Colour (SolLookAndFeel::kLabel));
        addAndMakeVisible (amountLabel);

        slotLabel.setJustificationType (juce::Justification::centred);
        slotLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        slotLabel.setColour (juce::Label::textColourId,
                             juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.6f));
        addAndMakeVisible (slotLabel);
    }

    /** Point the page at a slot's Amount parameter before pushing it. */
    void rebind (const juce::String& amountParamId,
                 const juce::String& effectName,
                 int slotIndex)
    {
        amountAtt.reset();
        setTitle (effectName);
        slotLabel.setText ("Chain position " + juce::String (slotIndex + 1),
                           juce::dontSendNotification);
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, amountParamId, amount);
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        slotLabel.setBounds (area.removeFromTop (18));

        const int knobSize = juce::jmin (area.getWidth(), area.getHeight() - 24, 180);
        auto knobArea = area.withSizeKeepingCentre (knobSize, knobSize + 24);
        amount.setBounds (knobArea.removeFromTop (knobSize + 20));
        amountLabel.setBounds (knobArea);
    }

    juce::AudioProcessorValueTreeState& apvts;

    juce::Slider amount;
    juce::Label  amountLabel, slotLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectDetailPage)
};
