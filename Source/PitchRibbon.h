/*
    PitchRibbon.h
    -------------
    The pitch-tracking display: the hero element of the main page, and the one
    thing on screen that is worth looking at while you sing.

    A scrolling history of two lines over a semitone grid:

        cyan   what Sol is HEARING   (detected pitch)
        amber  what Sol is SINGING   (the scale-snapped target)

    The gap between them IS the correction — when the plugin is working hard
    the two separate visibly, and when the take is already in tune they sit on
    top of each other. That reading is the whole point of the display; a bare
    numeric readout cannot show it.

    Vertical range follows the voice rather than being fixed: a fixed 0-127
    lane set would spend almost all its height on octaves nobody is singing.
    The centre eases toward the recent median so the grid does not lurch on a
    single bad detection, and the span is held at kSpanSemis.

    Silence draws the grid and nothing else — same rule as the meters. History
    is kept as MIDI note numbers (not Hz) so the vertical axis is linear in
    pitch, which is what makes a semitone the same height everywhere.
*/

#pragma once

#include <JuceHeader.h>

#include "ScaleQuantizer.h"
#include "SolLookAndFeel.h"

class PitchRibbon final : public juce::Component
{
public:
    PitchRibbon()
    {
        // A display, not a control: let the drag fall through to the window.
        setInterceptsMouseClicks (false, false);
        history.resize ((size_t) kHistory);
    }

