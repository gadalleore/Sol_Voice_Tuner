/*
    ChamferPanel.h
    --------------
    The base plate every Sol panel is built on: a straight-edged rectangle with
    the top-right corner sliced off on a diagonal.

    Ported from Giuseppe's React/SVG prototype (`ChamferPanel.tsx`, 2026-07-27).
    The prototype measured the element and stroked a single SVG path in real
    pixels so the outline stayed perfectly even at any size — the JUCE version
    does the same by rebuilding the path in paint() from getLocalBounds(),
    rather than scaling a fixed path (which would smear the stroke width when
    a panel is stretched).

    Geometry, matching the prototype exactly, with i = strokeWidth / 2:

        (i, i) ── (w-i-c, i)
                          ╲
                           (w-i, i+c)
                            │
        (i, h-i) ────── (w-i, h-i)

    Panels hold their content as a child component: call setContent() and the
    child is laid out inside getContentBounds(), which keeps clear of the
    diagonal. Subclasses that lay out their own controls should override
    layoutContent() instead.

    Default colours come from the prototype (warm grey outline on an unfilled
    plate); 63C-10 re-points these at the sun-white frutiger-aero palette.
*/

#pragma once

#include <JuceHeader.h>

#include "SolDither.h"
#include "SolLookAndFeel.h"

class ChamferPanel : public juce::Component
{
public:
    //--------------------------------------------------------------------------
    // Prototype defaults
    //--------------------------------------------------------------------------
    static constexpr float       kDefaultChamfer     = 44.0f;
    static constexpr float       kDefaultStrokeWidth = 6.0f;
    static constexpr float       kDefaultPadding     = 28.0f;
    static constexpr juce::uint32 kDefaultOutline    = 0xff171715; // near-black

    /** Black piping around the plate's border and the close X, so both hold an
        edge against whatever the transparent window is sitting on. Thin on
        purpose: enough to separate, not enough to read as a second frame. */
    static constexpr float kKeyline      = 1.0f;
    static constexpr float kKeylineAlpha = 0.72f;

    //--------------------------------------------------------------------------
    // Degradation
    //--------------------------------------------------------------------------
    static constexpr juce::uint32 kVignetteColour  = 0xff0b0d0c;

    /** Colour the photographic dirt marks are tinted, and how hard its
        brightness maps to alpha. */
    static constexpr juce::uint32 kLensDirtTint     = 0xff1a1a18;
    static constexpr float        kLensDirtStrength = 0.85f;

    /** Giuseppe's own dirt photograph vs the procedural marks. Kept switchable
        so the two can be compared directly. */
    void setUsePhotoDirt (bool shouldUsePhoto)
    {
        if (usePhotoDirt == shouldUsePhoto)
            return;

        usePhotoDirt = shouldUsePhoto;
        lensDirt = {};      // force a rebuild
        repaint();
    }

    void setGrainAlpha (float a)    { grainAlpha    = juce::jlimit (0.0f, 1.0f, a); repaint(); }
    void setVignetteAlpha (float a) { vignetteAlpha = juce::jlimit (0.0f, 1.0f, a); repaint(); }
    void setLensDirtAlpha (float a) { lensDirtAlpha = juce::jlimit (0.0f, 1.0f, a); repaint(); }

    /** Content stays this fraction of the chamfer clear of the sliced corner. */
    static constexpr float kChamferPaddingRatio = 0.35f;

    //--------------------------------------------------------------------------
    // Watermark defaults
    //--------------------------------------------------------------------------
    static constexpr juce::uint32 kDefaultWatermark = 0xffd8d5cf; // light grey

    /** Watermark height as a fraction of the plate's shorter edge. Small and
        discreet — it is a maker's mark, not a headline (Giuseppe, 2026-07-28). */
    static constexpr float kWatermarkHeightRatio = 0.042f;

    /** Watermark never grows past this fraction of the plate's inner width,
        so a very wide panel doesn't get an absurdly large word across it. */
    static constexpr float kWatermarkMaxWidthRatio = 0.62f;

    ChamferPanel()
    {
        // The plate is a surface, not a control: let clicks fall through to
        // whatever hosts it (FloatingShell uses them to drag the window).
        // Children still receive their own clicks.
        setInterceptsMouseClicks (false, true);

        closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
        addAndMakeVisible (closeButton);
    }

    /** Fired by the close X sitting in the chamfer cut. */
    std::function<void()> onClose;

