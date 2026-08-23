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
#include "SolPanel.h"
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
        // One of each per chain: an effect owns ONE set of controls here, the
        // way it does in Space Dust, so a second copy in the same chain would
        // silently share the first one's knobs. See EffectParams.h.
        // Duplicates ARE allowed (Giuseppe, 2026-08-23) — the same effect can
        // sit at two points in the chain.
        //
        // They share one control set, because a chain owns one panel per effect
        // (EffectParams.h): per-slot sets would multiply ~110 controls by 25
        // slots and hand the host a parameter list nobody could use. That is
        // why this was forbidden — the sharing was SILENT, and a second Reverb
        // that mysteriously moved with the first reads as a bug.
        //
        // So it is no longer silent: a doubled effect is marked on the rim and
        // names its twin on the detail page. Two instances of one reverb, early
        // and late in the chain, is a real thing to want; two instances with
        // secretly-linked knobs is not.
        wheel.allowDuplicates = true;
        wheel.itemsDraggable  = true;

        // Bigger than the default: these are the things you actually aim at,
        // and at 13pt on a rim that now carries a panelled band behind them
        // they read as captions rather than as the chain itself.
        wheel.setItemFontHeight (18.0f);
        wheel.setPillSize (150.0f, 34.0f);

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
        addAndMakeVisible (wheel);

        startTimerHz (15); // reflect host automation / preset changes
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        // Clear of the plate's right-hand column, like every other page.
        area = area.withTrimmedRight (kRightColumn);

        wheelPlate = area.expanded (kPlateBleed, 2);
        wheel.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        SolPage::paint (g);

        // One plate under the whole chain. The wheel is a single object — the
        // palette and the rim are two halves of one gesture — so splitting it
        // across several plates would imply a division that is not there.
        if (! wheelPlate.isEmpty())
            SolPanel::draw (g, wheelPlate.toFloat());
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

        const auto fxType = (VocalFx::EffectType) type;

        // Name the twin, if there is one. The controls below are shared with
        // it, and the user has to be told that on the page where they are
        // about to turn them.
        juce::StringArray twins;

        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
            if (s != slot && typeIndex (s) == type)
                twins.add (juce::String (s + 1));

        detailPage.setLinkedSlots (twins);

        detailPage.rebind (PitchCorrectorAudioProcessor::fxAmountParamId (chainIndex, slot),
                           VocalFx::effectTypeName (fxType),
                           fxType,
                           chainIndex,
                           slot,
                           [this] (VocalFx::EffectType t, const char* paramId)
                           {
                               return PitchCorrectorAudioProcessor::fxEffectParamId (chainIndex, t, paramId);
                           });
        stack.push (detailPage);
    }

    juce::AudioProcessorValueTreeState& apvts;
    const int chainIndex;

    static constexpr int kRightColumn = 110;
    static constexpr int kPlateBleed  = 6;

    juce::Rectangle<int> wheelPlate;

    WheelComponent   wheel;
    EffectDetailPage detailPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsWindowPage)
};
