/*
    VolumeArc.h
    -----------
    The volume control, from Giuseppe's sketch (2026-07-31). No knob body, no
    track, no readout — the value IS the drawing:

        at minimum   a bare line, pointing where the control is set;
        turning up   the line stays on the pointer and an arc unrolls behind
                     it, so the ring closes as the level comes up;
        at maximum   very nearly a full circle.

    So there is nothing to read against — no empty track sitting there implying
    a value that has not been set. The mark on screen is only ever the amount
    of volume there actually is.

    A juce::Slider underneath, so drag, double-click-to-default, host automation
    and the APVTS attachment all behave the way they do everywhere else; only
    paint() is ours.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class VolumeArc final : public juce::Slider
{
public:
    static constexpr int kWidth = 92;

    /** Diameter of the dial, and the air between the arc and its label. The
        gap is half the column's spacing unit (Giuseppe, 2026-07-31): dial, label
        and the toggle under them are one group, and they sit tighter to each
        other than the group does to the bars and the mark below. */
    static constexpr float kDial        = 62.0f;
    static constexpr float kArcLabelGap = 15.0f;
    static constexpr float kLabelHeight = 28.0f;

    /** The stroke the height below is worked out at. The live weight is set by
        the plate; if it ever differs wildly from this the gap drifts by half
        the difference, which is not worth making the layout dynamic for. */
    static constexpr float kDesignStroke = 5.0f;

    /** Where the arc's lowest ink sits, measured from the top of the dial box.

        NOT the bottom of the dial's bounding box: the sweep breaks at 4:30 and
        7:30, so its lowest points are cos(135°) down from the centre, a good
        8px above where the circle would have ended. Measuring the gap from the
        box instead would leave the label looking further adrift than asked. */
    static constexpr float kArcBottom = kDial * 0.5f
                                      + (kDial * 0.5f - kDesignStroke * 0.5f) * 0.70711f
                                      + kDesignStroke * 0.5f;

    static constexpr int kHeight = (int) (kArcBottom + kArcLabelGap + kLabelHeight + 0.5f);

    VolumeArc()
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setDoubleClickReturnValue (true, 0.0);   // unity, not silence
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

        // Sliders ask for the keyboard by default. Ours must not: the host
        // needs it for the transport. FloatingShell::refuseKeyboardFocus does
        // this for the whole tree anyway — kept here so a VolumeArc used
        // outside that shell still behaves.
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    /** Weight of the arc and its pointer. Set from the plate so the dial is
        drawn with the same line as the border around it. */
    void setStrokeWeight (float w)
    {
        stroke = juce::jmax (0.5f, w);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        if (kDial <= stroke * 2.0f)
            return;

        // The dial hangs from the top of the box and the label from the
        // bottom; the air between them is baked into kHeight, so neither has
        // to be positioned against the other here.
        const auto labelRow = b.withTop (b.getBottom() - kLabelHeight);

        const juce::Point<float> centre { b.getCentreX(), b.getY() + kDial * 0.5f };
        const float r = kDial * 0.5f - stroke * 0.5f;

        // Where the pointer stands: the parameter's own range, not the raw
        // value, so the skew on the volume curve is respected.
        const auto  range = getRange();
        const float t     = range.getLength() > 0.0
                              ? (float) ((getValue() - range.getStart()) / range.getLength())
                              : 0.0f;
        const float angle = kStartAngle + juce::jlimit (0.0f, 1.0f, t)
                                              * (kEndAngle - kStartAngle);

        // The sweep so far. At the bottom of the range there is none, and the
        // pointer below is the entire control. Amber, with a bloom under it —
        // the value colour everywhere on the night panel (2026-08-22).
        if (angle - kStartAngle > kMinSweep)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, r, r,
                               0.0f, kStartAngle, angle, true);

            g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.20f));
            g.strokePath (arc, juce::PathStrokeType (stroke * 2.1f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
            g.strokePath (arc, juce::PathStrokeType (stroke,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // The pointer: the full radius, centre out to the rim, so it reads as
        // a hand on a dial rather than a tick on a rim.
        const float sinA = std::sin (angle);
        const float cosA = std::cos (angle);

        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.drawLine ({ centre.x, centre.y,
                      centre.x + sinA * r,
                      centre.y - cosA * r },
                    stroke);

        g.setColour (juce::Colour (SolLookAndFeel::kLabel));
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  kLabelFont,
                                                  juce::Font::plain)));
        g.drawText ("Volume", labelRow, juce::Justification::centred, false);
    }

private:
    /** Three quarters of a turn with the gap at the bottom — angles run
        clockwise from 12 o'clock, which is what addCentredArc expects. */
    static constexpr float kStartAngle = -2.356f;   // 7:30
    static constexpr float kEndAngle   =  2.356f;   // 4:30

    /** Below this the arc is shorter than its own rounded cap, and drawing it
        just thickens the pointer's root. */
    static constexpr float kMinSweep = 0.06f;

    static constexpr float kLabelFont = 22.0f;

    /** Overwritten by the plate at construction; this is only the fallback for
        a VolumeArc used on its own. */
    float stroke = 4.8f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VolumeArc)
};