    //--------------------------------------------------------------------------
    /** Builds the plate silhouette for a w x h panel, inset by half the stroke
        so the outline sits fully inside the bounds. Exposed so other components
        can reuse the shape for clipping, shadows or hit-testing. */
    static juce::Path buildPath (float w, float h, float chamfer, float strokeWidth)
    {
        juce::Path p;

        if (w <= 0.0f || h <= 0.0f)
            return p;

        const float i = strokeWidth * 0.5f;
        const float c = juce::jmin (chamfer,
                                    juce::jmax (w - strokeWidth, 0.0f),
                                    juce::jmax (h - strokeWidth, 0.0f));

        p.startNewSubPath (i,         i);
        p.lineTo          (w - i - c, i);
        p.lineTo          (w - i,     i + c);
        p.lineTo          (w - i,     h - i);
        p.lineTo          (i,         h - i);
        p.closeSubPath();

        return p;
    }

    /** The plate silhouette at this panel's current size. */
    juce::Path getPanelPath() const
    {
        return buildPath ((float) getWidth(), (float) getHeight(), chamfer, strokeWidth);
    }

    //--------------------------------------------------------------------------
    // Appearance
    //--------------------------------------------------------------------------
    void setChamfer (float newChamfer)
    {
        chamfer = juce::jmax (0.0f, newChamfer);
        resized();
        repaint();
    }

    void setStrokeWidth (float newStrokeWidth)
    {
        strokeWidth = juce::jmax (0.0f, newStrokeWidth);
        repaint();
    }

    /** Inner padding for panel contents. */
    void setPadding (float newPadding)
    {
        padding = juce::jmax (0.0f, newPadding);
        resized();
    }

    /** Plate fill; default is unfilled, like the prototype's `fill="none"`. */
    void setPanelFill (juce::Colour newFill)
    {
        fill = newFill;
        filled = true;
        repaint();
    }

    void setUnfilled()
    {
        filled = false;
        repaint();
    }

    void setOutlineColour (juce::Colour newStroke)
    {
        stroke = newStroke;
        repaint();
    }

    //--------------------------------------------------------------------------
    // Watermark — brand text sitting on the plate, behind any content
    //--------------------------------------------------------------------------
    /** Sets the background wordmark; pass an empty string to remove it. */
    void setWatermark (const juce::String& text)
    {
        watermark = text;
        repaint();
    }

    void setWatermarkColour (juce::Colour newColour)
    {
        watermarkColour = newColour;
        repaint();
    }

    juce::String getWatermark() const { return watermark; }

    float getChamfer()     const noexcept { return chamfer; }
    float getStrokeWidth() const noexcept { return strokeWidth; }
    float getPadding()     const noexcept { return padding; }

    //--------------------------------------------------------------------------
    /** Content area inside the outline, kept clear of the diagonal corner —
        the right inset carries the extra `chamfer * 0.35` the prototype used. */
    juce::Rectangle<int> getContentBounds() const
    {
        const int pad      = juce::roundToInt (padding);
        const int rightPad = juce::roundToInt (padding + chamfer * kChamferPaddingRatio);

        auto r = getLocalBounds();

        // Content normally sits inside the padding on every side. Bleeding
        // left lets a shape anchored to the wheel's centre — which sits on
        // that edge — run right off the panel instead of stopping short of it.
        if (! bleedLeft)
            r = r.withTrimmedLeft (pad);

        return r.withTrimmedTop    (pad)
                .withTrimmedBottom (pad)
                .withTrimmedRight  (rightPad);
    }

    /** Lets content run to the panel's left edge, ignoring the left padding. */
    void setContentBleedsLeft (bool shouldBleed)
    {
        if (bleedLeft == shouldBleed)
            return;

        bleedLeft = shouldBleed;
        resized();
    }

    /** Hooks a child component into the plate; pass nullptr to detach. The
        panel does not take ownership — the caller keeps the component alive. */
    void setContent (juce::Component* newContent)
    {
        if (content == newContent)
            return;

        if (content != nullptr)
            removeChildComponent (content);

        content = newContent;

        if (content != nullptr)
            addAndMakeVisible (content);

        resized();
    }

    juce::Component* getContent() const noexcept { return content; }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto path = getPanelPath();

        if (path.isEmpty())
            return;

        if (filled)
        {
            g.setColour (fill);
            g.fillPath (path);
        }

        paintWatermark (g, path);

