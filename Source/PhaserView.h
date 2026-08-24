/*
    PhaserView.h
    ------------
    Where the Phaser's notches are, and where they are going (Giuseppe,
    2026-08-23).

    A phaser is the one effect on this panel whose controls tell you almost
    nothing about what it is doing. "Center 400 Hz, Depth 0.7, Stages 4" is
    three numbers describing a moving comb filter, and no one reads that and
    hears the sweep. So the panel shows it: a log-frequency strip with the
    notches marked, riding up and down their range in step with the audio.

    THE NOTCH POSITIONS ARE DERIVED FROM THE DSP, not drawn to look right.
    SpaceDustPhaser cascades N identical first-order all-pass sections at one
    corner frequency f. Each contributes a phase shift of -2·atan(f_probe / f),
    so the cascade cancels against the dry path wherever the total reaches an
    odd multiple of pi:

        N · 2·atan(f_notch / f) = (2k+1)·pi
        f_notch = f · tan((2k+1)·pi / 2N)          for 2k+1 < N

    which gives two notches at 4 stages and three at 6 — exactly the "4 = classic
    Phase 90, 6 = deeper swirl" the DSP's own header describes. The corner f
    sweeps between centre/(1+2·depth) and centre·(1+2·depth), which is
    updateAllPassCoefficients' range verbatim.

    The live position comes from the audio thread (VocalEffect::displayValue ->
    EffectChain's metered slot), NOT from a second LFO running here. A copy of
    the oscillator started at a different moment would look completely
    convincing while describing a sweep nobody is hearing, which is worse than
    showing nothing.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

#include <cmath>

class PhaserView final : public juce::Component
{
public:
    PhaserView() { setInterceptsMouseClicks (false, false); }

    /** From the effect's controls. `stages` is 4 or 6. */
    void setShape (float centreHz, float depth, int stages)
    {
        if (std::abs (centre - centreHz) < 0.01f
            && std::abs (depthAmt - depth) < 0.001f
            && numStages == stages)
            return;

        centre    = centreHz;
        depthAmt  = depth;
        numStages = stages;
        repaint();
    }

    /** 0..1 across the sweep, straight off the audio thread. Negative parks
        the marker in the middle — no reading rather than a wrong one. */
    void setSweep (float position)
    {
        const float p = position < 0.0f ? 0.5f : juce::jlimit (0.0f, 1.0f, position);

        if (std::abs (p - sweep) < 0.002f)
            return;

        sweep = p;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        if (r.getWidth() < 40.0f || r.getHeight() < 16.0f)
            return;

        // The well, like the EQ's — this is something you look into.
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.fillRoundedRectangle (r, 3.0f);

        // Decade marks, unlabelled: the strip is 30px tall and its job is to
        // show MOVEMENT, not to be read off. The octave ticks are enough to
        // tell you which end is which.
        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.55f));

        for (const float f : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine ((int) xFor (f, r), r.getY() + 2.0f, r.getBottom() - 2.0f);

        const float lo = juce::jlimit (kMinHz, kMaxHz, centre / (1.0f + depthAmt * 2.0f));
        const float hi = juce::jlimit (kMinHz, kMaxHz, centre * (1.0f + depthAmt * 2.0f));
        const float now = lo + (hi - lo) * sweep;

        // Each notch: the band it travels through, then where it is.
        for (int k = 0; 2 * k + 1 < numStages; ++k)
        {
            const float mul = std::tan ((float) (2 * k + 1) * juce::MathConstants<float>::pi
                                            / (2.0f * (float) numStages));

            const float bandL = xFor (lo * mul, r);
            const float bandR = xFor (hi * mul, r);

            // The travel, as a dim band. Says what the Depth knob is worth
            // without you having to move it and listen.
            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.13f));
            g.fillRect (juce::Rectangle<float> (bandL, r.getY() + 2.0f,
                                                juce::jmax (1.0f, bandR - bandL),
                                                r.getHeight() - 4.0f));

            // The notch itself, with a bloom: this is the lit, live element.
            const float x = xFor (now * mul, r);

            g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.28f));
            g.fillRect (x - 2.5f, r.getY() + 2.0f, 5.0f, r.getHeight() - 4.0f);

            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
            g.fillRect (x - 0.9f, r.getY() + 2.0f, 1.8f, r.getHeight() - 4.0f);
        }

        g.setColour (juce::Colour (SolLookAndFeel::kOutline));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    static constexpr int kHeight = 34;

private:
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;

    static float xFor (float f, juce::Rectangle<float> r)
    {
        const float t = std::log (juce::jlimit (kMinHz, kMaxHz, f) / kMinHz)
                      / std::log (kMaxHz / kMinHz);
        return r.getX() + t * r.getWidth();
    }

    float centre    = 400.0f;
    float depthAmt  = 0.7f;
    int   numStages = 4;
    float sweep     = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaserView)
};
