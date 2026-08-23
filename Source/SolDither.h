/*
    SolDither.h
    -----------
    Shared dither textures for Sol's look (Giuseppe, 2026-07-28).

    Two tiles, both generated once and cached:

      noiseTile()   - monochrome ordered dither. Drawn over a smooth gradient
                      at low alpha it breaks the banding you otherwise get
                      across a large soft ramp, and gives the surface a bit of
                      tooth instead of looking like flat vector fill.

      rainbowTile() - the same ordered-dither stipple, but hue-cycled. Used to
                      fill motion trails: clip to the shape the thing occupied
                      a few frames ago, draw this through the clip, and the
                      trail reads as a stylised rainbow dither rather than a
                      plain ghost.

    Both use an 8x8 Bayer matrix. Ordered dither (not random noise) is what
    gives the regular, deliberate stipple of old dithered graphics; random
    noise just looks like film grain.
*/

#pragma once

#include <cmath>

#include <JuceHeader.h>

namespace SolDither
{
    /** Classic 8x8 Bayer threshold matrix, normalised to 0..1. */
    inline float bayer (int x, int y) noexcept
    {
        static constexpr int m[8][8] = {
            {  0, 32,  8, 40,  2, 34, 10, 42 },
            { 48, 16, 56, 24, 50, 18, 58, 26 },
            { 12, 44,  4, 36, 14, 46,  6, 38 },
            { 60, 28, 52, 20, 62, 30, 54, 22 },
            {  3, 35, 11, 43,  1, 33,  9, 41 },
            { 51, 19, 59, 27, 49, 17, 57, 25 },
            { 15, 47,  7, 39, 13, 45,  5, 37 },
            { 63, 31, 55, 23, 61, 29, 53, 21 }
        };

        return (float) m[y & 7][x & 7] / 64.0f;
    }

    inline constexpr int kTileSize = 64;

    /** Monochrome stipple for knocking the banding out of gradients. */
    inline const juce::Image& noiseTile()
    {
        static const juce::Image tile = []
        {
            juce::Image img (juce::Image::ARGB, kTileSize, kTileSize, true);
            juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < kTileSize; ++y)
                for (int x = 0; x < kTileSize; ++x)
                {
                    // Sparse: only the darkest quarter of the matrix lights up,
                    // so the overlay stays a texture rather than a haze.
                    const float b = bayer (x, y);
                    const juce::uint8 a = b < 0.25f ? (juce::uint8) 46 : (juce::uint8) 0;

                    data.setPixelColour (x, y, juce::Colours::black.withAlpha (a / 255.0f));
                }

            return img;
        }();

