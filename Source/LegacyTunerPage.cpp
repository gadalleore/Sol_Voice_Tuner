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
        tb.setColour (juce::TextButton::buttonColourId,
                      juce::Colour (selected ? SolLookAndFeel::kAccentToggle
                                               : SolLookAndFeel::kPanel));
        tb.setColour (juce::TextButton::buttonOnColourId, juce::Colour (SolLookAndFeel::kPanelLight));
        tb.setColour (juce::TextButton::textColourOffId,  juce::Colour (SolLookAndFeel::kLabel));
        tb.setColour (juce::TextButton::textColourOnId,   juce::Colour (SolLookAndFeel::kTitleHi));
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
    productTitle.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
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
    addAndMakeVisible (roboticKnob);
    roboticLbl.setText ("Robotic", juce::dontSendNotification);
    roboticLbl.setJustificationType (juce::Justification::centred);
    roboticLbl.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
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
    addAndMakeVisible (subKnob);
    subLbl.setText ("Sub", juce::dontSendNotification);
    subLbl.setJustificationType (juce::Justification::centred);
    subLbl.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    subLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (subLbl);
    subAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                      PitchCorrectorAudioProcessor::PID_SUB, subKnob);

    formantKnob.setRange (-12.0, 12.0, 0.01);
    formantKnob.setTextValueSuffix (" st");
    formantKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    formantKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 22);
    addAndMakeVisible (formantKnob);
    formantLbl.setText ("Formant", juce::dontSendNotification);
    formantLbl.setJustificationType (juce::Justification::centred);
    formantLbl.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    formantLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    addAndMakeVisible (formantLbl);
    formantAtt = std::make_unique<SAtt> (processorRef.getAPVTS(),
                                          PitchCorrectorAudioProcessor::PID_FORMANT, formantKnob);

    // Centre-panel tab strip: Knobs | Scope
    auto setupTabBtn = [this] (juce::TextButton& b, int index)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (SolLookAndFeel::kPanel));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (SolLookAndFeel::kAccentToggle));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (SolLookAndFeel::kLabelAlt));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colour (SolLookAndFeel::kTitleHi));
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
    bendRangeKnob.setName (SolLookAndFeel::bendRangeSliderName);
    addAndMakeVisible (bendRangeKnob);
    bendRangeLbl.setText ("Bend range", juce::dontSendNotification);
    bendRangeLbl.setJustificationType (juce::Justification::centred);
    bendRangeLbl.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
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
    scaleLbl.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
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
    keyLbl.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    keyLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
    addAndMakeVisible (keyLbl);

    addAndMakeVisible (midiFollowBtn);
    midiFollowAtt = std::make_unique<BAtt> (processorRef.getAPVTS(),
                                               PitchCorrectorAudioProcessor::PID_MIDI_FOLLOW,
                                               midiFollowBtn);

    midiStatusLbl.setText ("", juce::dontSendNotification);
    midiStatusLbl.setJustificationType (juce::Justification::centredLeft);
    midiStatusLbl.setFont (juce::Font (juce::FontOptions (12.0f)));
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
    title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));

    value.setText ("No Sound", juce::dontSendNotification);
    value.setJustificationType (juce::Justification::centred);
    value.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                 26.0f, juce::Font::bold)));
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
    // Compact fixed widths + gaps so Scale / Key / MIDI Follow read as separate controls
    // (scale combo still fits longest scale name at typical editor widths).
    constexpr int scaleComboW = 200;
    constexpr int keyAreaW    = 236;
    constexpr int midiFollowW = 116;

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
    auto bendRangeCol = row.removeFromRight (juce::jmax (72, juce::jmin (row.getWidth() / 3, 100)))
                           .reduced (2, 2);
    bendRangeLbl.setBounds (bendRangeCol.removeFromTop (18));
    bendRangeKnob.setBounds (bendRangeCol);

    midiStatusLbl.setBounds (row.reduced (4, 16));
}

void LegacyTunerPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

    const auto full = getLocalBounds().toFloat();
    juce::ColourGradient grad (juce::Colour (0xff12122a), full.getCentreX(), 0.0f,
                               juce::Colour (SolLookAndFeel::kBackground),
                               full.getCentreX(), full.getHeight(), false);
    g.setGradientFill (grad);
    g.fillRect (full);

    juce::Rectangle<float> headerBar (0.0f, 0.0f, full.getWidth(), 50.0f);
    g.setColour (juce::Colour (SolLookAndFeel::kPanel).withAlpha (0.55f));
    g.fillRect (headerBar);
    g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.35f));
    g.fillRect (headerBar.removeFromBottom (1.5f));

    auto drawPitchPanel = [&] (juce::Rectangle<int> rect, bool isOut)
    {
        auto rf = rect.toFloat().reduced (1.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kPanel));
        g.fillRoundedRectangle (rf, 10.0f);

        juce::Colour edge = juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (isOut ? 0.45f : 0.28f);
        g.setColour (edge);
        g.drawRoundedRectangle (rf.reduced (0.5f), 10.0f, 1.5f);
    };

    drawPitchPanel (pitchInPanelBounds, false);
    drawPitchPanel (pitchOutPanelBounds, true);
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
