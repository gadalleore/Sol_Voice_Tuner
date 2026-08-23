/*
    LegacyTunerPage.cpp — moved from the pre-paging PluginEditor.cpp (63C-6).
*/

#include "LegacyTunerPage.h"
#include "ScaleQuantizer.h"
#include "SolLookAndFeel.h"

#include <cmath>

namespace
{
    void styleKeyNoteButton (juce::TextButton& tb, bool selected)
    {
        // Real toggle state, not just a colour swap: SolLookAndFeel's
        // drawButtonBackground/drawButtonText both key off getToggleState()
        // now (2026-08-22 revamp), so the button has to actually carry it.
        tb.setToggleState (selected, juce::dontSendNotification);
        tb.setColour (juce::TextButton::textColourOffId, juce::Colour (SolLookAndFeel::kLabel));
        tb.setColour (juce::TextButton::textColourOnId,  juce::Colour (SolLookAndFeel::kBackground));
    }

    /** No box behind a knob's value (2026-08-22 revamp), matching
        EffectDetailPage::styleKnob. Set on the instance rather than left to
        SolLookAndFeel's own default: these knobs are built as page members,
        constructed before the editor calls setLookAndFeel on the tree, and
        Slider only re-reads its text box's colours when setColour fires on
        the slider itself (Slider::colourChanged -> lookAndFeelChanged) — the
        LookAndFeel-level default alone never reaches a box already cached. */
    void clearKnobTextBoxChrome (juce::Slider& s)
    {
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    }

    /** Below this YIN confidence, treat input as "no sound" for the readout. */
    constexpr float kInputPitchConfidenceMin = 0.01f;

    bool hasUsablePitchInReadout (float hz, float confidence) noexcept
    {
        return std::isfinite (hz) && confidence >= kInputPitchConfidenceMin
               && hz >= PitchCorrectorAudioProcessor::minTrackedPitchHz;
    }

    bool hasUsablePitchOutReadout (float snappedHz) noexcept
    {
        return std::isfinite (snappedHz) && snappedHz > 0.0f;
    }

    juce::String formatPitchLine (float hz)
    {
        if (! std::isfinite (hz) || hz <= 0.0f)
            return "No Sound";

        const int midi = (int) std::lround (SolTune::hzToMidi (hz));
        return juce::String (hz, 1) + " Hz\n("
             + juce::String (SolTune::midiNoteName (midi).c_str()) + ")";
    }
}

