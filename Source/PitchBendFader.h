/*
    PitchBendFader.h
    ----------------
    63C-18: pitch-bend tab hosted in the always-visible meter sidebar.

    Range and throw are decoupled (so small ranges stay easy to play):
      - The FADER travels the full height like a normal pitch wheel. Full up =
        +range semitones, full down = -range. Releasing springs back to centre.
      - A click-drag BOX at the bottom sets the range in semitones (drag up to
        add, down to subtract).

    Bend amount is PID_PITCH_BEND (-1..1); range is PID_BEND_RANGE (0..24 st).
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
    static constexpr int boxH        = 20;    // semitone drag box at the bottom
    static constexpr float pxPerStep = 6.0f;  // drag sensitivity for the range box
    static constexpr float maxRange  = 24.0f;

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
        auto track = trackArea().toFloat().reduced (3.0f, 0.0f);
        const float cx     = track.getCentreX();
        const float cy     = track.getCentreY();
        const float trackW = juce::jmin (10.0f, track.getWidth());
        auto rail = juce::Rectangle<float> (cx - trackW * 0.5f, track.getY(), trackW, track.getHeight());

        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (rail, 3.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.8f));
        g.drawRoundedRectangle (rail, 3.0f, 1.0f);

        // Centre detent (bend = 0).
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        g.fillRect (rail.getX() - 2.0f, cy - 0.5f, rail.getWidth() + 4.0f, 1.0f);

        // Bend thumb, full travel.
        const float thumbY = cy - bendNorm * halfTravel();
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow));
        g.fillEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.drawEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f, 1.5f);

        // Semitone drag box.
        auto box = boxArea().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (draggingBox ? 0.9f : 0.5f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt));
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (rangeSt)) + " st",
                    box, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (e.mods.isPopupMenu())
            return;

        if (boxArea().contains (e.getPosition()))
        {
            mode         = Range;
            draggingBox  = true;
            rangeAtStart = rangeSt;
            dragStartY   = (float) e.y;
            rangeAtt.beginGesture();
            repaint();
        }
        else
        {
            mode = Bend;
            bendAtt.beginGesture();
            dragBend (e);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == Bend)       dragBend (e);
        else if (mode == Range) dragRange (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Range)
        {
            rangeAtt.endGesture();
            draggingBox = false;
            repaint();
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

    juce::Rectangle<int> boxArea()   const { return getLocalBounds().removeFromBottom (boxH); }
    juce::Rectangle<int> trackArea() const { return getLocalBounds().withTrimmedBottom (boxH + 4); }
    float halfTravel() const { return juce::jmax (4.0f, trackArea().toFloat().getHeight() * 0.5f - 8.0f); }

    void dragBend (const juce::MouseEvent& e)
    {
        const float cy = trackArea().toFloat().getCentreY();
        const float v  = juce::jlimit (-1.0f, 1.0f, (cy - (float) e.y) / halfTravel());
        bendAtt.setValueAsPartOfGesture (v);
    }

    void dragRange (const juce::MouseEvent& e)
    {
        const float delta = (dragStartY - (float) e.y) / pxPerStep;   // drag up = more
        const float st    = juce::jlimit (0.0f, maxRange, rangeAtStart + delta);
        rangeAtt.setValueAsPartOfGesture ((float) juce::roundToInt (st));
    }

    void timerCallback() override
    {
        stopTimer();
        bendAtt.setValueAsCompleteGesture (0.0f);
    }

    juce::ParameterAttachment bendAtt, rangeAtt;
    float bendNorm     = 0.0f;   // -1..1
    float rangeSt      = 2.0f;   // 0..24 semitones
    float rangeAtStart = 2.0f;
    float dragStartY   = 0.0f;
    bool  draggingBox  = false;
    Mode  mode         = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendFader)
};
