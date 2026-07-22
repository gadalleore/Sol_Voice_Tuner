/*
    HomePage.h
    ----------
    Window 1 of the paging UI: the Home wheel (63C-11 — wheels all the way
    down). The three destinations sit ON the wheel as fixed drill-in items,
    top to bottom in signal order — Input Global Effects, Harmonies/Tuning,
    Output Global Effects — and the wheel glides with hover like every other
    wheel. Items are not draggable here; clicking drills in.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"
#include "WheelComponent.h"

class HomePage final : public juce::Component
{
public:
    std::function<void()> onInputFx, onHarmonies, onOutputFx;

    HomePage()
    {
        wheel.setNumSlots (3);
        wheel.setPillSize (196.0f, 40.0f);
        wheel.emptyTypeId    = -1;      // every slot is always occupied
        wheel.itemsDraggable = false;

        wheel.getSlotType  = [] (int slot) { return slot; };
        wheel.nameProvider = [] (int typeId) -> juce::String
        {
            switch (typeId)
            {
                case 0:  return "Input Global Effects";
                case 1:  return "Harmonies / Tuning";
                case 2:  return "Output Global Effects";
                default: return {};
            }
        };
        wheel.onSlotClicked = [this] (int slot)
        {
            if (slot == 0 && onInputFx)   onInputFx();
            if (slot == 1 && onHarmonies) onHarmonies();
            if (slot == 2 && onOutputFx)  onOutputFx();
        };
        addAndMakeVisible (wheel);

        styleCaption (inCaption,  "INPUT");
        styleCaption (outCaption, "OUTPUT");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 8);

        inCaption .setBounds (r.removeFromTop (18));
        outCaption.setBounds (r.removeFromBottom (18));

        wheel.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));
    }

private:
    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.6f));
        addAndMakeVisible (l);
    }

    WheelComponent wheel;
    juce::Label    inCaption, outCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HomePage)
};
