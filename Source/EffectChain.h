/*
    EffectChain.h
    -------------
    Modular vocal-effect chain (63C-7, capacity + allocation reworked in
    63C-17) — the chain primitive of the v3 audio graph (63C-15),
    instantiated three times by the processor: input global, lead voice,
    and output global.

      * VocalEffect  — base interface: prepare / process / reset plus a single
        smoothed "Amount" macro per effect (one obvious sound, one knob).
      * SunSaturator — proof-of-architecture warm tanh saturator.
      * EffectChain  — 25 ordered slots, each empty or holding one effect.
        Signal flows slot 0 -> 24. Swaps/reorders are click-free: a slot fades
        its wet path to silence, exchanges the effect, then fades back in.

    Allocation model (63C-17): slots no longer pre-own every effect type
    (25 slots x 3 chains made that unscalable). Instances are constructed
    lazily on the MESSAGE thread (serviceSlot(), driven by a processor
    timer) and handed to the audio thread through a per-slot single-slot
    lock-free mailbox (`staged`); the audio thread retires the outgoing
    instance through a second mailbox (`retired`) which the message thread
    deletes. The audio thread stays allocation- and lock-free; a slot whose
    instance hasn't arrived yet simply stays faded out until it does.

    The UI talks through APVTS parameters which the processor reads once per
    block and forwards here on the audio thread.
*/

#pragma once

