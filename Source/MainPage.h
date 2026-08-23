/*
    MainPage.h
    ----------
    The root screen (Giuseppe, 2026-08-22). Replaces the Home WHEEL.

    The wheel put three words on screen and hid every control one drill-in
    away. That is a lovely object and a poor instrument: while tracking a vocal
    you want the pitch, the key and the correction controls visible AT ONCE,
    without navigating. Every pitch-correction plugin people actually track
    with — Auto-Tune, Waves Tune, Melodyne's inspector — is a single dense
    surface for exactly that reason, so this is one too:

        header      bypass, and the plate's own volume column to the right
        ribbon      PitchRibbon — the hero: heard vs. sung, over a note grid
        key/scale   the whole chromatic picker in view, no dropdown to open
        knobs       Robotic / Sub / Formant / Bend, labelled and permanent
        nav         the three destinations the wheel used to BE

    The wheel is not deleted — `WheelComponent` still drives both effects
    chains and the Home concept is preserved in the nav strip's three
    destinations. What changed is that it is no longer the root.

    Live pitch is PUSHED in from the editor's timer rather than polled by a
    timer here, so the whole UI still runs off a single clock (see
    PluginEditor::timerCallback).
*/

#pragma once

#include <JuceHeader.h>

#include "IconButton.h"
#include "PitchRibbon.h"
#include "PluginProcessor.h"
#include "ScaleQuantizer.h"
#include "SolIcons.h"
#include "SolPanel.h"
#include "SolLookAndFeel.h"

class MainPage final : public juce::Component
{
public:
    /** Where the nav strip goes. */
    std::function<void()> onInputFx, onHarmonies, onOutputFx;

    explicit MainPage (PitchCorrectorAudioProcessor& p)
        : processorRef (p), apvts (p.getAPVTS())
    {
        addAndMakeVisible (bypassBtn);
        bypassAtt = std::make_unique<BAtt> (apvts, PitchCorrectorAudioProcessor::PID_BYPASS, bypassBtn);

        addAndMakeVisible (ribbon);

        // ---- Key ---------------------------------------------------------
        styleCaption (keyLbl, "KEY");

        for (int i = 0; i < 12; ++i)
        {
            auto& b = keyBtns[(size_t) i];
            b.setButtonText (juce::String (SolTune::rootChoiceLabel (i)));
            b.getProperties().set (juce::Identifier (SolLookAndFeel::solKeyNoteButtonProperty), true);
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (SolLookAndFeel::kLabel));
            b.setColour (juce::TextButton::textColourOnId,  juce::Colour (SolLookAndFeel::kBackground));
            b.setTriggeredOnMouseDown (true);
            b.onClick = [this, i] { setRoot (i); };
            addAndMakeVisible (b);
        }

        // ---- Scale -------------------------------------------------------
        styleCaption (scaleLbl, "SCALE");
        for (int i = 0; i < (int) SolTune::Scale::NumScales; ++i)
            scaleBox.addItem (SolTune::scaleName (i), i + 1);
        addAndMakeVisible (scaleBox);
        scaleAtt = std::make_unique<CAtt> (apvts, PitchCorrectorAudioProcessor::PID_SCALE, scaleBox);

        addAndMakeVisible (midiBtn);
        midiAtt = std::make_unique<BAtt> (apvts, PitchCorrectorAudioProcessor::PID_MIDI_FOLLOW, midiBtn);

        // ---- Vocoder carrier ---------------------------------------------
        // Sits with SUB because SUB *is* the vocoder's depth: the effect is
        // a six-band channel vocoder whose carrier this picks. Separating
        // them would hide that one knob is the amount of the other.
        styleCaption (carrierLbl, "CARRIER");
        carrierLbl.setJustificationType (juce::Justification::centred);
        carrierBox.addItemList ({ "Saw", "Square", "Triangle", "Sine" }, 1);
        addAndMakeVisible (carrierBox);
        carrierAtt = std::make_unique<CAtt> (apvts,
                                             PitchCorrectorAudioProcessor::PID_VOC_CARRIER,
                                             carrierBox);

        // ---- Knobs -------------------------------------------------------
        addKnob (roboticKnob, roboticLbl, "ROBOTIC",
                 PitchCorrectorAudioProcessor::PID_ROBOTIC, roboticAtt, true);
        addKnob (subKnob,     subLbl,     "SUB",
                 PitchCorrectorAudioProcessor::PID_SUB,     subAtt,     true);
        addKnob (formantKnob, formantLbl, "FORMANT",
                 PitchCorrectorAudioProcessor::PID_FORMANT, formantAtt, false);
        addKnob (bendKnob,    bendLbl,    "BEND",
                 PitchCorrectorAudioProcessor::PID_BEND_RANGE, bendAtt, false);

        // ---- Nav ---------------------------------------------------------
        inputFxBtn .onClick = [this] { if (onInputFx)   onInputFx();   };
        harmonyBtn .onClick = [this] { if (onHarmonies) onHarmonies(); };
        outputFxBtn.onClick = [this] { if (onOutputFx)  onOutputFx();  };

