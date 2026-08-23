/*
    SolIcons.h
    ----------
    Sol's icon set, drawn as vector paths rather than shipped as images.

    Every glyph is authored in a 24 x 24 box and returned unscaled; call
    `fitted()` to place one in a real rectangle. Paths rather than PNGs because
    a plugin window is zoomable — `FloatingShell::setLogicalSize` locks the
    aspect but not the scale — and a bitmap set would need @1x/@2x/@3x variants
    that all go soft on a 4K display at 175%. A path is resolution-free and
    re-colours with `g.setColour`, which a tinted image cannot.

    House style, so a new glyph looks like it belongs:

      - STROKED, not filled. Weight comes from the PathStrokeType the caller
        uses (kStroke below is the default), so icons match the hairline
        weight of the panel rather than reading as heavy solid blobs.
      - Built from a 24-unit grid on whole or half units. Off-grid points are
        what make an icon set look hand-drawn.
      - Open forms. Sol's face is thin lines on a dark ground; a closed filled
        shape reads as a button, not as a symbol.
      - Signal flows LEFT TO RIGHT, matching the plate's own INPUT-at-top,
        OUTPUT-at-bottom convention on the pages.
*/

#pragma once

#include <JuceHeader.h>

namespace SolIcons
{
    /** The grid every glyph below is authored on. */
    inline constexpr float kGrid = 24.0f;

    /** Default stroke weight, in grid units — scale it with the icon. */
    inline constexpr float kStroke = 1.8f;

    /** Scales a 24x24 glyph into `area`, preserving aspect and centring. */
    inline juce::Path fitted (const juce::Path& glyph, juce::Rectangle<float> area)
    {
        juce::Path p (glyph);
        p.applyTransform (p.getTransformToScaleToFit (area, true,
                                                      juce::Justification::centred));
        return p;
    }

    /** Stroke weight that keeps its visual weight as the icon is scaled. */
    inline float strokeFor (juce::Rectangle<float> area) noexcept
    {
        return juce::jmax (1.0f, juce::jmin (area.getWidth(), area.getHeight())
                                     / kGrid * kStroke);
    }

    //--------------------------------------------------------------------------
    /** Arrow running INTO a wall: the input chain, processed before the voice. */
    inline juce::Path inputFx()
    {
        juce::Path p;
        p.startNewSubPath (3.0f, 12.0f);      // shaft
        p.lineTo          (15.0f, 12.0f);
        p.startNewSubPath (10.5f, 7.5f);      // head
        p.lineTo          (15.0f, 12.0f);
        p.lineTo          (10.5f, 16.5f);
        p.startNewSubPath (19.0f, 5.0f);      // the wall it enters
        p.lineTo          (19.0f, 19.0f);
        return p;
    }

    /** Arrow running OUT of a wall: the output chain, after everything. */
    inline juce::Path outputFx()
    {
        juce::Path p;
        p.startNewSubPath (5.0f, 5.0f);       // the wall it leaves
        p.lineTo          (5.0f, 19.0f);
        p.startNewSubPath (9.0f, 12.0f);      // shaft
        p.lineTo          (21.0f, 12.0f);
        p.startNewSubPath (16.5f, 7.5f);      // head
        p.lineTo          (21.0f, 12.0f);
        p.lineTo          (16.5f, 16.5f);
        return p;
    }

    /** Three voices sounding together — the lead in the middle, a harmony
        either side of it.

        Drawn as vertical strokes, NOT as the fan-from-a-stem this started as:
        that version put a "<" on the left, which at 16px is exactly the shape
        of an arrowhead and read as a third arrow next to the two FX chains.
        Verticals share nothing with the arrows and carry the chord metaphor
        on their own. */
    inline juce::Path harmonies()
    {
        juce::Path p;
        p.startNewSubPath (7.0f, 16.5f);      // a harmony
        p.lineTo          (7.0f, 8.5f);
        p.startNewSubPath (12.0f, 19.0f);     // the lead, tallest
        p.lineTo          (12.0f, 5.0f);
        p.startNewSubPath (17.0f, 16.5f);     // and the other harmony
        p.lineTo          (17.0f, 8.5f);
        return p;
    }

    /** Standard power mark, for Bypass. */
    inline juce::Path power()
    {
        juce::Path p;
        p.addCentredArc (12.0f, 13.0f, 7.0f, 7.0f, 0.0f,
                         juce::degreesToRadians (35.0f),
                         juce::degreesToRadians (325.0f), true);
        p.startNewSubPath (12.0f, 3.5f);
        p.lineTo          (12.0f, 11.5f);
        return p;
    }

    /** Five-pin DIN, for MIDI. Pins on the standard 180-degree spread. */
    inline juce::Path midi()
    {
        juce::Path p;
        p.addEllipse (3.0f, 3.0f, 18.0f, 18.0f);

        constexpr float deg[] { 180.0f, 135.0f, 90.0f, 45.0f, 0.0f };

        for (const float d : deg)
        {
            const float a = juce::degreesToRadians (d);
            const float cx = 12.0f - std::cos (a) * 6.0f;
            const float cy = 12.0f - std::sin (a) * 6.0f;
            p.addEllipse (cx - 1.1f, cy - 1.1f, 2.2f, 2.2f);
        }

        return p;
    }

    /** Sol's own mark: a disc with rays. Used where the product signs itself. */
    inline juce::Path sun()
    {
        juce::Path p;
        p.addEllipse (8.0f, 8.0f, 8.0f, 8.0f);

        for (int i = 0; i < 8; ++i)
        {
            const float a  = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const float ux = std::cos (a), uy = std::sin (a);
            p.startNewSubPath (12.0f + ux * 6.5f, 12.0f + uy * 6.5f);
            p.lineTo          (12.0f + ux * 10.0f, 12.0f + uy * 10.0f);
        }

        return p;
    }
}
