/*
    EffectsWindowPage.h
    -------------------
    The shared Effects window (63C-6 placeholder for 63C-8/63C-11): one page
    template bound to any of the processor's FX chains (input global, output
    global, later per-voice). Six slot rows with a type selector and Amount
    knob, attached straight to the chain's APVTS parameters — functional
    stand-in until the pull-out wheel UI (63C-11) replaces the rows.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectChain.h"
#include "PluginProcessor.h"
#include "SolPage.h"

class EffectsWindowPage final : public SolPage
{
public:
    EffectsWindowPage (juce::AudioProcessorValueTreeState& apvts,
                       int chainIndex,
                       const juce::String& titleText,
                       PageStack& stackToUse)
        : SolPage (stackToUse, titleText)
    {
        for (int slot = 0; slot < VocalFx::EffectChain::kNumSlots; ++slot)
        {
            auto& row = rows[(size_t) slot];

            row.label.setText ("Slot " + juce::String (slot + 1), juce::dontSendNotification);
            row.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            row.label.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
            addAndMakeVisible (row.label);

            for (int t = 0; t < (int) VocalFx::EffectType::NumTypes; ++t)
                row.typeBox.addItem (VocalFx::effectTypeName ((VocalFx::EffectType) t), t + 1);
            addAndMakeVisible (row.typeBox);
            row.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, PitchCorrectorAudioProcessor::fxTypeParamId (chainIndex, slot), row.typeBox);

            row.amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            row.amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (row.amount);
            row.amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, PitchCorrectorAudioProcessor::fxAmountParamId (chainIndex, slot), row.amount);
        }
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        const int rowH = juce::jmax (34, area.getHeight() / VocalFx::EffectChain::kNumSlots);

        for (auto& row : rows)
        {
            auto r = area.removeFromTop (rowH).reduced (0, 4);
            row.label  .setBounds (r.removeFromLeft (64));
            row.amount .setBounds (r.removeFromRight (juce::jmin (r.getHeight() + 20, 60)));
            r.removeFromRight (8);
            row.typeBox.setBounds (r.withSizeKeepingCentre (juce::jmin (220, r.getWidth()), 26));
        }
    }

    struct SlotRow
    {
        juce::Label    label;
        juce::ComboBox typeBox;
        juce::Slider   amount;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   amountAtt;
    };

    std::array<SlotRow, (size_t) VocalFx::EffectChain::kNumSlots> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsWindowPage)
};
