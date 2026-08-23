/*
    WheelComponent.h
    ----------------
    Generic wheel for the v3 paging UI (63C-11, geometry reworked in 63C-17) —
    wheels all the way down:

      * The wheel is a HALF WHEEL: its centre sits on the left edge of the
        component, so the visible part is a semicircle bulging rightward
        (per Giuseppe's sketches). Items are pills on the rim, top = input,
        bottom = output = signal order.
      * The visible half of the hub holds the palette of available items;
        pulling one out onto the rim activates it in that slot (draggable
        mode, e.g. the effects chains). Dragging rim->rim swaps slots;
        dropping off the rim removes. Non-draggable mode (e.g. the Home
        wheel) has fixed items.
      * Chains can hold more slots than fit on the semicircle (25 for the FX
        chains): slots sit at a fixed angular pitch and the rim SCROLLS.
        Primary scroll: HOVER the top/bottom edge of the wheel — the rim
        auto-scrolls smoothly through the elements while the cursor sits in
        the edge zone (works mid-drag too, so long chains can be assembled
        in one gesture). Secondary: mouse wheel outside the palette.
        Off-arc slots are culled.
      * The wheel ROTATES with hover: moving the mouse toward the top eases
        the wheel down (revealing items above) and vice versa. The full
        weighty spring/inertia treatment lands in 63C-12; `wheelPhase` stays
        a plain float so that pass can take over the motion.

    The component is a pure view: slot state lives in the owner's model
    (APVTS chain parameters for the effects windows) and is read/written
    through the std::function hooks below.
*/

#pragma once

#include <cmath>
#include <vector>

#include <JuceHeader.h>

#include "SolDither.h"
#include "SolLookAndFeel.h"
#include "SolPanel.h"
#include "SpectrumStrip.h"

