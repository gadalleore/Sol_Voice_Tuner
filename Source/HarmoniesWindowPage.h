/*
    HarmoniesWindowPage.h
    ---------------------
    Window 2 of the paging UI (63C-13). A WheelComponent whose first slot is the
    fixed Lead Voice and whose remaining slots are the 4 harmony voices, pulled
    from the "Harmony" palette in the hub. Clicking the Lead drills into the
    tuner; clicking a harmony selects it and reveals its per-voice panel: a
    semicircle PITCH WHEEL (interval in steps) plus level + pan.

    The KEY (root + scale) lives on this page and is global to every voice. The
    scale decides how the pitch wheel's steps are read: Chromatic = fixed
    semitones (dumb harmonizer); any scale = diatonic scale degrees (in-key
    harmony). The DSP (PluginProcessor::mixHarmonyVoices) resolves the same way.
*/

#pragma once

#include <JuceHeader.h>

#include "SolPage.h"
#include "WheelComponent.h"
#include "PluginProcessor.h"
#include "ScaleQuantizer.h"

class HarmoniesWindowPage final : public SolPage,
                                  private juce::Timer
{
public:
    std::function<void()> onOpenTuning;   // Lead Voice -> tuner

    HarmoniesWindowPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Harmonies"),
          apvts (apvtsIn)
    {
        // --- Key control (global root + scale) ---
        keyLbl.setText ("Key", juce::dontSendNotification);
        keyLbl.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        keyLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
        addAndMakeVisible (keyLbl);

        for (int i = 0; i < 12; ++i)
            rootBox.addItem (SolTune::rootName (i), i + 1);
        addAndMakeVisible (rootBox);
        rootAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, PitchCorrectorAudioProcessor::PID_ROOT, rootBox);

        for (int i = 0; i < (int) SolTune::Scale::NumScales; ++i)
            scaleBox.addItem (SolTune::scaleName (i), i + 1);
        addAndMakeVisible (scaleBox);
        scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, PitchCorrectorAudioProcessor::PID_SCALE, scaleBox);
        // Note: don't set scaleBox.onChange — the attachment owns it. The pitch
        // wheel's "semitones vs scale steps" label refreshes via the repaint timer.

        // --- Wheel: slot 0 = Lead, slots 1..kNumHarmony = harmony voices ---
        wheel.setNumSlots (1 + PitchCorrectorAudioProcessor::kNumHarmony);
        wheel.emptyTypeId     = typeEmpty;
        wheel.allowDuplicates = true;      // each slot is an independent voice
        wheel.itemsDraggable  = true;
        wheel.setPalette ({ { typeHarmony, "Harmony" } });

        wheel.getSlotType   = [this] (int slot)        { return slotType (slot); };
        wheel.setSlotType   = [this] (int slot, int t) { setSlot (slot, t); };
        wheel.onSlotClicked = [this] (int slot)        { clickSlot (slot); };
        wheel.nameProvider  = [] (int t)
        {
            return juce::String (t == typeLead ? "Lead" : t == typeHarmony ? "Harmony" : "");
        };
        addAndMakeVisible (wheel);

        // --- Per-voice panel (hidden until a harmony is selected) ---
        hint.setText ("Pull a Harmony from the wheel,\nthen click it to set its pitch.",
                      juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::centred);
        hint.setFont (juce::Font (juce::FontOptions (12.0f)));
        hint.setColour (juce::Label::textColourId,
                        juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.6f));
        addAndMakeVisible (hint);

        voiceTitle.setJustificationType (juce::Justification::centred);
        voiceTitle.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        voiceTitle.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
        addChildComponent (voiceTitle);

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
        addChildComponent (levelSlider);
        levelLbl.setText ("Level", juce::dontSendNotification);
        styleMini (levelLbl);

        panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
        addChildComponent (panSlider);
        panLbl.setText ("Pan", juce::dontSendNotification);
        styleMini (panLbl);

        startTimerHz (12);   // reflect host automation / preset changes
    }

    ~HarmoniesWindowPage() override { stopTimer(); }

