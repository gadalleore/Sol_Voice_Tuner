/*
    HarmoniesWindowPage.h
    ---------------------
    Window 2 of the paging UI (63C-13). The LEAD voice sits in the hub at the
    centre of the wheel; the harmony voices ring the rim around it. Every voice
    drills into a settings page: the Lead opens the tuner (onOpenTuning); each
    harmony opens a VoiceSettingsPage with its pitch wheel + level + pan. Empty
    rim positions show a "+" to spawn a harmony there.

    The KEY (root + scale) lives on this page and is global to every voice. The
    scale decides how the pitch wheel's steps read: Chromatic = fixed semitones
    (dumb harmonizer); any scale = diatonic scale degrees (in-key harmony). The
    DSP (PluginProcessor::mixHarmonyVoices) resolves the interval the same way.

    63C-16 will grow the harmony settings page into the full per-voice tuner
    chain (Autotune / Roboto / Vocoder); it slots in beside the pitch controls.
*/

#pragma once

#include <JuceHeader.h>

#include "SolPage.h"
#include "PluginProcessor.h"
#include "ScaleQuantizer.h"

//==============================================================================
/** Vertical pitch wheel: a semicircle bulging LEFT and opening RIGHT ("("),
    top = +12, middle = 0, bottom = -12. A knob rides the arc; drag up/down to
    set the interval, snapped to integer steps. */
class HarmonyPitchWheel final : public juce::Component
{
public:
    std::function<bool()> isChromatic;

