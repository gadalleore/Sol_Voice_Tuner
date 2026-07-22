/*
    EffectChain.h
    -------------
    Modular vocal-effect chain (63C-7) — the chain primitive of the v3 audio
    graph (63C-15), instantiated three times by the processor: input global,
    lead voice, and output global.

      * VocalEffect  — base interface: prepare / process / reset plus a single
        smoothed "Amount" macro per effect (one obvious sound, one knob).
      * SunSaturator — proof-of-architecture warm tanh saturator.
      * EffectChain  — 6 ordered slots, each empty or holding one effect.
        Signal flows slot 0 -> 5. Swaps/reorders are click-free: a slot fades
        its wet path to silence, exchanges the effect, then fades back in.

    Real-time contract: allocation- and lock-free after prepare(). The UI
    talks through APVTS parameters which the processor reads once per block
    and forwards here on the audio thread.
*/

#pragma once

#include <JuceHeader.h>
#include "VocalFx.h"

#include <array>
#include <cmath>

namespace VocalFx
{
    /** Every effect the chain can host. Order is the APVTS choice-parameter
        order — append new types at the end, never reorder, or saved sessions
        remap to the wrong sound. */
    enum class EffectType : int
    {
        Empty = 0,
        Saturate,
        NumTypes
    };

    inline const char* effectTypeName (EffectType t) noexcept
    {
        switch (t)
        {
            case EffectType::Saturate: return "Saturate";
            case EffectType::Empty:
            case EffectType::NumTypes: break;
        }
        return "Empty";
    }

    //==============================================================================
    /** Base interface for one wheel effect. Exactly one macro: Amount in [0, 1],
        smoothed internally so scroll/automation never zippers. */
    class VocalEffect
    {
    public:
        virtual ~VocalEffect() = default;

        virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
        virtual void reset() noexcept = 0;

        /** Process in place. Only called while the slot is audible. */
        virtual void process (juce::AudioBuffer<float>& buffer) noexcept = 0;

        void setAmount (float a) noexcept          { amountRamp.setTarget (juce::jlimit (0.0f, 1.0f, a)); }
        void setAmountImmediate (float a) noexcept { amountRamp.reset (juce::jlimit (0.0f, 1.0f, a)); }

    protected:
        GainRamp amountRamp;
    };

    //==============================================================================
    /** Warm tanh saturator — the end-to-end proof effect for the chain.
        Amount sweeps drive 1x -> 10x with small-signal makeup so it grows
        harmonically dense, not just loud. */
    class SunSaturator : public VocalEffect
    {
    public:
        void prepare (double sampleRate, int, int) override
        {
            amountRamp.prepare (sampleRate, 20.0f);
            reset();
        }

        void reset() noexcept override { amountRamp.reset (amountRamp.target); }

        void process (juce::AudioBuffer<float>& buffer) noexcept override
        {
            const int N   = buffer.getNumSamples();
            const int chs = buffer.getNumChannels();

            for (int i = 0; i < N; ++i)
            {
                const float a      = amountRamp.next();
                const float drive  = 1.0f + a * 9.0f;
                const float makeup = 1.0f / std::pow (drive, 0.75f);

                for (int c = 0; c < chs; ++c)
                {
                    float* x = buffer.getWritePointer (c);
                    x[i] = std::tanh (x[i] * drive) * makeup;
                }
            }
        }
    };

    //==============================================================================
    /** Six ordered slots of VocalEffects, processed in slot order after pitch
        correction. All slot changes go through a per-slot wet-gain fade
        (~8 ms tau): fade out -> exchange effect -> fade in, so swapping and
        reordering while audio runs never clicks. */
    class EffectChain
    {
    public:
        static constexpr int kNumSlots = 6;

        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            scratch.setSize (juce::jmax (numChannels, 1), juce::jmax (maxBlockSize, 1), false, true, true);

