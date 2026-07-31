/*
    EdgeMeters.h
    ------------
    Always-on output metering, pinned to the plate's right edge (Gard,
    2026-07-31). Deliberately the least UI a meter can be:

      - no frame, no track, no scale, no caption. Nothing is drawn for a
        channel that is silent, so at rest the strip is simply not there;
      - black ink for the signal, the plate's own text colour, so a running
        meter reads as part of the type rather than as a widget;
      - red ONLY above 0 dBFS. The bar does not change colour when it peaks —
        the slice standing above the clip line does, and nothing else;
      - a peak-hold tick per channel: the mark jumps to each new peak with the
        bar, then falls away slower, so a transient stays readable for a moment
        after the bar has dropped out from under it. Black at every level, clip
        line included — the red is reserved for live overshoot;
      - the same rainbow dither the wheel's labels leave behind them, streaked
        off the top edge of each bar. Same rule as everywhere else on the
        plate: it is a consequence of MOVEMENT, so a held level shows none of
        it and a falling one drags a smear.

    Levels arrive from the editor's existing scope timer (see PluginEditor's
    timerCallback), so this owns no timer and never touches the audio thread.
    Ballistics below are per frame at that timer's rate — retune them together
    with kScopeFps, not separately.
*/

#pragma once

#include <JuceHeader.h>

#include "SolDither.h"
#include "SolLookAndFeel.h"
#include "SpectrumStrip.h"

class EdgeMeters final : public juce::Component,
                         public SpectrumStrip::Inkable
{
public:
    /** One bar. Doubles as the unit the plate insets the strip by, so the
        clearance around the meters tracks their own weight. */
    static constexpr int kBarWidth = 18;

    /** Width the strip wants: two bars and the gap between them. */
    static constexpr int kWidth = kBarWidth * 2 + 6;

    EdgeMeters()
    {
        // Decoration only. The plate underneath is draggable, and swallowing
        // clicks here would punch a dead strip into the side of the window.
        setInterceptsMouseClicks (false, false);
    }

    /** One UI frame of post-chain peak level per channel, linear gain.
        Instant attack — a transient is on screen the frame it lands — then a
        hold just long enough to make a single clipped block visible, then an
        exponential release that drains to nothing and empties the strip. */
    void push (float peakL, float peakR)
    {
        const float in[2] { peakL, peakR };
        bool changed = false;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float wasLevel = level[ch];
            const float wasMark  = mark[ch];
            const float wasFade  = markFade[ch];

            if (in[ch] >= level[ch])
            {
                level[ch] = in[ch];
                hold[ch]  = kHoldFrames;
            }
            else if (hold[ch] > 0)
            {
                --hold[ch];
            }
            else
            {
                level[ch] *= kRelease;

                // Below the scale's floor there is nothing left to draw, so
                // snap to zero rather than repainting an invisible bar forever.
                if (dbNorm (level[ch]) <= 0.0f)
                    level[ch] = 0.0f;
            }

            // The tick takes every new high instantly and never sits below the
            // bar; left alone it sinks at its own rate and dissolves as it goes.
            if (level[ch] >= mark[ch])
            {
                mark[ch]     = level[ch];
                markHold[ch] = kMarkHoldFrames;
                markFade[ch] = 1.0f;
            }
            else if (markHold[ch] > 0)
            {
                --markHold[ch];
            }
            else
            {
                mark[ch]     *= kMarkRelease;
                markFade[ch] *= kMarkFade;

                // Gone once it is too faint to read, without waiting for the
                // position to crawl all the way down to the floor.
                if (markFade[ch] <= kMarkMinAlpha || dbNorm (mark[ch]) <= 0.0f)
                {
                    mark[ch]     = 0.0f;
                    markFade[ch] = 0.0f;
                }
            }

            changed = changed
                   || level[ch]    != wasLevel
                   || mark[ch]     != wasMark
                   || markFade[ch] != wasFade;
        }

        if (changed)
            repaint();
    }

    void paint (juce::Graphics& g) override { draw (g, nullptr); }

    /** The bars are the same black as the spectrum's, so where the two cross
        they would simply merge. Knocked out white instead, like the type. */
    void paintInk (juce::Graphics& g, juce::Colour ink) override { draw (g, &ink); }