//==============================================================================
LegacyTunerPage::LegacyTunerPage (PitchCorrectorAudioProcessor& p)
    : processorRef (p)
{
    productTitle.setText (JucePlugin_Name, juce::dontSendNotification);
    productTitle.setJustificationType (juce::Justification::centred);
    productTitle.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 22.0f, juce::Font::plain)));
    productTitle.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
    addAndMakeVisible (productTitle);

    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<BAtt> (processorRef.getAPVTS(),
                                         PitchCorrectorAudioProcessor::PID_BYPASS, bypassBtn);

    stylePitchReadout (pitchInTitle,  pitchInValue,  "Pitch In");
    stylePitchReadout (pitchOutTitle, pitchOutValue, "Pitch Out");
    addAndMakeVisible (pitchInTitle);
    addAndMakeVisible (pitchInValue);
    addAndMakeVisible (pitchOutTitle);
    addAndMakeVisible (pitchOutValue);

    roboticKnob.setRange (0.0, 1.0, 0.01);
    roboticKnob.setTextValueSuffix (" %");
    roboticKnob.textFromValueFunction = [] (double v)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 100.0));
    };
    roboticKnob.valueFromTextFunction = [] (const juce::String& t)
    {
        auto s = t.trim().removeCharacters ("%").trim();
        return s.getDoubleValue() / 100.0;
    };
    roboticKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    roboticKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 22);
    clearKnobTextBoxChrome (roboticKnob);
    addAndMakeVisible (roboticKnob);
    roboticLbl.setText ("Robotic", juce::dontSendNotification);
    roboticLbl.setJustificationType (juce::Justification::centred);
    roboticLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
    roboticLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (roboticLbl);
    roboticAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                          PitchCorrectorAudioProcessor::PID_ROBOTIC, roboticKnob);

    subKnob.setRange (0.0, 1.0, 0.01);
    subKnob.setTextValueSuffix (" %");
    subKnob.textFromValueFunction = [] (double v)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 100.0));
    };
    subKnob.valueFromTextFunction = [] (const juce::String& t)
    {
        auto s = t.trim().removeCharacters ("%").trim();
        return s.getDoubleValue() / 100.0;
    };
    subKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    subKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 22);
    clearKnobTextBoxChrome (subKnob);
    addAndMakeVisible (subKnob);
    subLbl.setText ("Sub", juce::dontSendNotification);
    subLbl.setJustificationType (juce::Justification::centred);
    subLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
    subLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (subLbl);
    subAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                      PitchCorrectorAudioProcessor::PID_SUB, subKnob);

    formantKnob.setRange (-12.0, 12.0, 0.01);
    formantKnob.setTextValueSuffix (" st");
    formantKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    formantKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 22);
    clearKnobTextBoxChrome (formantKnob);
    addAndMakeVisible (formantKnob);
    formantLbl.setText ("Formant", juce::dontSendNotification);
    formantLbl.setJustificationType (juce::Justification::centred);
    formantLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
    formantLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (formantLbl);
    formantAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                          PitchCorrectorAudioProcessor::PID_FORMANT, formantKnob);

    // Centre-panel tab strip: Knobs | Scope
    auto setupTabBtn = [this] (juce::TextButton& b, int index)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (SolLookAndFeel::kLabelAlt));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colour (SolLookAndFeel::kBackground));
        b.onClick = [this, index] { setCentreTab (index); };
        addAndMakeVisible (b);
    };
    setupTabBtn (tabKnobsBtn, 0);
    setupTabBtn (tabScopeBtn, 1);

    addAndMakeVisible (oscilloscope);
    setCentreTab (0);

    // 63C-18: pitch bend + volume moved to the always-visible MeterSidebar.
    bendRangeKnob.setRange (0.0, 12.0, 1.0);
    bendRangeKnob.setNumDecimalPlacesToDisplay (0);
    bendRangeKnob.setTextValueSuffix (" st");
    bendRangeKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    bendRangeKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    clearKnobTextBoxChrome (bendRangeKnob);
    bendRangeKnob.setName (SolLookAndFeel::bendRangeSliderName);
    addAndMakeVisible (bendRangeKnob);
    bendRangeLbl.setText ("Bend range", juce::dontSendNotification);
    bendRangeLbl.setJustificationType (juce::Justification::centred);
    bendRangeLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 11.0f, juce::Font::plain)));
    bendRangeLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (bendRangeLbl);
    bendRangeAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                            PitchCorrectorAudioProcessor::PID_BEND_RANGE, bendRangeKnob);

    for (int i = 0; i < (int) SolTune::Scale::NumScales; ++i)
        scaleBox.addItem (SolTune::scaleName (i), i + 1);
    scaleBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (scaleBox);
    scaleLbl.setText ("Scale", juce::dontSendNotification);
    scaleLbl.setJustificationType (juce::Justification::centredLeft);
    scaleLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 12.0f, juce::Font::plain)));
    scaleLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
    addAndMakeVisible (scaleLbl);
    scaleAtt = std::make_unique<CAtt> (processorRef.getAPVTS(),
                                        PitchCorrectorAudioProcessor::PID_SCALE, scaleBox);

    for (int i = 0; i < 12; ++i)
    {
        auto& b = keyNoteBtns[static_cast<size_t> (i)];
        b.setButtonText (juce::String (SolTune::rootChoiceLabel (i)));
        b.getProperties().set (juce::Identifier (SolLookAndFeel::solKeyNoteButtonProperty), true);
        b.setTriggeredOnMouseDown (true);
        const int idx = i;
        b.onClick = [this, idx]
        {
            auto* param = processorRef.getAPVTS().getParameter (PitchCorrectorAudioProcessor::PID_ROOT);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            {
                choice->beginChangeGesture();
                *choice = idx;
                choice->endChangeGesture();
            }
            else if (keyRootAtt != nullptr)
            {
                keyRootAtt->setValueAsCompleteGesture ((float) idx);
            }

            refreshKeyNoteSelection();
        };
        addAndMakeVisible (b);
    }

    if (auto* rootParam = dynamic_cast<juce::RangedAudioParameter*> (
            processorRef.getAPVTS().getParameter (PitchCorrectorAudioProcessor::PID_ROOT)))
    {
        keyRootAtt = std::make_unique<juce::ParameterAttachment> (
            *rootParam,
            [this] (float)
            {
                refreshKeyNoteSelection();
            },
            nullptr);
        keyRootAtt->sendInitialUpdate();
    }
    else
    {
        refreshKeyNoteSelection();
    }

    keyLbl.setText ("Key", juce::dontSendNotification);
    keyLbl.setJustificationType (juce::Justification::centredLeft);
    keyLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 12.0f, juce::Font::plain)));
    keyLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
    addAndMakeVisible (keyLbl);

    addAndMakeVisible (midiFollowBtn);
    midiFollowAtt = std::make_unique<BAtt> (processorRef.getAPVTS(),
                                               PitchCorrectorAudioProcessor::PID_MIDI_FOLLOW,
                                               midiFollowBtn);

    midiStatusLbl.setText ("", juce::dontSendNotification);
    midiStatusLbl.setJustificationType (juce::Justification::centredLeft);
    midiStatusLbl.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 12.0f, juce::Font::plain)));
    midiStatusLbl.setColour (juce::Label::textColourId,
                             juce::Colour (SolLookAndFeel::kValue).withAlpha (0.85f));
    addAndMakeVisible (midiStatusLbl);

    startTimerHz (30);
}

