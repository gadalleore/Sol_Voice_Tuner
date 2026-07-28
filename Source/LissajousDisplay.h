/*
    LissajousDisplay.h
    ------------------
    X/Y stereo phase display (goniometer) fed from the processor's scope
    snapshot. Extracted from MeterSidebar (63C-18) on 2026-07-28 so the wheel
    hub can show one too.

    Rotated 45 degrees like a studio goniometer: a mono signal draws a vertical
    line, anti-phase draws a horizontal one.

    Styling is optional so the same element works in two places: the metering
    sidebar wants a panel, a grid and a frame; the wheel hub wants a bare trace
    on a transparent background.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class LissajousDisplay final : public juce::Component
{
public:
    LissajousDisplay()
    {
        // A bare trace by default; it is a decoration, not a control.
        setInterceptsMouseClicks (false, false);
    }

    //--------------------------------------------------------------------------
    // Appearance
    //--------------------------------------------------------------------------
    void setTraceColour (juce::Colour c)   { traceColour = c; repaint(); }
    void setTraceAlpha (float a)           { traceAlpha = juce::jlimit (0.0f, 1.0f, a); repaint(); }
    void setTraceThickness (float t)       { traceThickness = juce::jmax (0.2f, t); repaint(); }

    /** Panel fill + rounded frame around the display. */
    void setShowFrame (bool shouldShow)    { showFrame = shouldShow; repaint(); }

    /** Centre cross-hairs. */
    void setShowGrid (bool shouldShow)     { showGrid = shouldShow; repaint(); }

    //--------------------------------------------------------------------------
    /** Feeds a new scope snapshot. Safe to call from a UI timer. */
    void update (const juce::AudioBuffer<float>& src, int validSamples)
    {
        const int n = validSamples > 0 ? juce::jmin (validSamples, src.getNumSamples())
                                       : src.getNumSamples();

        if (n <= 0 || src.getNumChannels() < 1)
            return;

        const int chs = src.getNumChannels();
        points.clearQuick();

        const float* l = src.getReadPointer (0);
        const float* r = src.getReadPointer (chs > 1 ? 1 : 0);

        const int stride = juce::jmax (1, n / kMaxPoints);

        for (int i = 0; i < n; i += stride)
            points.add ({ l[i], r[i] });

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (1.0f);

        if (area.isEmpty())
            return;

        if (showFrame)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kPanel));
            g.fillRoundedRectangle (area, 3.0f);
        }

        if (showGrid)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.6f));
            g.drawLine (area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom(), 0.5f);
            g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 0.5f);
        }

        if (points.size() > 1)
        {
            const float cx = area.getCentreX();
            const float cy = area.getCentreY();
            const float scale = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f * 0.9f;

            juce::Path p;
            bool first = true;

            for (const auto& pt : points)
            {
                // Goniometer rotation: mono = vertical, anti-phase = horizontal.
                const float x = cx + (pt.x - pt.y) * 0.7071f * scale;
                const float y = cy - (pt.x + pt.y) * 0.7071f * scale;

                if (first) { p.startNewSubPath (x, y); first = false; }
                else       { p.lineTo (x, y); }
            }

            g.setColour (traceColour.withAlpha (traceAlpha));
            g.strokePath (p, juce::PathStrokeType (traceThickness));
        }

        if (showFrame)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kOutline));
            g.drawRoundedRectangle (area, 3.0f, 1.0f);
        }
    }

private:
    static constexpr int kMaxPoints = 512;

    juce::Array<juce::Point<float>> points;

    juce::Colour traceColour  { juce::Colours::black };
    float        traceAlpha     = 0.85f;
    float        traceThickness = 1.0f;
    bool         showFrame      = false;
    bool         showGrid       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LissajousDisplay)
};