            for (auto& s : slots)
            {
                s.saturator.prepare (sampleRate, maxBlockSize, numChannels);
                s.wet.prepare (sampleRate, 8.0f);
                s.wet.reset (0.0f);
                s.active  = EffectType::Empty;
                s.desired = EffectType::Empty;
                s.amount  = 1.0f;
            }
        }

        void reset() noexcept
        {
            for (auto& s : slots)
            {
                s.saturator.reset();
                s.wet.reset (s.active == EffectType::Empty ? 0.0f : 1.0f);
            }
        }

        /** Audio thread, before process(): what the slot should hold. */
        void setSlotEffect (int slot, EffectType t) noexcept
        {
            if (juce::isPositiveAndBelow (slot, kNumSlots))
                slots[(size_t) slot].desired = clampType (t);
        }

        /** Audio thread, before process(): the slot's Amount macro. */
        void setSlotAmount (int slot, float amount) noexcept
        {
            if (juce::isPositiveAndBelow (slot, kNumSlots))
                slots[(size_t) slot].amount = juce::jlimit (0.0f, 1.0f, amount);
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const int N = buffer.getNumSamples();
            if (N <= 0) return;

            // Defensive resize if the host exceeds the prepared block size
            // (same pattern as the processor's scratch vectors).
            if (scratch.getNumSamples() < N || scratch.getNumChannels() < buffer.getNumChannels())
                scratch.setSize (juce::jmax (scratch.getNumChannels(), buffer.getNumChannels()),
                                 juce::jmax (scratch.getNumSamples(), N), false, true, true);

            const int chs = juce::jmin (buffer.getNumChannels(), scratch.getNumChannels());
            if (chs <= 0) return;

            for (auto& s : slots)
            {
                // --- Swap state machine (evaluated at block boundaries) ---
                if (s.desired != s.active)
                {
                    s.wet.setTarget (0.0f);
                    if (s.wet.current < kSilentWet)
                    {
                        s.active = s.desired;
                        if (auto* fx = s.effectFor (s.active))
                        {
                            fx->reset();
                            fx->setAmountImmediate (s.amount);
                            s.wet.setTarget (1.0f);
                        }
                    }
                }

                auto* fx = s.effectFor (s.active);
                if (fx == nullptr)
                    continue;                       // Empty slot: bit-exact passthrough

                fx->setAmount (s.amount);

                // Fully bypassed and staying bypassed — skip the work.
                if (s.wet.current < kSilentWet && s.wet.target < kSilentWet)
                    continue;

                // Dry/wet crossfade: effect renders into scratch, output blends.
                for (int c = 0; c < chs; ++c)
                    scratch.copyFrom (c, 0, buffer, c, 0, N);

                juce::AudioBuffer<float> wetView (scratch.getArrayOfWritePointers(), chs, 0, N);
                fx->process (wetView);

                for (int i = 0; i < N; ++i)
                {
                    const float w = s.wet.next();
                    for (int c = 0; c < chs; ++c)
                    {
                        float* out = buffer.getWritePointer (c);
                        out[i] += w * (scratch.getReadPointer (c)[i] - out[i]);
                    }
                }
            }
        }

        static EffectType clampType (EffectType t) noexcept
        {
            const int v = juce::jlimit (0, (int) EffectType::NumTypes - 1, (int) t);
            return (EffectType) v;
        }

    private:
        static constexpr float kSilentWet = 1.0e-3f;

        struct Slot
        {
            // One instance of every effect type lives here pre-prepared, so
            // exchanging effects is a pointer choice — no allocation, ever.
            SunSaturator saturator;

            VocalEffect* effectFor (EffectType t) noexcept
            {
                switch (t)
                {
                    case EffectType::Saturate: return &saturator;
                    case EffectType::Empty:
                    case EffectType::NumTypes: break;
                }
                return nullptr;
            }

            EffectType active  { EffectType::Empty };
            EffectType desired { EffectType::Empty };
            float      amount  { 1.0f };
            GainRamp   wet;
        };

        std::array<Slot, kNumSlots> slots;
        juce::AudioBuffer<float>    scratch;
    };
} // namespace VocalFx