        // Degradation passes, in order: the vignette darkens the surface, then
        // grain sits on top of everything so it reads as one layer of film
        // over the whole plate rather than texture on individual shapes.
        paintVignette (g, path);
        paintLensDirt (g, path);
        SolDither::fillPathGrain (g, path, grainAlpha);

        if (strokeWidth > 0.0f)
        {
            // A black keyline under the border, a hair wider than it
            // (Giuseppe, 2026-08-23). The shell is a transparent desktop
            // window, so the plate's edge lands straight on whatever happens
            // to be behind it — a pale wallpaper, a bright DAW — and a mid-grey
            // hairline has nothing to hold it against a light ground. Black
            // piping does, and it costs one extra stroke of the same path.
            g.setColour (juce::Colours::black.withAlpha (kKeylineAlpha));
            g.strokePath (path, juce::PathStrokeType (strokeWidth + kKeyline * 2.0f,
                                                      juce::PathStrokeType::mitered,
                                                      juce::PathStrokeType::butt));

            g.setColour (stroke);
            g.strokePath (path, juce::PathStrokeType (strokeWidth,
                                                      juce::PathStrokeType::mitered,
                                                      juce::PathStrokeType::butt));
        }
    }

    void resized() override
    {
        const auto area = getContentBounds();

        if (content != nullptr)
            content->setBounds (area);

        layoutCloseButton();
        closeButton.toFront (false);

        layoutContent (area);
    }

protected:
    /** Subclasses place their own controls inside the given content area. */
    virtual void layoutContent (juce::Rectangle<int>) {}