    HarmonyPitchWheel (juce::RangedAudioParameter& p)
        : att (p, [this] (float v) { value = (int) std::lround (v); repaint(); })
    {
        att.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        computeGeometry();

        // Arc.
        juce::Path arc;
        for (int i = 0; i <= 48; ++i)
        {
            const auto p = pointForU (-1.0f + 2.0f * (float) i / 48.0f);
            if (i == 0) arc.startNewSubPath (p); else arc.lineTo (p);
        }
        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.7f));
        g.strokePath (arc, juce::PathStrokeType (2.0f));

        // Step ticks.
        for (int s = -12; s <= 12; ++s)
        {
            const auto p    = pointForU ((float) s / 12.0f);
            const auto out  = (p - centre) / juce::jmax (1.0f, p.getDistanceFrom (centre));
            const float len = (s % 12 == 0) ? 7.0f : 4.0f;
            g.setColour (juce::Colour (s == value ? SolLookAndFeel::kAccentGlow
                                                  : SolLookAndFeel::kOutline)
                             .withAlpha (s == value ? 1.0f : 0.55f));
            g.drawLine ({ p, p - out * len }, s == value ? 2.5f : 1.0f);
        }

        // End + centre labels.
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        drawTickLabel (g, "+12", pointForU (1.0f));
        drawTickLabel (g, "0",   pointForU (0.0f));
        drawTickLabel (g, "-12", pointForU (-1.0f));

        // Knob riding the arc.
        const auto k = pointForU ((float) value / 12.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kAccentArc));
        g.fillEllipse (k.x - 9.0f, k.y - 9.0f, 18.0f, 18.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kBackground));
        g.drawEllipse (k.x - 9.0f, k.y - 9.0f, 18.0f, 18.0f, 2.0f);

        // Big value readout + mode, in the open (right) side.
        auto text = getLocalBounds().withTrimmedRight ((int) (getWidth() * 0.42f));
        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        g.drawText ((value > 0 ? "+" : "") + juce::String (value),
                    text.removeFromTop (text.getHeight() / 2), juce::Justification::centred);
        g.setColour (juce::Colour (SolLookAndFeel::kLabelAlt));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ((isChromatic && isChromatic()) ? "semitones" : "scale steps",
                    text.removeFromTop (18), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override { att.beginGesture(); setFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseUp   (const juce::MouseEvent&)   override { att.endGesture(); }

private:
    void computeGeometry()
    {
        auto b = getLocalBounds().toFloat().reduced (10.0f);
        radius = juce::jmin (b.getHeight() * 0.5f - 4.0f, b.getWidth() * 0.55f);
        centre = { b.getRight() - 6.0f, b.getCentreY() };   // circle centre on the RIGHT
    }

    juce::Point<float> pointForU (float u) const           // u = value/12 in [-1,1]
    {
        const float a = u * juce::MathConstants<float>::halfPi;
        return { centre.x - radius * std::cos (a), centre.y - radius * std::sin (a) };
    }

    void drawTickLabel (juce::Graphics& g, const juce::String& t, juce::Point<float> at)
    {
        g.drawText (t, juce::Rectangle<float> (at.x - 34.0f, at.y - 7.0f, 26.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    void setFromMouse (const juce::MouseEvent& e)
    {
        computeGeometry();
        const float topY = centre.y - radius, botY = centre.y + radius;
        const float y    = juce::jlimit (topY, botY, (float) e.y);
        const int   v    = juce::jlimit (-12, 12, juce::roundToInt (juce::jmap (y, topY, botY, 12.0f, -12.0f)));
        att.setValueAsPartOfGesture ((float) v);
    }

    juce::ParameterAttachment att;
    juce::Point<float> centre;
    float radius = 0.0f;
    int   value  = 0;
};

//==============================================================================
/** Per-voice drill-in settings page: pitch wheel + level + pan (+ remove).
    63C-16 adds the per-voice tuner chain wheel here. */
class VoiceSettingsPage final : public SolPage,
                                private juce::Timer
{
public:
    VoiceSettingsPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Harmony"), apvts (apvtsIn)
    {
        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        addAndMakeVisible (levelSlider);
        levelLbl.setText ("Level", juce::dontSendNotification);  styleLbl (levelLbl);

        panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        addAndMakeVisible (panSlider);
        panLbl.setText ("Pan", juce::dontSendNotification);      styleLbl (panLbl);

        removeBtn.setButtonText ("Remove voice");
        removeBtn.onClick = [this] { removeVoice(); };
        addAndMakeVisible (removeBtn);

        startTimerHz (10);   // refresh the pitch wheel's semitones/steps label
    }

    ~VoiceSettingsPage() override { stopTimer(); }

    void rebind (int voice)
    {
        boundVoice = voice;
        setTitle ("Harmony " + juce::String (voice + 1));

        pitchWheel = std::make_unique<HarmonyPitchWheel> (
            *apvts.getParameter (PitchCorrectorAudioProcessor::harmIntervalParamId (voice)));
        pitchWheel->isChromatic = [this] { return scaleIsChromatic(); };
        addAndMakeVisible (*pitchWheel);

        levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, PitchCorrectorAudioProcessor::harmLevelParamId (voice), levelSlider);
        panAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, PitchCorrectorAudioProcessor::harmPanParamId (voice), panSlider);

        resized();
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        auto right = area.removeFromRight (juce::jmin (240, area.getWidth() / 2));
        if (pitchWheel) pitchWheel->setBounds (area.reduced (4));

        right.reduce (10, 8);
        levelLbl.setBounds    (right.removeFromTop (16));
        levelSlider.setBounds (right.removeFromTop (26));
        right.removeFromTop (10);
        panLbl.setBounds      (right.removeFromTop (16));
        panSlider.setBounds   (right.removeFromTop (26));
        right.removeFromTop (18);
        removeBtn.setBounds   (right.removeFromTop (30));
    }

    void timerCallback() override { if (pitchWheel) pitchWheel->repaint(); }

    bool scaleIsChromatic() const
    {
        if (auto* p = apvts.getRawParameterValue (PitchCorrectorAudioProcessor::PID_SCALE))
            return (int) std::lround (p->load()) == (int) SolTune::Scale::Chromatic;
        return false;
    }

    void removeVoice()
    {
        if (boundVoice >= 0)
            if (auto* p = apvts.getParameter (PitchCorrectorAudioProcessor::harmEnableParamId (boundVoice)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (0.0f);
                p->endChangeGesture();
            }
        stack.pop();
    }

    void styleLbl (juce::Label& l)
    {
        l.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
        addAndMakeVisible (l);
    }

    juce::AudioProcessorValueTreeState& apvts;
    int boundVoice = -1;

    std::unique_ptr<HarmonyPitchWheel> pitchWheel;
    juce::Label  levelLbl, panLbl;
    juce::Slider levelSlider, panSlider;
    juce::TextButton removeBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt, panAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceSettingsPage)
};

//==============================================================================
/** Lead in the centre hub; harmony voices on the rim around it. Occupied rim
    slots are clickable pills (drill into settings); empty slots show a "+"
    that spawns a harmony there. */
class HarmonyWheel final : public juce::Component
{
public:
    HarmonyWheel() = default;

    std::function<bool (int)> isOn;        // voice enabled?
    std::function<void()>     onLead;      // centre clicked
    std::function<void (int)> onHarmony;   // occupied rim slot clicked
    std::function<void (int)> onAdd;       // empty rim slot clicked

    void paint (juce::Graphics& g) override
    {
        computeGeometry();

        // Rim guide circle.
        g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.3f));
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

        // Spokes + rim slots.
        for (int i = 0; i < kNumHarmony; ++i)
        {
            const auto p   = slotCentre (i);
            const bool on  = isOn && isOn (i);
            const bool hot = i == hovered;

            g.setColour (juce::Colour (SolLookAndFeel::kOutline).withAlpha (0.25f));
            g.drawLine ({ centre, p }, 1.0f);

            auto pill = juce::Rectangle<float> (pillW, pillH).withCentre (p);
            if (on)
            {
                g.setColour (juce::Colour (hot ? SolLookAndFeel::kPanelLight : SolLookAndFeel::kPanel));
                g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
                g.setColour (juce::Colour (hot ? SolLookAndFeel::kOutlineHi : SolLookAndFeel::kOutline));
                g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, hot ? 2.0f : 1.2f);
                g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                g.drawText ("Harmony " + juce::String (i + 1), pill, juce::Justification::centred);
            }
            else
            {
                juce::Path ring;
                ring.addEllipse (p.x - addR, p.y - addR, addR * 2.0f, addR * 2.0f);
                juce::Path dashed;
                const float d[] = { 4.0f, 4.0f };
                juce::PathStrokeType (hot ? 2.2f : 1.4f).createDashedStroke (dashed, ring, d, 2);
                g.setColour (juce::Colour (hot ? SolLookAndFeel::kOutlineHi : SolLookAndFeel::kOutline)
                                 .withAlpha (hot ? 0.95f : 0.6f));
                g.fillPath (dashed);
                g.setColour (juce::Colour (hot ? SolLookAndFeel::kTitleHi : SolLookAndFeel::kLabelAlt));
                g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
                g.drawText ("+", pill, juce::Justification::centred);
            }
        }

        // Lead hub.
        const bool leadHot = hovered == kLeadHit;
        g.setColour (juce::Colour (leadHot ? SolLookAndFeel::kPanelLight : SolLookAndFeel::kPanel));
        g.fillEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f);
        g.setColour (juce::Colour (leadHot ? SolLookAndFeel::kOutlineHi : SolLookAndFeel::kAccentGlow)
                         .withAlpha (0.7f));
        g.drawEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f, 2.0f);
        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("LEAD", juce::Rectangle<float> (centre.x - hubR, centre.y - 9.0f, hubR * 2.0f, 18.0f),
                    juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h != hovered) { hovered = h; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered != -2) { hovered = -2; repaint(); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h == kLeadHit)   { if (onLead) onLead(); }
        else if (h >= 0)
        {
            if (isOn && isOn (h)) { if (onHarmony) onHarmony (h); }
            else                  { if (onAdd)     onAdd (h);     }
        }
    }