        for (auto* b : { &inputFxBtn, &harmonyBtn, &outputFxBtn })
            addAndMakeVisible (b);
    }

    /** Driven from the editor's timer. */
    void tick()
    {
        ribbon.push (processorRef.getDetectedPitchHz(),
                     processorRef.getDetectionConfidence(),
                     processorRef.getSnappedTargetHz());

        // The key picker is not a parameter attachment (twelve buttons, one
        // choice), so it has to be refreshed against the parameter or host
        // automation and preset recalls would leave the wrong key lit.
        refreshKeySelection();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

        // The display sits in a bolted plate, and the control banks below each
        // get one of their own, so the page reads as hardware assembled from
        // parts rather than as regions of one flat surface.
        //
        // All three share ONE column (see resized): they used to be derived
        // from their own contents' bounds, which had each already been inset by
        // a different amount, so the plates ended up a few pixels wider or
        // narrower than each other. Stacked vertically that misalignment is the
        // most visible thing on the page — plates in a rack line up.
        if (! ribbonPlate.isEmpty()) SolPanel::draw (g, ribbonPlate.toFloat());
        if (! keyPlate.isEmpty())    SolPanel::draw (g, keyPlate.toFloat(),  false);
        if (! knobPlate.isEmpty())   SolPanel::draw (g, knobPlate.toFloat(), false);
    }

    void resized() override
    {
        // Keep clear of the plate's right-hand column (volume / mono / meters
        // / mark) — see the note in TuningWindowPage.
        auto r = getLocalBounds().reduced (kInset, 0).withTrimmedRight (kRightColumn);

        r.removeFromTop (2);
        bypassBtn.setBounds (r.removeFromTop (kHeaderH).removeFromLeft (86));

        // One column every plate is cut from, so their edges cannot disagree.
        const int plateX = r.getX() - kPlateBleed;
        const int plateW = r.getWidth() + kPlateBleed * 2;
        const auto plateRow = [plateX, plateW] (juce::Rectangle<int> row, int grow)
        {
            return juce::Rectangle<int> (plateX, row.getY() - grow,
                                         plateW, row.getHeight() + grow * 2);
        };

        r.removeFromTop (kGap);
        {
            auto row = r.removeFromTop (kRibbonH);
            ribbonPlate = plateRow (row, 2);
            ribbon.setBounds (row.reduced (10, 8));
        }

        r.removeFromTop (kGap + 4);

        // ---- Key block | Scale block -------------------------------------
        {
            auto row = r.removeFromTop (kKeyRowH);
            keyPlate = plateRow (row, 2);

            auto keyBlock = row.removeFromLeft (kKeyBlockW);
            keyLbl.setBounds (keyBlock.removeFromTop (kCaptionH));

            constexpr int cols = 6, rows = 2, gap = 4;
            const int cw = (keyBlock.getWidth() - (cols - 1) * gap) / cols;
            const int ch = juce::jmax (18, (keyBlock.getHeight() - gap) / rows);

            for (int row2 = 0; row2 < rows; ++row2)
                for (int c = 0; c < cols; ++c)
                    keyBtns[(size_t) (row2 * cols + c)]
                        .setBounds (keyBlock.getX() + c * (cw + gap),
                                    keyBlock.getY() + row2 * (ch + gap), cw, ch);

            row.removeFromLeft (kGap * 2);

            // Scale and MIDI share one line: stacking them needed more height
            // than the key grid beside them has, so MIDI spilled into the knob
            // row below. They belong together anyway — MIDI Follow is what
            // OVERRIDES the scale, so seeing them side by side reads correctly.
            auto scaleBlock = row;
            scaleLbl.setBounds (scaleBlock.removeFromTop (kCaptionH));

            auto line = scaleBlock.removeFromTop (26);
            midiBtn.setBounds (line.removeFromRight (64));
            line.removeFromRight (8);
            scaleBox.setBounds (line);
        }

        r.removeFromTop (kGap);

        // ---- Knob row -----------------------------------------------------
        {
            auto row = r.removeFromTop (kKnobRowH);
            knobPlate = plateRow (row, 2);

            // The carrier picker takes the right of the bank, directly beside
            // SUB — the knob whose depth it colours.
            {
                auto carrierCell = row.removeFromRight (kCarrierW);
                carrierLbl.setBounds (carrierCell.removeFromTop (kCaptionH));
                carrierBox.setBounds (carrierCell.removeFromTop (26).reduced (6, 0));
            }

            juce::Slider* knobs[] { &roboticKnob, &subKnob, &formantKnob, &bendKnob };
            juce::Label*  labels[] { &roboticLbl, &subLbl, &formantLbl, &bendLbl };

            const int cell = row.getWidth() / 4;

            for (int i = 0; i < 4; ++i)
            {
                auto c = row.removeFromLeft (cell);
                labels[i]->setBounds (c.removeFromTop (kCaptionH));
                knobs[i]->setBounds (c.reduced ((c.getWidth() - kKnobSize) / 2, 0)
                                      .withHeight (kKnobSize + kValueH));
            }
        }

        r.removeFromTop (kGap);

        // ---- Nav ----------------------------------------------------------
        {
            auto row = r.removeFromTop (kNavH);
            const int cell = row.getWidth() / 3;
            inputFxBtn .setBounds (row.removeFromLeft (cell).reduced (2, 0));
            harmonyBtn .setBounds (row.removeFromLeft (cell).reduced (2, 0));
            outputFxBtn.setBounds (row.reduced (2, 0));
        }
    }