private:
    //==========================================================================
    /** The close control: a bare grey X floating in the sliced corner, with no
        button chrome around it. Sits outside the plate path, so on a
        transparent window it reads as hanging in space. */
    class CloseX final : public juce::Component
    {
    public:
        std::function<void()> onClick;

        CloseX() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        /** Matches the plate's border colour. */
        void setStrokeColour (juce::Colour c) { strokeColour = c; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const bool hot = isMouseOverOrDragging();
            const float thickness = juce::jmax (2.5f, getWidth() * 0.155f);

            // Inset by half the stroke only, so the X reaches the very top and
            // right edges of its square instead of floating inside it.
            auto r = getLocalBounds().toFloat().reduced (thickness * 0.5f);

            // Same black piping as the plate's border, and for the same reason:
            // the X hangs in the cut-away corner with nothing behind it but the
            // desktop, so on anything pale it needs its own edge to read
            // against (Giuseppe, 2026-08-23).
            for (const auto pass : { true, false })
            {
                g.setColour (pass ? juce::Colours::black.withAlpha (kKeylineAlpha)
                                  : (hot ? strokeColour.darker (0.35f) : strokeColour));

                const float t = pass ? thickness + kKeyline * 2.0f : thickness;

                g.drawLine (r.getX(), r.getY(), r.getRight(), r.getBottom(), t);
                g.drawLine (r.getRight(), r.getY(), r.getX(), r.getBottom(), t);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
                onClick();
        }

    private:
        juce::Colour strokeColour { kDefaultOutline };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloseX)
    };

    /** Sizes the X to sit ENTIRELY inside the cut-away triangle, so on a
        transparent window it floats free of the plate rather than straddling
        the diagonal.

        The cut is the triangle (w-c, 0), (w, 0), (w, c) — a right angle at the
        top-right corner with legs of length c. The largest axis-aligned square
        that fits inside it, anchored at that corner, has side c/2: its inner
        corner (w - c/2, c/2) lands exactly on the diagonal. So the X is half
        the chamfer, flush with the top and right edges. */
    void layoutCloseButton()
    {
        const float w = (float) getWidth();
        const float c = juce::jmin (chamfer, w, (float) getHeight());

        // c/2 is the largest square that fits inside the cut; sit well under
        // that so the X stays clear of the diagonal.
        const float size = c * 0.34f;

        if (size < 10.0f)
        {
            closeButton.setVisible (false);
            return;
        }

        closeButton.setVisible (true);
        closeButton.setStrokeColour (stroke);
        closeButton.setBounds (juce::Rectangle<float> (w - size, 0.0f, size, size)
                                   .toNearestInt());
    }

    /** Smudges, dust and streaks, as if seen through a handled lens. Cached at
        the panel's size — regenerating it per frame would both cost far too
        much and make the dirt crawl. */
    void paintLensDirt (juce::Graphics& g, const juce::Path& path)
    {
        if (lensDirtAlpha <= 0.0f || getWidth() <= 0 || getHeight() <= 0)
            return;

        if (lensDirt.isNull()
            || lensDirt.getWidth()  != getWidth()
            || lensDirt.getHeight() != getHeight())
        {
            if (usePhotoDirt)
            {
                // Giuseppe's own dirt plate, embedded. It is a positive — dark
                // marks on a light field — so the conversion inverts: darkness
                // becomes opacity, and the paper it was shot on drops out.
                const auto photo = juce::ImageCache::getFromMemory (
                                       BinaryData::lens_dirt_jpg,
                                       BinaryData::lens_dirt_jpgSize);

                lensDirt = SolDither::tintMaskToOverlay (photo, getWidth(), getHeight(),
                                                         juce::Colour (kLensDirtTint),
                                                         kLensDirtStrength,
                                                         true);   // invert
            }

            // Falls back to the procedural marks if the texture is missing.
            if (lensDirt.isNull())
                lensDirt = SolDither::makeLensDirt (getWidth(), getHeight());
        }

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (path);
        g.setOpacity (lensDirtAlpha);
        g.drawImageAt (lensDirt, 0, 0);
    }

    /** Corners fall away into shadow. Same trick as the orb: hold one RGB and
        vary only alpha, so the midtones do not get dragged toward black. */
    void paintVignette (juce::Graphics& g, const juce::Path& path) const
    {
        if (vignetteAlpha <= 0.0f)
            return;

        const auto b = getLocalBounds().toFloat();

        if (b.isEmpty())
            return;

        // Reach the corners, not just the edges.
        const float reach = std::sqrt (b.getWidth()  * b.getWidth()
                                     + b.getHeight() * b.getHeight()) * 0.5f;

        juce::ColourGradient v (juce::Colour (kVignetteColour).withAlpha (0.0f),
                                b.getCentreX(), b.getCentreY(),
                                juce::Colour (kVignetteColour).withAlpha (vignetteAlpha),
                                b.getCentreX() + reach, b.getCentreY(),
                                true);

        v.addColour (0.45, juce::Colour (kVignetteColour).withAlpha (vignetteAlpha * 0.10f));

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (path);
        g.setGradientFill (v);
        g.fillRect (b);
    }

    /** Draws the brand wordmark centred on the plate, sized from the plate's
        shorter edge so it grows and shrinks with the panel, then capped so a
        very wide window doesn't stretch the word across the whole plate.
        Clipped to the silhouette so it never spills past the chamfer. */
    void paintWatermark (juce::Graphics& g, const juce::Path& path) const
    {
        if (watermark.isEmpty())
            return;

        const auto w = (float) getWidth();
        const auto h = (float) getHeight();

        float fontHeight = juce::jmin (w, h) * kWatermarkHeightRatio;

        if (fontHeight < 1.0f)
            return;

        auto makeFont = [] (float px)
        {
            return juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  px, juce::Font::plain));
        };

        auto font = makeFont (fontHeight);

        // Shrink to fit if the word is wider than its share of the plate.
        const float maxWidth = w * kWatermarkMaxWidthRatio;
        const float textWidth = juce::GlyphArrangement::getStringWidth (font, watermark);

        if (textWidth > maxWidth && textWidth > 0.0f)
        {
            fontHeight *= maxWidth / textWidth;

            if (fontHeight < 1.0f)
                return;

            font = makeFont (fontHeight);
        }

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (path);

        g.setColour (watermarkColour);
        g.setFont (font);
        g.drawText (watermark, getLocalBounds(), juce::Justification::centred, false);
    }

    float chamfer     = kDefaultChamfer;
    float strokeWidth = kDefaultStrokeWidth;
    float padding     = kDefaultPadding;

    bool         filled = false;
    juce::Colour fill   { juce::Colours::transparentBlack };
    juce::Colour stroke { kDefaultOutline };

    juce::String watermark;
    juce::Colour watermarkColour { kDefaultWatermark };

    // Very light: texture you notice only if you look for it.
    float grainAlpha     = 0.0f;   // clinical: no degradation
    float vignetteAlpha  = 0.0f;
    float lensDirtAlpha  = 0.0f;   // smudges off (Giuseppe, 2026-07-31) — the plate
                                   // reads cleaner without them. setLensDirtAlpha()
                                   // brings them back if we want to compare.
    bool  bleedLeft      = false;
    bool  usePhotoDirt   = true;

    juce::Image lensDirt;

    CloseX closeButton;

    juce::Component* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChamferPanel)
};