private:
    static constexpr int kNumHarmony = PitchCorrectorAudioProcessor::kNumHarmony;
    static constexpr int kLeadHit    = -1;

    void computeGeometry()
    {
        auto b = getLocalBounds().toFloat().reduced (8.0f);
        centre = b.getCentre();
        radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - juce::jmax (pillW, pillH) * 0.5f - 4.0f;
        radius = juce::jmax (40.0f, radius);
    }

    juce::Point<float> slotCentre (int i) const
    {
        // Even spread around the circle, starting at top, clockwise.
        const float a = -juce::MathConstants<float>::halfPi
                      + (float) i * juce::MathConstants<float>::twoPi / (float) kNumHarmony;
        return { centre.x + radius * std::cos (a), centre.y + radius * std::sin (a) };
    }

    int hit (juce::Point<float> p)
    {
        computeGeometry();
        if (p.getDistanceFrom (centre) <= hubR + 2.0f)
            return kLeadHit;
        for (int i = 0; i < kNumHarmony; ++i)
            if (juce::Rectangle<float> (pillW, pillH).withCentre (slotCentre (i)).expanded (6.0f).contains (p))
                return i;
        return -2;
    }

    static constexpr float pillW = 92.0f, pillH = 30.0f, hubR = 40.0f, addR = 15.0f;

    juce::Point<float> centre;
    float radius  = 0.0f;
    int   hovered = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyWheel)
};

