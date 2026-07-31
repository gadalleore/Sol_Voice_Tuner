/*
    SpectrumStrip.h
    ---------------
    Black spectrum analyser across the foot of the plate (Gard, 2026-07-31).
    The analysis is Spacedust's, ported over: 2048-point FFT, Hann window, a
    log-frequency axis so every octave gets equal width, and heavy temporal
    averaging that rises faster than it falls so a held note reads as a steady
    bar instead of boiling.

    What is new here is how it sits on the plate. Sol's furniture is black ink
    on white, and so is the spectrum, so bars crossing the type would simply
    swallow it. Instead the strip KNOCKS ITS BARS OUT of whatever is under
    them: it draws the bars, then redraws the elements it overlaps in white,
    clipped to the bars. Type crossed by a bar reads white-on-black for exactly
    the width of that bar and black-on-white either side of it.

    Doing that by inverting the pixels underneath would mean re-rendering the
    whole plate to an image every frame. Instead anything that wants to survive
    the bars implements Inkable and is registered here — the strip asks it to
    redraw itself in one flat colour, which is cheap and exact.

    No frame, no grid, no background: like the meters, only the bars exist.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class SpectrumStrip final : public juce::Component
{
public:
    /** Implemented by anything the bars should knock out rather than cover.

        `paintInk` must redraw whatever the component normally draws in black,
        in `ink` instead, in its OWN local coordinates — the strip sets up the
        transform. It is a stencil, not a second full paint: hover states,
        trails and textures should be left out. */
    struct Inkable
    {
        virtual ~Inkable() = default;
        virtual void paintInk (juce::Graphics& g, juce::Colour ink) = 0;
    };

    SpectrumStrip()
    {
        // Decoration only, and it lies across half the plate — swallowing
        // clicks here would kill the window drag along the whole bottom.
        setInterceptsMouseClicks (false, false);

        fft    = std::make_unique<juce::dsp::FFT> (kFftOrder);
        window = std::make_unique<juce::dsp::WindowingFunction<float>> (
                     kFftSize, juce::dsp::WindowingFunction<float>::hann);

        fftData.assign ((size_t) kFftSize * 2, 0.0f);
        samples.assign ((size_t) kFftSize, 0.0f);
        magnitudes.assign ((size_t) kNumBins, kMinDb);
    }

    /** Registers a component whose ink survives the bars. Neither pointer is
        owned; both must outlive the strip, which is why only siblings declared
        alongside it in the plate get registered. */
    void addInkable (juce::Component* component, Inkable* inkable)
    {
        if (component != nullptr && inkable != nullptr)
            inkables.push_back ({ component, inkable });
    }

    void setSampleRate (double sr) noexcept
    {
        if (sr > 0.0)
            sampleRate = (float) sr;
    }

    /** Set once by the owner: fills `dest` with the most recent `numSamples`
        of gap-free audio. */
    std::function<void (float* dest, int numSamples)> fillSamples;

    /** One frame: pull the newest window of audio, transform it, smooth, draw.
        Driven from the editor's timer rather than one of our own, so the whole
        UI still runs off a single clock. */
    void pull()
    {
        if (! fillSamples || ! isShowing())
            return;

        fillSamples (samples.data(), kFftSize);

        std::copy (samples.begin(), samples.end(), fftData.begin());
        window->multiplyWithWindowingTable (fftData.data(), kFftSize);
        std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        // Smoothed in dB, and only here — decaying inside paint() as well would
        // make the display jitter with the repaint rate.
        for (int i = 1; i < kNumBins; ++i)
        {
            const float mag = fftData[(size_t) i];

            const float db = mag > 0.0f
                ? juce::jlimit (kMinDb, kMaxDb,
                                20.0f * std::log10 (mag / (float) kNumBins + 1.0e-6f) + kDisplayGainDb)
                : kMinDb;

            float& prev = magnitudes[(size_t) i];
            prev += (db - prev) * (db > prev ? kAttack : kRelease);
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth();
        const float h = (float) getHeight();

        if (w <= 0.0f || h <= 0.0f)
            return;

        // ---- Where the bars are -----------------------------------------
        //
        // Collected as a region as well as drawn, because the knock-out pass
        // below needs to clip to exactly the shape that was painted.
        juce::RectangleList<int> bars;

        const float nyquist  = sampleRate * 0.5f;
        const float hiFreq   = juce::jmin (kMaxFreq, nyquist);

        if (hiFreq <= kMinFreq)
            return;

        const float logRatio  = std::log (hiFreq / kMinFreq);
        const float binsPerHz = (float) kFftSize / sampleRate;
        const float topBin    = (float) (kNumBins - 1);

        // Whole-pixel pitch, deliberately.
        //
        // Dividing the width into a fractional column width and rounding each
        // column's left edge independently makes the gaps alternate between 1
        // and 2 px — at a glance the bars look like they clump into uneven
        // groups (Gard, 2026-07-31). An integer pitch gives every bar the same
        // width and every gap the same size, and the few pixels that do not
        // divide evenly are split between the two ends where nothing lines up
        // against them.
        const int numColumns = juce::jmax (1, (int) w / kColumnPitch);
        const int barW       = kColumnPitch - kColumnGap;
        const int originX    = ((int) w - numColumns * kColumnPitch) / 2;

        for (int c = 0; c < numColumns; ++c)
        {
            const float f0 = kMinFreq * std::exp (logRatio * ((float) c       / (float) numColumns));
            const float f1 = kMinFreq * std::exp (logRatio * ((float) (c + 1) / (float) numColumns));

            const float db = peakBetween (juce::jmin (topBin, f0 * binsPerHz),
                                          juce::jmin (topBin, f1 * binsPerHz));

            const float barH = juce::jmap (db, kMinDb, kMaxDb, 0.0f, h) * kBarHeightScale;

            if (barH < 1.0f)
                continue;

            const int top = juce::roundToInt (h - barH);

            bars.addWithoutMerging ({ originX + c * kColumnPitch, top,
                                      barW, juce::roundToInt (h) - top });
        }

        if (bars.isEmpty())
            return;

        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.fillRectList (bars);

        // ---- Knock the furniture back out of them ------------------------
        for (const auto& entry : inkables)
        {
            if (! entry.component->getBounds().intersects (getBounds()))
                continue;

            juce::Graphics::ScopedSaveState saved (g);

            g.reduceClipRegion (bars);
            g.addTransform (juce::AffineTransform::translation (
                                (float) (entry.component->getX() - getX()),
                                (float) (entry.component->getY() - getY())));

            entry.inkable->paintInk (g, juce::Colour (SolLookAndFeel::kBackground));
        }
    }

private:
    /** Peak dB across a span of bins, interpolating below one bin's width so
        the low end keeps a smooth skirt instead of going stepped. */
    float peakBetween (float binLo, float binHi) const
    {
        if (binHi - binLo < 1.0f)
        {
            const float centre = 0.5f * (binLo + binHi);
            const int   b0     = juce::jlimit (1, kNumBins - 2, (int) std::floor (centre));
            const float frac   = juce::jlimit (0.0f, 1.0f, centre - (float) b0);

            return magnitudes[(size_t) b0]
                 + (magnitudes[(size_t) (b0 + 1)] - magnitudes[(size_t) b0]) * frac;
        }

        const int lo = juce::jmax (1,            (int) std::floor (binLo));
        const int hi = juce::jmin (kNumBins - 1, (int) std::ceil  (binHi));

        float peak = kMinDb;

        for (int bin = lo; bin <= hi; ++bin)
            peak = juce::jmax (peak, magnitudes[(size_t) bin]);

        return peak;
    }

    static constexpr int kFftOrder = 11;               // 2048 pt, ~21 Hz/bin
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kNumBins  = kFftSize / 2 + 1;

    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;

    static constexpr float kMinDb         = -90.0f;
    static constexpr float kMaxDb         =   9.0f;
    static constexpr float kDisplayGainDb =   9.0f;

    static constexpr float kAttack  = 0.22f;
    static constexpr float kRelease = 0.12f;

    /** Bar-to-bar pitch and the hairline between them, both in whole pixels:
        barW = pitch - gap. Keep them integers — see the note in paint(). */
    static constexpr int kColumnPitch = 3;
    static constexpr int kColumnGap   = 1;

    static constexpr float kBarHeightScale = 0.80f;  // peaks stop short of the top

    struct Entry
    {
        juce::Component* component;
        Inkable*         inkable;
    };

    std::vector<Entry> inkables;

    std::unique_ptr<juce::dsp::FFT>                        fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>>   window;

    std::vector<float> fftData, samples, magnitudes;

    float sampleRate = 44100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumStrip)
};
