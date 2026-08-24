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
#include "SolPanel.h"

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

        addAndMakeVisible (inCaption);
        addAndMakeVisible (outCaption);
    }

    void resized() final
    {
        auto r = getLocalBounds().reduced (kEdgeInset, 0);

        auto header = r.removeFromTop (kHeaderHeight);
        back .setBounds (header.removeFromLeft (kBackWidth)
                               .withSizeKeepingCentre (kBackWidth, kBackHeight));
        header.removeFromLeft (kEdgeInset);
        title.setBounds (header);

        // The captions say where the signal enters and leaves THIS PAGE'S
        // chain, so they belong over the chain — not centred on a page whose
        // other half is an inspector the signal never passes through
        // (Giuseppe, 2026-08-23). Subclasses narrow the span; the default is
        // the whole width, which is right for a page that IS its content.
        const auto span = edgeCaptionSpan (r);

        inCaption .setBounds (r.removeFromTop (kCaptionHeight)
                                .withX (span.getStart()).withWidth (span.getLength()));
        outCaption.setBounds (r.removeFromBottom (kCaptionHeight)
                                .withX (span.getStart()).withWidth (span.getLength()));

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

    static constexpr float kTitleHeight = 27.0f;

    static constexpr int kCaptionHeight = 22;

    /** Subclasses place their controls inside the given content area. */
    virtual void layoutContent (juce::Rectangle<int> area) = 0;

    /** Horizontal span the INPUT / OUTPUT markers sit over, within the page's
        content area. Override to point them at the part of the page the signal
        actually flows through. */
    virtual juce::Range<int> edgeCaptionSpan (juce::Rectangle<int> content) const
    {
        return { content.getX(), content.getRight() };
    }

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

            // Back rides its own plate now (Giuseppe, 2026-08-23). As bare
            // type it was the one control on a panelled page still floating
            // on the surface, which made the most-used control look like the
            // least deliberate thing on it.
            SolPanel::draw (g, getLocalBounds().toFloat().reduced (1.0f, 2.0f), false, 5.0f);

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

    //==========================================================================
    /** IN and OUT, as a marker on the machine rather than a caption under it.

        These were 10.5pt grey type at half alpha, which on a panelled page is
        the quietest thing on the surface — so the one label that tells you
        which way the signal runs read as a footnote (Giuseppe, 2026-08-23).

        Now it is a plate with a rule running out of it to either side, and a
        chevron pointing the way the audio goes. Both chevrons point DOWN,
        because that is the whole claim: in at the top, out at the bottom. Drawn
        in kAccentCool, since this is about signal and cyan is what signal is. */
    class FlowCaption final : public juce::Component
    {
    public:
        FlowCaption (const juce::String& t) : text (t)
        {
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);

            if (r.getWidth() < 60.0f || r.getHeight() < 10.0f)
                return;

            // Amber, not cyan (Giuseppe, 2026-08-23). Cyan is reserved for
            // LIVE SIGNAL — the goniometer trace, the meters — and this is not
            // a live reading, it is signage: a permanent mark on the machine
            // saying which way the audio runs. Signage on this panel is amber
            // and black, the same family as the hazard notices, and a marker
            // in the live-signal colour that never moves reads as a meter that
            // is broken.
            const auto ink = juce::Colour (SolLookAndFeel::kAccentArc);

            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                      11.0f, juce::Font::bold)));

            const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            const auto  plate = r.withSizeKeepingCentre (textW + kPlatePad, r.getHeight());

            // The rule. It runs the full span and the plate sits ON it, so the
            // marker reads as a tap off a signal path rather than as a label
            // floating in space.
            g.setColour (ink.withAlpha (0.28f));
            g.fillRect (r.getX(), r.getCentreY() - 0.5f,
                        plate.getX() - r.getX() - kRuleGap, 1.0f);
            g.fillRect (plate.getRight() + kRuleGap, r.getCentreY() - 0.5f,
                        r.getRight() - plate.getRight() - kRuleGap, 1.0f);

            SolPanel::draw (g, plate, false, 5.0f);
            g.setColour (ink.withAlpha (0.55f));
            g.strokePath (SolPanel::plateShape (plate, 5.0f), juce::PathStrokeType (1.0f));

            g.setColour (ink);
            g.drawText (text, plate.toNearestInt(), juce::Justification::centred);

            // Chevrons on the rule either side, pointing the way it flows.
            for (const float cx : { plate.getX() - kRuleGap - kChevronOut,
                                    plate.getRight() + kRuleGap + kChevronOut })
                drawChevron (g, cx, r.getCentreY(), ink);
        }

    private:
        static void drawChevron (juce::Graphics& g, float cx, float cy, juce::Colour ink)
        {
            juce::Path p;
            p.startNewSubPath (cx - kChevronW, cy - kChevronW * 0.6f);
            p.lineTo          (cx,             cy + kChevronW * 0.6f);
            p.lineTo          (cx + kChevronW, cy - kChevronW * 0.6f);

            g.setColour (ink.withAlpha (0.7f));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        static constexpr float kPlatePad   = 22.0f;
        static constexpr float kRuleGap    =  7.0f;
        static constexpr float kChevronOut = 12.0f;
        static constexpr float kChevronW   =  4.0f;

        const juce::String text;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlowCaption)
    };

    juce::Label title;
    BackWord    back;
    FlowCaption inCaption  { "INPUT" };
    FlowCaption outCaption { "OUTPUT" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolPage)
};
