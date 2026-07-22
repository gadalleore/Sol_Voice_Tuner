/*
    SolPage.h
    ---------
    Base page for the Xbox-style paging UI (63C-6): header with a back button
    and title, plus INPUT / OUTPUT edge captions — signal enters at the top of
    every page and leaves at the bottom, matching the Home wheel orientation.
    Subclasses lay out their controls in layoutContent().
*/

#pragma once

#include <JuceHeader.h>

#include "PageStack.h"
#include "SolLookAndFeel.h"

class SolPage : public juce::Component
{
public:
    SolPage (PageStack& stackToUse, const juce::String& titleText)
        : stack (stackToUse)
    {
        title.setText (titleText, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
        addAndMakeVisible (title);

        backBtn.onClick = [this] { stack.pop(); };
        addAndMakeVisible (backBtn);

        styleEdgeCaption (inCaption,  "INPUT");
        styleEdgeCaption (outCaption, "OUTPUT");
    }

    void resized() final
    {
        auto r = getLocalBounds();

        auto header = r.removeFromTop (44);
        backBtn.setBounds (header.removeFromLeft (86).reduced (10, 9));
        header.removeFromRight (86); // keep the title optically centred
        title.setBounds (header);

        inCaption .setBounds (r.removeFromTop (16));
        outCaption.setBounds (r.removeFromBottom (16));

        layoutContent (r.reduced (14, 6));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

        g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.55f));
        g.fillRect (getLocalBounds().removeFromTop (44));
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.35f));
        g.fillRect (0, 43, getWidth(), 1);
    }

protected:
    /** Subclasses place their controls inside the given content area. */
    virtual void layoutContent (juce::Rectangle<int> area) = 0;

    PageStack& stack;

private:
    void styleEdgeCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        addAndMakeVisible (l);
    }

    juce::Label      title;
    juce::TextButton backBtn { "< Back" };
    juce::Label      inCaption, outCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolPage)
};
