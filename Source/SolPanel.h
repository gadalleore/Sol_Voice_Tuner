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

    /** Hazard-label ground and ink. A notice is not a panel — it is a sticker
        stuck TO one, and on real hardware that sticker is yellow with black
        stripes because it has to be read before anything else on the machine. */
    inline constexpr juce::uint32 kHazard    = 0xffe3b005;
    inline constexpr juce::uint32 kHazardInk = 0xff14140f;

    inline constexpr float kStripePitch = 9.0f;
    inline constexpr float kStripeWidth = 3.4f;

    /** A warning label: yellow ground, black diagonal hatching along the top
        and bottom edges, black rule around it. Caller draws the text in
        kHazardInk over the middle.

        The hatching runs at 45 degrees and is clipped to the plate's own
        shape, so the stripes get cut by the chamfer exactly the way a real
        printed label is cut by the die. */
    inline void drawNotice (juce::Graphics& g, juce::Rectangle<float> r,
                            bool lit = false, float cut = 7.0f)
    {
        if (r.getWidth() < 12.0f || r.getHeight() < 12.0f)
            return;

        cut = juce::jmin (cut, juce::jmin (r.getWidth(), r.getHeight()) * 0.22f);
        const auto shape = plateShape (r, cut);

        // Fully OPAQUE, always. A warning label is a sticker on the machine,
        // not a tint over it — at 82% the ring and its labels read straight
        // through the yellow and the notice stopped being a surface of its own
        // (Giuseppe, 2026-08-23). `lit` changes the shade, never the alpha.
        g.setColour (juce::Colour (kHazard).darker (lit ? 0.0f : 0.14f));
        g.fillPath (shape);

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (shape, {});

            const float band = juce::jlimit (4.0f, 7.0f, r.getHeight() * 0.22f);
            g.setColour (juce::Colour (kHazardInk).withAlpha (0.9f));

            // One pass per edge, hatching only inside its band.
            for (const auto& strip : { r.withHeight (band),
                                       r.withTrimmedTop (r.getHeight() - band) })
            {
                juce::Graphics::ScopedSaveState s2 (g);
                g.reduceClipRegion (strip.toNearestInt());

                for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += kStripePitch)
                    g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), kStripeWidth);
            }
        }

        g.setColour (juce::Colour (kHazardInk));
        g.strokePath (shape, juce::PathStrokeType (1.2f));
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

        // Seam lines just inside the edge. A plate this size in the real world
        // is not one pressing — it has a rolled lip, and the line where that
        // lip meets the face is what tells you the panel has THICKNESS. Two
        // shallow insets read as that lip without becoming a frame.
        if (r.getWidth() > 60.0f && r.getHeight() > 40.0f)
        {
            for (const auto& seam : { std::pair<float, float> { 3.5f, 0.30f },
                                      std::pair<float, float> { 6.0f, 0.16f } })
            {
                const auto inner = r.reduced (seam.first);

                if (inner.getWidth() < 8.0f || inner.getHeight() < 8.0f)
                    break;

                g.setColour (juce::Colour (SolLookAndFeel::kBackground).withAlpha (seam.second));
                g.strokePath (plateShape (inner, juce::jmax (2.0f, cut - seam.first)),
                              juce::PathStrokeType (1.0f));
            }
        }

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
