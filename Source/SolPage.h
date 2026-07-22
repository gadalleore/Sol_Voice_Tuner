/*
    SolPage.h
    ---------
    Base page for the Xbox-style paging UI (63C-6): title header plus
    INPUT / OUTPUT edge captions — signal enters at the top of every page and
    leaves at the bottom, matching the Home wheel orientation.

    63C-17: back is a slim full-height VERTICAL BAR on the left edge of the
    window (per Gard's review) — the wheel's flat edge starts just to its
    right. Every drilled-in page gets the bar automatically; Home is not a
    SolPage, so the root shows nothing.

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
        setOpaque (true);   // 63C-17: pages fully cover whatever is beneath

        title.setText (titleText, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
        addAndMakeVisible (title);

        backBar.onClick = [this] { stack.pop(); };
        addAndMakeVisible (backBar);

        styleEdgeCaption (inCaption,  "INPUT");
        styleEdgeCaption (outCaption, "OUTPUT");
    }

    void resized() final
    {
        auto r = getLocalBounds();

        // Slim full-height back bar hugging the window's left edge; the
        // wheel's flat edge starts to its right.
        backBar.setBounds (r.removeFromLeft (kBackBarWidth));

        auto header = r.removeFromTop (44);
        title.setBounds (header);

        inCaption .setBounds (r.removeFromTop (16));
        outCaption.setBounds (r.removeFromBottom (16));

        layoutContent (r.reduced (14, 6));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

        g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.55f));
        g.fillRect (getLocalBounds().removeFromTop (44).withTrimmedLeft (kBackBarWidth));
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.35f));
        g.fillRect (kBackBarWidth, 43, getWidth() - kBackBarWidth, 1);
    }

protected:
    static constexpr int kBackBarWidth = 34;

    /** Subclasses place their controls inside the given content area. */
    virtual void layoutContent (juce::Rectangle<int> area) = 0;

    /** For pages that rebind to different content (e.g. EffectDetailPage). */
    void setTitle (const juce::String& t) { title.setText (t, juce::dontSendNotification); }

    PageStack& stack;

private:
    //==========================================================================
    /** 63C-17: the vertical back bar — a slim strip down the window's left
        edge with a chevron and stacked B-A-C-K letters; highlights on hover,
        whole strip is the click target. */
    class BackBar final : public juce::Component
    {
    public:
        std::function<void()> onClick;

        BackBar() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            const bool hot = isMouseOverOrDragging();

            g.fillAll (juce::Colour (SolLookAndFeel::kPanel)
                           .withAlpha (hot ? 0.95f : 0.6f));
            g.setColour (juce::Colour (hot ? SolLookAndFeel::kOutlineHi
                                           : SolLookAndFeel::kAccentGlow)
                             .withAlpha (hot ? 0.9f : 0.45f));
            g.fillRect (getWidth() - 1, 0, 1, getHeight());

            g.setColour (juce::Colour (hot ? SolLookAndFeel::kTitleHi
                                           : SolLookAndFeel::kLabel));

            // Chevron above vertically stacked letters, optically centred.
            const float cx = (float) getWidth() * 0.5f;
            const float cy = (float) getHeight() * 0.5f;

            juce::Path chevron;
            chevron.startNewSubPath (cx + 4.0f, cy - 46.0f);
            chevron.lineTo          (cx - 4.0f, cy - 38.0f);
            chevron.lineTo          (cx + 4.0f, cy - 30.0f);
            g.strokePath (chevron, juce::PathStrokeType (2.2f,
                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            const juce::String letters ("BACK");
            for (int i = 0; i < letters.length(); ++i)
                g.drawText (letters.substring (i, i + 1),
                            juce::Rectangle<float> (0.0f, cy - 18.0f + (float) i * 15.0f,
                                                    (float) getWidth(), 15.0f),
                            juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
                onClick();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackBar)
    };

    void styleEdgeCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        addAndMakeVisible (l);
    }

    juce::Label title;
    BackBar     backBar;
    juce::Label inCaption, outCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolPage)
};
