/*
    EffectsWindowPage.h
    -------------------
    The shared Effects window (63C-11): a WheelComponent bound to one of the
    processor's FX chains (input global, output global, later per-voice). The
    palette in the wheel's hub lists every available effect type — read
    dynamically from the chain's APVTS choice parameter, so new effects
    (63C-8) appear automatically. Pulling an effect onto the rim writes the
    slot's type parameter; clicking a placed effect drills into its detail
    page. The 25-slot parameter model (63C-17) is the backing store; the
    wheel is the view, so host automation and DAW-restored state show up too
    (repaint timer). The rim scrolls through the slots that don't fit on the
    semicircle.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectChain.h"
#include "EffectDetailPage.h"
#include "PluginProcessor.h"
#include "SolPage.h"
#include "WheelComponent.h"

class EffectsWindowPage final : public SolPage,
                                private juce::Timer
{
public:
    EffectsWindowPage (juce::AudioProcessorValueTreeState& apvtsIn,
                       int chainIndexIn,
                       const juce::String& titleText,
                       PageStack& stackToUse)
        : SolPage (stackToUse, titleText),
          apvts (apvtsIn),
          chainIndex (chainIndexIn),
          detailPage (apvtsIn, stackToUse)
    {
        wheel.setNumSlots (VocalFx::EffectChain::kNumSlots);
        wheel.emptyTypeId     = (int) VocalFx::EffectType::Empty;
        wheel.allowDuplicates = true;
        wheel.itemsDraggable  = true;

        // Palette from the slot-1 choice parameter (skip index 0 = Empty) so
        // future effect types appear without touching this page.
        std::vector<WheelComponent::Item> items;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (
                PitchCorrectorAudioProcessor::fxTypeParamId (chainIndex, 0))))
        {
            for (int i = 1; i < choice->choices.size(); ++i)
                items.push_back ({ i, choice->choices[i] });
        }
        wheel.setPalette (std::move (items));

        wheel.getSlotType   = [this] (int slot)         { return typeIndex (slot); };
        wheel.setSlotType   = [this] (int slot, int t)  { setTypeIndex (slot, t); };
        wheel.onSlotClicked = [this] (int slot)         { openDetail (slot); };
        wheel.onBackClicked = [this]                    { stack.pop(); };   // 63C-17: back on the wheel
        addAndMakeVisible (wheel);

        startTimerHz (15); // reflect host automation / preset changes
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        wheel.setBounds (area);
    }

    void timerCallback() override { wheel.repaint(); }

    int typeIndex (int slot) const
    {
        if (auto* v = apvts.getRawParameterValue (
                PitchCorrectorAudioProcessor::fxTypeParamId (chainIndex, slot)))
            return (int) std::lround (v->load());
        return (int) VocalFx::EffectType::Empty;
    }

    void setTypeIndex (int slot, int type)
    {
        if (auto* p = apvts.getParameter (
                PitchCorrectorAudioProcessor::fxTypeParamId (chainIndex, slot)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) type));
            p->endChangeGesture();
        }
    }

    void openDetail (int slot)
    {
        const int type = typeIndex (slot);
        if (type == (int) VocalFx::EffectType::Empty)
            return;

        detailPage.rebind (PitchCorrectorAudioProcessor::fxAmountParamId (chainIndex, slot),
                           VocalFx::effectTypeName ((VocalFx::EffectType) type),
                           slot);
        stack.push (detailPage);
    }

    juce::AudioProcessorValueTreeState& apvts;
    const int chainIndex;

    WheelComponent   wheel;
    EffectDetailPage detailPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsWindowPage)
};