//==============================================================================
class HarmoniesWindowPage final : public SolPage,
                                  private juce::Timer
{
public:
    std::function<void()> onOpenTuning;   // Lead -> tuner

    HarmoniesWindowPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Harmonies"),
          apvts (apvtsIn),
          voicePage (apvtsIn, stackToUse)
    {
        keyLbl.setText ("Key", juce::dontSendNotification);
        keyLbl.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        keyLbl.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabelAlt));
        addAndMakeVisible (keyLbl);

        for (int i = 0; i < 12; ++i) rootBox.addItem (SolTune::rootName (i), i + 1);
        addAndMakeVisible (rootBox);
        rootAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, PitchCorrectorAudioProcessor::PID_ROOT, rootBox);

        for (int i = 0; i < (int) SolTune::Scale::NumScales; ++i)
            scaleBox.addItem (SolTune::scaleName (i), i + 1);
        addAndMakeVisible (scaleBox);
        scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, PitchCorrectorAudioProcessor::PID_SCALE, scaleBox);

        wheel.isOn      = [this] (int v) { return voiceOn (v); };
        wheel.onLead    = [this]         { if (onOpenTuning) onOpenTuning(); };
        wheel.onHarmony = [this] (int v) { openVoice (v); };
        wheel.onAdd     = [this] (int v) { setEnable (v, true); openVoice (v); };
        addAndMakeVisible (wheel);

        startTimerHz (12);   // reflect host/preset changes on the wheel
    }

    ~HarmoniesWindowPage() override { stopTimer(); }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        auto top = area.removeFromTop (34);
        keyLbl.setBounds   (top.removeFromLeft (34));
        rootBox.setBounds  (top.removeFromLeft (70).reduced (2));
        scaleBox.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth())).reduced (2));
        wheel.setBounds (area);
    }

    void timerCallback() override { wheel.repaint(); }

    bool voiceOn (int v) const
    {
        if (auto* p = apvts.getRawParameterValue (PitchCorrectorAudioProcessor::harmEnableParamId (v)))
            return p->load() >= 0.5f;
        return false;
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

    void openVoice (int voice)
    {
        voicePage.rebind (voice);
        stack.push (voicePage);
    }

    juce::AudioProcessorValueTreeState& apvts;

    juce::Label    keyLbl;
    juce::ComboBox rootBox, scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt, scaleAtt;

    HarmonyWheel      wheel;
    VoiceSettingsPage voicePage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniesWindowPage)
};
