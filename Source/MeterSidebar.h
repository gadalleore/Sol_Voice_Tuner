/*
    MeterSidebar.h
    --------------
    63C-18: always-visible metering column on the right edge of the window,
    per Gard's sketch #3. Lives OUTSIDE the PageStack (owned by PluginEditor),
    so it stays on screen on every page. Top to bottom:

        Volume knob
        stereo L/R level bars | pitch-bend fader (pull the tab up/down)
        Lissajous X/Y phase display
        Oscilloscope

    Data flow: the processor publishes per-block peak/RMS through atomics and
    a double-buffered post-chain scope snapshot; a 30 Hz timer here polls both.
    Nothing touches the audio thread.
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PitchBendFader.h"
#include "OscilloscopeComponent.h"
#include "SolLookAndFeel.h"

class MeterSidebar final : public juce::Component,
                           private juce::Timer
{
public:
    static constexpr int kWidth = 140;

    explicit MeterSidebar (PitchCorrectorAudioProcessor& p)
        : processorRef (p)
    {
        setOpaque (true);

        styleCaption (volumeLbl,    "Volume");
        styleCaption (meterLbl,     "Metering");
        styleCaption (bendLbl,      "Pitch Bend");
        styleCaption (lissajousLbl, "Lissajous");
        styleCaption (scopeLbl,     "Oscilloscope");

        volumeKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        volumeKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
        volumeKnob.setTextValueSuffix (" dB");
        volumeKnob.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (volumeKnob);
        volumeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.getAPVTS(), PitchCorrectorAudioProcessor::PID_VOLUME, volumeKnob);

        addAndMakeVisible (levelMeter);

        bendFader.setRange (-1.0, 1.0, 0.01);
        bendFader.setSliderStyle (juce::Slider::LinearVertical);
        bendFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        bendFader.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (bendFader);
        bendAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.getAPVTS(), PitchCorrectorAudioProcessor::PID_PITCH_BEND, bendFader);

        addAndMakeVisible (lissajous);
        addAndMakeVisible (oscilloscope);

        startTimerHz (30);
    }

    ~MeterSidebar() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

        // Divider hugging the sidebar's left edge, mirroring the back bar.
        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.35f));
        g.fillRect (0, 0, 1, getHeight());

        g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.35f));
        g.fillRect (getLocalBounds().withTrimmedLeft (1));
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (1).reduced (8, 6);

        volumeLbl.setBounds (r.removeFromTop (14));
        volumeKnob.setBounds (r.removeFromTop (72).reduced (26, 0));
        r.removeFromTop (2);

        // Bottom-up: scope, then Lissajous, remainder is the meter/bend row.
        scopeLbl.setBounds (r.removeFromBottom (14));
        auto scopeArea = r.removeFromBottom (72);
        oscilloscope.setBounds (scopeArea);
        r.removeFromBottom (4);

        auto lisArea = r.removeFromBottom (juce::jmin (r.getWidth() - 14, 96));
        lissajous.setBounds (lisArea.withSizeKeepingCentre (lisArea.getHeight(), lisArea.getHeight()));
        lissajousLbl.setBounds (r.removeFromBottom (14));
        r.removeFromBottom (4);

        // Meters (left) | pitch-bend fader (right), captions above each.
        auto captions = r.removeFromTop (14);
        const int bendW = juce::jmax (34, r.getWidth() / 3);
        meterLbl.setBounds (captions.withTrimmedRight (bendW));
        bendLbl .setBounds (captions.removeFromRight (bendW + 8).translated (0, 0));
        auto bendCol = r.removeFromRight (bendW);
        bendFader.setBounds (bendCol);
        levelMeter.setBounds (r.reduced (6, 2));
    }

private:
    //==========================================================================
    /** Two vertical peak/RMS bars, -60..+6 dB, peak-hold ticks. */
    class StereoLevelMeter final : public juce::Component
    {
    public:
        void setLevels (float rmsL, float rmsR, float peakL, float peakR) noexcept
        {
            rms[0] = rmsL;  rms[1] = rmsR;
            peak[0] = peakL; peak[1] = peakR;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat();
            const float gap  = 4.0f;
            const float barW = (area.getWidth() - gap) * 0.5f;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto bar = juce::Rectangle<float> (area.getX() + (float) ch * (barW + gap),
                                                   area.getY(), barW, area.getHeight());

                g.setColour (juce::Colour (SolLookAndFeel::kPanel));
                g.fillRoundedRectangle (bar, 2.0f);

                const float rmsY = juce::jmap (dbNorm (rms[ch]), bar.getBottom(), bar.getY());
                g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.85f));
                g.fillRoundedRectangle (bar.withTop (rmsY), 2.0f);

                if (peak[ch] > 1.0e-5f)
                {
                    const float pkY = juce::jmap (dbNorm (peak[ch]), bar.getBottom(), bar.getY());
                    g.setColour (peak[ch] >= 1.0f ? juce::Colours::orangered
                                                  : juce::Colour (SolLookAndFeel::kTitleHi));
                    g.fillRect (bar.getX(), pkY - 1.0f, bar.getWidth(), 2.0f);
                }

                g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.8f));
                g.drawRoundedRectangle (bar, 2.0f, 1.0f);

                // 0 dBFS tick.
                const float zeroY = juce::jmap (dbNorm (1.0f), bar.getBottom(), bar.getY());
                g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.5f));
                g.fillRect (bar.getX(), zeroY, bar.getWidth(), 1.0f);
            }
        }

    private:
        /** Linear gain -> 0..1 position over a -60..+6 dB scale. */
        static float dbNorm (float lin) noexcept
        {
            const float db = juce::Decibels::gainToDecibels (lin, -60.0f);
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        }

        float rms[2]  { 0.0f, 0.0f };
        float peak[2] { 0.0f, 0.0f };
    };

    //==========================================================================
    /** X/Y stereo phase display fed from the processor's scope snapshot. */
    class LissajousComponent final : public juce::Component
    {
    public:
        void update (const juce::AudioBuffer<float>& src, int validSamples)
        {
            const int n = validSamples > 0 ? juce::jmin (validSamples, src.getNumSamples())
                                           : src.getNumSamples();
            if (n <= 0 || src.getNumChannels() < 1)
                return;

            const int chs = src.getNumChannels();
            points.clearQuick();
            const float* l = src.getReadPointer (0);
            const float* r = src.getReadPointer (chs > 1 ? 1 : 0);

            const int stride = juce::jmax (1, n / kMaxPoints);
            for (int i = 0; i < n; i += stride)
                points.add ({ l[i], r[i] });

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat().reduced (1.0f);

            g.setColour (juce::Colour (SolLookAndFeel::kPanel));
            g.fillRoundedRectangle (area, 3.0f);

            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.6f));
            g.drawLine (area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom(), 0.5f);
            g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 0.5f);

            if (points.size() > 1)
            {
                const float cx = area.getCentreX();
                const float cy = area.getCentreY();
                const float scale = area.getWidth() * 0.5f * 0.9f;

                juce::Path p;
                bool first = true;
                for (const auto& pt : points)
                {
                    // Goniometer rotation: mono = vertical line, anti-phase = horizontal.
                    const float x = cx + (pt.x - pt.y) * 0.7071f * scale;
                    const float y = cy - (pt.x + pt.y) * 0.7071f * scale;
                    if (first) { p.startNewSubPath (x, y); first = false; }
                    else         p.lineTo (x, y);
                }

                g.setColour (juce::Colour (SolLookAndFeel::kAccentArc).withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }

            g.setColour (juce::Colour (SolLookAndFeel::kOutline));
            g.drawRoundedRectangle (area, 3.0f, 1.0f);
        }

    private:
        static constexpr int kMaxPoints = 256;
        juce::Array<juce::Point<float>> points;
    };

    //==========================================================================
    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.8f));
        addAndMakeVisible (l);
    }

    void timerCallback() override
    {
        // Meter ballistics: instant attack, exponential release, peak hold.
        for (int ch = 0; ch < 2; ++ch)
        {
            const float pk  = processorRef.getAndClearMeterPeak (ch);
            const float rms = processorRef.getMeterRms (ch);

            rmsDisp[ch] = juce::jmax (rms, rmsDisp[ch] * 0.82f);

            if (pk >= peakDisp[ch])
            {
                peakDisp[ch] = pk;
                peakHold[ch] = 24;              // ~0.8 s at 30 Hz
            }
            else if (peakHold[ch] > 0)
            {
                --peakHold[ch];
            }
            else
            {
                peakDisp[ch] *= 0.88f;
                if (peakDisp[ch] < 1.0e-5f) peakDisp[ch] = 0.0f;
            }
        }
        levelMeter.setLevels (rmsDisp[0], rmsDisp[1], peakDisp[0], peakDisp[1]);

        const auto& snap = processorRef.getScopeBuffer();
        const int   n    = processorRef.getScopeValidSamples();
        oscilloscope.update (snap, n);
        lissajous.update (snap, n);
    }

    PitchCorrectorAudioProcessor& processorRef;

    juce::Label volumeLbl, meterLbl, bendLbl, lissajousLbl, scopeLbl;

    juce::Slider          volumeKnob { juce::Slider::RotaryHorizontalVerticalDrag,
                                       juce::Slider::TextBoxBelow };
    PitchBendFaderSlider  bendFader  { processorRef };
    StereoLevelMeter      levelMeter;
    LissajousComponent    lissajous;
    OscilloscopeComponent oscilloscope;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAtt, bendAtt;

    float rmsDisp[2]  { 0.0f, 0.0f };
    float peakDisp[2] { 0.0f, 0.0f };
    int   peakHold[2] { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterSidebar)
};