        return tile;
    }

    /** Film grain: speckle in BOTH directions, light and dark. A black-only
        stipple disappears against a dark surface; real grain is signal noise,
        so some of it has to be brighter than what it sits on.

        Pseudo-random rather than ordered — grain should look photographic, not
        like a halftone screen, which is the opposite of what bayer() is for. */
    inline const juce::Image& grainTile()
    {
        static const juce::Image tile = []
        {
            juce::Image img (juce::Image::ARGB, kTileSize, kTileSize, true);
            juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);

            juce::Random rng (0x50Lu);   // fixed seed: the grain must not crawl

            for (int y = 0; y < kTileSize; ++y)
                for (int x = 0; x < kTileSize; ++x)
                {
                    const float r = rng.nextFloat();
                    juce::Colour c = juce::Colours::transparentBlack;

                    // Weighted toward the LIGHT speckle since the night-panel
                    // pass (2026-08-22): the ground is now dark, so the bright
                    // grains are the ones that read and the dark ones only
                    // deepen it. Both directions are still present — grain
                    // that only ever goes one way reads as dust, not noise.
                    if (r < 0.16f)        c = juce::Colours::white.withAlpha (0.34f);
                    else if (r < 0.30f)   c = juce::Colours::black.withAlpha (0.40f);
                    else if (r < 0.36f)   c = juce::Colours::white.withAlpha (0.16f);

                    data.setPixelColour (x, y, c);
                }

            return img;
        }();

        return tile;
    }

    enum class Chan { r, g, b };

    /** Single-channel stipple. Three of these, offset from each other along
        the direction of travel, give the RGB smear that old music
        visualisers used — chromatic separation, not a hue sweep. Each channel
        gets a different slice of the Bayer matrix so they interleave rather
        than stacking into white. */
    inline const juce::Image& channelStipple (Chan c)
    {
        auto build = [] (juce::Colour tint, float lo, float hi)
        {
            juce::Image img (juce::Image::ARGB, kTileSize, kTileSize, true);
            juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < kTileSize; ++y)
                for (int x = 0; x < kTileSize; ++x)
                {
                    const float b = bayer (x, y);
                    const bool  on = b >= lo && b < hi;

                    data.setPixelColour (x, y, on ? tint : juce::Colours::transparentBlack);
                }

            return img;
        };

        static const juce::Image red   = build (juce::Colour (0xffff2b2b), 0.00f, 0.34f);
        static const juce::Image green = build (juce::Colour (0xff2bff5a), 0.34f, 0.67f);
        static const juce::Image blue  = build (juce::Colour (0xff2b6bff), 0.67f, 1.00f);

        switch (c)
        {
            case Chan::r: return red;
            case Chan::g: return green;
            case Chan::b: default: return blue;
        }
    }

    /** Tiles `tile` across `area`, at `alpha`. Caller sets any clip first. */
    inline void tileOver (juce::Graphics& g, const juce::Image& tile,
                          juce::Rectangle<float> area, float alpha)
    {
        if (area.isEmpty() || alpha <= 0.0f)
            return;

        juce::Graphics::ScopedSaveState saved (g);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));

        const int x0 = (int) std::floor (area.getX());
        const int y0 = (int) std::floor (area.getY());
        const int x1 = (int) std::ceil  (area.getRight());
        const int y1 = (int) std::ceil  (area.getBottom());

        for (int y = y0; y < y1; y += kTileSize)
            for (int x = x0; x < x1; x += kTileSize)
                g.drawImageAt (tile, x, y);
    }

    /** Stamps `shape`, displaced by `offset`, in one colour channel's stipple. */
    inline void fillPathChannel (juce::Graphics& g, const juce::Path& shape,
                                 juce::Point<float> offset, Chan c, float alpha)
    {
        if (shape.isEmpty() || alpha <= 0.0f)
            return;

        auto ghost = shape;

        if (! offset.isOrigin())
            ghost.applyTransform (juce::AffineTransform::translation (offset.x, offset.y));

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (ghost);
        tileOver (g, channelStipple (c), ghost.getBounds(), alpha);
    }

    /** Draws an RGB streak: `steps` stamps of `shape` spread evenly back along
        `displacement`, cycling red -> green -> blue and fading with distance.

        Separating three channels by a fraction of a one-frame movement is
        invisible — the separation has to span the whole travel. Interleaving
        the channels along the streak is what produces the smeared prism look
        rather than three tidy ghosts. */
    inline void streakRgb (juce::Graphics& g, const juce::Path& shape,
                           juce::Point<float> displacement, int steps, float alpha)
    {
        if (shape.isEmpty() || steps <= 0 || alpha <= 0.0f)
            return;

        static constexpr Chan order[] = { Chan::r, Chan::g, Chan::b };

        // Furthest-back first so nearer, brighter stamps land on top.
        for (int i = steps; i >= 1; --i)
        {
            const float t = (float) i / (float) steps;

            const juce::Point<float> offset { displacement.x * t, displacement.y * t };

            // Linear falloff with distance: far enough back to read as a tail,
            // without the aggressive curve that made it disappear entirely.
            fillPathChannel (g, shape, offset, order[i % 3], alpha * (1.0f - t * 0.75f));
        }
    }

    /** Lays the monochrome stipple over `shape` — used to give a smooth
        gradient some tooth and kill its banding. */
    inline void fillPathDithered (juce::Graphics& g, const juce::Path& shape, float alpha)
    {
        if (shape.isEmpty() || alpha <= 0.0f)
            return;

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (shape);
        tileOver (g, noiseTile(), shape.getBounds(), alpha);
    }

    /** Builds a dirty-lens overlay at the given size: soft smudges, scattered
        dust, and a few wiped streaks — the marks on a lens that has been
        handled, rather than uniform noise.

        Generated once per size and cached by the caller; it is far too
        expensive to rebuild per frame, and it must not change between frames
        or it would shimmer instead of sitting on the glass. */
    inline juce::Image makeLensDirt (int width, int height)
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true);

        juce::Graphics g (img);
        juce::Random rng (0x1E45Lu);   // fixed seed: the dirt is a fixed object

        const float w = (float) width;
        const float h = (float) height;
        const float diag = std::sqrt (w * w + h * h);

        // Everything is drawn DARK. On a white surface a white smudge is
        // invisible by definition — grime on glass over a bright background
        // reads as shadow, not highlight.

        // Smudges: broad, very faint, soft-edged blooms.
        for (int i = 0; i < 26; ++i)
        {
            const float r  = diag * (0.03f + rng.nextFloat() * 0.11f);
            const float cx = rng.nextFloat() * w;
            const float cy = rng.nextFloat() * h;
            const float a  = 0.03f + rng.nextFloat() * 0.06f;

            juce::ColourGradient blob (juce::Colours::black.withAlpha (a), cx, cy,
                                       juce::Colours::black.withAlpha (0.0f), cx + r, cy,
                                       true);
            g.setGradientFill (blob);
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        // Streaks: where a cloth was dragged across the glass.
        for (int i = 0; i < 7; ++i)
        {
            const float cx  = rng.nextFloat() * w;
            const float cy  = rng.nextFloat() * h;
            const float len = diag * (0.10f + rng.nextFloat() * 0.28f);
            const float thk = 2.0f + rng.nextFloat() * 7.0f;
            const float ang = rng.nextFloat() * juce::MathConstants<float>::twoPi;

            juce::Path streak;
            streak.addEllipse (-len * 0.5f, -thk * 0.5f, len, thk);
            streak.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));

            g.setColour (juce::Colours::black.withAlpha (0.03f + rng.nextFloat() * 0.05f));
            g.fillPath (streak);
        }

        // Dust: small dark specks, the only really visible marks.
        for (int i = 0; i < 520; ++i)
        {
            const float cx = rng.nextFloat() * w;
            const float cy = rng.nextFloat() * h;
            const float r  = 0.4f + rng.nextFloat() * 1.9f;

            g.setColour (juce::Colours::black.withAlpha (0.08f + rng.nextFloat() * 0.28f));
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        return img;
    }

    /** Turns a photographic scratch/dirt map into a tinted overlay at the size
        it will be drawn.

        The source is a mask — white marks on black — so its brightness becomes
        the ALPHA of a single tint colour. Drawing the photo directly would
        paint a black rectangle with faint light scratches; what we want is a
        transparent sheet carrying dark marks.

        Scaled first, converted second: the conversion then runs over panel-
        sized pixels rather than the full texture. */
    inline juce::Image tintMaskToOverlay (const juce::Image& mask, int w, int h,
                                          juce::Colour tint, float strength,
                                          bool invert = false)
    {
        if (mask.isNull() || w <= 0 || h <= 0)
            return {};

        juce::Image scaled (juce::Image::RGB, w, h, false);

        {
            juce::Graphics sg (scaled);
            sg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            sg.drawImage (mask, 0, 0, w, h, 0, 0, mask.getWidth(), mask.getHeight());
        }

        juce::Image out (juce::Image::ARGB, w, h, true);

        const juce::Image::BitmapData src (scaled, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float lum = src.getPixelColour (x, y).getBrightness();

                // A photograph of dirt is a POSITIVE — dark marks on a light
                // background — so its darkness is the signal and it has to be
                // inverted. A mask is the other way round: bright marks on
                // black, where brightness is the signal.
                const float signal = invert ? (1.0f - lum) : lum;

                dst.setPixelColour (x, y, tint.withAlpha (juce::jlimit (0.0f, 1.0f, signal * strength)));
            }

        return out;
    }

    /** Lays film grain over `shape`. */
    inline void fillPathGrain (juce::Graphics& g, const juce::Path& shape, float alpha)
    {
        if (shape.isEmpty() || alpha <= 0.0f)
            return;

        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (shape);
        tileOver (g, grainTile(), shape.getBounds(), alpha);
    }
}
