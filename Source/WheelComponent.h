/*
    WheelComponent.h
    ----------------
    Generic wheel for the v3 paging UI (63C-11), used by every page —
    wheels all the way down:

      * Items sit ON the wheel: pills positioned along the rim of a drawn
        wheel (rim + hub), top = input, bottom = output = signal order.
      * The wheel's hub holds the palette of available items; pulling one out
        onto the rim activates it in that slot (draggable mode, e.g. the
        effects chains). Dragging rim->rim swaps slots; dropping off the rim
        removes. Non-draggable mode (e.g. the Home wheel) has fixed items.
      * Clicking an occupied item notifies the page (drill-in via PageStack).
      * The wheel ROTATES with hover: moving the mouse toward the top eases
        the wheel down (revealing items above) and vice versa — a smooth
        eased rotation driven per-frame. The full weighty spring/inertia
        treatment lands in 63C-12; `wheelPhase` stays a plain float so that
        pass can take over the motion.

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

    int  emptyTypeId     = 0;
    bool allowDuplicates = true;
    bool itemsDraggable  = true;   // false = fixed drill-in items (Home wheel)

    /** Rotation offset (radians, clockwise) applied to every slot angle.
        Eased toward the hover target each frame; animation-pass hook. */
    float wheelPhase = 0.0f;

    void setNumSlots (int n)              { numSlots = juce::jmax (1, n); repaint(); }
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

        // The wheel itself: full rim, brighter active arc, hub disc.
        {
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.35f));
            g.drawEllipse (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f, 1.2f);

            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               slotAngle (0) - 0.16f,
                               slotAngle (numSlots - 1) + 0.16f, true);
            g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.45f));
            g.strokePath (arc, juce::PathStrokeType (3.5f));

            const float hubR = hubRadius();
            g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.55f));
            g.fillEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f);
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.5f));
            g.drawEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.0f);
        }

        // Slots on the rim.
        for (int i = 0; i < numSlots; ++i)
        {
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

        // Palette inside the hub.
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
                g.drawText (palette[i].name, pill, juce::Justification::centred);
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
        targetPhase = 0.0f;
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
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool moved  = e.position.getDistanceFrom (mouseDownPos) > 5.0f;
        const auto source = dragSource;
        dragSource = DragSource::none;

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
        if (palette.empty() || ! paletteClip.contains (e.position))
            return;

        const float span = (float) palette.size() * paletteRowH - paletteClip.getHeight();
        paletteScroll = juce::jlimit (0.0f, juce::jmax (0.0f, span),
                                      paletteScroll - wheel.deltaY * 48.0f);
        repaint();
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
    void computeGeometry()
    {
        const auto b = getLocalBounds().toFloat();
        radius = juce::jmin (b.getHeight() * 0.46f, b.getWidth() * 0.52f);
        centre = { b.getX() + b.getWidth() * 0.34f, b.getCentreY() };

        paletteClip = juce::Rectangle<float> (pillW + 24.0f,
                                              juce::jmin (hubRadius() * 1.7f,
                                                          b.getHeight() * 0.62f))
                          .withCentre (centre);
    }

    float hubRadius() const noexcept { return radius * 0.55f; }

    float slotAngle (int i) const
    {
        // Slots along the right-hand quarter-arc, top (input) to bottom (output).
        const float t = numSlots > 1 ? (float) i / (float) (numSlots - 1) : 0.5f;
        return juce::degreesToRadians (45.0f + 90.0f * t) + wheelPhase;
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

    juce::Rectangle<float> paletteRect (int index) const
    {
        const float top = paletteClip.getY() + 20.0f
                        + (float) index * paletteRowH - paletteScroll;
        return { paletteClip.getCentreX() - pillW * 0.5f, top, pillW, pillH };
    }

    int hitSlot (juce::Point<float> p) const
    {
        for (int i = 0; i < numSlots; ++i)
            if (pillAround (slotCentre (i)).expanded (8.0f).contains (p))
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

    float pillW = 104.0f, pillH = 30.0f;

    int numSlots = 6;
    std::vector<Item> palette;

    juce::Point<float> centre;
    float radius = 0.0f;
    juce::Rectangle<float> paletteClip;
    float paletteScroll = 0.0f;
    float targetPhase   = 0.0f;
    int   hoveredSlot   = -1;

    DragSource dragSource = DragSource::none;
    int dragPaletteIndex = -1, dragFromSlot = -1, dragTypeId = 0;
    juce::Point<float> dragPos, mouseDownPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelComponent)
};