class WheelComponent final : public juce::Component,
                             public  SpectrumStrip::Inkable,
                             private juce::Timer
{
public:
    WheelComponent() = default;

    struct Item
    {
        int typeId {};
        juce::String name;
    };

    //==========================================================================
    // Model hooks (owner supplies these).
    std::function<int (int slot)>              getSlotType;   // typeId in slot (emptyTypeId = vacant)
    std::function<void (int slot, int typeId)> setSlotType;   // draggable mode only
    std::function<void (int slot)>             onSlotClicked; // fired for occupied slots only
    std::function<juce::String (int typeId)>   nameProvider;  // display name for a typeId

    int  emptyTypeId     = 0;
    bool allowDuplicates = true;
    bool itemsDraggable  = true;   // false = fixed drill-in items (Home wheel)

    /** Rotation offset (radians, clockwise) applied to every slot angle.
        Eased toward the hover target each frame; animation-pass hook. */
    float wheelPhase = 0.0f;

    void setNumSlots (int n)              { numSlots = juce::jmax (1, n); clampRimScroll(); repaint(); }
    void setPillSize (float w, float h)   { pillW = w; pillH = h; repaint(); }

    /** Point size of the item labels on the rim. */
    void setItemFontHeight (float h)      { itemFontHeight = juce::jmax (1.0f, h); repaint(); }

    /** How far in the painted rim/hub circles sit relative to the slot radius.
        1.0 puts the rim exactly under the labels; smaller values pull the
        circles toward the left edge so the labels stand clear of them. */
    void setRingScale (float s)           { ringScale = juce::jlimit (0.05f, 1.0f, s); repaint(); }

    /** Pin the ring to one radius regardless of the component's size, so a
        window that resizes to fit its content doesn't resize the wheel with
        it. Zero (the default) derives the radius from the bounds as before. */
    void setFixedRadius (float r)         { fixedRadius = juce::jmax (0.0f, r); resized(); }

    /** Just the item labels, flat, in one colour — the spectrum strip uses
        this to knock the type back out of its bars. A stencil, not a second
        paint: no rim, no trails, no dashed empty slots. */
    void paintInk (juce::Graphics& g, juce::Colour ink) override
    {
        computeGeometry();

        g.setColour (ink);

        for (int i = 0; i < numSlots; ++i)
            if (slotVisible (i) && slotType (i) != emptyTypeId)
                g.fillPath (labelPath (i));
    }

    /** How far the whole window was just thrown, in plate pixels, pointing
        BACK the way it came (see paintTrail's sign convention).

        Shaking the window moves everything on it together, so nothing moves
        relative to the plate and the ordinary motion trails see no travel at
        all. Handing them the window's own displacement is what makes the
        smear follow the throw. */
    void setShakeMotion (juce::Point<float> m)
    {
        if (m == shakeMotion)
            return;

        shakeMotion = m;
        repaint();
    }

    /** Weight of the ring around the orb. Set from the plate so the circle is
        drawn with the same line as the border around it. */
    void setRingThickness (float t)
    {
        ringThickness = juce::jmax (0.1f, t);
        repaint();
    }

    /** Orb size, as a multiple of the hub radius. */
    void setOrbScale (float s)
    {
        orbScale = juce::jlimit (0.05f, 2.0f, s);
        layoutHubContent();
        repaint();
    }

    /** How far the orb sits in from the wheel axis, as a fraction of the rim
        radius. 0 leaves it centred on the axis (and so half off the panel). */
    void setOrbOffsetRatio (float r)
    {
        orbOffsetRatio = juce::jlimit (0.0f, 1.0f, r);
        layoutHubContent();
        repaint();
    }

    /** Component shown inside the hub circle — the wheel's centre. Not owned.
        The wheel's centre sits on the left edge, so only the right half of the
        hub is visible; the content is fitted to that visible half. */
    void setHubContent (juce::Component* c)
    {
        if (hubContent == c)
            return;

        if (hubContent != nullptr)
            removeChildComponent (hubContent);

        hubContent = c;

        if (hubContent != nullptr)
        {
            addAndMakeVisible (hubContent);
            hubContent->toBack();
        }

        layoutHubContent();
    }

    void resized() override
    {
        computeGeometry();
        layoutHubContent();

        // The ring has to be turning before anyone touches it. The timer used
        // to be started only by a mouse event, so an untouched wheel sat dead
        // still — which looked exactly like the rotation not working.
        startTimerHz (animFps);
    }

    void setPalette (std::vector<Item> items)
    {
        palette = std::move (items);
        paletteScroll = 0.0f;
        repaint();
    }

    /** The wheel spans the whole panel but only uses the hub and the rim band
        where pills sit. Everything else — the empty space to the right of the
        arc — is dead area, so let those clicks pass through to the window
        underneath, which uses them to drag the plugin around. */
    bool hitTest (int x, int y) override
    {
        if (radius <= 0.0f)     // geometry not computed yet (before first paint)
            return true;

        const juce::Point<float> p ((float) x, (float) y);
        const float d = p.getDistanceFrom (centre);

        if (p.getDistanceFrom (orbCentre()) <= orbRadius())   // the orb
            return true;

        if (std::abs (d - radius) <= pillH) // rim band where the slots sit
            return true;

        // The labels run rightward from their slot, well past the rim, so the
        // rim band alone leaves most of each word unhoverable.
        for (int i = 0; i < numSlots; ++i)
            if (slotVisible (i) && labelBox (i).contains (p))
                return true;

        // The steering strips along the top and bottom edges.
        //
        // Restricted to the wheel's own side of the panel. Claiming the FULL
        // width of those strips meant the wheel swallowed the top and bottom
        // of the inspector beside it — including, at the very top, the region
        // the plate paints through — and the page stopped rendering entirely
        // (Giuseppe, 2026-08-23).
        if (itemsDraggable && (float) x <= centre.x + radius + pillW)
        {
            const float zone = (float) getHeight() * kEdgeZoneFrac;

            if ((float) y <= zone || (float) y >= (float) getHeight() - zone)
                return true;
        }

        return false;
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        computeGeometry();

        // The wheel itself: rim circle (left half clips away) and hub.
        //
        // The DRAWN circles are pulled back toward the left edge by ringScale
        // so the item labels — which stay out at the full slot radius — sit
        // clear of them instead of on top (Giuseppe, 2026-07-28). Slot geometry
        // and hit-testing are unaffected; this is purely what gets painted.
        {
            // ---- The orb: nothing at all ------------------------------------
            //
            // It used to carry a radial rim shadow. Gone (Giuseppe, 2026-07-31):
            // the orb is now bare plate, so the trace runs on clean white and
            // the ring below is the only thing describing the shape. The orb
            // still EXISTS as geometry — it is what sizes and places the
            // Lissajous and the ring — it simply is not painted.

            // ---- The outer ring: one crisp, high-contrast line --------------
            //
            // A single flat stroke, no gradient and no dither on it. Fading a
            // structural line out along its length is a decorative move; sharp
            // and minimal wants the geometry stated once, cleanly.
            //
            // Only when there is an orb to ring. The Home wheel has hub content
            // (the Lissajous) the ring frames; the Effects/Harmonies wheels have
            // a palette there instead, positioned right up against the axis —
            // an unconditional ring drawn behind it just cuts through the
            // palette's words. "Nothing draws what isn't there" applies to the
            // wheel's own furniture too.
            if (hubContent != nullptr)
            {
                // Concentric with the ORB, not the wheel axis. Anchored to the
                // axis it could only ever pass beside the offset orb; ringing
                // the orb is what makes the two read as one object.
                const auto  rc = orbCentre();
                const float rR = orbRadius() * kRingOrbGap;

                juce::Path rimPath;
                rimPath.addEllipse (rc.x - rR, rc.y - rR, rR * 2.0f, rR * 2.0f);

                // Smeared behind itself while the window is being thrown. The
                // streak fills whatever shape it is given, so it takes the
                // ring's OUTLINE — handing it the ellipse would stamp a solid
                // disc of stipple across the middle of the plate.
                if (shakeMotion.getDistanceFromOrigin() >= kTrailMinSmear)
                {
                    juce::Path stroked;
                    juce::PathStrokeType (ringThickness).createStrokedPath (stroked, rimPath);

                    SolDither::streakRgb (g, stroked, shakeMotion, kTrailSteps, kTrailAlpha);
                }

                g.setColour (juce::Colour (kRingLight));
                g.strokePath (rimPath, juce::PathStrokeType (ringThickness));
            }

            // No accent arc: the rim reads from the items alone (Giuseppe, 2026-07-28).
        }

        // ---- The rim itself: a segmented band the items ride on ------------
        //
        // The rim used to be implied — a radius the labels happened to sit at,
        // with nothing drawn. As a band it becomes the object it always was:
        // a ring seen edge-on, grey and panelled, with the chain running round
        // it. The segment marks are keyed to wheelPhase and rimScroll, so the
        // ring visibly TURNS as you hover or scroll rather than the words
        // sliding along an invisible track (Giuseppe, 2026-08-23).
        if (scrollableRim() || itemsDraggable)
        {
            juce::Graphics::ScopedSaveState rimState (g);

            // The ring passes BEHIND the palette, not through it. The band
            // sweeps out from the axis and back, so its upper and lower thirds
            // cross exactly where the palette list sits — leaving it underneath
            // put a grey plate behind twelve words and cost both of them their
            // legibility. Cutting the palette out of the band also reads better
            // mechanically: the list is a plate mounted over the ring.
            if (! palette.empty())
                g.excludeClipRegion (paletteClip.toNearestInt().expanded (4, 2));

            const float bandW = juce::jlimit (14.0f, 30.0f, radius * 0.13f);

            juce::Path band;
            band.addEllipse (centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f);

            // The plate of the ring.
            g.setColour (juce::Colour (SolLookAndFeel::kPanelLight));
            g.strokePath (band, juce::PathStrokeType (bandW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::butt));

            // A brighter inner rail and a darker outer one: lit from inside,
            // which is what stops a flat band reading as a painted stripe.
            for (const auto& rail : { std::pair<float, juce::uint32> { -0.5f, SolLookAndFeel::kOutlineHi },
                                      std::pair<float, juce::uint32> {  0.5f, SolLookAndFeel::kBackground } })
            {
                juce::Path edge;
                const float r = radius + rail.first * bandW;
                edge.addEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
                g.setColour (juce::Colour (rail.second).withAlpha (0.55f));
                g.strokePath (edge, juce::PathStrokeType (1.0f));
            }

            // Panel joints, marching with the wheel.
            const float pitch  = kRimSegment;
            const float offset = std::fmod (wheelPhase, pitch);

            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.8f));

            for (float a = offset; a < juce::MathConstants<float>::twoPi; a += pitch)
            {
                const float s = std::sin (a), c = std::cos (a);
                g.drawLine (centre.x + s * (radius - bandW * 0.5f),
                            centre.y - c * (radius - bandW * 0.5f),
                            centre.x + s * (radius + bandW * 0.5f),
                            centre.y - c * (radius + bandW * 0.5f),
                            1.0f);
            }
        }

        // ---- "Pull to remove", outboard of the rim -------------------------
        //
        // Dropping a placed effect off the rim deletes it (see mouseUp), and
        // nothing said so: the only way to discover it was by accident. The
        // arrow points the way OUT — rightward, away from the ring — and lights
        // up while a slot is actually being dragged, which is the one moment
        // the instruction is about to be needed (Giuseppe, 2026-08-23).
        if (itemsDraggable && radius > 0.0f)
        {
            const bool live = dragSource == DragSource::slot;

            // Outboard of the rim, on the side you pull toward, and clamped
            // inside the panel so a large radius cannot push it off the edge.
            const float wantX = centre.x + radius + kRemoveHintGap;
            const auto box = juce::Rectangle<float> (kRemoveHintW, kRemoveHintH)
                                 .withX (juce::jmin (wantX, (float) getWidth() - kRemoveHintW - 10.0f))
                                 .withY (centre.y - kRemoveHintH * 0.5f);

            {
                SolPanel::drawNotice (g, box, live);

                const auto ink = juce::Colour (SolPanel::kHazardInk);

                auto row = box.reduced (8.0f, 7.0f);
                const auto arrow = row.removeFromRight (18.0f);

                g.setColour (ink);
                g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                          9.5f, juce::Font::plain)));
                g.drawFittedText ("PULL TO\nREMOVE", row.toNearestInt(),
                                  juce::Justification::centredRight, 2);

                // A shaft with a head: unmistakably "this way out".
                const float cy = arrow.getCentreY();
                const float x0 = arrow.getX(), x1 = arrow.getRight();

                g.drawLine (x0, cy, x1, cy, live ? 1.8f : 1.3f);

                juce::Path head;
                head.startNewSubPath (x1 - 5.0f, cy - 4.0f);
                head.lineTo          (x1,        cy);
                head.lineTo          (x1 - 5.0f, cy + 4.0f);
                g.strokePath (head, juce::PathStrokeType (live ? 1.8f : 1.3f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }
        }


        // Slots on the rim (culled to the visible arc).
        for (int i = 0; i < numSlots; ++i)
        {
            if (! slotVisible (i))
                continue;

            const auto pos      = slotCentre (i);
            const int  type     = slotType (i);
            const bool occupied = type != emptyTypeId;

            // Slots are allowed slightly past the arc so scrolling does not
            // pop them in and out — but at full strength that meant a socket
            // sliced in half by the panel edge, top and bottom, which read as
            // a drawing fault. They fade over the last part of their travel
            // instead (Giuseppe, 2026-08-23).
            const float edgeFade = slotEdgeFade (i);

            if (edgeFade <= 0.01f)
                continue;
            const bool isTarget = dragging() && i == hitSlot (dragPos);
            const bool hovered  = ! dragging() && i == hoveredSlot;

            // No spokes: items sit on the rim unattached (Giuseppe, 2026-07-28).

            if (occupied)
            {
                // The WHOLE PART sits on the ring — the same little plate it
                // was in the tray and in your hand, not its name stripped out
                // of it (Giuseppe, 2026-08-23). A part that arrives as bare
                // text has visibly stopped being the object you were carrying.
                const auto pill = labelBox (i);
                const bool dimmed = dragSource == DragSource::slot && dragFromSlot == i;
                const float a = (dimmed ? 0.35f : 1.0f) * edgeFade;

                {
                    juce::Graphics::ScopedSaveState partState (g);
                    g.setOpacity (a);

                    SolPanel::draw (g, pill.reduced (1.0f, 1.5f), false, 5.0f);

                    if (hovered)
                    {
                        g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.6f));
                        g.drawRoundedRectangle (pill.reduced (1.0f, 1.5f), 3.0f, 1.2f);
                    }

                    g.setColour (juce::Colour (hovered ? kHoverText : SolLookAndFeel::kTitleHi));
                    g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                              paletteFontHeight(),
                                                              juce::Font::plain)));
                    g.drawText (nameForType (type), pill.reduced (6.0f, 0.0f),
                                juce::Justification::centredLeft);

                    // A doubled effect gets a tie-bar along the foot of its
                    // plate. The two instances share one control set (see
                    // EffectsWindowPage), and that has to be visible on the
                    // ring — otherwise the second silently tracks the first.
                    if (countOfType (type) > 1)
                    {
                        g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
                        g.fillRect (pill.getX() + 5.0f, pill.getBottom() - 3.5f,
                                    pill.getWidth() - 10.0f, 1.5f);
                    }
                }
            }
            else
            {
                // An empty slot is a MOUNTING SOCKET, not a dotted outline: a
                // recess sunk into the ring with a bayonet's lugs around it, so
                // it reads as somewhere a part goes rather than as a hole in
                // the drawing (Giuseppe, 2026-08-23).
                const float r = slotRingR;

                // The recess: darker than the band, so it reads as depth.
                g.setColour (juce::Colour (SolLookAndFeel::kBackground)
                                 .withAlpha ((isTarget ? 0.85f : 0.65f) * edgeFade));
                g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

                // Its rim, and a lit inner lip just inside it.
                g.setColour (juce::Colour (isTarget ? SolLookAndFeel::kAccentArc
                                                    : SolLookAndFeel::kOutlineHi)
                                 .withAlpha ((isTarget ? 1.0f : 0.7f) * edgeFade));
                g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f,
                               isTarget ? 2.2f : 1.4f);

                g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha ( 0.5f * edgeFade));
                g.drawEllipse (pos.x - r + 3.0f, pos.y - r + 3.0f,
                               (r - 3.0f) * 2.0f, (r - 3.0f) * 2.0f, 1.0f);

                // Four lugs on the diagonals — off the axes so they cannot be
                // mistaken for the rim's own joint marks.
                g.setColour (juce::Colour (isTarget ? SolLookAndFeel::kAccentArc
                                                    : SolLookAndFeel::kOutlineHi)
                                 .withAlpha ((isTarget ? 0.95f : 0.6f) * edgeFade));

                for (int lug = 0; lug < 4; ++lug)
                {
                    const float a = juce::MathConstants<float>::pi * (0.25f + 0.5f * (float) lug);
                    const float ux = std::cos (a), uy = std::sin (a);
                    g.drawLine (pos.x + ux * (r - 1.0f), pos.y + uy * (r - 1.0f),
                                pos.x + ux * (r + 3.5f), pos.y + uy * (r + 3.5f),
                                isTarget ? 2.0f : 1.3f);
                }
            }
        }

        // No rim-scroll hints: the ring is whole, so there is nothing off
        // screen to advertise. The two captions they left floating above and
        // below the plate were the artefacts (Giuseppe, 2026-08-23).

        // Palette inside the visible half of the hub.
        if (! palette.empty())
        {
            g.saveState();
            g.reduceClipRegion (paletteClip.toNearestInt());

            // The instruction, on its own plate. "PULL OUT" said what to do
            // with your hand but not what it accomplishes; this names the
            // gesture AND its result, which is the whole job of the label on
            // an unfamiliar control surface (Giuseppe, 2026-08-23).
            auto header = paletteClip;
            {
                const auto plate = header.removeFromTop (kPaletteHeaderH).reduced (2.0f, 2.0f);
                SolPanel::drawNotice (g, plate);

                g.setColour (juce::Colour (SolPanel::kHazardInk));
                g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                          9.5f, juce::Font::plain)));
                g.drawFittedText ("PULL INTO RING\nTO ADD EFFECT",
                                  plate.reduced (5.0f, 6.0f).toNearestInt(),
                                  juce::Justification::centred, 2);
            }

            // Say so when the list runs past the box, at both ends.
            if (const float maxScroll = maxPaletteScroll(); maxScroll > 0.001f)
            {
                const juce::Rectangle<float> gutter (paletteClip.getRight() - 16.0f,
                                                     paletteClip.getY(),
                                                     14.0f, paletteClip.getHeight());

                g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.45f));

                if (paletteScroll > 0.001f)
                    g.drawText ("^", gutter.withHeight (kPaletteHeaderH),
                                juce::Justification::centred);

                if (paletteScroll < maxScroll - 0.001f)
                    g.drawText ("v", gutter.withTop (gutter.getBottom() - kPaletteHeaderH),
                                juce::Justification::centred);
            }

            for (size_t i = 0; i < palette.size(); ++i)
            {
                const auto pill = paletteRect ((int) i);
                if (! pill.intersects (paletteClip))
                    continue;

                const bool isDragged = dragSource == DragSource::palette
                                    && dragPaletteIndex == (int) i;
                const bool hovered   = ! dragging() && (int) i == hoveredPalette;

                // Each effect gets its own plate (Giuseppe, 2026-08-23). As
                // bare words the palette read as a list of text; as plates they
                // read as PARTS in a tray — which is what they are, since the
                // gesture is to pick one up and mount it on the ring.
                SolPanel::draw (g, pill.reduced (1.0f, 1.5f), false, 5.0f);

                if (hovered)
                {
                    g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.5f));
                    g.drawRoundedRectangle (pill.reduced (1.0f, 1.5f), 3.0f, 1.2f);
                }

                g.setColour (juce::Colour (hovered ? kHoverText : SolLookAndFeel::kTitleHi)
                                 .withAlpha (isDragged ? 0.35f : 1.0f));
                g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                          paletteFontHeight(), juce::Font::plain)));
                g.drawText (palette[i].name, pill.reduced (6.0f, 0.0f),
                            juce::Justification::centredLeft);
            }

            g.restoreState();
        }

        // Drag ghost on top of everything. Bare text, like the items — no pill
        // behind it (Giuseppe, 2026-07-28: no ovals around the words).
        if (dragging())
        {
            // The part does NOT change as you pull it out (Giuseppe,
            // 2026-08-23). It used to be re-drawn at the rim's pill size and
            // font, so a plate picked out of the tray grew and re-typeset in
            // the hand — which read as picking up one thing and carrying a
            // different one. The ghost now keeps the palette plate's own size
            // and point size all the way onto the ring.
            const bool fromTray = dragSource == DragSource::palette;

            const auto size = fromTray ? paletteRect (juce::jmax (0, dragPaletteIndex))
                                       : labelBox (juce::jmax (0, dragFromSlot));

            const auto ghost = juce::Rectangle<float> (size.getWidth(), size.getHeight())
                                   .withCentre (dragPos);

            SolPanel::draw (g, ghost.reduced (1.0f, 1.5f), false, 5.0f);

            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.7f));
            g.drawRoundedRectangle (ghost.reduced (1.0f, 1.5f), 3.0f, 1.2f);

            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                      fromTray ? paletteFontHeight()
                                                               : itemFontHeight,
                                                      juce::Font::plain)));
            g.drawText (nameForType (dragTypeId), ghost.reduced (6.0f, 0.0f),
                        fromTray ? juce::Justification::centredLeft
                                 : juce::Justification::centred);
        }
    }

    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        computeGeometry();
        updateHover (e.position);
    }

    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoveredSlot    = -1;
        hoveredPalette = -1;
        edgeScrollDir  = 0.0f;
        targetPhase   = 0.0f;
        startTimerHz (animFps);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        computeGeometry();
        mouseDownPos = e.position;

        if (const int p = hitPalette (e.position); itemsDraggable && p >= 0)
        {
            dragSource       = DragSource::palette;
            dragPaletteIndex = p;
            dragTypeId       = palette[(size_t) p].typeId;
            dragPos          = e.position;
        }
        else if (const int s = hitSlot (e.position); s >= 0 && slotType (s) != emptyTypeId)
        {
            dragSource   = DragSource::slot;
            dragFromSlot = s;
            dragTypeId   = slotType (s);
            dragPos      = e.position;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSource == DragSource::none || ! itemsDraggable)
            return;

        dragPos = e.position;

        // Edge zones stay live mid-drag so long chains can be assembled in
        // one gesture: hold the drag near the top/bottom and the rim scrolls.
        edgeScrollDir = computeEdgeScrollDir (e.position);
        if (edgeScrollDir != 0.0f)
            startTimerHz (animFps);

        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool moved  = e.position.getDistanceFrom (mouseDownPos) > 5.0f;
        const auto source = dragSource;
        dragSource    = DragSource::none;
        edgeScrollDir = computeEdgeScrollDir (e.position);

        if (source == DragSource::none)
            return;

        if (! moved || ! itemsDraggable)
        {
            // A click, not a drag: drill into occupied slots.
            if (source == DragSource::slot && onSlotClicked != nullptr)
                onSlotClicked (dragFromSlot);
            repaint();
            return;
        }

        const int target = hitSlot (e.position);

        if (source == DragSource::palette)
        {
            if (target >= 0 && (allowDuplicates || ! typeIsPlaced (dragTypeId)))
            {
                applySlotType (target, dragTypeId);

                // Placing an effect IS asking to set it up (Giuseppe,
                // 2026-08-23). Nobody drags a Reverb onto the chain and then
                // wants it at whatever the defaults happen to be — so open it,
                // rather than making the drop and the click two separate
                // gestures for one intention.
                if (onSlotClicked != nullptr)
                    onSlotClicked (target);
            }
        }
        else if (source == DragSource::slot)
        {
            // Removal is RADIAL: pull the effect outward off the ring and it
            // comes off, the way the arrow says (Giuseppe, 2026-08-23).
            //
            // It used to require escaping every slot's hit box, and a label is
            // pillW wide — so you had to drag most of the way across the panel
            // before anything happened, which read as the gesture not working.
            // Distance from the axis is the honest test: past the rim by a
            // little and it is off.
            const float pulled = e.position.getDistanceFrom (centre) - radius;

            if (pulled > kRemovePull)
            {
                applySlotType (dragFromSlot, emptyTypeId);
            }
            else if (target >= 0 && target != dragFromSlot)
            {
                // Swap with the target slot (also handles dropping onto a vacancy).
                applySlotType (dragFromSlot, slotType (target));
                applySlotType (target, dragTypeId);
            }
        }

        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        computeGeometry();

        // Over the palette: scroll the palette list.
        if (! palette.empty() && paletteClip.contains (e.position))
        {
            paletteScroll = juce::jlimit (0.0f, maxPaletteScroll(),
                                          paletteScroll - wheel.deltaY * 48.0f);
            repaint();
            return;
        }

        // Anywhere else: scroll the rim through the chain's slots.
        if (scrollableRim())
        {
            rimScroll = juce::jlimit (0.0f, maxRimScroll(),
                                      rimScroll - wheel.deltaY * slotPitch * 1.5f);
            hoveredSlot = hitSlot (e.position);
            repaint();
        }
    }