    /** One UI frame. `detectedHz` <= 0 or a confidence under the gate reads as
        silence and breaks the line rather than drawing a spurious slide down
        to zero. */
    void push (float detectedHz, float confidence, float targetHz)
    {
        Sample s;
        s.voiced = std::isfinite (detectedHz) && detectedHz > 0.0f
                && confidence >= kConfidenceGate;

        if (s.voiced)
        {
            s.detected = (float) SolTune::hzToMidi (detectedHz);
            s.target   = (std::isfinite (targetHz) && targetHz > 0.0f)
                           ? (float) SolTune::hzToMidi (targetHz)
                           : s.detected;
        }

        history[(size_t) writePos] = s;
        writePos = (writePos + 1) % kHistory;

        if (s.voiced)
        {
            // Ease the window toward the note being sung. Snapping straight to
            // it would make the grid jump a whole lane on one octave error.
            const float want = s.target;
            centreNote = haveCentre ? centreNote + (want - centreNote) * kCentreEase
                                    : want;
            haveCentre = true;

            latest = s;
            latestAge = 0;
        }
        else if (latestAge < kReadoutHold)
        {
            ++latestAge;
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        const auto plot = b.withTrimmedLeft (kGutter);

        const float lo = centreNote - kSpanSemis * 0.5f;
        const float hi = centreNote + kSpanSemis * 0.5f;

        drawGrid (g, plot, lo, hi);

        if (! haveCentre)
            return;

        drawLine (g, plot, lo, hi, false);   // target, behind
        drawLine (g, plot, lo, hi, true);    // detected, in front

        drawReadout (g, b);
    }

private:
    struct Sample
    {
        float detected = 0.0f;
        float target   = 0.0f;
        bool  voiced   = false;
    };

    /** y for a MIDI note in the current window. */
    static float noteY (float note, juce::Rectangle<float> plot, float lo, float hi)
    {
        const float t = (note - lo) / juce::jmax (0.001f, hi - lo);
        return plot.getBottom() - t * plot.getHeight();
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot, float lo, float hi)
    {
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  10.0f, juce::Font::plain)));

        for (int n = (int) std::floor (lo); n <= (int) std::ceil (hi); ++n)
        {
            const float y = noteY ((float) n, plot, lo, hi);
            if (y < plot.getY() - 1.0f || y > plot.getBottom() + 1.0f)
                continue;

            // C is the anchor of the octave and gets the brighter rule; the
            // rest are hairlines so the grid reads as texture, not as a table.
            const bool isC = (n % 12) == 0;

            g.setColour (juce::Colour (isC ? SolLookAndFeel::kOutlineHi
                                           : SolLookAndFeel::kOutline)
                             .withAlpha (isC ? 0.45f : 0.30f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

            // Label every C, plus the note actually being sung, so the axis is
            // readable without papering it in text.
            const bool labelIt = isC
                              || (latestAge < kReadoutHold && latest.voiced
                                  && juce::roundToInt (latest.target) == n);

            if (labelIt)
            {
                g.setColour (juce::Colour (isC ? SolLookAndFeel::kLabel
                                               : SolLookAndFeel::kLabelAlt));
                g.drawText (juce::String (SolTune::midiNoteName (n).c_str()),
                            juce::Rectangle<float> (0.0f, y - 7.0f, kGutter - 6.0f, 14.0f),
                            juce::Justification::centredRight, false);
            }
        }
    }

    /** One of the two traces. Broken wherever the input was unvoiced, so a
        pause reads as a gap instead of a dive to the bottom of the window. */
    void drawLine (juce::Graphics& g, juce::Rectangle<float> plot,
                   float lo, float hi, bool detected)
    {
        juce::Path p;
        bool open = false;

        for (int i = 0; i < kHistory; ++i)
        {
            const auto& s = history[(size_t) ((writePos + i) % kHistory)];

            if (! s.voiced)
            {
                open = false;
                continue;
            }

            const float x = plot.getX() + plot.getWidth() * ((float) i / (float) (kHistory - 1));
            const float y = noteY (detected ? s.detected : s.target, plot, lo, hi);

            if (! open) { p.startNewSubPath (x, y); open = true; }
            else        { p.lineTo (x, y); }
        }

        if (p.isEmpty())
            return;

        const auto colour = juce::Colour (detected ? SolLookAndFeel::kAccentCool
                                                   : SolLookAndFeel::kAccentArc);

        // The detected line is the live one and carries a bloom; the target is
        // a reference and stays flat, so the two never compete.
        if (detected)
        {
            g.setColour (colour.withAlpha (0.22f));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        g.setColour (colour.withAlpha (detected ? 1.0f : 0.85f));
        g.strokePath (p, juce::PathStrokeType (detected ? 1.8f : 1.4f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    /** Note name and cents-off, top-right, over the grid. */
    void drawReadout (juce::Graphics& g, juce::Rectangle<float> b)
    {
        if (latestAge >= kReadoutHold || ! latest.voiced)
            return;

        const int   note  = juce::roundToInt (latest.target);
        const float cents = (latest.detected - latest.target) * 100.0f;
        const bool  inTune = std::abs (cents) <= kInTuneCents;

        auto box = b.reduced (10.0f, 8.0f).removeFromTop (44.0f).removeFromRight (132.0f);

        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  30.0f, juce::Font::plain)));
        g.drawText (juce::String (SolTune::midiNoteName (note).c_str()),
                    box.removeFromLeft (66.0f), juce::Justification::centredRight, false);

        // Green only when it is actually in tune — the one place the display
        // says "good" rather than just reporting.
        g.setColour (juce::Colour (inTune ? SolLookAndFeel::kSuccess
                                          : SolLookAndFeel::kLabel));
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  13.0f, juce::Font::plain)));
        g.drawText ((cents >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (cents)) + " c",
                    box, juce::Justification::centredLeft, false);
    }

    /** Frames of history on screen. At the editor's 60 Hz that is ~4 s. */
    static constexpr int   kHistory        = 240;
    static constexpr float kSpanSemis      = 15.0f;   // window height, in semitones
    static constexpr float kCentreEase     = 0.06f;   // per frame
    static constexpr float kConfidenceGate = 0.01f;   // matches LegacyTunerPage's readout gate
    static constexpr float kInTuneCents    = 8.0f;
    static constexpr int   kReadoutHold    = 45;      // frames the readout survives a gap
    static constexpr float kGutter         = 34.0f;   // note-label column

    std::vector<Sample> history;
    int   writePos   = 0;

    float centreNote = 60.0f;
    bool  haveCentre = false;

    Sample latest;
    int    latestAge = kReadoutHold;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchRibbon)
};