LegacyTunerPage::~LegacyTunerPage() = default;

void LegacyTunerPage::stylePitchReadout (juce::Label& title,
                                         juce::Label& value,
                                         const juce::String& titleText)
{
    title.setText (titleText, juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 13.0f, juce::Font::plain)));
    title.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));

    value.setText ("No Sound", juce::dontSendNotification);
    value.setJustificationType (juce::Justification::centred);
    value.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                                                                                  26.0f, juce::Font::plain)));
    value.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kTitleHi));
}

void LegacyTunerPage::setCentreTab (int t)
{
    currentCentreTab = juce::jlimit (0, 1, t);
    const bool knobs = (currentCentreTab == 0);

    roboticKnob.setVisible (knobs); roboticLbl.setVisible (knobs);
    subKnob    .setVisible (knobs); subLbl    .setVisible (knobs);
    formantKnob.setVisible (knobs); formantLbl.setVisible (knobs);
    oscilloscope.setVisible (! knobs);

    tabKnobsBtn.setToggleState (knobs,  juce::dontSendNotification);
    tabScopeBtn.setToggleState (! knobs, juce::dontSendNotification);
    resized();
}

void LegacyTunerPage::refreshKeyNoteSelection()
{
    int sel = 0;
    if (auto* raw = processorRef.getAPVTS().getRawParameterValue (PitchCorrectorAudioProcessor::PID_ROOT))
        sel = juce::jlimit (0, 11, juce::roundToInt (raw->load()));

    for (int i = 0; i < 12; ++i)
        styleKeyNoteButton (keyNoteBtns[static_cast<size_t> (i)], i == sel);
}

