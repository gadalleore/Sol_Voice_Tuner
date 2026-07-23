/*
    PitchBendFader.h
    ----------------
    63C-18: pitch-bend tab hosted in the always-visible meter sidebar.

    Two interactions in one vertical control (per Gard's sketch #3):
      - Grab the MIDDLE and drag -> bend the pitch. Releases spring back to
        centre after a short delay, like a hardware pitch wheel.
      - Grab either END (top or bottom) and pull toward/away from centre ->
        set the bend RANGE in semitones. The two range handles mirror each
        other; the shaded band shows how far a full bend will reach.

    Bend amount is PID_PITCH_BEND (-1..1); range is PID_BEND_RANGE (0..12 st).
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SolLookAndFeel.h"

class PitchBendFader final : public juce::Component,
                            private juce::Timer
{
public:
    static constexpr int snapDelayMs = 100;   // spring-back delay after release
    static constexpr int handleZone  = 18;    // px at each end that grab the range
    static constexpr int readoutH    = 14;    // bottom strip for the "st" readout

    explicit PitchBendFader (PitchCorrectorAudioProcessor& p)
        : bendAtt  (*p.getAPVTS().getParameter (PitchCorrectorAudioProcessor::PID_PITCH_BEND),
                    [this] (float v) { bendNorm = v; repaint(); }),
          rangeAtt (*p.getAPVTS().getParameter (PitchCorrectorAudioProcessor::PID_BEND_RANGE),
                    [this] (float v) { rangeSt = v; repaint(); })
    {
        bendAtt.sendInitialUpdate();
        rangeAtt.sendInitialUpdate();
    }

    ~PitchBendFader() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const float cy   = centreY();
        const float half = halfTravel();
        const float ext  = (rangeSt / 12.0f) * half;

        auto content = getLocalBounds().withTrimmedBottom (readoutH).toFloat().reduced (3.0f, 2.0f);
        const float cx     = content.getCentreX();
        const float trackW = juce::jmin (10.0f, content.getWidth());
        auto track = juce::Rectangle<float> (cx - trackW * 0.5f, content.getY(),
                                             trackW, content.getHeight());

        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (track, 3.0f);

        // Shaded band = reach of a full bend at the current range.
        if (ext > 0.5f)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.18f));
            g.fillRoundedRectangle ({ track.getX(), cy - ext, track.getWidth(), ext * 2.0f }, 3.0f);
        }

        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.8f));
        g.drawRoundedRectangle (track, 3.0f, 1.0f);

        // Centre detent (bend = 0).
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        g.fillRect (track.getX() - 2.0f, cy - 0.5f, track.getWidth() + 4.0f, 1.0f);

        // Range handles at each end of the reach.
        for (float hy : { cy - ext, cy + ext })
        {
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.fillRoundedRectangle ({ track.getX() - 3.0f, hy - 2.5f,
                                      track.getWidth() + 6.0f, 5.0f }, 2.0f);
        }

        // Bend thumb.
        const float thumbY = cy - bendNorm * half;
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow));
        g.fillEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.drawEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f, 1.5f);

        // Range readout.
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("±") + juce::String (juce::roundToInt (rangeSt)) + " st",
                    getLocalBounds().removeFromBottom (readoutH), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (e.mods.isPopupMenu())
            return;

        const float cy = centreY();
        auto content   = getLocalBounds().withTrimmedBottom (readoutH);

        if (e.y <= content.getY() + handleZone || e.y >= content.getBottom() - handleZone)
        {
            mode = Range;
            rangeAtt.beginGesture();
            dragRange (e);
        }
        else
        {
            mode = Bend;
            bendAtt.beginGesture();
            dragBend (e);
        }
        juce::ignoreUnused (cy);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == Range)      dragRange (e);
        else if (mode == Bend)  dragBend (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Range)
        {
            rangeAtt.endGesture();
        }
        else if (mode == Bend)
        {
            bendAtt.endGesture();
            if (! juce::approximatelyEqual (bendNorm, 0.0f))
                startTimer (snapDelayMs);   // spring back to centre
        }
        mode = None;
    }

private:
    enum Mode { None, Bend, Range };

    juce::Rectangle<int> contentArea() const { return getLocalBounds().withTrimmedBottom (readoutH); }
    float centreY()    const { return contentArea().toFloat().getCentreY(); }
    float halfTravel() const { return juce::jmax (4.0f, contentArea().toFloat().getHeight() * 0.5f - 8.0f); }

    void dragBend (const juce::MouseEvent& e)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, (centreY() - (float) e.y) / halfTravel());
        bendAtt.setValueAsPartOfGesture (v);
    }

    void dragRange (const juce::MouseEvent& e)
    {
        const float dist = juce::jlimit (0.0f, halfTravel(), std::abs (centreY() - (float) e.y));
        const float st   = juce::jlimit (0.0f, 12.0f, (dist / halfTravel()) * 12.0f);
        rangeAtt.setValueAsPartOfGesture ((float) juce::roundToInt (st));
    }

    void timerCallback() override
    {
        stopTimer();
        bendAtt.setValueAsCompleteGesture (0.0f);
    }

    juce::ParameterAttachment bendAtt, rangeAtt;
    float bendNorm = 0.0f;   // -1..1
    float rangeSt  = 2.0f;   // 0..12 semitones
    Mode  mode     = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendFader)
};
