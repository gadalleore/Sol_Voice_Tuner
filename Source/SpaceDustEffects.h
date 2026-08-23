/*
    SpaceDustEffects.h
    ------------------
    Space Dust's effects, made playable on Sol's wheel (63C-8).

    The DSP itself is ported unchanged in Source/fx/ — Space Dust's Effects tab
    (Reverb, Delay, Grain Delay, Phaser, Flanger, Trance Gate) and its Saturation
    Color tab (Bit Crush, Soft Clip, Compress, Lo-Fi, Final EQ). What lives HERE
    is the wiring: each class below reads its effect's controls out of the
    per-block value snapshot and hands them to the ported processor. Every knob,
    toggle and dropdown that effect has in Space Dust is connected, with Space
    Dust's own ranges and behaviour; EffectParams.h is where those controls are
    declared and is the only place that describes them.

    An adapter's job is a straight translation, nothing more. Where a value has
    to change units to reach the DSP (a percentage to a 0-1 mix, a dropdown index
    to an oversampling factor) the conversion is the same one Space Dust's
    processor does, and it is noted.

    Two things the ported processors assume that the chain does not guarantee,
    both handled once in SpaceDustEffect below:

      * a stereo buffer — Grain Delay and Reverb are hard-wired to two channels,
        and Sol supports a mono layout, so a mono block is widened and folded;
      * a host playhead — Trance Gate and Delay lock to tempo; the chain hands
        one down per block, and both fall back to a sane free rate without it.

    NOT ported: Space Dust's Transient. It is a TR-808/909 drum-hit synthesiser
    triggered by note-on, not a processor — it makes a kick, not a treatment of
    the voice — so there is nothing for it to do in a vocal chain.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectParams.h"
#include "VocalEffectBase.h"

#include "fx/SolPingPongDelay.h"
#include "fx/SpaceDustBitCrusher.h"
#include "fx/SpaceDustCompressor.h"
#include "fx/SpaceDustFinalEQ.h"
#include "fx/SpaceDustFlanger.h"
#include "fx/SpaceDustGrainDelay.h"
#include "fx/SpaceDustLofi.h"
#include "fx/SpaceDustPhaser.h"
#include "fx/SpaceDustReverb.h"
#include "fx/SpaceDustTranceGate.h"
#include "fx/SpaceDustSoftClipper.h"

#include <cmath>
#include <memory>

namespace VocalFx
{
    //==============================================================================
    /** Base for every ported Space Dust processor.

        Owns one `Fx`, prepares it from the chain's prepare() arguments, and once
        per block hands this effect's slice of the parameter snapshot to
        applyParams(), which the subclass turns into that effect's own settings.
        The Fx keeps its own per-parameter smoothing, so setting them at block
        rate is enough.

        Everything here is allocation-free once prepared: `wide` and the Fx's own
        buffers are sized in prepare(), and only a host exceeding its declared
        block size can trigger the defensive grow (the same guard EffectChain
        uses). */
    template <typename Fx>
    class SpaceDustEffect : public VocalEffect
    {
    public:
        void setPlayHead (juce::AudioPlayHead* ph) noexcept override { playHead = ph; }

        void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
        {
            const int blockSize = juce::jmax (1, maxBlockSize);

            // Always prepared stereo: the mono case is widened in process(),
            // so the Fx never sees a layout it was not written for.
            spec.sampleRate       = juce::jmax (sampleRate, 1.0);
            spec.maximumBlockSize = (juce::uint32) blockSize;
            spec.numChannels      = 2;

            fx.prepare (spec);
            wide.setSize (2, blockSize, false, true, true);

            reset();
        }

        void reset() noexcept override
        {
            fx.reset();
            wide.clear();
        }

        void applyParams (const float* values) noexcept override
        {
            if (values != nullptr)
                apply (values);
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept override
        {
            const int N   = buffer.getNumSamples();
            const int chs = buffer.getNumChannels();
            if (N <= 0 || chs <= 0)
                return;

            if (chs >= 2)
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, 0, N);
                processFx (view);
                return;
            }

            // Mono chain: widen to two channels, process, fold back.
            if (wide.getNumSamples() < N)
                wide.setSize (2, N, false, true, true);

            const float* src = buffer.getReadPointer (0);
            juce::FloatVectorOperations::copy (wide.getWritePointer (0), src, N);
            juce::FloatVectorOperations::copy (wide.getWritePointer (1), src, N);

            juce::AudioBuffer<float> view (wide.getArrayOfWritePointers(), 2, 0, N);
            processFx (view);

            float*       dst = buffer.getWritePointer (0);
            const float* l   = view.getReadPointer (0);
            const float* r   = view.getReadPointer (1);

            for (int i = 0; i < N; ++i)
                dst[i] = (l[i] + r[i]) * 0.5f;
        }

    protected:
        /** Translate this effect's controls into its own parameter struct.
            Block rate, audio thread. `v` is indexed by the effect's ...Idx enum. */
        virtual void apply (const float* v) = 0;

        /** A Toggle control's value, as the bool the DSP wants. */
        static bool flag (const float* v, int i) noexcept { return v[i] > 0.5f; }

        /** A Choice control's value, as its index. */
        static int choice (const float* v, int i) noexcept { return (int) std::lround (v[i]); }

        /** Run the ported effect over a guaranteed 2-channel buffer. Virtual so
            an effect whose upstream processor is wrapped in extra work in Space
            Dust's processBlock (Reverb's input drive and decay-zero flush) can
            reproduce it. */
        virtual void processFx (juce::AudioBuffer<float>& stereo)
        {
            callProcess (fx, stereo, spec.sampleRate, playHead, 0);
        }

        /** Host tempo, or 120 when the host offers none. */
        double currentBpm() const
        {
            if (playHead != nullptr)
                if (auto pos = playHead->getPosition())
                    if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0)
                        return *bpm;

            return 120.0;
        }

        Fx                     fx;
        juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };

        /** The current block's playhead, or null. Borrowed, never retained. */
        juce::AudioPlayHead* playHead = nullptr;

    private:
        // Trance Gate's process() wants the sample rate and playhead too; every
        // other ported effect takes just the buffer. Overload resolution picks
        // whichever that Fx actually has (int beats long on the tag argument,
        // so the three-argument form wins wherever it compiles).
        template <typename T>
        static auto callProcess (T& f, juce::AudioBuffer<float>& b, double sr,
                                 juce::AudioPlayHead* ph, int)
            -> decltype (f.process (b, sr, ph), void())
        {
            f.process (b, sr, ph);
        }

        template <typename T>
        static void callProcess (T& f, juce::AudioBuffer<float>& b, double,
                                 juce::AudioPlayHead*, long)
        {
            f.process (b);
        }

        juce::AudioBuffer<float> wide;
    };

    //==============================================================================
    /** Sol's own warm tanh saturator — the effect the chain was built and proved
        against, kept alongside the ported ones. Drive sweeps 1x to 10x with
        small-signal makeup, so it grows harmonically dense rather than just loud. */
    class SunSaturator final : public VocalEffect
    {
    public:
        void prepare (double sampleRate, int, int) override
        {
            driveRamp.prepare (sampleRate, 20.0f);
            mixRamp  .prepare (sampleRate, 20.0f);
            reset();
        }

        void reset() noexcept override
        {
            driveRamp.reset (driveRamp.target);
            mixRamp  .reset (mixRamp.target);
        }

        void applyParams (const float* v) noexcept override
        {
            if (v == nullptr)
                return;

            driveRamp.setTarget (juce::jlimit (0.0f, 1.0f, v[SaturateIdx::Drive]));
            mixRamp  .setTarget (juce::jlimit (0.0f, 1.0f, v[SaturateIdx::Mix]));
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept override
        {
            const int N   = buffer.getNumSamples();
            const int chs = buffer.getNumChannels();

            for (int i = 0; i < N; ++i)
            {
                const float a      = driveRamp.next();
                const float mix    = mixRamp.next();
                const float drive  = 1.0f + a * 9.0f;
                const float makeup = 1.0f / std::pow (drive, 0.75f);

                for (int c = 0; c < chs; ++c)
                {
                    float* x = buffer.getWritePointer (c);
                    const float wet = std::tanh (x[i] * drive) * makeup;
                    x[i] += mix * (wet - x[i]);
                }
            }
        }

    private:
        GainRamp driveRamp, mixRamp;
    };

    //==============================================================================
    /** Reverb — Schroeder (Freeverb) or Void Verb (the Dattorro plate), with the
        wet path filtered either side and optionally warmed. */
    class ReverbEffect final : public SpaceDustEffect<SpaceDustReverb>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustReverb::Parameters p;
            p.type      = choice (v, ReverbIdx::Type);
            p.wetMix    = v[ReverbIdx::WetMix];
            p.decayTime = v[ReverbIdx::DecayTime];

            p.filterOn             = flag (v, ReverbIdx::FilterOn);
            p.filterHPCutoff       = v[ReverbIdx::HpCut];
            p.filterHPResonance    = v[ReverbIdx::HpRes];
            p.filterLPCutoff       = v[ReverbIdx::LpCut];
            p.filterLPResonance    = v[ReverbIdx::LpRes];
            p.filterWarmSaturation = flag (v, ReverbIdx::WarmSat);

            decayTime = p.decayTime;
            wetMix    = p.wetMix;

            fx.setParameters (p);
        }

        void processFx (juce::AudioBuffer<float>& stereo) override
        {
            // Two things Space Dust's processBlock does around this effect, kept
            // here so the reverb behaves the way it does there.
            //
            // Decay at the very bottom: flush once and bypass. Void Verb keeps
            // diffusing at decay 0, so without this the tank never goes quiet.
            if (decayTime <= 0.001f)
            {
                if (lastDecay > 0.001f || lastDecay < 0.0f)
                    fx.reset();

                lastDecay = decayTime;
                return;
            }

            lastDecay = decayTime;

            // Input drive, 0 dB dry to +3 dB fully wet, compensating the level
            // the wet path costs.
            stereo.applyGain (std::pow (10.0f, juce::jlimit (0.0f, 1.0f, wetMix) * 3.0f / 20.0f));

            SpaceDustEffect<SpaceDustReverb>::processFx (stereo);
        }

    private:
        float decayTime = 16.0f, wetMix = 0.33f, lastDecay = -1.0f;
    };

    //==============================================================================
    /** Ping-pong delay: tempo-synced or free, with filtered feedback. */
    class DelayEffect final : public SpaceDustEffect<SolPingPongDelay>
    {
    protected:
        void apply (const float* v) override
        {
            SolPingPongDelay::Parameters p;
            p.enabled        = true;
            p.sync           = flag (v, DelayIdx::Sync);
            p.rate           = v[DelayIdx::Rate];
            p.decayPercent   = v[DelayIdx::Decay];
            p.mixPercent     = v[DelayIdx::Mix];
            p.pingPong       = flag (v, DelayIdx::PingPong);
            p.filterOn       = flag (v, DelayIdx::FilterOn);
            p.hpCutoffHz     = v[DelayIdx::HpCut];
            p.hpRes          = v[DelayIdx::HpRes];
            p.lpCutoffHz     = v[DelayIdx::LpCut];
            p.lpRes          = v[DelayIdx::LpRes];
            p.warmSaturation = flag (v, DelayIdx::WarmSat);
            p.bpm            = currentBpm();

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** Portal-style granular delay. */
    class GrainDelayEffect final : public SpaceDustEffect<SpaceDustGrainDelay>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustGrainDelay::Parameters p;
            p.enabled        = true;
            p.delayMs        = v[GrainIdx::Time];
            p.grainSizeMs    = v[GrainIdx::Size];
            p.pitchSemitones = v[GrainIdx::Pitch];
            p.mix            = v[GrainIdx::Mix]   * 0.01f;   // percent -> 0-1
            p.decay          = v[GrainIdx::Decay] * 0.01f;   // percent -> 0-1.5
            p.density        = v[GrainIdx::Density];
            p.jitter         = v[GrainIdx::Jitter] * 0.01f;  // percent -> 0-1
            p.pingPong       = flag (v, GrainIdx::PingPong);

            p.filterOn       = flag (v, GrainIdx::FilterOn);
            p.hpCutoffHz     = v[GrainIdx::HpCut];
            p.hpRes          = v[GrainIdx::HpRes];
            p.lpCutoffHz     = v[GrainIdx::LpCut];
            p.lpRes          = v[GrainIdx::LpRes];
            p.warmSaturation = flag (v, GrainIdx::WarmSat);

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** MXR Phase 90-style phaser. Script mode is the no-feedback variant, so it
        zeroes the feedback path rather than merely labelling it — the ported DSP
        reads the feedback value alone. */
    class PhaserEffect final : public SpaceDustEffect<SpaceDustPhaser>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustPhaser::Parameters p;
            p.enabled      = true;
            p.rateHz       = v[PhaserIdx::Rate];
            p.depth        = v[PhaserIdx::Depth];
            p.scriptMode   = flag (v, PhaserIdx::Script);
            p.feedback     = p.scriptMode ? 0.0f : v[PhaserIdx::Feedback];
            p.mix          = v[PhaserIdx::Mix];
            p.centreHz     = v[PhaserIdx::Centre];
            p.numStages    = choice (v, PhaserIdx::Stages) == 0 ? 4 : 6;
            p.stereoOffset = v[PhaserIdx::Width];
            p.vintageMode  = flag (v, PhaserIdx::Vintage);

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** Comb-filter flanger. */
    class FlangerEffect final : public SpaceDustEffect<SpaceDustFlanger>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustFlanger::Parameters p;
            p.enabled  = true;
            p.rateHz   = v[FlangerIdx::Rate];
            p.depth    = v[FlangerIdx::Depth];
            p.feedback = v[FlangerIdx::Feedback];
            p.width    = v[FlangerIdx::Width];
            p.mix      = v[FlangerIdx::Mix];

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** Tempo-synced step gate. */
    class TranceGateEffect final : public SpaceDustEffect<SpaceDustTranceGate>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustTranceGate::Parameters p;
            p.enabled   = true;
            p.numSteps  = kStepCounts[juce::jlimit (0, 2, choice (v, GateIdx::Steps))];
            p.sync      = flag (v, GateIdx::Sync);
            p.rate      = v[GateIdx::Rate];
            p.attackMs  = v[GateIdx::Attack];
            p.releaseMs = v[GateIdx::Release];
            p.mix       = v[GateIdx::Mix];

            for (int i = 0; i < SpaceDustTranceGate::kMaxSteps; ++i)
                p.stepOn[i] = flag (v, GateIdx::Step1 + i);

            fx.setParameters (p);
        }

    private:
        static constexpr int kStepCounts[3] = { 4, 8, 16 };
    };

    //==============================================================================
    /** Bit depth + sample rate destruction. */
    class BitCrushEffect final : public SpaceDustEffect<SpaceDustBitCrusher>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustBitCrusher::Parameters p;
            p.enabled = true;
            p.amount  = v[CrushIdx::Amount];
            p.rate    = v[CrushIdx::Rate];
            p.mix     = v[CrushIdx::Mix];

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** KClip-style mastering clipper, five curves, oversampled. */
    class SoftClipEffect final : public SpaceDustEffect<SpaceDustSoftClipper>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustSoftClipper::Parameters p;
            p.enabled = true;
            p.mode    = choice (v, ClipIdx::Mode);
            p.drive   = v[ClipIdx::Drive];   // 0-1; the clipper maps to 0.5-5.0
            p.knee    = v[ClipIdx::Knee];    // 0-1; the clipper maps to 0.3-1.0
            p.mix     = v[ClipIdx::Mix];

            // Dropdown index -> factor, as upstream.
            static constexpr int factors[4] = { 2, 4, 8, 16 };
            p.oversample = factors[juce::jlimit (0, 3, choice (v, ClipIdx::Oversample))];

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** SSL / 1176 / Opto compressor. */
    class CompressEffect final : public SpaceDustEffect<SpaceDustCompressor>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustCompressor::Parameters p;
            p.enabled      = true;
            p.type         = choice (v, CompIdx::Type);
            p.thresholdDb  = v[CompIdx::Threshold];
            p.ratio        = v[CompIdx::Ratio];
            p.attackMs     = v[CompIdx::Attack];
            p.releaseMs    = v[CompIdx::Release];
            p.makeupGainDb = v[CompIdx::Makeup];
            p.mix          = v[CompIdx::Mix];
            p.autoRelease  = flag (v, CompIdx::AutoRelease);
            p.softClip     = flag (v, CompIdx::SoftClip);

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** RC-20-style lo-fi: HF rolloff, bit and rate reduction, hiss, wow and
        flutter, all off one macro — as it is upstream. */
    class LofiEffect final : public SpaceDustEffect<SpaceDustLofi>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustLofi::Parameters p;
            p.enabled = true;
            p.amount  = v[LofiIdx::Amount];

            fx.setParameters (p);
        }
    };

    //==============================================================================
    /** Five-band end-of-chain EQ, every band with its own shape. */
    class FinalEQEffect final : public SpaceDustEffect<SpaceDustFinalEQ>
    {
    protected:
        void apply (const float* v) override
        {
            SpaceDustFinalEQ::Parameters p;
            p.enabled = true;

            for (int b = 0; b < 5; ++b)
            {
                const int base = b * EqIdx::perBand;
                auto&     band = p.bands[(size_t) b];

                band.freqHz = v[base + 0];
                band.gainDb = v[base + 1];
                band.Q      = v[base + 2];
                band.type   = SpaceDustFinalEQ::typeFromChoiceIndex (choice (v, base + 3));
            }

            fx.setParameters (p);
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
            case EffectType::Saturate:   fx = std::make_unique<SunSaturator>();     break;
            case EffectType::Reverb:     fx = std::make_unique<ReverbEffect>();     break;
            case EffectType::Delay:      fx = std::make_unique<DelayEffect>();      break;
            case EffectType::GrainDelay: fx = std::make_unique<GrainDelayEffect>(); break;
            case EffectType::Phaser:     fx = std::make_unique<PhaserEffect>();     break;
            case EffectType::Flanger:    fx = std::make_unique<FlangerEffect>();    break;
            case EffectType::TranceGate: fx = std::make_unique<TranceGateEffect>(); break;
            case EffectType::BitCrush:   fx = std::make_unique<BitCrushEffect>();   break;
            case EffectType::SoftClip:   fx = std::make_unique<SoftClipEffect>();   break;
            case EffectType::Compress:   fx = std::make_unique<CompressEffect>();   break;
            case EffectType::Lofi:       fx = std::make_unique<LofiEffect>();       break;
            case EffectType::FinalEQ:    fx = std::make_unique<FinalEQEffect>();    break;
            case EffectType::Empty:
            case EffectType::NumTypes:   break;
        }

        if (fx != nullptr)
            fx->typeTag = t;
        return fx;
    }
} // namespace VocalFx
