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
#include "SignalMeter.h"
#include "SolPage.h"
#include "SolPanel.h"
#include "WheelComponent.h"

class EffectsWindowPage final : public SolPage,
                                public  SizedPage,
                                private juce::Timer
{
public:
    /** Wired by the editor so a change of selection can re-fit the window. */
    std::function<void()> onSizeWanted;

    EffectsWindowPage (juce::AudioProcessorValueTreeState& apvtsIn,
                       VocalFx::EffectChain& chainIn,
                       int chainIndexIn,
                       const juce::String& titleText,
                       PageStack& stackToUse)
        : SolPage (stackToUse, titleText),
          apvts (apvtsIn),
          chain (chainIn),
          chainIndex (chainIndexIn),
          detailPage (apvtsIn)
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
        // The ring keeps one size whatever the window does (Giuseppe,
        // 2026-08-23) — see WheelComponent::setFixedRadius.
        wheel.setFixedRadius (kRingRadius);

        addAndMakeVisible (wheel);
        addChildComponent (detailPage);   // hidden until something is selected

        // Metering either side of the selected effect: what is arriving, and
        // what is leaving. Hidden with the inspector, because with nothing
        // selected there is no "either side" to be on.
        addChildComponent (inMeter);
        addChildComponent (outMeter);

        // The inspector changing shape changes the window's shape.
        detailPage.onSizeChanged = [this]
        {
            resized();
            if (onSizeWanted != nullptr) onSizeWanted();
        };

        startTimerHz (15); // reflect host automation / preset changes
    }

