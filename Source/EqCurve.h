/*
    EqCurve.h
    ---------
    The Parametric EQ's curve: five band handles you drag on a plot of what the
    filter is actually doing (Giuseppe, 2026-08-23).

    Until now this effect was twenty-one knobs in a grid — b1Freq, b1Gain, b1Q,
    b1Type, b2Freq... — which is a faithful list of its parameters and a useless
    way to operate an EQ. You cannot hear a number. Every EQ worth using, Space
    Dust's included, is a curve you grab, because the thing you are trying to
    change IS a shape, and the control should be the same object as the readout.

    So: drag a handle sideways for frequency, up and down for gain, scroll on it
    for Q. The four knobs still exist below, bound to the same parameters, for
    when you want to type an exact number or automate one — the curve and the
    knobs are two views of one set of values, not two sets.

    THE CURVE IS NOT AN ILLUSTRATION. It asks the very coefficients the audio
    thread runs — juce::dsp::IIR::Coefficients, built by the same factory calls
    in the same order as SpaceDustFinalEQ::updateCoefficients — for their
    magnitude at each frequency. A hand-derived approximation would be smaller
    code and would start lying the moment either side was touched; this cannot
    drift from the sound, because it IS the sound's own arithmetic.

    Low Pass and High Pass carry no gain (SpaceDustFinalEQ::typeUsesGain), so
    their handles ride the 0 dB line and only move horizontally. Dragging one
    vertically does nothing, which is correct and, with the handle pinned where
    you can see it, obvious.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectParams.h"
#include "SolLookAndFeel.h"
#include "fx/SpaceDustFinalEQ.h"

#include <array>
#include <cmath>
#include <functional>

class EqCurve final : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kNumBands = 5;

    /** Fired when the user picks a band, so the inspector can show that band's
        knobs. */
    std::function<void (int)> onBandSelected;

    /** Fired when a band's TYPE changes, so the inspector can grey out the gain
        knob on a cut. */
    std::function<void()> onBandTypeChanged;

    EqCurve() { setWantsKeyboardFocus (false); }

    /** Point the display at one chain's EQ parameters. `paramIdFor` maps a
        control id ("b1Freq") to its full APVTS id, exactly as the inspector
        does for its knobs. */
    void bind (juce::AudioProcessorValueTreeState& state,
               const std::function<juce::String (const juce::String&)>& paramIdFor)
    {
        apvts = &state;

        for (int b = 0; b < kNumBands; ++b)
        {
            const juce::String n (b + 1);

            bands[(size_t) b] = { fetch (paramIdFor ("b" + n + "Freq")),
                                  fetch (paramIdFor ("b" + n + "Gain")),
                                  fetch (paramIdFor ("b" + n + "Q")),
                                  fetch (paramIdFor ("b" + n + "Type")) };
        }

        curveDirty = true;
        startTimerHz (kPollHz);
        repaint();
    }

    void unbind()
    {
        stopTimer();
        apvts = nullptr;
        bands = {};
    }

    bool isBound() const noexcept { return apvts != nullptr; }

    int  selectedBand() const noexcept { return selected; }

    /** Which shape band `b` is set to, for the inspector's gain-knob greying. */
    SpaceDustFinalEQ::BandType typeOf (int b) const noexcept
    {
        return SpaceDustFinalEQ::typeFromChoiceIndex ((int) std::lround (valueOf (b, Param::type)));
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto plot = plotArea();

        if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
            return;

        // The well. Recessed rather than raised: an EQ curve is something you
        // look INTO, and the whole panel around it is already plate.
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.fillRoundedRectangle (plot, 3.0f);

        paintGrid (g, plot);

        if (! isBound())
            return;

        rebuildIfNeeded (plot);

        // Each band's own contribution, faint, so you can see which handle owns
        // which bump when two of them overlap in the sum.
        for (int b = 0; b < kNumBands; ++b)
        {
            if (std::abs (valueOf (b, Param::gain)) < 0.05f && ! isCut (b))
                continue;                       // flat: nothing to show

            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc)
                             .withAlpha (b == selected ? 0.34f : 0.15f));
            g.strokePath (bandPaths[(size_t) b], juce::PathStrokeType (1.0f));
        }

        // The sum: what the EQ is doing, with a bloom under it. This is a lit
        // element — it is the one thing on the panel that is ALIVE — and the
        // bloom is the palette's own way of saying so.
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.16f));
        g.strokePath (sumPath, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));

        g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
        g.strokePath (sumPath, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved));

        paintHandles (g, plot);
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isBound())
            return;

        const int hit = handleAt (e.position);

        if (hit < 0)
            return;

        select (hit);

        // Right-click / ctrl-click changes the band's shape without going to
        // the dropdown — the handle is the band, so the band's menu belongs on
        // the handle.
        if (e.mods.isPopupMenu())
        {
            showTypeMenu (hit);
            return;
        }

        dragging = hit;
        beginGesture (hit);
        grabOffset = e.position - handlePos (hit, plotArea());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;

        const auto plot = plotArea();
        const auto p    = e.position - grabOffset;

        setValue (dragging, Param::freq, freqAt (p.x, plot));

        // A cut has no gain to drag — its handle stays on the 0 dB line.
        if (! isCut (dragging))
            setValue (dragging, Param::gain, gainAt (p.y, plot));

        curveDirty = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0)
            return;

        endGesture (dragging);
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = handleAt (e.position);

        if (hit < 0 || ! isBound())
            return;

        // Flatten the band. The fastest thing you want from an EQ handle after
        // "move it" is "undo it", and hunting the gain knob back to exactly
        // zero by hand is a job nobody should have.
        select (hit);

        if (isCut (hit))
            return;

        if (auto* p = bands[(size_t) hit].gain)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            p->endChangeGesture();
        }

        curveDirty = true;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int over = handleAt (e.position);

        if (over != hovered)
        {
            hovered = over;
            setMouseCursor (over >= 0 ? juce::MouseCursor::DraggingHandCursor
                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0) { hovered = -1; repaint(); }
    }

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& w) override
    {
        if (! isBound())
            return;

        // Scroll targets whatever is under the pointer, falling back to the
        // selected band, so you can adjust Q without hitting the handle exactly.
        const int b = handleAt (e.position) >= 0 ? handleAt (e.position) : selected;

        if (b < 0 || std::abs (w.deltaY) < 1.0e-4f)
            return;

        // Scrolling AWAY from you narrows the band. That is the convention
        // every EQ uses, and the sign here is inverted to get it: measured on
        // this machine, a wheel-up notch arrives as a NEGATIVE deltaY.
        const float dy = -(w.isReversed ? -w.deltaY : w.deltaY);

        // Geometric, not linear: Q runs 0.1 to 10, so a fixed step is a huge
        // change at the bottom and imperceptible at the top.
        const float q = valueOf (b, Param::q) * std::exp (dy * kQWheelRate);

        if (auto* p = bands[(size_t) b].q)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.1f, 10.0f, q)));
            p->endChangeGesture();
        }

        select (b);
        curveDirty = true;
        repaint();
    }

    void resized() override { curveDirty = true; }