private:
    using BAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                  10.0f, juce::Font::plain)));
        l.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
        addAndMakeVisible (l);
    }

    void addKnob (juce::Slider& s, juce::Label& l, const juce::String& caption,
                  const char* paramId, std::unique_ptr<SAtt>& att, bool asPercent)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, kValueH);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (SolLookAndFeel::kValue));
        // Set on the instance, not left to the LookAndFeel: the box is a Label
        // JUCE caches at the first setTextBoxStyle — see CLAUDE.md.
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);

        if (asPercent)
        {
            s.textFromValueFunction = [] (double v)
            {
                return juce::String (juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 100.0)) + " %";
            };
            s.valueFromTextFunction = [] (const juce::String& t)
            {
                return t.trim().removeCharacters ("% ").getDoubleValue() / 100.0;
            };
        }

        addAndMakeVisible (s);
        att = std::make_unique<SAtt> (apvts, paramId, s);

        l.setJustificationType (juce::Justification::centred);
        styleCaption (l, caption);
        l.setJustificationType (juce::Justification::centred);
    }

    void setRoot (int index)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                apvts.getParameter (PitchCorrectorAudioProcessor::PID_ROOT)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
            p->endChangeGesture();
        }
    }

    void refreshKeySelection()
    {
        int sel = 0;
        if (auto* raw = apvts.getRawParameterValue (PitchCorrectorAudioProcessor::PID_ROOT))
            sel = juce::jlimit (0, 11, juce::roundToInt (raw->load()));

        if (sel == litKey)
            return;

        litKey = sel;

        for (int i = 0; i < 12; ++i)
            keyBtns[(size_t) i].setToggleState (i == sel, juce::dontSendNotification);
    }

    // Layout. One inset, one gap; everything else is derived.
    static constexpr int kInset      = 16;
    static constexpr int kGap        = 8;

    /** How far every plate oversails the content column. One number, shared,
        because plates stacked in a rack have to line up. */
    static constexpr int kPlateBleed = 6;
    static constexpr int kCarrierW   = 92;
    // Budgeted against the content area LESS the spectrum footer the plate
    // draws over the bottom of it: the rows have to fit in what is left, or
    // the nav strip ends up under the analyser.
    static constexpr int kHeaderH    = 26;
    static constexpr int kRibbonH    = 132;
    static constexpr int kCaptionH   = 14;
    static constexpr int kKeyRowH    = 58;
    static constexpr int kKeyBlockW  = 238;
    static constexpr int kKnobRowH   = 78;
    static constexpr int kKnobSize   = 44;
    static constexpr int kValueH     = 16;
    static constexpr int kNavH       = 24;

    /** Width the plate's volume/meter/mark column needs — matches
        TuningWindowPage::kRightColumn. */
    static constexpr int kRightColumn = 110;

    PitchCorrectorAudioProcessor&        processorRef;
    juce::AudioProcessorValueTreeState&  apvts;

    PitchRibbon ribbon;

    /** Where the control banks sit, so paint() can put a plate behind each. */
    juce::Rectangle<int> ribbonPlate, keyPlate, knobPlate;

    IconButton bypassBtn { "Bypass", SolIcons::power(), true };
    IconButton midiBtn   { "MIDI",   SolIcons::midi(),  true };
    std::unique_ptr<BAtt> bypassAtt, midiAtt;

    juce::Label      keyLbl, scaleLbl, carrierLbl;
    std::array<juce::TextButton, 12> keyBtns;
    juce::ComboBox   scaleBox, carrierBox;
    std::unique_ptr<CAtt> scaleAtt, carrierAtt;
    int litKey = -1;

    juce::Slider roboticKnob, subKnob, formantKnob, bendKnob;
    juce::Label  roboticLbl,  subLbl,  formantLbl,  bendLbl;
    std::unique_ptr<SAtt> roboticAtt, subAtt, formantAtt, bendAtt;

    IconButton inputFxBtn  { "Input FX",  SolIcons::inputFx()   };
    IconButton harmonyBtn  { "Harmonies", SolIcons::harmonies() };
    IconButton outputFxBtn { "Output FX", SolIcons::outputFx()  };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPage)
};