private:
    /** `flat` non-null replaces every colour, and suppresses the trail — a
        stencil of where the bars are, not a second rendering of them. */
    void draw (juce::Graphics& g, const juce::Colour* flat)
    {
        const auto  area = getLocalBounds().toFloat();
        const float barW = (area.getWidth() - kBarGap) * 0.5f;

        if (barW <= 0.0f)
            return;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float norm     = dbNorm (level[ch]);
            const float markNorm = dbNorm (mark[ch]);

            // Silent channel: draw absolutely nothing.
            if (norm <= 0.0f && markNorm <= 0.0f)
            {
                if (flat == nullptr)
                    trailFor (ch).clearQuick();

                continue;
            }

            const juce::Rectangle<float> col (area.getX() + (float) ch * (barW + kBarGap),
                                              area.getY(), barW, area.getHeight());

            const float top   = col.getBottom() - norm      * col.getHeight();
            const float clipY = col.getBottom() - kClipNorm * col.getHeight();

            if (norm > 0.0f)
            {
                // Rainbow dither over the ground the top edge has just given up.
                if (flat == nullptr)
                    paintTrail (g, col, top, trailFor (ch));

                // Ink up to the clip line...
                g.setColour (flat != nullptr ? *flat
                                             : juce::Colour (SolLookAndFeel::kTitleHi));
                g.fillRect (col.withTop (juce::jmax (top, clipY)));

                // ...and red for the overshoot alone, if there is any.
                if (top < clipY)
                {
                    g.setColour (flat != nullptr ? *flat : juce::Colour (kClipRed));
                    g.fillRect (col.withTop (top).withBottom (clipY));
                }
            }

            // The peak-hold tick, trailing above the bar on its slower fall.
            if (markNorm > 0.0f)
            {
                const float markY = col.getBottom() - markNorm * col.getHeight();

                g.setColour ((flat != nullptr ? *flat
                                              : juce::Colour (SolLookAndFeel::kTitleHi))
                                 .withAlpha (juce::jlimit (0.0f, 1.0f, markFade[ch])));
                g.fillRect (col.withY (juce::jmin (markY, col.getBottom() - kMarkThickness))
                               .withHeight (kMarkThickness));
            }
        }
    }

    /** Linear gain -> 0..1 up the bar, over a -60..+6 dB scale. Returns 0 at
        and below the floor, which is what makes silence draw nothing. */
    static float dbNorm (float lin) noexcept
    {
        const float db = juce::Decibels::gainToDecibels (lin, kFloorDb);
        return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kCeilDb - kFloorDb));
    }

    //==========================================================================
    // Motion trails — the wheel's treatment (WheelComponent::paintTrail),
    // applied to the one part of a meter that actually moves: the top edge.
    //==========================================================================
    juce::Array<float>& trailFor (int ch)
    {
        return trails[(size_t) juce::jlimit (0, 1, ch)];
    }

    /** Records where the bar's top edge is now and streaks the head shape back
        over where it has just been. A held level travels nowhere and so draws
        nothing at all. */
    void paintTrail (juce::Graphics& g, juce::Rectangle<float> col,
                     float top, juce::Array<float>& history)
    {
        if (history.isEmpty() || std::abs (top - history.getLast()) > kTrailMinStep)
        {
            history.add (top);

            while (history.size() > kTrailLength)
                history.remove (0);
        }
        else if (history.size() > 1)
        {
            // Standing still: collapse the streak back into the bar fast — two
            // frames' worth per frame, so it is gone in a blink.
            history.remove (0);

            if (history.size() > 1)
                history.remove (0);
        }

        // The streak spans the whole distance travelled, not one frame's worth.
        const float displacement = history.getFirst() - top;

        if (std::abs (displacement) < kTrailMinSmear)
            return;

        // Only the head smears. Streaking the whole bar would stamp a column
        // of stipple down the plate every time the level moved.
        juce::Path head;
        head.addRectangle (col.withTop (top)
                              .withHeight (juce::jmin (kTrailHeadHeight, col.getBottom() - top)));

        SolDither::streakRgb (g, head, { 0.0f, displacement }, kTrailSteps, kTrailAlpha);
    }

    static constexpr float kFloorDb  = -60.0f;
    static constexpr float kCeilDb   =   6.0f;

    /** Height of the 0 dBFS line — everything above it is the red slice. */
    static constexpr float kClipNorm = (0.0f - kFloorDb) / (kCeilDb - kFloorDb);

    /** Kept in step with kWidth above — paint derives the bar width from the
        strip it is given, so these two have to agree. */
    static constexpr float kBarGap   = 6.0f;

    // Bar ballistics. ~60 dB/s of release at 60 Hz, and a hold short enough to
    // read as the meter answering the audio rather than lagging behind it.
    static constexpr float kRelease    = 0.89f;   // per frame
    static constexpr int   kHoldFrames = 4;       // ~65 ms at 60 Hz

    /** Peak-hold ticks: still slower than the bar, by enough that the mark
        reads as its own object, but out of the way quickly once it starts to
        drop — it sinks and dissolves at the same time. */
    static constexpr float kMarkRelease    = 0.955f;  // ~25 dB/s at 60 Hz
    static constexpr float kMarkFade       = 0.93f;
    static constexpr float kMarkMinAlpha   = 0.06f;
    static constexpr int   kMarkHoldFrames = 12;      // ~0.2 s at 60 Hz
    static constexpr float kMarkThickness  = 2.0f;

    // Motion trails, matched to the wheel's so the plate smears as one piece.
    static constexpr int   kTrailLength     = 8;      // frames of travel retained
    static constexpr int   kTrailSteps      = 9;      // stamps along the streak
    static constexpr float kTrailMinStep    = 0.9f;   // px before a ghost is kept
    static constexpr float kTrailMinSmear   = 2.5f;   // px of travel before drawing
    static constexpr float kTrailAlpha      = 0.75f;
    static constexpr float kTrailHeadHeight = 6.0f;   // px of bar top that smears

    /** Warm, not signal-red: it has to sit on a white plate without shouting. */
    static constexpr juce::uint32 kClipRed = 0xffd8261c;

    float level[2] { 0.0f, 0.0f };
    int   hold[2]  { 0, 0 };

    float mark[2]     { 0.0f, 0.0f };
    float markFade[2] { 0.0f, 0.0f };
    int   markHold[2] { 0, 0 };

    std::array<juce::Array<float>, 2> trails;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeMeters)
};
