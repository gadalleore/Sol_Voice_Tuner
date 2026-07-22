/*
    WheelComponent.h
    ----------------
    Generic wheel for the v3 paging UI (63C-11, geometry reworked in 63C-17) —
    wheels all the way down:

      * The wheel is a HALF WHEEL: its centre sits on the left edge of the
        component, so the visible part is a semicircle bulging rightward
        (per Gard's sketches). Items are pills on the rim, top = input,
        bottom = output = signal order.
      * The visible half of the hub holds the palette of available items;
        pulling one out onto the rim activates it in that slot (draggable
        mode, e.g. the effects chains). Dragging rim->rim swaps slots;
        dropping off the rim removes. Non-draggable mode (e.g. the Home
        wheel) has fixed items.
      * Chains can hold more slots than fit on the semicircle (25 for the FX
        chains): slots sit at a fixed angular pitch and the rim SCROLLS
        (mouse wheel over the wheel, outside the palette) through them;
        off-arc slots are culled.
      * When the owner sets `onBackClicked`, a "< BACK" pill rides the top of
        the wheel — back lives on the wheel itself (63C-17), clicking it pops
        the page.
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

#include "SolLookAndFeel.h"

class WheelComponent final : public juce::Component,
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

    /** When set, a "< BACK" pill is shown riding the top of the wheel. */
    std::function<void()> onBackClicked;

    int  emptyTypeId     = 0;
    bool allowDuplicates = true;
    bool itemsDraggable  = true;   // false = fixed drill-in items (Home wheel)

    /** Rotation offset (radians, clockwise) applied to every slot angle.
        Eased toward the hover target each frame; animation-pass hook. */
    float wheelPhase = 0.0f;

    void setNumSlots (int n)              { numSlots = juce::jmax (1, n); clampRimScroll(); repaint(); }
    void setPillSize (float w, float h)   { pillW = w; pillH = h; repaint(); }

    void setPalette (std::vector<Item> items)
    {
        palette = std::move (items);
        paletteScroll = 0.0f;
        repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        computeGeometry();

        // The wheel itself: rim circle (left half clips away), active arc, hub.
        {
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.35f));
            g.drawEllipse (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f, 1.2f);

            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               arcStart - 0.10f, arcEnd + 0.10f, true);
            g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.45f));
            g.strokePath (arc, juce::PathStrokeType (3.5f));

            const float hubR = hubRadius();
            g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.55f));
            g.fillEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f);
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.5f));
            g.drawEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.0f);
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

            // Spoke from hub edge to the slot.
            {
                const auto dir = (pos - centre) / juce::jmax (1.0f, pos.getDistanceFrom (centre));
                g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.3f));
                g.drawLine ({ centre + dir * hubRadius(), pos - dir * (pillH * 0.55f) }, 1.0f);
            }

            if (occupied)
            {
                auto pill = pillAround (pos);
                g.setColour (juce::Colour (hovered ? SolLookAndFeel::kPanelLight
                                                   : SolLookAndFeel::kPanel));
                g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);

                g.setColour (juce::Colour ((isTarget || hovered) ? SolLookAndFeel::kOutlineHi
                                                                 : SolLookAndFeel::kOutline));
                g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f,
                                        (isTarget || hovered) ? 2.0f : 1.2f);

                const bool dimmed = dragSource == DragSource::slot && dragFromSlot == i;
                g.setColour (juce::Colour (SolLookAndFeel::kTitleHi)
                                 .withAlpha (dimmed ? 0.35f : 1.0f));
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                g.drawText (nameForType (type), pill, juce::Justification::centred);
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

        // Back pill riding the top of the wheel.
        if (onBackClicked != nullptr)
        {
            const auto pill = backPill();
            g.setColour (juce::Colour (SolLookAndFeel::kPanel)
                             .withAlpha (backHovered ? 1.0f : 0.85f));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (juce::Colour (backHovered ? SolLookAndFeel::kOutlineHi
                                                   : SolLookAndFeel::kAccentGlow)
                             .withAlpha (backHovered ? 1.0f : 0.7f));
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, backHovered ? 2.0f : 1.4f);
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText ("< BACK", pill, juce::Justification::centred);
        }

        // Rim-scroll hints when the chain extends past the visible arc.
        if (scrollableRim())
        {
            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
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
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
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
                g.setFont (juce::Font (juce::FontOptions (12.5f)));
                g.drawText (palette[i].name, pill.reduced (6.0f, 0.0f),
                            juce::Justification::centred);
            }

            g.restoreState();
        }

        // Drag ghost on top of everything.
        if (dragging())
        {
            auto ghost = pillAround (dragPos);
            g.setColour (juce::Colour (SolLookAndFeel::kAccentToggle).withAlpha (0.85f));
            g.fillRoundedRectangle (ghost, ghost.getHeight() * 0.5f);
            g.setColour (juce::Colour (SolLookAndFeel::kOutlineHi));
            g.drawRoundedRectangle (ghost, ghost.getHeight() * 0.5f, 1.6f);
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
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
        hoveredSlot = -1;
        backHovered = false;
        targetPhase = 0.0f;
        startTimerHz (animFps);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        computeGeometry();
        mouseDownPos = e.position;
        pressedBack  = onBackClicked != nullptr && backPill().contains (e.position);

        if (pressedBack)
            return;

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
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool moved  = e.position.getDistanceFrom (mouseDownPos) > 5.0f;
        const auto source = dragSource;
        dragSource = DragSource::none;

        if (pressedBack)
        {
            pressedBack = false;
            if (! moved && onBackClicked != nullptr && backPill().contains (e.position))
                onBackClicked();
            return;
        }

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
        backHovered = onBackClicked != nullptr && backPill().contains (pos);

        const float halfH = juce::jmax (1.0f, (float) getHeight() * 0.5f);
        const float norm  = juce::jlimit (-1.0f, 1.0f, (pos.y - centre.y) / halfH);
        targetPhase = -norm * maxHoverPhase;

        startTimerHz (animFps);
        repaint();
    }

    void timerCallback() override
    {
        const float diff = targetPhase - wheelPhase;

        if (std::abs (diff) < 0.0005f)
        {
            wheelPhase = targetPhase;
            stopTimer();
        }
        else
        {
            wheelPhase += diff * 0.16f;
        }

        repaint();
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
        centre = { b.getX() + 2.0f, b.getCentreY() };

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

    juce::Rectangle<float> pillAround (juce::Point<float> p) const
    {
        return juce::Rectangle<float> (pillW, pillH).withCentre (p);
    }

    /** The back pill rides the top of the wheel, just above the slot arc,
        following the hover glide (wheelPhase) but never the rim scroll. */
    juce::Rectangle<float> backPill() const
    {
        const float a = backAngle + wheelPhase;
        const juce::Point<float> p { centre.x + radius * std::sin (a),
                                     centre.y - radius * std::cos (a) };
        return juce::Rectangle<float> (76.0f, 26.0f).withCentre (p);
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
    static constexpr float backAngle = 0.10f;                                    // back pill anchor (~6 deg)

    float pillW = 104.0f, pillH = 30.0f;

    int numSlots = 6;
    std::vector<Item> palette;

    juce::Point<float> centre;
    float radius = 0.0f;
    juce::Rectangle<float> paletteClip;
    float paletteScroll = 0.0f;
    float rimScroll     = 0.0f;
    float targetPhase   = 0.0f;
    int   hoveredSlot   = -1;
    bool  backHovered   = false;
    bool  pressedBack   = false;

    DragSource dragSource = DragSource::none;
    int dragPaletteIndex = -1, dragFromSlot = -1, dragTypeId = 0;
    juce::Point<float> dragPos, mouseDownPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelComponent)
};
