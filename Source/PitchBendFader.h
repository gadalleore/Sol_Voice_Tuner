/*
    PitchBendFader.h
    ----------------
    63C-18: pitch-bend tab hosted in the always-visible meter sidebar.

    Two interactions in one vertical control (per Gard's sketch #3):
      - Grab the MIDDLE and drag -> bend the pitch. Releasing springs back to
        centre after a short delay, like a hardware pitch wheel.
      - Grab either END grip (top or bottom) and pull outward/inward -> set the
        bend RANGE in semitones. Dragging is relative to where you grabbed, so
        it never yanks the bend; the shaded band shows how far a full bend
        reaches and the readout shows the current range.

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
    static constexpr int gripH       = 20;    // px at each end that grab the range
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

        auto content = contentArea().toFloat().reduced (3.0f, 0.0f);
        const float cx     = content.getCentreX();
        const float trackW = juce::jmin (10.0f, content.getWidth());
        auto track = juce::Rectangle<float> (cx - trackW * 0.5f, content.getY(),
                                             trackW, content.getHeight());

        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (track, 3.0f);

        // Shaded band = reach of a full bend at the current range.
        if (ext > 0.5f)
        {
            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.20f));
            g.fillRoundedRectangle ({ track.getX(), cy - ext, track.getWidth(), ext * 2.0f }, 3.0f);
        }

        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.8f));
        g.drawRoundedRectangle (track, 3.0f, 1.0f);

        // Centre detent (bend = 0).
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
        g.fillRect (track.getX() - 2.0f, cy - 0.5f, track.getWidth() + 4.0f, 1.0f);

        // End grips (fixed at the physical ends) — the range grab targets.
        drawGrip (g, content.getX(), content.getRight(), (float) contentArea().getY() + gripH * 0.5f, true);
        drawGrip (g, content.getX(), content.getRight(), (float) contentArea().getBottom() - gripH * 0.5f, false);

        // Bend thumb.
        const float thumbY = cy - bendNorm * half;
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow));
        g.fillEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.drawEllipse (cx - 6.0f, thumbY - 6.0f, 12.0f, 12.0f, 1.5f);

        // Range readout.
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (rangeSt)) + " st",
                    getLocalBounds().removeFromBottom (readoutH), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (e.mods.isPopupMenu())
            return;

        auto content = contentArea();
        dragStartY   = (float) e.y;

        if (e.y < content.getY() + gripH || e.y > content.getBottom() - gripH)
        {
            mode          = (e.y < content.getCentreY()) ? RangeTop : RangeBottom;
            rangeAtStart  = rangeSt;
            rangeAtt.beginGesture();
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
        if (mode == Bend)   dragBend (e);
        else if (mode != None) dragRange (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == RangeTop || mode == RangeBottom)
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
    enum Mode { None, Bend, RangeTop, RangeBottom };

    juce::Rectangle<int> contentArea() const { return getLocalBounds().withTrimmedBottom (readoutH); }
    juce::Rectangle<int> travelArea()  const { return contentArea().reduced (0, gripH); }
    float centreY()    const { return travelArea().toFloat().getCentreY(); }
    float halfTravel() const { return juce::jmax (4.0f, travelArea().toFloat().getHeight() * 0.5f - 6.0f); }

    void dragBend (const juce::MouseEvent& e)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, (centreY() - (float) e.y) / halfTravel());
        bendAtt.setValueAsPartOfGesture (v);
    }

    void dragRange (const juce::MouseEvent& e)
    {
        // Relative drag: pulling a grip outward (away from centre) grows the range.
        const float outward = (mode == RangeTop) ? (dragStartY - (float) e.y)
                                                 : ((float) e.y - dragStartY);
        const float pxPerStep = juce::jmax (6.0f, halfTravel() / 12.0f);
        const float st = juce::jlimit (0.0f, 12.0f, rangeAtStart + outward / pxPerStep);
        rangeAtt.setValueAsPartOfGesture ((float) juce::roundToInt (st));
    }

    void drawGrip (juce::Graphics& g, float x0, float x1, float y, bool pointUp)
    {
        juce::Rectangle<float> bar (x0, y - 5.0f, x1 - x0, 10.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.fillRoundedRectangle (bar, 3.0f);

        // Two ticks hinting the grip is draggable, plus a direction chevron.
        g.setColour (juce::Colour (SolLookAndFeel::kBackground).withAlpha (0.75f));
        const float cx = bar.getCentreX();
        const float dir = pointUp ? -1.0f : 1.0f;
        juce::Path chev;
        chev.startNewSubPath (cx - 3.0f, y + dir * -1.5f);
        chev.lineTo (cx,        y + dir * 1.5f);
        chev.lineTo (cx + 3.0f, y + dir * -1.5f);
        g.strokePath (chev, juce::PathStrokeType (1.2f));
    }

    void timerCallback() override
    {
        stopTimer();
        bendAtt.setValueAsCompleteGesture (0.0f);
    }

    juce::ParameterAttachment bendAtt, rangeAtt;
    float bendNorm     = 0.0f;   // -1..1
    float rangeSt      = 2.0f;   // 0..12 semitones
    float rangeAtStart = 2.0f;
    float dragStartY   = 0.0f;
    Mode  mode         = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendFader)
};
