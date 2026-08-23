/*
    EffectDetailPage.h
    ------------------
    One effect's controls (63C-8).

    Nothing here is written per effect. rebind() looks the effect's control set
    up in EffectParams.h and builds exactly what it declares — a knob, a toggle
    or a dropdown per entry, attached to the matching APVTS parameter — so a
    control cannot appear on the page without existing in the audio graph, and
    adding one to the table is the whole job.

    Above them sits the slot's own Amount: how much of this effect is heard at
    this point in the chain, on top of whatever Mix the effect itself has.

    The face follows the Home page (SolPage): black on white, brand typeface,
    no panels behind anything.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectParams.h"
#include "SolLookAndFeel.h"
#include "SolPage.h"

#include <memory>
#include <vector>

class EffectDetailPage final : public SolPage
{
public:
    EffectDetailPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Effect"), apvts (apvtsIn)
    {
        styleKnob (amount);
        addAndMakeVisible (amount);

        styleCaption (amountLabel, "Amount");
        addAndMakeVisible (amountLabel);

        styleCaption (slotLabel, {});
        slotLabel.setAlpha (0.6f);
        addAndMakeVisible (slotLabel);
    }

    /** Point the page at one slot before pushing it: the slot's Amount, and the
        full control set of whatever effect that slot holds. */
    void rebind (const juce::String& amountParamId,
                 const juce::String& effectName,
                 VocalFx::EffectType type,
                 int chainIndex,
                 int slotIndex,
                 const std::function<juce::String (VocalFx::EffectType, const char*)>& paramIdFor)
    {
        setTitle (effectName);
        slotLabel.setText ("Chain position " + juce::String (slotIndex + 1),
                           juce::dontSendNotification);

        amountAtt.reset();
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, amountParamId, amount);

        buildControls (type, paramIdFor);
        resized();
    }

private:
    //==========================================================================
    /** One declared control: whichever widget its kind calls for, its caption,
        and the attachment that keeps it and the parameter in step. */
    struct Control
    {
        VocalFx::ParamKind kind {};

        std::unique_ptr<juce::Slider>      knob;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox>    combo;
        std::unique_ptr<juce::Label>       caption;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   knobAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   toggleAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;

        juce::Component* widget() const
        {
            if (knob   != nullptr) return knob.get();
            if (toggle != nullptr) return toggle.get();
            return combo.get();
        }
    };

    void buildControls (VocalFx::EffectType type,
                        const std::function<juce::String (VocalFx::EffectType, const char*)>& paramIdFor)
    {
        // Attachments must die before the components they drive.
        for (auto& c : controls)
        {
            c.knobAtt.reset();
            c.toggleAtt.reset();
            c.comboAtt.reset();
        }
        controls.clear();

        for (const auto& p : VocalFx::effectParams (type))
        {
            const auto paramId = paramIdFor (type, p.id);
            if (apvts.getParameter (paramId) == nullptr)
                continue;                       // declared but not built — skip rather than crash

            Control c;
            c.kind = p.kind;

            switch (p.kind)
            {
                case VocalFx::ParamKind::Toggle:
                    c.toggle = std::make_unique<juce::ToggleButton>();
                    styleToggle (*c.toggle);
                    addAndMakeVisible (*c.toggle);
                    c.toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        apvts, paramId, *c.toggle);
                    break;

                case VocalFx::ParamKind::Choice:
                    c.combo = std::make_unique<juce::ComboBox>();
                    styleCombo (*c.combo);
                    addAndMakeVisible (*c.combo);
                    c.comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, paramId, *c.combo);
                    break;

                case VocalFx::ParamKind::Knob:
                default:
                    c.knob = std::make_unique<juce::Slider>();
                    styleKnob (*c.knob);
                    c.knob->setTextValueSuffix (p.unit);
                    addAndMakeVisible (*c.knob);
                    c.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, paramId, *c.knob);
                    break;
            }

            c.caption = std::make_unique<juce::Label>();
            styleCaption (*c.caption, p.name);
            addAndMakeVisible (*c.caption);

            controls.push_back (std::move (c));
        }
    }

    //==========================================================================
    void layoutContent (juce::Rectangle<int> area) override
    {
        slotLabel.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);

        // The slot's own Amount leads, on its own row, because it is the one
        // control that is about the CHAIN rather than about this effect.
        {
            auto row = area.removeFromTop (kAmountSize + kCaptionH);
            auto box = row.removeFromLeft (kAmountSize + 24);
            amount     .setBounds (box.removeFromTop (kAmountSize));
            amountLabel.setBounds (box);
        }

        area.removeFromTop (6);

        if (controls.empty())
            return;

        // A plain grid, as many columns as fit. Toggles and dropdowns take one
        // cell each, the same as a knob, so rows always line up.
        const int cellW = kCellWidth;
        const int cellH = kCellHeight;
        const int cols  = juce::jmax (1, area.getWidth() / cellW);

        for (size_t i = 0; i < controls.size(); ++i)
        {
            const int col = (int) i % cols;
            const int row = (int) i / cols;

            juce::Rectangle<int> cell (area.getX() + col * cellW,
                                       area.getY() + row * cellH,
                                       cellW, cellH);

            if (cell.getBottom() > area.getBottom())
                break;                     // off the page: the wheel scrolls, this does not

            auto& c = controls[i];
            auto  captionArea = cell.removeFromBottom (kCaptionH);
            c.caption->setBounds (captionArea);

            auto widgetArea = cell.reduced (4, 2);

            if (c.knob != nullptr)
                c.knob->setBounds (widgetArea);
            else if (c.toggle != nullptr)
                c.toggle->setBounds (widgetArea.withSizeKeepingCentre (24, 24));
            else if (c.combo != nullptr)
                c.combo->setBounds (widgetArea.withSizeKeepingCentre (widgetArea.getWidth(), 22));
        }
    }

    //==========================================================================
    // Sun-white styling: ink on the plate, no fills, no borders.
    static void styleKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
        s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (SolLookAndFeel::kLabel));
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    static void styleToggle (juce::ToggleButton& b)
    {
        b.setColour (juce::ToggleButton::tickColourId,       juce::Colour (SolLookAndFeel::kTitleHi));
        b.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (SolLookAndFeel::kOutline));
    }

    static void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (SolLookAndFeel::kOutline));
        c.setColour (juce::ComboBox::textColourId,       juce::Colour (SolLookAndFeel::kTitleHi));
        c.setColour (juce::ComboBox::arrowColourId,      juce::Colour (SolLookAndFeel::kLabel));
    }

    static void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 11.0f, juce::Font::plain)));
        l.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    }

    static constexpr int kAmountSize = 62;
    static constexpr int kCaptionH   = 14;
    static constexpr int kCellWidth  = 78;
    static constexpr int kCellHeight = 78;

    juce::AudioProcessorValueTreeState& apvts;

    juce::Slider amount;
    juce::Label  amountLabel, slotLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;

    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectDetailPage)
};