private:
    //==========================================================================
    enum class Param { freq, gain, q, type };

    struct BandRef
    {
        juce::RangedAudioParameter* freq = nullptr;
        juce::RangedAudioParameter* gain = nullptr;
        juce::RangedAudioParameter* q    = nullptr;
        juce::RangedAudioParameter* type = nullptr;
    };

    juce::RangedAudioParameter* fetch (const juce::String& id) const
    {
        return apvts != nullptr ? apvts->getParameter (id) : nullptr;
    }

    juce::RangedAudioParameter* paramFor (int b, Param which) const
    {
        if (! juce::isPositiveAndBelow (b, kNumBands))
            return nullptr;

        const auto& r = bands[(size_t) b];

        switch (which)
        {
            case Param::freq: return r.freq;
            case Param::gain: return r.gain;
            case Param::q:    return r.q;
            case Param::type: return r.type;
        }
        return nullptr;
    }

    float valueOf (int b, Param which) const
    {
        auto* p = paramFor (b, which);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void setValue (int b, Param which, float v)
    {
        if (auto* p = paramFor (b, which))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    void beginGesture (int b) { for (auto w : { Param::freq, Param::gain }) if (auto* p = paramFor (b, w)) p->beginChangeGesture(); }
    void endGesture   (int b) { for (auto w : { Param::freq, Param::gain }) if (auto* p = paramFor (b, w)) p->endChangeGesture(); }

    bool isCut (int b) const
    {
        return ! SpaceDustFinalEQ::typeUsesGain (typeOf (b));
    }

    void select (int b)
    {
        if (b == selected)
            return;

        selected = b;

        if (onBandSelected != nullptr)
            onBandSelected (b);

        repaint();
    }

    void showTypeMenu (int b)
    {
        juce::PopupMenu m;
        const auto names = SpaceDustFinalEQ::typeChoices();
        const int  cur   = (int) std::lround (valueOf (b, Param::type));

        for (int i = 0; i < names.size(); ++i)
            m.addItem (i + 1, names[i], true, i == cur);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, b] (int choice)
                         {
                             if (choice <= 0)
                                 return;

                             if (auto* p = paramFor (b, Param::type))
                             {
                                 p->beginChangeGesture();
                                 p->setValueNotifyingHost (p->convertTo0to1 ((float) (choice - 1)));
                                 p->endChangeGesture();
                             }

                             curveDirty = true;

                             if (onBandTypeChanged != nullptr)
                                 onBandTypeChanged();

                             repaint();
                         });
    }

    //==========================================================================
    // Geometry. Frequency is logarithmic because hearing is; gain is linear in
    // dB because that is what the numbers on the knob say.
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced (kPlotInsetX, kPlotInsetY)
                                          .withTrimmedBottom (kScaleH);
    }

    static float xForFreq (float f, juce::Rectangle<float> plot)
    {
        const float t = std::log (juce::jlimit (kMinHz, kMaxHz, f) / kMinHz)
                      / std::log (kMaxHz / kMinHz);
        return plot.getX() + t * plot.getWidth();
    }

    static float freqAt (float x, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        return kMinHz * std::pow (kMaxHz / kMinHz, t);
    }

    static float yForGain (float db, juce::Rectangle<float> plot)
    {
        return plot.getCentreY() - juce::jlimit (-kRangeDb, kRangeDb, db)
                                       * plot.getHeight() * 0.5f / kRangeDb;
    }

    static float gainAt (float y, juce::Rectangle<float> plot)
    {
        return juce::jlimit (-kMaxGainDb, kMaxGainDb,
                             (plot.getCentreY() - y) * kRangeDb * 2.0f
                                 / juce::jmax (1.0f, plot.getHeight()));
    }

    juce::Point<float> handlePos (int b, juce::Rectangle<float> plot) const
    {
        return { xForFreq (valueOf (b, Param::freq), plot),
                 yForGain (isCut (b) ? 0.0f : valueOf (b, Param::gain), plot) };
    }

    int handleAt (juce::Point<float> p) const
    {
        if (! isBound())
            return -1;

        const auto plot = plotArea();

        // Nearest wins, so overlapping handles are still separable — first-hit
        // would always give you the same one of a stacked pair.
        int   best = -1;
        float bestDist = kHandleHit;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float d = p.getDistanceFrom (handlePos (b, plot));

            if (d < bestDist) { bestDist = d; best = b; }
        }

        return best;
    }

    //==========================================================================
    void paintGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 8.5f, juce::Font::plain)));

        // Decade lines, plus the 2/5 marks inside each — the standard EQ ruler,
        // and the only way a log axis is readable without labelling every line.
        for (const float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                               2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForFreq (f, plot);
            const bool  decade = (f == 100.0f || f == 1000.0f || f == 10000.0f);

            g.setColour (juce::Colour (SolLookAndFeel::kOutline)
                             .withAlpha (decade ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

            if (! decade && f != 20000.0f)
                continue;

            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.55f));
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k"
                                     : juce::String ((int) f),
                        juce::Rectangle<float> (x - 16.0f, plot.getBottom() + 1.0f, 32.0f, kScaleH)
                            .toNearestInt(),
                        juce::Justification::centred);
        }

        // dB lines. The 0 line is drawn brighter: it is the only one that means
        // anything on its own — everything above it is boost and below is cut.
        for (const float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = yForGain (db, plot);

            g.setColour (juce::Colour (db == 0.0f ? SolLookAndFeel::kOutlineHi
                                                  : SolLookAndFeel::kOutline)
                             .withAlpha (db == 0.0f ? 0.45f : 0.45f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

            if (db == 0.0f)
                continue;

            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.4f));
            g.drawText (juce::String ((int) db),
                        juce::Rectangle<float> (plot.getX() + 2.0f, y - 8.0f, 24.0f, 10.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }

        g.setColour (juce::Colour (SolLookAndFeel::kOutline));
        g.drawRoundedRectangle (plot, 3.0f, 1.0f);
    }

    void paintHandles (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto  p   = handlePos (b, plot);
            const bool  on  = (b == selected);
            const bool  hot = (b == hovered) || (b == dragging);
            const float r   = on ? kHandleR + 1.5f : kHandleR;

            // A cut's handle is drawn hollow: it does not carry gain, and a
            // solid dot on the 0 dB line looks like a bell set flat.
            const auto ink = juce::Colour (SolLookAndFeel::kAccentArc);

            if (hot || on)
            {
                g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (hot ? 0.30f : 0.18f));
                g.fillEllipse (juce::Rectangle<float> (r * 3.0f, r * 3.0f).withCentre (p));
            }

            const auto dot = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p);

            if (isCut (b))
            {
                g.setColour (juce::Colour (SolLookAndFeel::kBackground));
                g.fillEllipse (dot);
                g.setColour (ink);
                g.drawEllipse (dot, 1.8f);
            }
            else
            {
                g.setColour (ink.withAlpha (on || hot ? 1.0f : 0.82f));
                g.fillEllipse (dot);
            }

            // The band number, so a handle and its knobs are the same object.
            g.setColour (juce::Colour (isCut (b) ? SolLookAndFeel::kAccentArc
                                                 : SolLookAndFeel::kBackground));
            g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                      8.5f, juce::Font::bold)));
            g.drawText (juce::String (b + 1), dot.toNearestInt(), juce::Justification::centred);
        }
    }

    //==========================================================================
    /** Rebuilds the response paths from the SAME coefficients the audio thread
        uses. One point per pixel column: the curve is never smoother than the
        screen it is drawn on, so anything finer is thrown away. */
    void rebuildIfNeeded (juce::Rectangle<float> plot)
    {
        if (! curveDirty)
            return;

        curveDirty = false;

        const double sr = sampleRate();
        const int    n  = juce::jlimit (2, 1024, (int) plot.getWidth());

        sumPath.clear();
        for (auto& p : bandPaths)
            p.clear();

        // Coefficients once per band, not once per point.
        std::array<juce::dsp::IIR::Coefficients<float>::Ptr, kNumBands> coeffs;

        for (int b = 0; b < kNumBands; ++b)
            coeffs[(size_t) b] = coefficientsFor (b, sr);

        for (int i = 0; i < n; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            const float f = freqAt (x, plot);

            // Above Nyquist there is no response to plot, and asking for one
            // returns nonsense. Hold the curve flat there rather than drawing
            // a cliff that is an artefact of the sample rate, not the EQ.
            const double probe = juce::jmin ((double) f, sr * 0.5 - 1.0);

            float sumDb = 0.0f;

            for (int b = 0; b < kNumBands; ++b)
            {
                if (coeffs[(size_t) b] == nullptr)
                    continue;

                const float db = juce::Decibels::gainToDecibels (
                    (float) coeffs[(size_t) b]->getMagnitudeForFrequency (probe, sr), -60.0f);

                sumDb += db;
                addTo (bandPaths[(size_t) b], i, x, yForGain (db, plot));
            }

            addTo (sumPath, i, x, yForGain (sumDb, plot));
        }
    }

    static void addTo (juce::Path& p, int i, float x, float y)
    {
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    /** The same switch as SpaceDustFinalEQ::updateCoefficients, on the same
        clamped values. Kept literally parallel to it — if a shape is ever added
        there, this is the other half of the change. */
    juce::dsp::IIR::Coefficients<float>::Ptr coefficientsFor (int b, double sr) const
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;

        const float freq = juce::jlimit (20.0f, 20000.0f, valueOf (b, Param::freq));
        const float gain = juce::jlimit (-15.0f, 15.0f,   valueOf (b, Param::gain));
        const float q    = juce::jlimit (0.1f, 10.0f,     valueOf (b, Param::q));
        const float A    = std::pow (10.0f, gain / 20.0f);

        // A corner at or above Nyquist is not representable, and the factories
        // assert on it. The audio thread never sees this because its sample
        // rate is whatever the host gave it; the display can be asked to draw
        // 20 kHz at 44.1 k, where the top of the axis is past the limit.
        const float safe = (float) juce::jmin ((double) freq, sr * 0.49);

        switch (typeOf (b))
        {
            case SpaceDustFinalEQ::BandType::LowShelf:  return Coeffs::makeLowShelf  (sr, safe, q, A);
            case SpaceDustFinalEQ::BandType::HighShelf: return Coeffs::makeHighShelf (sr, safe, q, A);
            case SpaceDustFinalEQ::BandType::LowPass:   return Coeffs::makeLowPass   (sr, safe, q);
            case SpaceDustFinalEQ::BandType::HighPass:  return Coeffs::makeHighPass  (sr, safe, q);
            case SpaceDustFinalEQ::BandType::Bell:
            default:                                    return Coeffs::makePeakFilter (sr, safe, q, A);
        }
    }

    double sampleRate() const
    {
        const double sr = apvts != nullptr ? apvts->processor.getSampleRate() : 0.0;
        return sr > 0.0 ? sr : 48000.0;
    }

    //==========================================================================
    /** Polled rather than listened to: parameterChanged() can arrive on the
        audio thread, and twenty values is nothing to compare. This is what
        makes host automation and preset loads show up on the curve. */
    void timerCallback() override
    {
        if (! isBound())
            return;

        bool changed = false;
        bool typeChanged = false;

        for (int b = 0; b < kNumBands; ++b)
        {
            for (const auto w : { Param::freq, Param::gain, Param::q, Param::type })
            {
                auto* p = paramFor (b, w);

                if (p == nullptr)
                    continue;

                const float v = p->getValue();
                auto& cached = lastSeen[(size_t) b][(size_t) w];

                if (std::abs (v - cached) > 1.0e-6f)
                {
                    cached  = v;
                    changed = true;
                    typeChanged = typeChanged || (w == Param::type);
                }
            }
        }

        if (! changed)
            return;

        curveDirty = true;

        if (typeChanged && onBandTypeChanged != nullptr)
            onBandTypeChanged();

        repaint();
    }

    //==========================================================================
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;

    /** The plot's vertical span, and the furthest a band can actually be
        pushed. Drawn wider than the parameter allows so a maxed band does not
        touch the ceiling — a curve pinned to the frame reads as clipped. */
    static constexpr float kRangeDb   = 18.0f;
    static constexpr float kMaxGainDb = 15.0f;

    static constexpr float kPlotInsetX = 4.0f;
    static constexpr float kPlotInsetY = 4.0f;
    static constexpr float kScaleH     = 11.0f;

    static constexpr float kHandleR   = 7.0f;
    static constexpr float kHandleHit = 15.0f;

    /** Q multiplier per unit of wheel travel, applied through exp(). A notch
        arrives as about 0.23 of travel, so this puts one notch at roughly
        1.35x — a few notches is a real change, and one is still fine enough
        to land on a value. At 0.45 the whole wheel barely moved it. */
    static constexpr float kQWheelRate = 1.3f;

    static constexpr int kPollHz = 20;

    juce::AudioProcessorValueTreeState* apvts = nullptr;
    std::array<BandRef, kNumBands> bands {};

    /** Normalised values as of the last poll, for change detection. */
    std::array<std::array<float, 4>, kNumBands> lastSeen {};

    juce::Path sumPath;
    std::array<juce::Path, kNumBands> bandPaths;
    bool curveDirty = true;

    int selected = 0;
    int hovered  = -1;
    int dragging = -1;
    juce::Point<float> grabOffset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurve)
};