private:
    void layoutContent (juce::Rectangle<int> area) override
    {
        // Clear of the plate's right-hand column, like every other page.
        area = area.withTrimmedRight (kRightColumn);

        // The inspector takes the right of the page when something is
        // selected, and the ring keeps the rest. With nothing selected the
        // ring has the lot and the window shrinks to suit.
        const bool showing = detailPage.hasEffect();

        inMeter .setVisible (showing);
        outMeter.setVisible (showing);

        if (showing)
        {
            auto block = area.removeFromRight (inspectorBlockWidth (area));
            area.removeFromRight (kGap);

            // Input on the LEFT of the effect, output on the RIGHT — the two
            // meters bracket the module the way the signal does.
            inMeter .setBounds (block.removeFromLeft  (SignalMeter::kWidth));
            outMeter.setBounds (block.removeFromRight (SignalMeter::kWidth));

            detailPage.setBounds (block.reduced (kMeterGap, 0));
        }

        wheelPlate = area.expanded (kPlateBleed, 2);
        wheel.setBounds (area);
    }

    /** INPUT / OUTPUT belong over the RING — that is the thing the signal runs
        through. Centred on the page they straddled the inspector too, which
        implied the selected effect was somehow the whole chain. */
    juce::Range<int> edgeCaptionSpan (juce::Rectangle<int> content) const override
    {
        auto area = content.withTrimmedRight (kRightColumn);

        if (detailPage.hasEffect())
            area = area.withTrimmedRight (inspectorBlockWidth (area) + kGap);

        return { area.getX(), area.getRight() };
    }

    /** Inspector plus the two meters flanking it. */
    int inspectorBlockWidth (juce::Rectangle<int> area) const
    {
        const int flank = (SignalMeter::kWidth + kMeterGap) * 2;

        return juce::jlimit (kInspectorMinW + flank,
                             juce::jmax (kInspectorMinW + flank, area.getWidth() - kRingMinW),
                             detailPage.preferredLogicalSize().x + flank);
    }

    /** Ring plus whatever the selected effect needs beside it. */
    juce::Point<int> preferredLogicalSize() const override
    {
        if (! detailPage.hasEffect())
            return { kBareW, kBareH };

        const auto want = detailPage.preferredLogicalSize();
        const int  flank = (SignalMeter::kWidth + kMeterGap) * 2;

        return { juce::jlimit (kBareW, kMaxW, kBareW + want.x + flank + kGap),
                 juce::jlimit (kBareH, kMaxH, juce::jmax (kBareH, want.y)) };
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

    void timerCallback() override
    {
        wheel.repaint();

        if (detailPage.hasEffect())
        {
            inMeter .setLevel (chain.getMeteredInLevel());
            outMeter.setLevel (chain.getMeteredOutLevel());
        }

        // Backstop for leg two. The shell reports a settle for every animated
        // resize, but it does NOT animate when the size it is asked for is the
        // one it already has, or when the editor is off the desktop — and a
        // selection stuck half-made, with the panel retracted and nothing
        // deployed, is the worst outcome available here. If the settle has not
        // arrived by now, take the second leg anyway.
        if (pendingSlot >= 0 && ++pendingTicks > kPendingTimeoutTicks)
            windowSettled();
    }

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

    /** Selecting an effect happens in TWO legs: the panel RETRACTS to the bare
        ring, and only then opens out to what the new effect needs (Giuseppe,
        2026-08-23).

        Morphing straight from one effect's panel to the next was a single
        ambiguous slide — the window got wider or narrower for no reason you
        could see, and the controls under it changed at some unrelated moment.
        Closing first makes the two facts separate and legible: THAT one is put
        away, THIS one is brought out. It is also the honest mechanical
        reading, which is the whole language of this UI — a panel does not
        become a different panel, it stows and another deploys.

        Leg two is fired by the shell's onResizeSettled, through
        windowSettled() below. */
    void openDetail (int slot)
    {
        const int type = typeIndex (slot);
        if (type == (int) VocalFx::EffectType::Empty)
            return;

        // Already showing this one: nothing to stage. Retracting and
        // redeploying the same panel is a flash with no information in it.
        if (slot == shownSlot && detailPage.hasEffect())
            return;

        if (detailPage.hasEffect())
        {
            pendingSlot  = slot;
            pendingTicks = 0;
            shownSlot    = -1;

            detailPage.clearEffect();   // -> onSizeChanged -> the window retracts
            return;
        }

        showSlot (slot);
    }

public:
    /** Leg two: the retraction has finished, so deploy what was asked for.

        Wired to FloatingShell::onResizeSettled by the editor. A no-op when
        nothing is pending, so the editor can call it on every effects page
        without knowing which one is on screen. */
    void windowSettled()
    {
        if (pendingSlot < 0)
            return;

        const int slot = pendingSlot;
        pendingSlot = -1;

        showSlot (slot);
    }

private:
    void showSlot (int slot)
    {
        const int type = typeIndex (slot);
        if (type == (int) VocalFx::EffectType::Empty)
            return;

        shownSlot = slot;

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

        // Point the audio thread's meters at this slot.
        chain.setMeteredSlot (slot);

        // No push: the inspector is already on this page, beside the ring.
        resized();
    }

    juce::AudioProcessorValueTreeState& apvts;
    VocalFx::EffectChain& chain;
    const int chainIndex;

    static constexpr int kRightColumn   = 110;
    static constexpr int kGap           = 10;
    static constexpr int kRingMinW      = 300;
    static constexpr int kInspectorMinW = 240;
    static constexpr int kMeterGap      = 6;

    /** The ring's one size, in logical pixels — see setFixedRadius. Sized to
        the BARE page, so it is the same ring whether or not an inspector is
        open beside it. */
    static constexpr float kRingRadius  = 178.0f;

    /** The page with nothing selected, and the ceiling once something is. */
    static constexpr int kBareW = 620, kBareH = 450;
    static constexpr int kMaxW  = 1180, kMaxH = 660;
    static constexpr int kPlateBleed  = 6;

    /** At the page's 15 Hz tick: ~0.6 s, comfortably longer than a retraction
        (the shell eases 28% a frame at 60 fps, so it arrives in about a third
        of a second) and short enough not to read as a hang if it is needed. */
    static constexpr int kPendingTimeoutTicks = 9;

    juce::Rectangle<int> wheelPlate;

    /** Which slot the inspector is showing, and which one it is retracting in
        order to show. -1 for neither. */
    int shownSlot   = -1;
    int pendingSlot = -1;
    int pendingTicks = 0;

    WheelComponent   wheel;
    EffectDetailPage detailPage;
    SignalMeter      inMeter  { "IN"  };
    SignalMeter      outMeter { "OUT" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsWindowPage)
};