//==============================================================================
void LegacyTunerPage::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (50);
    bypassBtn.setBounds (header.removeFromRight (118).reduced (8, 10));
    productTitle.setBounds (header.reduced (12, 8));

    auto bottom = r.removeFromBottom (124).reduced (16, 10);
    auto main   = r.reduced (14, 8);

    const int totalW = main.getWidth();
    const int leftW  = juce::roundToInt ((float) totalW * 0.28f);
    const int rightW = juce::roundToInt ((float) totalW * 0.30f);

    auto leftCol  = main.removeFromLeft (leftW);
    auto rightCol = main.removeFromRight (rightW);
    auto centre   = main.reduced (6, 0);

    pitchInPanelBounds = leftCol;
    {
        auto inCol = leftCol.reduced (10, 10);
        pitchInTitle.setBounds (inCol.removeFromTop (22));
        pitchInValue.setBounds (inCol);
    }

    pitchOutPanelBounds = rightCol;
    {
        auto outCol = rightCol.reduced (10, 10);
        pitchOutTitle.setBounds (outCol.removeFromTop (22));
        pitchOutValue.setBounds (outCol);
    }

    // Tab strip across the top of the centre column
    auto tabStrip = centre.removeFromTop (24);
    {
        const int tabW = juce::jmax (60, juce::jmin (110, tabStrip.getWidth() / 2 - 4));
        tabKnobsBtn.setBounds (tabStrip.removeFromLeft (tabW));
        tabStrip.removeFromLeft (4);
        tabScopeBtn.setBounds (tabStrip.removeFromLeft (tabW));
    }
    centre.removeFromTop (4);

    if (currentCentreTab == 0)
    {
        auto knobRow = centre;
        const int gap = juce::jmax (8, knobRow.getWidth() / 40);
        const int rw  = (knobRow.getWidth() - 2 * gap) / 3;

        auto leftKnob = knobRow.removeFromLeft (rw);
        roboticLbl.setBounds (leftKnob.removeFromTop (18));
        roboticKnob.setBounds (leftKnob);

        knobRow.removeFromLeft (gap);
        auto midKnob = knobRow.removeFromLeft (rw);
        subLbl.setBounds (midKnob.removeFromTop (18));
        subKnob.setBounds (midKnob);

        knobRow.removeFromLeft (gap);
        formantLbl.setBounds (knobRow.removeFromTop (18));
        formantKnob.setBounds (knobRow);
    }
    else
    {
        oscilloscope.setBounds (centre.reduced (2, 2));
    }

    auto row = bottom;
    constexpr int bottomStripGap = 12;
    // Compact widths + gaps so Scale / Key / MIDI Follow read as separate controls
    // (scale combo still fits longest scale name at typical editor widths).
    //
    // Shrunk together when the row cannot hold them all. These used to be hard
    // constants summing to more than the page gets once the plate's right-hand
    // column is reserved (TuningWindowPage::kRightColumn), and because the
    // blocks are taken off the left with removeFromLeft, the overflow all
    // landed on the LAST control — the bend-range knob simply stopped being
    // laid out. Scaling keeps every control on the page instead of silently
    // dropping the one at the end.
    constexpr int wantScale = 200, wantKey = 236, wantMidi = 116, wantBend = 72;
    constexpr int wantTotal = wantScale + wantKey + wantMidi + wantBend
                            + bottomStripGap * 3 + 6;

    const float fit = juce::jlimit (0.55f, 1.0f,
                                    (float) row.getWidth() / (float) wantTotal);

    const int scaleComboW = juce::roundToInt (wantScale * fit);
    const int keyAreaW    = juce::roundToInt (wantKey   * fit);
    const int midiFollowW = juce::roundToInt (wantMidi  * fit);
    const int bendRangeW  = juce::roundToInt (wantBend  * fit);

    auto scaleBlock = row.removeFromLeft (scaleComboW);
    scaleLbl.setBounds (scaleBlock.removeFromTop (18));
    scaleBox.setBounds (scaleBlock.removeFromTop (34));
    row.removeFromLeft (bottomStripGap);

    auto keyBlock = row.removeFromLeft (keyAreaW);
    keyLbl.setBounds (keyBlock.removeFromTop (18));
    {
        auto grid = keyBlock;
        constexpr int cols = 6;
        constexpr int rows = 2;
        constexpr int g = 3;
        const int cw = (grid.getWidth() - (cols - 1) * g) / cols;
        const int ch = juce::jmax (20, (grid.getHeight() - (rows - 1) * g) / rows);

        for (int keyRow = 0; keyRow < rows; ++keyRow)
            for (int c = 0; c < cols; ++c)
            {
                const int i = keyRow * cols + c;
                const int x = grid.getX() + c * (cw + g);
                const int y = grid.getY() + keyRow * (ch + g);
                keyNoteBtns[static_cast<size_t> (i)].setBounds (x, y, cw, ch);
            }
    }
    row.removeFromLeft (bottomStripGap);

    midiFollowBtn.setBounds (row.removeFromLeft (midiFollowW).withHeight (34).translated (0, 18));
    row.removeFromLeft (bottomStripGap);

    row.removeFromRight (6);
    // Bend range knob (pitch bend + volume moved to the MeterSidebar, 63C-18).
    // Width comes from the same `fit` as everything else on the row, so it
    // shrinks with its neighbours rather than being the one that vanishes.
    auto bendRangeCol = row.removeFromRight (juce::jmin (row.getWidth(), bendRangeW))
                           .reduced (2, 2);
    bendRangeLbl.setBounds (bendRangeCol.removeFromTop (18));
    bendRangeKnob.setBounds (bendRangeCol);

    midiStatusLbl.setBounds (row.reduced (4, 16));
}

void LegacyTunerPage::paint (juce::Graphics& g)
{
    // Bare plate — no title pane (SolPage.h, Giuseppe 2026-08-16): no filled
    // header, no divider rule, no panel behind the readouts. This page used
    // to paint its own pre-rebrand navy header/panel chrome underneath the
    // SolPage furniture around it; gone, so it reads as the same white
    // surface as every other page.
    g.fillAll (juce::Colour (SolLookAndFeel::kBackground));
}

void LegacyTunerPage::timerCallback()
{
    const float inHz  = processorRef.getDetectedPitchHz();
    const float inCf  = processorRef.getDetectionConfidence();
    const float outHz = processorRef.getSnappedTargetHz();

    pitchInValue.setText (hasUsablePitchInReadout (inHz, inCf) ? formatPitchLine (inHz) : "No Sound",
                          juce::dontSendNotification);
    pitchOutValue.setText (hasUsablePitchOutReadout (outHz) ? formatPitchLine (outHz) : "No Sound",
                           juce::dontSendNotification);

    const bool midiOn = processorRef.getAPVTS().getRawParameterValue (
                            PitchCorrectorAudioProcessor::PID_MIDI_FOLLOW)->load() > 0.5f;

    if (! midiOn)
        midiStatusLbl.setText ("", juce::dontSendNotification);
    else if (processorRef.getActiveMidiNoteCount() > 0
             && processorRef.getLastMidiChannel() > 0)
        midiStatusLbl.setText ("Receiving MIDI from Channel "
                                   + juce::String (processorRef.getLastMidiChannel()),
                               juce::dontSendNotification);
    else
        midiStatusLbl.setText ("No midi", juce::dontSendNotification);

    if (currentCentreTab == 1 && oscilloscope.isVisible())
    {
        oscilloscope.update (processorRef.getScopeBuffer(),
                             processorRef.getScopeValidSamples());
        oscilloscope.repaint();
    }

    repaint();
}