#include <JuceHeader.h>
#include "VocalFx.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

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

        /** Set by the factory; lets the audio thread identify a handed-off
            instance without any further synchronisation. */
        EffectType typeTag { EffectType::Empty };

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
    /** Constructs a fresh, unprepared instance of the given type.
        Message thread (or prepare-time) only — never the audio thread. */
    inline std::unique_ptr<VocalEffect> createEffect (EffectType t)
    {
        std::unique_ptr<VocalEffect> fx;

        switch (t)
        {
            case EffectType::Saturate:  fx = std::make_unique<SunSaturator>(); break;
            case EffectType::Empty:
            case EffectType::NumTypes:  break;
        }

        if (fx != nullptr)
            fx->typeTag = t;
        return fx;
    }

    //==============================================================================
    /** 25 ordered slots of VocalEffects, processed in slot order. All slot
        changes go through a per-slot wet-gain fade (~8 ms tau): fade out ->
        exchange effect -> fade in, so swapping and reordering while audio
        runs never clicks.

        Threading (63C-17 lazy allocation):
          * audio thread: setSlotEffect / setSlotAmount / process / reset.
          * message thread: serviceSlot() periodically (constructs + stages
            instances, deletes retired ones).
          * prepare-time (audio not running): prepare / installImmediate. */
    class EffectChain
    {
    public:
        static constexpr int kNumSlots = 25;

        ~EffectChain()
        {
            for (auto& s : slots)
                s.freeAllInstances();
        }

        /** Audio must not be running. Drops every instance (params re-install
            via installImmediate + serviceSlot afterwards). */
        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            spec = { sampleRate, juce::jmax (maxBlockSize, 1), juce::jmax (numChannels, 1) };

            scratch.setSize (spec.channels, spec.blockSize, false, true, true);

            for (auto& s : slots)
            {
                s.freeAllInstances();
                s.wet.prepare (sampleRate, 8.0f);
                s.wet.reset (0.0f);
                s.active  = EffectType::Empty;
                s.desired = EffectType::Empty;
                s.amount  = 1.0f;
                s.lastProvidedType = EffectType::Empty;
            }
        }

        /** Prepare-time only (audio not running): synchronously give a slot
            its instance so session load / prepareToPlay has no async gap. */
        void installImmediate (int slot, EffectType t)
        {
            if (! juce::isPositiveAndBelow (slot, kNumSlots))
                return;

            auto& s = slots[(size_t) slot];
            t = clampType (t);

            s.freeAllInstances();
            s.lastProvidedType = t;
            s.active  = t;
            s.desired = t;

            if (auto fx = createEffect (t))
            {
                fx->prepare (spec.sampleRate, spec.blockSize, spec.channels);
                fx->setAmountImmediate (s.amount);
                s.current = fx.release();
                s.wet.reset (1.0f);
            }
            else
            {
                s.wet.reset (0.0f);
            }
        }

        /** MESSAGE THREAD, periodic: give the slot what it needs to reach
            `desiredType`, and free anything the audio thread retired. */
        void serviceSlot (int slot, EffectType desiredType)
        {
            if (! juce::isPositiveAndBelow (slot, kNumSlots))
                return;

            auto& s = slots[(size_t) slot];
            desiredType = clampType (desiredType);

            // Free whatever the audio thread has retired.
            if (auto* dead = s.retired.exchange (nullptr))
                delete dead;

            if (desiredType == s.lastProvidedType)
                return;

            // Drop any staged instance that was never consumed (stale wish).
            if (auto* stale = s.staged.exchange (nullptr))
                delete stale;

            s.lastProvidedType = desiredType;

            if (desiredType == EffectType::Empty)
                return;                     // audio side just retires its current

            auto fx = createEffect (desiredType);
            if (fx == nullptr)
                return;

            fx->prepare (spec.sampleRate, spec.blockSize, spec.channels);

            // Publish: typeTag was written before this release-store, so the
            // audio thread reading the pointer (acquire) sees a complete object.
            s.staged.store (fx.release(), std::memory_order_release);
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

        void reset() noexcept
        {
            for (auto& s : slots)
            {
                if (s.current != nullptr)
                    s.current->reset();
                s.wet.reset (s.active == EffectType::Empty || s.current == nullptr ? 0.0f : 1.0f);
            }
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
                updateSlotInstance (s);

                auto* fx = s.current;
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
            // Audio-thread-owned instance (deleted only via the retire mailbox
            // or at prepare/destruction time when audio is not running).
            VocalEffect* current = nullptr;

            // Single-slot mailboxes: message thread -> audio thread (staged)
            // and audio thread -> message thread (retired).
            std::atomic<VocalEffect*> staged  { nullptr };
            std::atomic<VocalEffect*> retired { nullptr };

            // Message-thread bookkeeping: the type we last provided for.
            EffectType lastProvidedType { EffectType::Empty };

            EffectType active  { EffectType::Empty };
            EffectType desired { EffectType::Empty };
            float      amount  { 1.0f };
            GainRamp   wet;

            /** Only when audio is guaranteed not running. */
            void freeAllInstances()
            {
                delete current;                       current = nullptr;
                delete staged .exchange (nullptr);
                delete retired.exchange (nullptr);
            }
        };

        /** Audio thread: per-block swap state machine. Fades out on a type
            change, then completes the exchange once (a) the slot is silent,
            (b) the staged instance has arrived (for non-Empty targets) and
            (c) the retire mailbox has room for the outgoing instance. Until
            then the slot just stays faded out — never blocks, never allocates. */
        void updateSlotInstance (Slot& s) noexcept
        {
            const bool typeMatches = s.active == s.desired
                                  && (s.desired == EffectType::Empty) == (s.current == nullptr);
            if (typeMatches)
                return;

            s.wet.setTarget (0.0f);
            if (s.wet.current >= kSilentWet)
                return;                              // still fading out

            if (s.desired == EffectType::Empty)
            {
                if (s.current != nullptr)
                {
                    if (s.retired.load (std::memory_order_relaxed) != nullptr)
                        return;                      // mailbox full — retry next block
                    s.retired.store (s.current, std::memory_order_release);
                    s.current = nullptr;
                }
                s.active = EffectType::Empty;
                return;
            }

            auto* incoming = s.staged.load (std::memory_order_acquire);
            if (incoming == nullptr || incoming->typeTag != s.desired)
                return;                              // instance not here yet

            if (s.current != nullptr && s.retired.load (std::memory_order_relaxed) != nullptr)
                return;                              // no room to retire — retry

            incoming = s.staged.exchange (nullptr, std::memory_order_acq_rel);
            if (incoming == nullptr)
                return;                              // raced with a re-stage; retry

            if (s.current != nullptr)
                s.retired.store (s.current, std::memory_order_release);

            s.current = incoming;
            s.active  = incoming->typeTag;
            s.current->reset();
            s.current->setAmountImmediate (s.amount);
            s.wet.setTarget (1.0f);
        }

        struct PrepareSpec
        {
            double sampleRate = 44100.0;
            int    blockSize  = 512;
            int    channels   = 2;
        };

        PrepareSpec spec;
        std::array<Slot, kNumSlots> slots;
        juce::AudioBuffer<float>    scratch;
    };
} // namespace VocalFx
