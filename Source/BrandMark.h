/*
    BrandMark.h
    -----------
    The 63C mark, at the foot of the right-hand column (Gard, 2026-07-31).

    The asset is white on transparent — drawn for dark surfaces — and Sol's
    plate is white, so it would be invisible as shipped. Rather than keep a
    second, inverted copy of the PNG in the tree for someone to forget to
    update, the recolour happens here: the alpha channel already carries the
    entire shape (including its antialiased edges), so ink is just that alpha
    with the plate's own text colour behind it. Redraw it white for a dark
    theme by changing one constant, not by re-exporting the file.

    The recolour is JUCE's own alpha-mask fill — drawImage's
    `fillAlphaChannelWithCurrentBrush` — rather than a hand-rolled pass over
    the pixels. Same result, one call, and it goes through the renderer instead
    of round-tripping every pixel through premultiplied ARGB and back.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"
#include "SpectrumStrip.h"

class BrandMark final : public juce::Component,
                        public SpectrumStrip::Inkable
{
public:
    /** The source art's aspect, used to size the component from a width. */
    static constexpr float kAspect = 808.0f / 232.0f;

    BrandMark()
    {
        // A mark, not a control — clicks belong to the plate underneath.
        setInterceptsMouseClicks (false, false);
    }

    void setInkColour (juce::Colour c)
    {
        if (ink == c)
            return;

        ink = c;
        repaint();
    }

    void paint (juce::Graphics& g) override { draw (g, ink); }

    /** Knocked back out of the spectrum's bars in white — same mark, same
        placement, opposite colour. */
    void paintInk (juce::Graphics& g, juce::Colour colour) override { draw (g, colour); }

private:
    void draw (juce::Graphics& g, juce::Colour colour)
    {
        const auto source = juce::ImageCache::getFromMemory (BinaryData::logo_63c_png,
                                                             BinaryData::logo_63c_pngSize);

        if (source.isNull() || getWidth() <= 0 || getHeight() <= 0)
            return;

        // The art is a flat silhouette: every bit of shape it has lives in the
        // alpha channel, so the source's own white is discarded and the mask
        // is filled with ink instead. That last argument is the whole trick.
        g.setColour (colour);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (source, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::centred,
                     true);   // fillAlphaChannelWithCurrentBrush
    }

    juce::Colour ink { juce::Colour (SolLookAndFeel::kTitleHi) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrandMark)
};
