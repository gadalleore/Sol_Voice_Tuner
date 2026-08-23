/*
    SolPage.h
    ---------
    Base page for the Xbox-style paging UI (63C-6): title plus INPUT / OUTPUT
    edge captions — signal enters at the top of every page and leaves at the
    bottom, matching the Home wheel orientation.

    The face is the Home page's face (Giuseppe, 2026-08-16). There is no title
    PANE: no filled header, no divider rule, no panel behind anything. Black ink
    on the white plate, brand typeface, exactly like the wheel's own items — a
    drilled-in page should read as the same object as the page it came from, not
    as a dialog that opened on top of it.

    Back is a word, not a bar. The full-height strip it replaced (63C-17) sat
    hard against x = 0, and the plate lets its content bleed to the window edge,
    so the strip ran underneath the border and off the glass. The word sits
    inside the same left inset everything else respects.

    Subclasses lay out their controls in layoutContent().
*/

#pragma once

#include <JuceHeader.h>

#include "PageStack.h"
#include "SolLookAndFeel.h"

/** Implemented by a page that wants the WINDOW sized to it.

    Sol's UI is its own desktop window rather than the host's rectangle, so it
    can change shape without renegotiating anything with the DAW — which makes
    per-page sizing practical here in a way it is not for an ordinary plugin.
    A Lo-Fi with two controls and a Trance Gate with twenty-three do not want
    the same panel.

    Returns the LOGICAL size (the design size the content is laid out at);
    FloatingShell keeps whatever zoom the user has applied on top of it. */
struct SizedPage
{
    virtual ~SizedPage() = default;
    virtual juce::Point<int> preferredLogicalSize() const = 0;
};

class SolPage : public juce::Component
{
public:
    SolPage (PageStack& stackToUse, const juce::String& titleText)
        : stack (stackToUse)
    {
        setOpaque (true);   // 63C-17: pages fully cover whatever is beneath

        // Same ink, same typeface and the same weight of presence as a Home
        // wheel item — the title is a word on the plate, not a header.
        title.setText (titleText, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centredLeft);
        title.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                      kTitleHeight, juce::Font::plain)));
        title.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
        addAndMakeVisible (title);

        back.onClick = [this] { stack.pop(); };
        addAndMakeVisible (back);

        styleEdgeCaption (inCaption,  "INPUT");
        styleEdgeCaption (outCaption, "OUTPUT");
    }

    void resized() final
    {
        auto r = getLocalBounds().reduced (kEdgeInset, 0);

        auto header = r.removeFromTop (kHeaderHeight);
        back .setBounds (header.removeFromLeft (kBackWidth)
                               .withSizeKeepingCentre (kBackWidth, kBackHeight));
        header.removeFromLeft (kEdgeInset);
        title.setBounds (header);

        inCaption .setBounds (r.removeFromTop (16));
        outCaption.setBounds (r.removeFromBottom (16));

        layoutContent (r.reduced (0, 6));
    }

    void paint (juce::Graphics& g) override
    {
        // Bare plate. No header fill, no rule under the title — the page is the
        // same white surface the Home wheel sits on.
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));
    }

protected:
    /** Left/right breathing room. The plate lets content bleed to the window
        edge, so a page has to keep itself off the border. */
    static constexpr int kEdgeInset   = 18;
    static constexpr int kHeaderHeight = 40;
    static constexpr int kBackWidth   = 62;
    static constexpr int kBackHeight  = 26;

    static constexpr float kTitleHeight = 22.0f;

    /** Subclasses place their controls inside the given content area. */
    virtual void layoutContent (juce::Rectangle<int> area) = 0;

    /** For pages that rebind to different content (e.g. EffectDetailPage). */
    void setTitle (const juce::String& t) { title.setText (t, juce::dontSendNotification); }

    PageStack& stack;

private:
    //==========================================================================
    /** Back, as a word on the plate: a chevron and "BACK" in the brand face,
        no panel and no border behind it. Greys on hover, the same way the
        wheel's items do. */
    class BackWord final : public juce::Component
    {
    public:
        std::function<void()> onClick;

        BackWord() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            const bool hot = isMouseOverOrDragging();

            g.setColour (juce::Colour (hot ? kHoverInk : SolLookAndFeel::kTitleHi));

            const float cy = (float) getHeight() * 0.5f;

            juce::Path chevron;
            chevron.startNewSubPath (9.0f, cy - 5.0f);
            chevron.lineTo          (3.0f, cy);
            chevron.lineTo          (9.0f, cy + 5.0f);
            g.strokePath (chevron, juce::PathStrokeType (1.8f,
                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
            g.drawText ("BACK", getLocalBounds().withTrimmedLeft (15),
                        juce::Justification::centredLeft);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
                onClick();
        }

    private:
        /** Same accent the wheel uses for a hovered item — hover lights up on
            the night panel rather than greying out (2026-08-22). */
        static constexpr juce::uint32 kHoverInk = SolLookAndFeel::kAccentArc;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackWord)
    };

    void styleEdgeCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 10.5f, juce::Font::plain)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        addAndMakeVisible (l);
    }

    juce::Label title;
    BackWord    back;
    juce::Label inCaption, outCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolPage)
};