private:
    enum class DragSource { none, palette, slot };

    //==========================================================================
    // Hover-driven rotation: hovering high rotates the wheel down (revealing
    // items above), hovering low rotates it up. Eased per-frame toward the
    // target; 63C-12 replaces this easing with the weighty spring treatment.
    void updateHover (juce::Point<float> pos)
    {
        hoveredSlot    = hitSlot (pos);
        hoveredPalette = hitPalette (pos);

        // Hover no longer nudges the phase: it fought the idle rotation, and
        // on a whole ring there is nothing hidden for a nudge to reveal.

        // Edge zones: parking the cursor near the top/bottom of the wheel
        // auto-scrolls the rim through chains longer than the visible arc.
        edgeScrollDir = computeEdgeScrollDir (pos);

        startTimerHz (animFps);
        repaint();
    }

    /** -1..1: how hard to auto-scroll the rim. Bottom edge scrolls forward
        through the elements, top edge scrolls back; 0 outside the zones. */
    float computeEdgeScrollDir (juce::Point<float> pos) const
    {
        // No scrollableRim() guard: the ring always turns, so the edge zones
        // are always live. That guard is what silently disabled hover-steering
        // when scrolling was removed (Giuseppe, 2026-08-23).
        if (! itemsDraggable)
            return 0.0f;

        // Not over the palette. The list runs most of the panel's height, so
        // its bottom entries fall inside the steering strip — and reading down
        // the list to find Parametric EQ would spin the chain out from under
        // whatever you were about to drop it on (Giuseppe, 2026-08-23).
        // Hovering a thing you are about to pick up must not move everything
        // else.
        if (! palette.empty() && paletteClip.expanded (kPaletteClearPad).contains (pos))
            return 0.0f;

        const float h    = juce::jmax (1.0f, (float) getHeight());
        const float zone = h * kEdgeZoneFrac;

        if (pos.y < zone)
            return -(1.0f - pos.y / zone);
        if (pos.y > h - zone)
            return (pos.y - (h - zone)) / zone;
        return 0.0f;
    }

    void timerCallback() override
    {
        bool busy = false;

        // The ring turns ONLY while you are steering it (Giuseppe, 2026-08-23).
        //
        // It idled at one revolution a minute for a while, and that was wrong
        // in a way that took seeing it to notice: a chain that rearranges
        // itself while you read it is not a chain you can read. Slot 3 is
        // wherever slot 3 happens to have drifted to. Motion here has to MEAN
        // something, and the only thing it can mean is "you asked for it" —
        // hover the top and the ring rides up, hover the bottom and it comes
        // down. Hands off, it holds still and stays readable.
        //
        // A drag freezes it too: that is the one moment you are aiming at a
        // specific slot, and a moving target would fight the gesture.
        if (edgeScrollDir != 0.0f && ! dragging())
        {
            wheelPhase += edgeScrollDir * kSteerRate / (float) animFps;

            // Wrap both ways: steering backwards takes the phase negative.
            const float twoPi = juce::MathConstants<float>::twoPi;
            if (wheelPhase >  twoPi) wheelPhase -= twoPi;
            if (wheelPhase < -twoPi) wheelPhase += twoPi;

            busy = true;
        }

        // The hover-glide toward `targetPhase` that used to live here is gone.
        // Nothing sets targetPhase any more, so it sat at zero and hauled
        // wheelPhase back to zero every frame — which silently cancelled the
        // idle rotation above (Giuseppe, 2026-08-23). Rim scrolling went with
        // it: scrollableRim() is false on a whole ring.

        // Keep ticking while any smear is still draining, otherwise the last
        // frame of trail would freeze on screen when motion stops.
        if (trailsDraining())
            busy = true;

        if (! busy)
            stopTimer();

        repaint();
    }

    bool trailsDraining() const
    {
        for (const auto& t : trails)
            if (t.size() > 1)
                return true;

        return false;
    }

    //==========================================================================
    bool dragging() const noexcept { return dragSource != DragSource::none; }

    int slotType (int slot) const
    {
        return getSlotType != nullptr ? getSlotType (slot) : emptyTypeId;
    }

    void applySlotType (int slot, int typeId)
    {
        if (setSlotType != nullptr)
            setSlotType (slot, typeId);
    }

    /** How many slots currently hold this type. >1 means the instances share
        one control set and the rim says so. */
    int countOfType (int typeId) const
    {
        int n = 0;

        for (int i = 0; i < numSlots; ++i)
            if (slotType (i) == typeId)
                ++n;

        return n;
    }

    bool typeIsPlaced (int typeId) const
    {
        for (int i = 0; i < numSlots; ++i)
            if (slotType (i) == typeId)
                return true;
        return false;
    }

    juce::String nameForType (int typeId) const
    {
        if (nameProvider != nullptr)
            return nameProvider (typeId);

        for (const auto& item : palette)
            if (item.typeId == typeId)
                return item.name;
        return {};
    }

    //==========================================================================
    // Geometry. JUCE arc convention: angle 0 = 12 o'clock, positive clockwise.
    // 63C-17: HALF WHEEL — the wheel centre is pinned to the component's left
    // edge, so only the right half (angles arcStart..arcEnd, the semicircle
    // bulging rightward) is visible.
    void computeGeometry()
    {
        const auto b = getLocalBounds().toFloat();

        // The wheel keeps its original placement — centre pinned to the LEFT
        // edge, so what you see is the right-hand side of a big ring passing
        // by. What changed (Giuseppe, 2026-08-23) is that the ring is now
        // WHOLE: the 25 slots are spread around the full circle and it turns
        // continuously, so slots enter at the top, sweep across the visible
        // side and leave at the bottom, round and round. It used to be a bare
        // semicircle that had to be scrolled.
        // A FIXED radius where the caller asks for one (Giuseppe, 2026-08-23).
        // The window changes shape to suit the effect you have open; the ring
        // is not part of that. Growing the wheel because the inspector beside
        // it got wider makes the whole page feel like it is breathing, and the
        // slot you were about to click moves out from under the cursor. Still
        // clamped to what actually fits, so a squeezed panel degrades instead
        // of overflowing.
        const float fit = juce::jmin (b.getHeight() * 0.52f, b.getWidth() * 0.62f);
        radius = fixedRadius > 0.0f ? juce::jmin (fixedRadius, fit) : fit;
        centre = { b.getX(), b.getCentreY() };   // flush to the edge

        const float hubR = hubRadius();
        const float palW = juce::jlimit (72.0f, 150.0f, hubR - 14.0f);

        // Tall enough for the whole list where the panel allows it (63C-8 took
        // the effects palette from two entries to twelve, and a list you have
        // to scroll to discover is a list you do not know is there). Falls back
        // to the hub-sized box when there is nothing much to show.
        const float wanted = (float) palette.size() * kPaletteMaxRowH + kPaletteHeaderH;
        const float palH   = juce::jlimit (juce::jmin (hubR * 1.6f, b.getHeight() * 0.62f),
                                           b.getHeight() * 0.96f,
                                           wanted);

        paletteClip = { centre.x + 8.0f, centre.y - palH * 0.5f, palW, palH };

        // Squeeze the rows to fit before falling back to scrolling.
        paletteRowH = kPaletteMaxRowH;
        if (! palette.empty())
            paletteRowH = juce::jlimit (kPaletteMinRowH, kPaletteMaxRowH,
                                        (palH - kPaletteHeaderH) / (float) palette.size());

        paletteScroll = juce::jlimit (0.0f, maxPaletteScroll(), paletteScroll);

        clampRimScroll();
    }

    float hubRadius() const noexcept { return radius * 0.62f; }

    /** Nothing scrolls: the ring is whole, so every slot is always shown. */
    bool scrollableRim() const noexcept { return false; }

    float maxRimScroll() const noexcept
    {
        return juce::jmax (0.0f, (float) (numSlots - 1) * slotPitch - (arcEnd - arcStart));
    }

    void clampRimScroll() noexcept
    {
        rimScroll = juce::jlimit (0.0f, maxRimScroll(), rimScroll);
    }

    /** Slots are spread evenly around the WHOLE circle. No scrolling: every
        slot in the chain is on screen at once, which is the point of the ring
        being whole. */
    float slotAngle (int i) const
    {
        return juce::MathConstants<float>::twoPi * (float) i / (float) juce::jmax (1, numSlots)
             + wheelPhase;
    }

    /** Only the right-hand side of the ring is on the panel — the rest of the
        circle is off the left edge, where the centre sits. A slot counts as
        visible while it is on that side, with a margin so it is already
        drawn (and fading in) before it reaches the edge. */
    bool slotVisible (int i) const
    {
        return std::sin (slotAngle (i)) > kVisibleSin;
    }

    /** 1 across the middle of the visible side, easing to 0 as a slot turns
        away toward the top or bottom edge. This is what lets the ring rotate
        forever without slots being sliced off by the panel edge — they
        dissolve as they go round the back and reappear the same way. */
    float slotEdgeFadeFor (int i) const
    {
        const float s = std::sin (slotAngle (i));
        const float edge = juce::jlimit (0.0f, 1.0f, (s - kVisibleSin) / kFadeSpan);

        // ...and a second fade where the ring passes BEHIND the palette.
        //
        // The wheel's centre is pinned to the left edge and the palette sits in
        // the hub, so near the top and bottom of the arc a slot's plate swings
        // across the list of effects you are trying to drag FROM. Two panels
        // occupying the same pixels reads as a bug either way round, and of the
        // two the palette is the one you are aiming at, so the slot yields
        // (Giuseppe, 2026-08-23).
        return edge * juce::jmin (1.0f, paletteClearanceFor (i));
    }

    /** 1 where a slot's plate is clear of the palette, easing to 0 as it slides
        underneath. Measured on the horizontal overlap alone: the palette is a
        tall column, so how far the plate has crossed its right edge is the
        whole story. */
    float paletteClearanceFor (int i) const
    {
        if (palette.empty() || paletteClip.isEmpty())
            return 1.0f;

        const auto pill = labelBox (i);

        if (! pill.intersects (paletteClip.expanded (kPaletteClearPad)))
            return 1.0f;

        const float over = paletteClip.getRight() + kPaletteClearPad - pill.getX();

        return juce::jlimit (0.0f, 1.0f, 1.0f - over / juce::jmax (1.0f, pill.getWidth()));
    }

    /** 1 well inside the arc, easing to 0 as a slot reaches either end of it.
        This is what stops a socket being sliced off by the panel edge as it
        scrolls past — it dissolves instead of being cut. */
    float slotEdgeFade (int i) const { return slotEdgeFadeFor (i); }

    juce::Point<float> slotCentre (int i) const
    {
        const float a = slotAngle (i);
        return { centre.x + radius * std::sin (a),
                 centre.y - radius * std::cos (a) };
    }

    /** Label box for a slot, anchored at the slot and running rightward.
        Centring it on the slot pushed the top and bottom labels off the left
        edge — their slots sit near the wheel's centre, which is on that edge.
        Anchoring left keeps every label on screen AND preserves the stagger
        that makes them read as following the arc (Giuseppe, 2026-07-28). */
    //==========================================================================
    // Motion trails
    //==========================================================================
    /** One slot's label as a filled outline, at the size it is currently drawn
        (hover included, so a stencil taken from it lines up exactly). */
    juce::Path labelPath (int slot)
    {
        const auto  pill    = labelBox (slot);
        const bool  hovered = ! dragging() && slot == hoveredSlot;

        // Hovered items swell very slightly — enough to feel alive without
        // shifting the layout.
        const float h = hovered ? itemFontHeight * kHoverFontScale
                                : itemFontHeight;

        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                             h, juce::Font::plain)),
                              nameForType (slotType (slot)),
                              pill.getX(), pill.getY(),
                              pill.getWidth(), pill.getHeight(),
                              juce::Justification::centredLeft, 1);

        juce::Path p;
        glyphs.createPath (p);

        return p;
    }

    /** Position history for one slot's label, newest last. */
    juce::Array<juce::Point<float>>& trailFor (int slot)
    {
        while (trails.size() <= slot)
            trails.add ({});

        return trails.getReference (slot);
    }

    /** Records where the label is now and paints the rainbow dither over the
        positions it has just left. Nothing is drawn while the label is still,
        so the effect only ever appears as a consequence of movement. */
    void paintTrail (juce::Graphics& g, const juce::Path& shape,
                     juce::Point<float> pos, juce::Array<juce::Point<float>>& history)
    {
        if (history.isEmpty() || pos.getDistanceFrom (history.getLast()) > kTrailMinStep)
        {
            history.add (pos);

            while (history.size() > kTrailLength)
                history.remove (0);
        }
        else if (history.size() > 1)
        {
            // Standing still: collapse the streak back into the label fast —
            // two frames' worth per frame, so it is gone in a blink.
            history.remove (0);

            if (history.size() > 1)
                history.remove (0);
        }

        // The streak spans the whole distance travelled, not one frame's worth
        // — plus however far the window itself was just thrown.
        const auto displacement = (history.getFirst() - pos) + shakeMotion;

        if (displacement.getDistanceFromOrigin() < kTrailMinSmear)
            return;

        SolDither::streakRgb (g, shape, displacement, kTrailSteps, kTrailAlpha);
    }

    /** Radius of the orb — independent of the hub, which is a palette region. */
    float orbRadius() const noexcept { return hubRadius() * ringScale * orbScale; }

    /** The orb's centre, pushed in from the wheel axis so it clears the panel
        edge. Offset is a fraction of the painted rim radius, so it tracks the
        wheel's size instead of being a fixed pixel nudge. */
    juce::Point<float> orbCentre() const noexcept
    {
        return { centre.x + radius * ringScale * orbOffsetRatio, centre.y };
    }

    /** Fits the hub content into the visible (right) half of the hub circle. */
    void layoutHubContent()
    {
        if (hubContent == nullptr)
            return;

        if (radius <= 0.0f)
            computeGeometry();

        const float oR = orbRadius();

        if (oR < 8.0f)
        {
            hubContent->setVisible (false);
            return;
        }

        hubContent->setVisible (true);

        // Square inscribed in the orb, centred on the orb's own centre — which
        // is inset from the wheel axis, so the whole thing is on screen.
        const float side = oR * juce::MathConstants<float>::sqrt2 * 0.94f;
        hubContent->setBounds (juce::Rectangle<float> (side, side)
                                   .withCentre (orbCentre()).toNearestInt());
    }

    juce::Rectangle<float> pillAround (juce::Point<float> p) const
    {
        return { p.x - pillH * 0.3f, p.y - pillH * 0.5f, pillW, pillH };
    }

    /** Where a slot's LABEL sits: pushed OUTWARD along the rim's own normal
        rather than straight rightward from the slot.

        Straight-right was fine in the middle of the arc and wrong at its ends,
        where the rim curves back toward the axis — the top and bottom labels
        ran straight into the palette column and came out from behind it
        half-eaten. Following the normal carries them up-and-right at the top
        and down-and-right at the bottom, which is off the palette and also
        reads as the name belonging to that point on the ring
        (Giuseppe, 2026-08-23). */
    /** The size of a PART, wherever it happens to be — in the tray, in your
        hand mid-drag, or mounted on the ring. One size for all three, taken
        from the palette's own row metrics, so an effect never changes shape as
        it moves (Giuseppe, 2026-08-23). */
    juce::Point<float> partSize() const
    {
        if (palette.empty())
            return { pillW, pillH };

        return { juce::jmax (60.0f, paletteClip.getWidth() - 8.0f),
                 juce::jmax (16.0f, juce::jmin (pillH, paletteRowH - 4.0f)) };
    }

    juce::Rectangle<float> labelBox (int slot) const
    {
        const float a = slotAngle (slot);
        const auto  p = slotCentre (slot);
        const juce::Point<float> outward { std::sin (a), -std::cos (a) };
        const auto sz = partSize();

        return juce::Rectangle<float> (sz.x, sz.y)
                   .withCentre (p + outward * kLabelOutset);
    }

    juce::Rectangle<float> scrollHintTop() const
    {
        const juce::Point<float> p { centre.x + radius * std::sin (arcStart),
                                     centre.y - radius * std::cos (arcStart) };
        return { p.x + pillW * 0.65f, p.y - 8.0f, 60.0f, 14.0f };
    }

    juce::Rectangle<float> scrollHintBottom() const
    {
        const juce::Point<float> p { centre.x + radius * std::sin (arcEnd),
                                     centre.y - radius * std::cos (arcEnd) };
        return { p.x + pillW * 0.65f, p.y - 6.0f, 60.0f, 14.0f };
    }

    juce::Rectangle<float> paletteRect (int index) const
    {
        const float top = paletteClip.getY() + kPaletteHeaderH
                        + (float) index * paletteRowH - paletteScroll;
        return { paletteClip.getX() + 4.0f, top,
                 paletteClip.getWidth() - 8.0f,
                 juce::jmin (pillH, paletteRowH - 4.0f) };
    }

    /** Palette words scale with their row so they stay inside it. */
    float paletteFontHeight() const noexcept
    {
        return juce::jlimit (10.0f, 14.0f, paletteRowH * 0.58f);
    }

    /** How far the palette can scroll — 0 when the whole list already fits. */
    float maxPaletteScroll() const noexcept
    {
        return juce::jmax (0.0f, (float) palette.size() * paletteRowH
                                     - (paletteClip.getHeight() - kPaletteHeaderH));
    }

    int hitSlot (juce::Point<float> p) const
    {
        for (int i = 0; i < numSlots; ++i)
            if (slotVisible (i)
                // A slot faded out behind the palette must not be clickable
                // either, or the top of the list picks up hits from a plate
                // nobody can see.
                && slotEdgeFadeFor (i) > kHitFadeFloor
                && labelBox (i).expanded (8.0f).contains (p))
                return i;
        return -1;
    }

    int hitPalette (juce::Point<float> p) const
    {
        if (palette.empty() || ! paletteClip.contains (p))
            return -1;

        for (size_t i = 0; i < palette.size(); ++i)
            if (paletteRect ((int) i).contains (p))
                return (int) i;
        return -1;
    }

    static constexpr int   animFps       = 60;
    static constexpr float maxHoverPhase = 0.22f;   // radians of hover glide
    static constexpr float slotRingR     = 13.0f;

    /** How far a label is pushed out along the rim normal, clear of the hub. */
    static constexpr float kLabelOutset  = 30.0f;

    /** How far past the rim a slot has to be pulled to come off the ring. */
    static constexpr float kRemovePull   = 30.0f;

    // Palette rows shrink from the comfortable size toward the tight one to fit
    // the list in; below that it scrolls. The header is the "PULL OUT" caption.
    static constexpr float kPaletteMaxRowH  = 36.0f;
    static constexpr float kPaletteMinRowH  = 17.0f;
    static constexpr float kPaletteHeaderH  = 34.0f;

    /** The "pull to remove" plate, sitting outboard of the rim. */
    static constexpr float kRemoveHintW   = 104.0f;
    static constexpr float kRemoveHintH   = 34.0f;
    static constexpr float kRemoveHintGap = 26.0f;

    // Visible semicircle (right half of the circle, top -> bottom).
    static constexpr float arcStart  = 0.30f;                                    // ~17 deg
    static constexpr float arcEnd    = juce::MathConstants<float>::pi - 0.30f;   // ~163 deg
    static constexpr float slotPitch = 0.38f;                                    // ~22 deg between chain slots

    // Hover-edge auto-scroll (63C-17): zone depth as a fraction of component
    // height, and full-tilt scroll speed in radians of rim per second.
    static constexpr float kEdgeZoneFrac   = 0.16f;

    /** How much a hovered item's label grows. */
    static constexpr float kHoverFontScale = 1.02f;

    /** Hovered labels light UP in the accent. On the white plate hover went
        grey — darker ink on bright paper reads as "picked out". Inverted onto
        the night panel that same move DIMS the word, which reads as disabled;
        on a dark ground the way to pick something out is to make it glow
        (2026-08-22). */
    static constexpr juce::uint32 kHoverText = SolLookAndFeel::kAccentArc;

    // The orb's ring. The orb itself is unpainted, so this line is the whole
    // of it. On the white plate it was full-strength ink; on the night panel
    // (2026-08-22) that inverts to a near-white circle at kPlateStroke, which
    // becomes the brightest object on screen — a lot of shout for a frame
    // around the goniometer. Dropped to the hairline role so the trace inside
    // it is what draws the eye.
    static constexpr juce::uint32 kRingLight  = SolLookAndFeel::kOutline;

    /** Angular pitch of the rim's panel joints, in radians. Coarse enough that
        the segments read as PLATES rather than as a gear's teeth. */
    static constexpr float kRimSegment        = 0.19f;

    /** How far round the ring stays on the panel, as sin(angle), and the
        span over which a slot fades as it turns away. */
    static constexpr float kVisibleSin        = -0.12f;
    static constexpr float kFadeSpan          = 0.22f;

    /** Breathing room around the palette before a slot starts yielding to it,
        and how solid a slot has to be before it will take a click. */
    static constexpr float kPaletteClearPad   = 6.0f;
    static constexpr float kHitFadeFloor      = 0.45f;

    /** How far the ring sits outside the orb, as a multiple of its radius. */
    static constexpr float kRingOrbGap        = 1.30f;
    static constexpr float kRingDitherAlpha   = 0.14f;

    // Motion trails: a visible streak while moving, gone almost immediately
    // once the label settles.
    static constexpr int   kTrailLength   = 8;     // frames of travel retained
    static constexpr int   kTrailSteps    = 9;     // stamps along the streak
    static constexpr float kTrailMinStep  = 0.9f;  // px before a ghost is kept
    static constexpr float kTrailMinSmear = 2.5f;  // px of travel before drawing
    static constexpr float kTrailAlpha    = 0.75f;
    static constexpr float kEdgeScrollRate = 1.4f;

    /** Radians per second while an edge zone is hovered — fast enough to
        cross the ring deliberately, slow enough to stop where you want. */
    static constexpr float kSteerRate      = 1.5f;

    float fixedRadius = 0.0f;
    float pillW = 104.0f, pillH = 30.0f;
    float itemFontHeight = 13.0f;
    float ringScale      = 1.0f;

    /** Overwritten by the editor to match the plate's border. */
    float ringThickness  = 3.2f;

    /** This frame's window throw, fed in by the editor. */
    juce::Point<float> shakeMotion;
    float orbScale       = 1.0f;
    float orbOffsetRatio = 0.0f;

    juce::Component* hubContent = nullptr;

    /** Per-slot label position history, for the rainbow dither trails. */
    juce::Array<juce::Array<juce::Point<float>>> trails;

    int numSlots = 6;
    std::vector<Item> palette;

    juce::Point<float> centre;
    float radius = 0.0f;
    juce::Rectangle<float> paletteClip;
    float paletteRowH   = kPaletteMaxRowH;   // squeezed in computeGeometry()
    float paletteScroll = 0.0f;
    float rimScroll     = 0.0f;
    float targetPhase   = 0.0f;
    float edgeScrollDir  = 0.0f;
    int   hoveredSlot    = -1;
    int   hoveredPalette = -1;

    DragSource dragSource = DragSource::none;
    int dragPaletteIndex = -1, dragFromSlot = -1, dragTypeId = 0;
    juce::Point<float> dragPos, mouseDownPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelComponent)
};

