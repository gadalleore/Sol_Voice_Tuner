/*
    IconButton.h
    ------------
    A button that carries one of `SolIcons`' vector glyphs beside its label.

    Wears the same state rule as everything else on the panel (see CLAUDE.md,
    "Controls"): off is a quiet surface, on is a solid accent chip with the
    content knocked out of it. The icon is stroked in whatever colour the text
    is, so the two always agree — a glyph that stays bright while its label
    goes dark reads as a rendering fault.

    Works both as a momentary action (the nav strip) and as a parameter toggle
    (Bypass, MIDI): pass `toggles` and attach it with a normal
    `AudioProcessorValueTreeState::ButtonAttachment`, which takes a juce::Button
    and so accepts this directly.
*/

#pragma once

#include <JuceHeader.h>

#include "SolIcons.h"
#include "SolLookAndFeel.h"

class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& text, juce::Path glyphToUse, bool toggles = false)
        : juce::Button (text), glyph (std::move (glyphToUse))
    {
        setButtonText (text);
        setClickingTogglesState (toggles);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        // The host needs the keyboard for its transport; FloatingShell refuses
        // focus tree-wide, but a button used outside that shell should behave.
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    /** Point size of the label. */
    void setFontHeight (float h) { fontHeight = juce::jmax (1.0f, h); repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b  = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        const float a = isEnabled() ? 1.0f : 0.45f;

        if (on)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.18f * a));
            g.fillRoundedRectangle (b.expanded (2.5f), 5.5f);
            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (a));
            g.fillRoundedRectangle (b, 3.0f);
        }
        else
        {
            g.setColour (juce::Colour ((highlighted || down) ? SolLookAndFeel::kPanelLight
                                                             : SolLookAndFeel::kPanel)
                             .withAlpha (a));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (juce::Colour ((highlighted || down) ? SolLookAndFeel::kOutlineHi
                                                             : SolLookAndFeel::kOutline)
                             .withAlpha (a));
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
        }

        const auto ink = juce::Colour (on ? SolLookAndFeel::kBackground
                                          : SolLookAndFeel::kValue).withAlpha (a);

        // Icon and label are measured together and centred as one block, so a
        // short label does not leave the pair drifting left in a wide button.
        const auto  text  = getButtonText();
        const float iconH = juce::jmin (b.getHeight() - 8.0f, fontHeight + 4.0f);
        const float textW = text.isEmpty()
                              ? 0.0f
                              : juce::GlyphArrangement::getStringWidth (labelFont(), text);
        const float gap   = text.isEmpty() ? 0.0f : 6.0f;
        const float total = iconH + gap + textW;

        auto cursor = b.withSizeKeepingCentre (juce::jmin (total, b.getWidth() - 8.0f),
                                               b.getHeight());

        const auto iconArea = cursor.removeFromLeft (iconH);

        if (! glyph.isEmpty())
        {
            g.setColour (ink);
            g.strokePath (SolIcons::fitted (glyph, iconArea.reduced (1.0f)),
                          juce::PathStrokeType (SolIcons::strokeFor (iconArea),
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        }

        if (text.isNotEmpty())
        {
            cursor.removeFromLeft (gap);
            g.setColour (ink);
            g.setFont (labelFont());
            g.drawText (text, cursor, juce::Justification::centredLeft, false);
        }
    }

private:
    juce::Font labelFont() const
    {
        return juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                              fontHeight, juce::Font::plain));
    }

    juce::Path glyph;
    float      fontHeight = 12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
