/*
    VocalEffectBase.h
    -----------------
    The vocabulary of the effect chain: what effects EXIST (EffectType) and what
    one IS (VocalEffect).

    Split out of EffectChain.h in 63C-8 so the two sides can reference each
    other without a cycle: the control tables (EffectParams.h) and the concrete
    effects (SpaceDustEffects.h) need this base, the factory needs the concrete
    effects, and EffectChain needs the factory. Include order is base -> tables
    -> effects + factory -> chain.
*/

#pragma once

#include <JuceHeader.h>
#include "VocalFx.h"

#include <cmath>
#include <memory>

namespace VocalFx
{
    /** Every effect the chain can host. Order is the APVTS choice-parameter
        order — append new types at the end, never reorder, or saved sessions
        remap to the wrong sound.

        Saturate is Sol's own. Everything from Reverb down is ported from Space
        Dust (Synth VST V2) — its Effects tab (Reverb, Delay, Grain Delay,
        Phaser, Flanger, Trance Gate) then its Saturation Color tab (Bit Crush,
        Soft Clip, Compress, Lo-Fi, Final EQ), in Space Dust's own chain order,
        so dropping them into a chain top-to-bottom reproduces that signal path. */
    enum class EffectType : int
    {
        Empty = 0,
        Saturate,
        Reverb,
        Delay,
        GrainDelay,
        Phaser,
        Flanger,
        TranceGate,
        BitCrush,
        SoftClip,
        Compress,
        Lofi,
        FinalEQ,
        NumTypes
    };

    /** Rim label for a type. Kept short — these are drawn on the wheel. */
    inline const char* effectTypeName (EffectType t) noexcept
    {
        switch (t)
        {
            case EffectType::Saturate:   return "Saturate";
            case EffectType::Reverb:     return "Reverb";
            case EffectType::Delay:      return "Delay";
            case EffectType::GrainDelay: return "Grain Delay";
            case EffectType::Phaser:     return "Phaser";
            case EffectType::Flanger:    return "Flanger";
            case EffectType::TranceGate: return "Trance Gate";
            case EffectType::BitCrush:   return "Bit Crush";
            case EffectType::SoftClip:   return "Soft Clip";
            case EffectType::Compress:   return "Compress";
            case EffectType::Lofi:       return "Lo-Fi";
            // "Parametric EQ", not "Final EQ" (Giuseppe, 2026-08-23). It was
            // Space Dust's name for it, where it really is FINAL — the fixed
            // last stage of that synth's chain. Here it is one of 25 ordered
            // slots and can sit anywhere, so "Final" described a position it
            // no longer has. The enum stays FinalEQ: the value is what saved
            // sessions store, and the DSP it maps to is unchanged.
            case EffectType::FinalEQ:    return "Parametric EQ";
            case EffectType::Empty:
            case EffectType::NumTypes:   break;
        }
        return "Empty";
    }

    //==============================================================================
    /** Base interface for one effect in a chain slot.

        Controls arrive through applyParams() as a block-rate snapshot of that
        effect type's APVTS values, in the order EffectParams.h declares them —
        the effect never touches the APVTS itself, so nothing on the audio thread
        ever looks up a parameter by name. */
    class VocalEffect
    {
    public:
        virtual ~VocalEffect() = default;

        virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
        virtual void reset() noexcept = 0;

        /** Process in place. Only called while the slot is audible. */
        virtual void process (juce::AudioBuffer<float>& buffer) noexcept = 0;

        /** This effect's control values for the coming block, indexed by its
            own `...Idx` enum in EffectParams.h. Never null in practice; the
            implementations still guard. Audio thread, before process(). */
        virtual void applyParams (const float* values) noexcept = 0;

        /** Set by the chain once per block, before process(), for the effects
            that follow host tempo (Trance Gate, Delay). Null is normal — a host
            need not supply a playhead, and every effect must still sound. */
        virtual void setPlayHead (juce::AudioPlayHead*) noexcept {}

        /** One number this effect wants its panel to be able to SHOW, 0..1, or
            a negative value for "nothing to show" (the default).

            For the Phaser it is where the sweep currently is, so the panel can
            draw the notches moving. It has to come from the DSP rather than
            from a copy of the LFO in the UI: a second oscillator started at a
            different moment would look convincing and be describing a sweep
            that is not the one you are hearing.

            Read on the audio thread only, in EffectChain::process, which
            publishes it to an atomic the way it does the meters. */
        virtual float displayValue() const noexcept { return -1.0f; }

        /** Set by the factory; lets the audio thread identify a handed-off
            instance without any further synchronisation. */
        EffectType typeTag { EffectType::Empty };
    };
} // namespace VocalFx
