/*
    PitchBendFader.h
    ----------------
    63C-18: pitch-bend tab hosted in the always-visible meter sidebar.

    Two moving tabs set the bend RANGE; the fader bends BETWEEN them:
      - Drag either tab (top or bottom) toward/away from centre -> sets the
        range in semitones. The tabs move to where you drag them.
      - Drag the fader thumb in the MIDDLE -> bends the pitch. Its travel is
        bounded by the two tabs, so it can never go past them. Releasing
        springs it back to centre, like a hardware pitch wheel.

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
    static constexpr int readoutH    = 14;    // bottom strip for the "st" readout
    static constexpr float grabTol   = 7.0f;  // px tolerance when grabbing a tab

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
        const float cy  = centreY();
        const float ext = extent();

        auto content = contentArea().toFloat().reduced (3.0f, 0.0f);
        const float cx     = content.getCentreX();
        const float trackW = juce::jmin (10.0f, content.getWidth());
        auto track = juce::Rectangle<float> (cx - trackW * 0.5f, content.getY(),
                                             trackW, content.getHeight());

        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (track, 3.0f);

        // Band between the tabs = where the fader can travel.
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

        // The two range tabs (they move with the range).
        drawTab (g, content.getX(), content.getRight(), cy - ext);
        drawTab (g, content.getX(), content.getRight(), cy + ext);

        // Bend thumb, bounded to sit between the tabs.
        const float thumbY = cy - bendNorm * ext;
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

        const float cy  = centreY();
        const float ext = extent();

        if ((float) e.y < cy)
        {
            if ((float) e.y <= cy - ext + grabTol) { mode = Range; rangeAtt.beginGesture(); dragRange (e); return; }
        }
        else if ((float) e.y >= cy + ext - grabTol)
        {
            mode = Range; rangeAtt.beginGesture(); dragRange (e); return;
        }

        mode = Bend;
        bendAtt.beginGesture();
        dragBend (e);
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
    float centreY()   const { return contentArea().toFloat().getCentreY(); }
    float maxExtent() const { return juce::jmax (4.0f, contentArea().toFloat().getHeight() * 0.5f - 8.0f); }
    float extent()    const { return (rangeSt / 24.0f) * maxExtent(); }

    void dragBend (const juce::MouseEvent& e)
    {
        const float ext = extent();
        if (ext < 1.0f) { bendAtt.setValueAsPartOfGesture (0.0f); return; }   // no range -> no bend
        const float v = juce::jlimit (-1.0f, 1.0f, (centreY() - (float) e.y) / ext);
        bendAtt.setValueAsPartOfGesture (v);
    }

    void dragRange (const juce::MouseEvent& e)
    {
        // The grabbed tab follows the cursor; range = its distance from centre.
        const float dist = juce::jlimit (0.0f, maxExtent(), std::abs (centreY() - (float) e.y));
        const float st   = juce::jlimit (0.0f, 24.0f, (dist / maxExtent()) * 24.0f);
        rangeAtt.setValueAsPartOfGesture ((float) juce::roundToInt (st));
    }

    void drawTab (juce::Graphics& g, float x0, float x1, float y)
    {
        juce::Rectangle<float> bar (x0, y - 3.0f, x1 - x0, 6.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kBackground).withAlpha (0.6f));
        g.fillRect (bar.getCentreX() - 4.0f, y - 0.5f, 8.0f, 1.0f);
    }

    void timerCallback() override
    {
        stopTimer();
        bendAtt.setValueAsCompleteGesture (0.0f);
    }

    juce::ParameterAttachment bendAtt, rangeAtt;
    float bendNorm = 0.0f;   // -1..1
    float rangeSt  = 2.0f;   // 0..24 semitones
    Mode  mode     = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendFader)
};
