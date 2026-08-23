/*
    SolPanel.h
    ----------
    An armoured sub-plate: the surface every module on a page sits on.

    The window's own silhouette is a rectangle with a corner sliced off
    (ChamferPanel). This repeats that cut at module scale so a page reads as
    PLATES BOLTED TOGETHER rather than as boxes drawn on a background — the
    mechanical, panelled look, where every surface is a piece of hardware with
    an edge and a fixing.

    Three things do that work, and they are cheap:

      * the chamfer, which is the house shape and the reason a panel never
        reads as a generic rounded rectangle;
      * a bevel — one light edge along the top, one dark along the bottom —
        which is all it takes for a flat fill to read as a raised plate;
      * bolts at the corners, drawn as a recess with a highlight so they read
        as sunk into the plate rather than sitting on it.

    Deliberately NOT skeuomorphic: no brushed-metal bitmaps, no drop shadows,
    no gloss. Sol's face is flat ink and accent light, and a photoreal panel
    under flat controls looks like two different products. This is the
    suggestion of a plate, at the same weight as everything else.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

namespace SolPanel
{
    /** Default corner cut. Scaled down for small panels by `draw`. */
    inline constexpr float kChamfer  = 9.0f;
    inline constexpr float kBoltInset = 7.0f;
    inline constexpr float kBoltR     = 2.1f;

    /** The house shape: a rectangle with the top-right and bottom-left corners
        sliced, so the plate reads as cut from sheet rather than drawn. Two
        opposite corners rather than all four — the asymmetry is what stops it
        looking like a lozenge, and it echoes the window's single cut. */
    inline juce::Path plateShape (juce::Rectangle<float> r, float cut)
    {
        cut = juce::jlimit (0.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.4f, cut);

        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        p.lineTo (r.getRight() - cut, r.getY());
        p.lineTo (r.getRight(),       r.getY() + cut);
        p.lineTo (r.getRight(),       r.getBottom());
        p.lineTo (r.getX() + cut,     r.getBottom());
        p.lineTo (r.getX(),           r.getBottom() - cut);
        p.closeSubPath();
        return p;
    }

    /** Paints one plate. `bolts` is worth turning off for very small panels,
        where four dots in the corners is just noise. */
    inline void draw (juce::Graphics& g, juce::Rectangle<float> r,
                      bool bolts = true, float cut = kChamfer)
    {
        if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
            return;

        // Small panels get a proportionally smaller cut, or the corner eats
        // the whole edge and the shape stops reading as a rectangle at all.
        cut = juce::jmin (cut, juce::jmin (r.getWidth(), r.getHeight()) * 0.22f);

        const auto shape = plateShape (r, cut);

        // Lit from above, like every panel in a rack.
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (SolLookAndFeel::kPanelLight), r.getCentreX(), r.getY(),
            juce::Colour (SolLookAndFeel::kPanel),      r.getCentreX(), r.getBottom(), false));
        g.fillPath (shape);

        // The bevel. Drawn as two clipped strokes of the same outline rather
        // than as separate edge lines, so it follows the chamfer for free.
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (r.withHeight (r.getHeight() * 0.5f).toNearestInt());
            g.setColour (juce::Colour (SolLookAndFeel::kOutlineHi).withAlpha (0.35f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (r.withTrimmedTop (r.getHeight() * 0.5f).toNearestInt());
            g.setColour (juce::Colour (SolLookAndFeel::kBackground).withAlpha (0.55f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        g.setColour (juce::Colour (SolLookAndFeel::kOutline));
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        if (! bolts || r.getWidth() < 46.0f || r.getHeight() < 34.0f)
            return;

        // Fixings, on the two square corners only — the cut corners have no
        // material to put one in, which is the sort of detail that makes the
        // shape read as machined instead of decorative.
        const juce::Point<float> at[] {
            { r.getX() + kBoltInset,     r.getY() + kBoltInset },
            { r.getRight() - kBoltInset, r.getBottom() - kBoltInset }
        };

        for (const auto& c : at)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kBackground).withAlpha (0.8f));
            g.fillEllipse (c.x - kBoltR, c.y - kBoltR, kBoltR * 2.0f, kBoltR * 2.0f);
            g.setColour (juce::Colour (SolLookAndFeel::kOutlineHi).withAlpha (0.5f));
            g.drawEllipse (c.x - kBoltR, c.y - kBoltR, kBoltR * 2.0f, kBoltR * 2.0f, 0.8f);
        }
    }
}
