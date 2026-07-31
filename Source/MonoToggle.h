/*
    MonoToggle.h
    ------------
    A toggle that is nothing but its own name (Gard, 2026-07-31).

        off   the words in black, on bare plate. No box, no check, no lamp —
              at rest it reads as a label, because an unset switch has nothing
              to announce;
        on    the same words knocked out white inside a black box, so the state
              is legible across the room and unmistakably deliberate.

    Times New Roman rather than the brand face: this is the one piece of the
    interface that behaves like set type instead of like an instrument, and the
    serif is what makes it read that way.

    The box is drawn around the laid-out glyphs, not around the component, so
    it hugs whatever word the button is given.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class MonoToggle final : public juce::Button
{
public:
    static constexpr int kHeight = 36;

    explicit MonoToggle (const juce::String& text)
        : juce::Button (text)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        // Buttons ask for the keyboard by default. Ours must not: the host
        // needs it for the transport. FloatingShell::refuseKeyboardFocus does
        // this for the whole tree anyway — kept here so a MonoToggle used
        // outside that shell still behaves.
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    /** Width the words need at their full size, plus the box around them.

        Measured rather than guessed: addFittedText SHRINKS type that will not
        fit, so a component sized by eye would silently scale the label off the
        size it is supposed to share with Volume. */
    int getPreferredWidth() const
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (labelFont(), getButtonText(), 0.0f, 0.0f);

        return juce::roundToInt (glyphs.getBoundingBox (0, -1, true).getWidth()
                                     + kPadX * 2.0f) + 2;
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat();
        const bool on   = getToggleState();

        // Laid out once: the fill needs the glyphs' extent, and drawing from
        // the same arrangement guarantees the box and the word cannot disagree.
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (labelFont(), getButtonText(),
                              area.getX(), area.getY(),
                              area.getWidth(), area.getHeight(),
                              juce::Justification::centred, 1);

        if (on)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.fillRect (glyphs.getBoundingBox (0, -1, true).expanded (kPadX, kPadY));
        }
        else if (highlighted)
        {
            // Off and under the pointer: the only hint that the label is live.
            g.setColour (juce::Colour (SolLookAndFeel::kOutline));
            g.fillRect (glyphs.getBoundingBox (0, -1, true).expanded (kPadX, kPadY));
        }

        g.setColour (on ? juce::Colour (SolLookAndFeel::kBackground)
                        : juce::Colour (SolLookAndFeel::kTitleHi));
        glyphs.draw (g);
    }

private:
    static juce::Font labelFont()
    {
        return juce::Font (juce::FontOptions (kTypeface, kFont, juce::Font::plain));
    }

    static constexpr const char* kTypeface = "Times New Roman";

    /** Set to match VolumeArc::kLabelFont — the two sit one above the other in
        the column and have to read as the same size of type. */
    static constexpr float kFont = 22.0f;

    static constexpr float kPadX = 9.0f;
    static constexpr float kPadY = 5.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonoToggle)
};
