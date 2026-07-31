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
            if (slotVisible (i) && pillAround (slotCentre (i)).contains (p))
                return true;

        // Hover-scroll strips, but only when there is actually a chain to
        // scroll — otherwise they would eat drags along the top and bottom.
        if (scrollableRim())
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

        // Slots on the rim (culled to the visible arc).
        for (int i = 0; i < numSlots; ++i)
        {
            if (! slotVisible (i))
                continue;

            const auto pos      = slotCentre (i);
            const int  type     = slotType (i);
            const bool occupied = type != emptyTypeId;
            const bool isTarget = dragging() && i == hitSlot (dragPos);
            const bool hovered  = ! dragging() && i == hoveredSlot;

            // No spokes: items sit on the rim unattached (Giuseppe, 2026-07-28).

            if (occupied)
            {
                // No pill shape: the label alone is the item. Hover and drag
                // states now read through colour rather than a border.
                auto pill = pillAround (pos);

                const bool dimmed = dragSource == DragSource::slot && dragFromSlot == i;

                // Laid out as a path once, then reused: the trail just draws
                // the same shape translated back to where the label was on
                // earlier frames.
                const auto textPath = labelPath (i);

                // Rainbow dither wherever this label has just been.
                paintTrail (g, textPath, pos, trailFor (i));

                g.setColour (juce::Colour (hovered ? kHoverText
                                                   : SolLookAndFeel::kTitleHi)
                                 .withAlpha (dimmed ? 0.35f : 1.0f));
                g.fillPath (textPath);
            }
            else
            {
                juce::Path ring;
                ring.addEllipse (pos.x - slotRingR, pos.y - slotRingR,
                                 slotRingR * 2.0f, slotRingR * 2.0f);
                juce::Path dashed;
                const float dashes[] = { 4.0f, 4.0f };
                juce::PathStrokeType (isTarget ? 2.2f : 1.4f)
                    .createDashedStroke (dashed, ring, dashes, 2);
                g.setColour (juce::Colour (isTarget ? SolLookAndFeel::kOutlineHi
                                                    : SolLookAndFeel::kOutline)
                                 .withAlpha (isTarget ? 0.95f : 0.6f));
                g.fillPath (dashed);
            }
        }

        // Rim-scroll hints when the chain extends past the visible arc.
        if (scrollableRim())
        {
            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 10.0f, juce::Font::plain)));
            if (rimScroll > 0.001f)
                g.drawText ("^ more", scrollHintTop(),    juce::Justification::centredLeft);
            if (rimScroll < maxRimScroll() - 0.001f)
                g.drawText ("v more", scrollHintBottom(), juce::Justification::centredLeft);
        }

        // Palette inside the visible half of the hub.
        if (! palette.empty())
        {
            g.saveState();
            g.reduceClipRegion (paletteClip.toNearestInt());

            auto header = paletteClip;
            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 10.5f, juce::Font::plain)));
            g.drawText ("PULL OUT", header.removeFromTop (16.0f),
                        juce::Justification::centred);

            for (size_t i = 0; i < palette.size(); ++i)
            {
                const auto pill = paletteRect ((int) i);
                if (! pill.intersects (paletteClip))
                    continue;

                const bool isDragged = dragSource == DragSource::palette
                                    && dragPaletteIndex == (int) i;

                g.setColour (juce::Colour (SolLookAndFeel::kPanel)
                                 .withAlpha (isDragged ? 0.4f : 1.0f));
                g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
                g.setColour (juce::Colour (SolLookAndFeel::kOutline));
                g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);

                g.setColour (juce::Colour (SolLookAndFeel::kLabel)
                                 .withAlpha (isDragged ? 0.5f : 1.0f));
                g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 12.5f, juce::Font::plain)));
                g.drawText (palette[i].name, pill.reduced (6.0f, 0.0f),
                            juce::Justification::centred);
            }

            g.restoreState();
        }

        // Drag ghost on top of everything. Bare text, like the items — no pill
        // behind it (Giuseppe, 2026-07-28: no ovals around the words).
        if (dragging())
        {
            auto ghost = pillAround (dragPos);
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi).withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
            g.drawText (nameForType (dragTypeId), ghost, juce::Justification::centred);
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
        hoveredSlot   = -1;
        edgeScrollDir = 0.0f;
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
                applySlotType (target, dragTypeId);
        }
        else if (source == DragSource::slot && target != dragFromSlot)
        {
            if (target >= 0)
            {
                // Swap with the target slot (also handles dropping onto a vacancy).
                applySlotType (dragFromSlot, slotType (target));
                applySlotType (target, dragTypeId);
            }
            else
            {
                // Dropped off the rim: remove.
                applySlotType (dragFromSlot, emptyTypeId);
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
            const float span = (float) palette.size() * paletteRowH - paletteClip.getHeight();
            paletteScroll = juce::jlimit (0.0f, juce::jmax (0.0f, span),
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
        hoveredSlot = hitSlot (pos);

        const float halfH = juce::jmax (1.0f, (float) getHeight() * 0.5f);
        const float norm  = juce::jlimit (-1.0f, 1.0f, (pos.y - centre.y) / halfH);
        targetPhase = -norm * maxHoverPhase;

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
        if (! scrollableRim())
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

        // Hover glide toward the target rotation.
        const float diff = targetPhase - wheelPhase;
        if (std::abs (diff) < 0.0005f)
        {
            wheelPhase = targetPhase;
        }
        else
        {
            wheelPhase += diff * 0.16f;
            busy = true;
        }

        // Continuous edge-zone rim scrolling (also active mid-drag).
        if (edgeScrollDir != 0.0f && scrollableRim())
        {
            const float before = rimScroll;
            rimScroll = juce::jlimit (0.0f, maxRimScroll(),
                                      rimScroll + edgeScrollDir * kEdgeScrollRate / (float) animFps);
            if (rimScroll != before)
                busy = true;

            if (dragging())
                busy = true;    // keep ticking while a drag holds the zone
            else
                hoveredSlot = -1;
        }

        // Keep ticking while any smear is still draining, otherwise the last
        // frame of trail would freeze on screen when motion stops.
        if (trailsDraining())
            busy = true;

        if (! busy && edgeScrollDir == 0.0f)
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
        radius = juce::jmin (b.getHeight() * 0.52f, b.getWidth() * 0.62f);
        centre = { b.getX(), b.getCentreY() };   // flush to the edge

        const float hubR = hubRadius();
        const float palW = juce::jlimit (72.0f, 150.0f, hubR - 14.0f);
        const float palH = juce::jmin (hubR * 1.6f, b.getHeight() * 0.62f);
        paletteClip = { centre.x + 8.0f, centre.y - palH * 0.5f, palW, palH };

        clampRimScroll();
    }

    float hubRadius() const noexcept { return radius * 0.62f; }

    bool scrollableRim() const noexcept
    {
        return itemsDraggable
            && (float) (numSlots - 1) * slotPitch > (arcEnd - arcStart) + 0.001f;
    }

    float maxRimScroll() const noexcept
    {
        return juce::jmax (0.0f, (float) (numSlots - 1) * slotPitch - (arcEnd - arcStart));
    }

    void clampRimScroll() noexcept
    {
        rimScroll = juce::jlimit (0.0f, maxRimScroll(), rimScroll);
    }

    float slotAngle (int i) const
    {
        if (! scrollableRim())
        {
            // Few enough slots: spread them evenly across the semicircle.
            const float t = ((float) i + 0.5f) / (float) numSlots;
            return arcStart + (arcEnd - arcStart) * t + wheelPhase;
        }

        // Many slots: fixed pitch + rim scroll.
        return arcStart + (float) i * slotPitch - rimScroll + wheelPhase;
    }

    bool slotVisible (int i) const
    {
        const float a = slotAngle (i);
        return a >= arcStart - slotPitch * 0.45f
            && a <= arcEnd   + slotPitch * 0.45f;
    }

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
        const auto  pill    = pillAround (slotCentre (slot));
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
        const float top = paletteClip.getY() + 20.0f
                        + (float) index * paletteRowH - paletteScroll;
        return { paletteClip.getX() + 4.0f, top,
                 paletteClip.getWidth() - 8.0f, pillH };
    }

    int hitSlot (juce::Point<float> p) const
    {
        for (int i = 0; i < numSlots; ++i)
            if (slotVisible (i)
                && pillAround (slotCentre (i)).expanded (8.0f).contains (p))
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
    static constexpr float paletteRowH   = 36.0f;

    // Visible semicircle (right half of the circle, top -> bottom).
    static constexpr float arcStart  = 0.30f;                                    // ~17 deg
    static constexpr float arcEnd    = juce::MathConstants<float>::pi - 0.30f;   // ~163 deg
    static constexpr float slotPitch = 0.38f;                                    // ~22 deg between chain slots

    // Hover-edge auto-scroll (63C-17): zone depth as a fraction of component
    // height, and full-tilt scroll speed in radians of rim per second.
    static constexpr float kEdgeZoneFrac   = 0.16f;

    /** How much a hovered item's label grows. */
    static constexpr float kHoverFontScale = 1.02f;

    /** Hovered labels go grey rather than picking up the accent colour. */
    static constexpr juce::uint32 kHoverText = 0xff868682;  // grey on hover

    // The orb's ring. The orb itself is unpainted, so this line is the whole
    // of it — hence solid black rather than the old near-black hairline.
    static constexpr juce::uint32 kRingLight  = 0xff0d0d0c;  // black ink

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
    float paletteScroll = 0.0f;
    float rimScroll     = 0.0f;
    float targetPhase   = 0.0f;
    float edgeScrollDir = 0.0f;
    int   hoveredSlot   = -1;

    DragSource dragSource = DragSource::none;
    int dragPaletteIndex = -1, dragFromSlot = -1, dragTypeId = 0;
    juce::Point<float> dragPos, mouseDownPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelComponent)
};

