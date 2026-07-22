/*
    HarmoniesWindowPage.h
    ---------------------
    Window 2 of the paging UI (63C-6 placeholder): starts with the Lead Voice
    on the wheel; harmony voices get pulled from the wheel centre when 63C-13
    lands. For now: the Lead Voice entry drills into its Tuning page.
*/

#pragma once

#include <JuceHeader.h>

#include "SolPage.h"

class HarmoniesWindowPage final : public SolPage
{
public:
    std::function<void()> onOpenTuning;

    HarmoniesWindowPage (PageStack& stackToUse)
        : SolPage (stackToUse, "Harmonies")
    {
        leadBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (SolLookAndFeel::kPanel));
        leadBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (SolLookAndFeel::kTitleHi));
        leadBtn.onClick = [this] { if (onOpenTuning) onOpenTuning(); };
        addAndMakeVisible (leadBtn);

        placeholder.setText ("Pull-out harmony voices arrive with 63C-13",
                             juce::dontSendNotification);
        placeholder.setJustificationType (juce::Justification::centred);
        placeholder.setFont (juce::Font (juce::FontOptions (12.5f)));
        placeholder.setColour (juce::Label::textColourId,
                               juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.55f));
        addAndMakeVisible (placeholder);
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        leadBtn.setBounds (area.withSizeKeepingCentre (juce::jmin (300, area.getWidth() - 20), 48));
        placeholder.setBounds (area.removeFromBottom (28));
    }

    juce::TextButton leadBtn { "Lead Voice  |  Tuning" };
    juce::Label      placeholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniesWindowPage)
};