private:
    //==========================================================================
    // Semicircle pitch wheel: drag up/down to set the voice interval in steps.
    class PitchWheel final : public juce::Component
    {
    public:
        std::function<bool()> isChromatic;

        explicit PitchWheel (juce::RangedAudioParameter& p)
            : att (p, [this] (float v) { value = (int) std::lround (v); repaint(); })
        {
            att.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (4.0f);
            const juce::Point<float> c { b.getX() + 6.0f, b.getCentreY() };
            const float r = juce::jmin (b.getWidth() - 12.0f, b.getHeight() * 0.5f - 2.0f);

            // Arc + step ticks (top = +12, bottom = -12).
            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.5f));
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, angleFor (12), angleFor (-12), true);
            g.strokePath (arc, juce::PathStrokeType (2.0f));

            for (int s = -12; s <= 12; ++s)
            {
                const float a  = angleFor (s);
                const float rr = (s % 12 == 0) ? r - 9.0f : r - 5.0f;
                const juce::Point<float> p1 { c.x + std::sin (a) * rr, c.y - std::cos (a) * rr };
                const juce::Point<float> p2 { c.x + std::sin (a) * r,  c.y - std::cos (a) * r  };
                g.setColour (juce::Colour (s == value ? SolLookAndFeel::kAccentGlow
                                                      : SolLookAndFeel::kOutline)
                                 .withAlpha (s == value ? 1.0f : 0.5f));
                g.drawLine ({ p1, p2 }, s == value ? 2.5f : 1.0f);
            }

            // Needle.
            const float a = angleFor (value);
            g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
            g.drawLine ({ c, { c.x + std::sin (a) * r, c.y - std::cos (a) * r } }, 2.5f);

            // Readout.
            g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
            g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
            g.drawText ((value > 0 ? "+" : "") + juce::String (value),
                        getLocalBounds().removeFromTop (getHeight() / 2),
                        juce::Justification::centred);

            const bool chrom = isChromatic && isChromatic();
            g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (chrom ? "semitones" : "scale steps",
                        getLocalBounds().removeFromBottom (16), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent& e) override { att.beginGesture(); setFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }
        void mouseUp   (const juce::MouseEvent&)   override { att.endGesture(); }

    private:
        static float angleFor (int step)   // -12..12 -> arc angle (0 = up)
        {
            const float t = (float) step / 12.0f;                       // -1..1
            return -t * (juce::MathConstants<float>::pi * 0.66f);        // ±~119°
        }

        void setFromMouse (const juce::MouseEvent& e)
        {
            const float t = 1.0f - (float) e.y / (float) juce::jmax (1, getHeight());   // top=1
            const int   v = juce::jlimit (-12, 12, juce::roundToInt (juce::jmap (t, 0.0f, 1.0f, -12.0f, 12.0f)));
            att.setValueAsPartOfGesture ((float) v);
        }

        juce::ParameterAttachment att;
        int value = 0;
    };

    //==========================================================================
    static constexpr int typeEmpty   = 0;
    static constexpr int typeLead    = 1;
    static constexpr int typeHarmony = 2;

    void styleMini (juce::Label& l)
    {
        l.setFont (juce::Font (juce::FontOptions (10.5f)));
        l.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
        addChildComponent (l);
    }

    int slotType (int slot) const
    {
        if (slot == 0) return typeLead;
        const int v = slot - 1;
        if (auto* p = apvts.getRawParameterValue (PitchCorrectorAudioProcessor::harmEnableParamId (v)))
            return p->load() >= 0.5f ? typeHarmony : typeEmpty;
        return typeEmpty;
    }

    void setSlot (int slot, int type)
    {
        if (slot == 0) return;                       // Lead is fixed
        setEnable (slot - 1, type != typeEmpty);
        if (type != typeEmpty) selectVoice (slot - 1);
    }

    void setEnable (int voice, bool on)
    {
        if (auto* p = apvts.getParameter (PitchCorrectorAudioProcessor::harmEnableParamId (voice)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    }

    void clickSlot (int slot)
    {
        if (slot == 0) { if (onOpenTuning) onOpenTuning(); return; }
        selectVoice (slot - 1);
    }

    bool scaleIsChromatic() const
    {
        if (auto* p = apvts.getRawParameterValue (PitchCorrectorAudioProcessor::PID_SCALE))
            return (int) std::lround (p->load()) == (int) SolTune::Scale::Chromatic;
        return false;
    }

    void selectVoice (int voice)
    {
        selectedVoice = voice;

        pitchWheel = std::make_unique<PitchWheel> (
            *apvts.getParameter (PitchCorrectorAudioProcessor::harmIntervalParamId (voice)));
        pitchWheel->isChromatic = [this] { return scaleIsChromatic(); };
        addAndMakeVisible (*pitchWheel);

        levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, PitchCorrectorAudioProcessor::harmLevelParamId (voice), levelSlider);
        panAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, PitchCorrectorAudioProcessor::harmPanParamId (voice), panSlider);

        voiceTitle.setText ("Harmony " + juce::String (voice + 1), juce::dontSendNotification);
        refreshPanel();
        resized();
    }

    void refreshPanel()
    {
        const bool show = selectedVoice >= 0 && slotType (selectedVoice + 1) == typeHarmony;
        if (! show) selectedVoice = -1;

        hint.setVisible (! show);
        voiceTitle.setVisible (show);
        levelSlider.setVisible (show);  levelLbl.setVisible (show);
        panSlider.setVisible (show);    panLbl.setVisible (show);
        if (pitchWheel) pitchWheel->setVisible (show);
        if (pitchWheel) pitchWheel->repaint();
    }

    void timerCallback() override { wheel.repaint(); if (pitchWheel) pitchWheel->repaint(); }

    void layoutContent (juce::Rectangle<int> area) override
    {
        auto top = area.removeFromTop (34);
        keyLbl.setBounds (top.removeFromLeft (34));
        rootBox.setBounds (top.removeFromLeft (70).reduced (2));
        scaleBox.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth())).reduced (2));

        auto panel = area.removeFromRight (juce::jmin (200, area.getWidth() / 3));
        wheel.setBounds (area);

        panel.reduce (8, 6);
        hint.setBounds (panel);
        voiceTitle.setBounds (panel.removeFromTop (22));
        if (pitchWheel) pitchWheel->setBounds (panel.removeFromTop (juce::jmin (150, panel.getHeight() - 80)));
        panel.removeFromTop (6);
        levelLbl.setBounds (panel.removeFromTop (14));
        levelSlider.setBounds (panel.removeFromTop (24));
        panLbl.setBounds (panel.removeFromTop (14));
        panSlider.setBounds (panel.removeFromTop (24));
    }

    juce::AudioProcessorValueTreeState& apvts;

    juce::Label    keyLbl;
    juce::ComboBox rootBox, scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt, scaleAtt;

    WheelComponent wheel;

    juce::Label  hint, voiceTitle, levelLbl, panLbl;
    juce::Slider levelSlider, panSlider;
    std::unique_ptr<PitchWheel> pitchWheel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt, panAtt;

    int selectedVoice = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniesWindowPage)
};
