/*
    SignalMeter.h
    -------------
    A thin vertical level meter, for flanking a module with what goes INTO it
    and what comes OUT (Giuseppe, 2026-08-23).

    The point is not precision — there is a proper stereo meter on the plate for
    that. The point is that an effect page should tell you, without you having to
    listen for it, that signal is arriving and that something is leaving. A muted
    Reverb and a Reverb with nothing feeding it look identical on a page of
    knobs; they do not look identical with a meter on each side.

    So this reads as one column of light rather than as an instrument:

      * a recessed track, drawn only as a hairline — "nothing draws what isn't
        there", so silence is an empty channel, not a row of dark segments;
      * segments rather than a continuous bar, because a segmented column reads
        as a METER at a glance where a plain fill reads as a progress bar;
      * cyan at the bottom running to amber and then to the clip red at the top,
        which is the palette's own vocabulary: cyan is live signal, and red is
        above 0 dBFS and nothing else.

    Levels come in as linear peak and are shown on a dB scale, because a linear
    meter spends four fifths of its travel in the top 6 dB and reads as either
    off or pinned.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class SignalMeter final : public juce::Component
{
public:
    explicit SignalMeter (const juce::String& captionText) : caption (captionText)
    {
        setInterceptsMouseClicks (false, false);
    }

    /** Linear peak, 0..1+. Repaints only on a visible change — this is driven
        from a UI timer and the page has a ring animating beside it. */
    void setLevel (float linearPeak)
    {
        const float d = toDisplay (linearPeak);

        if (std::abs (d - shown) > 0.004f)
        {
            shown = d;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // Caption first: the meter is useless if you can't tell which side of
        // the effect it is.
        auto capArea = r.removeFromBottom (kCaptionH);
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  9.0f, juce::Font::plain)));
        g.drawText (caption, capArea.toNearestInt(), juce::Justification::centred);

        const auto track = r.reduced ((r.getWidth() - kTrackW) * 0.5f, 2.0f);

        if (track.getHeight() < kSegment * 2.0f)
            return;

        // The channel. A hairline, not a filled well — an empty meter should
        // read as nothing rather than as a bar sitting at zero.
        g.setColour (juce::Colour (SolLookAndFeel::kOutline));
        g.drawRoundedRectangle (track, 1.5f, 1.0f);

        if (shown <= 0.001f)
            return;

        const int   rows = juce::jmax (1, (int) (track.getHeight() / kSegment));
        const float lit  = shown * (float) rows;

        for (int i = 0; i < rows; ++i)
        {
            const float amount = juce::jlimit (0.0f, 1.0f, lit - (float) i);
            if (amount <= 0.0f)
                break;

            // Position of this segment up the scale, which is what picks the
            // colour — the top of the meter is red whether or not you are on it.
            const float up = (float) i / (float) juce::jmax (1, rows - 1);

            const auto ink = up > kHotFrom
                               ? juce::Colour (SolLookAndFeel::kClip)
                               : juce::Colour (SolLookAndFeel::kAccentCool)
                                     .interpolatedWith (juce::Colour (SolLookAndFeel::kAccentArc),
                                                        juce::jlimit (0.0f, 1.0f, up / kHotFrom));

            const float y = track.getBottom() - (float) (i + 1) * kSegment;
            const auto  seg = juce::Rectangle<float> (track.getX() + 1.0f, y + kSegGap,
                                                      track.getWidth() - 2.0f,
                                                      kSegment - kSegGap * 2.0f);

            // The topmost lit segment fades in rather than snapping, so the
            // column moves smoothly instead of stepping.
            g.setColour (ink.withMultipliedAlpha (0.35f + 0.65f * amount));
            g.fillRect (seg);
        }
    }

    /** Room the meter needs beside a module. */
    static constexpr int kWidth    = 26;
    static constexpr int kCaptionH = 13;

private:
    /** Linear peak -> 0..1 up a dB scale. kFloorDb is the bottom of the
        column; above 0 dBFS the meter is already in the red band. */
    static float toDisplay (float linearPeak) noexcept
    {
        if (linearPeak <= 1.0e-5f)
            return 0.0f;

        const float db = juce::Decibels::gainToDecibels (linearPeak, kFloorDb);
        return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kCeilDb - kFloorDb));
    }

    static constexpr float kFloorDb = -54.0f;
    static constexpr float kCeilDb  =   6.0f;

    /** Where the column stops being "signal" and starts being "too much".
        -54..+6 dB over the track, so 0 dBFS sits at 0.9 of the travel. */
    static constexpr float kHotFrom = 0.9f;

    static constexpr float kTrackW  = 11.0f;
    static constexpr float kSegment =  5.0f;
    static constexpr float kSegGap  =  0.9f;

    const juce::String caption;
    float shown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalMeter)
};
