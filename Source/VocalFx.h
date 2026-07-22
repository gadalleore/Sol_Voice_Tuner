/*
    VocalFx.h
    ---------
    Lightweight real-time-safe DSP blocks layered on top of the pitch-shifted
    voice when the Robotic knob is engaged:

      * BandLimitedSawCarrier — polyBLEP saw locked to the snapped fundamental,
        mixed in as a vocoder-style sub layer.
      * DeEsser              — feed-forward sibilance attenuator (always on,
        deeper at high Robotic).
      * PinkNoiseSource      — warm breathy pink-noise texture for the upper
        Robotic range.

    All members are header-only, allocation-free after prepare(), and intended
    to be called from PluginProcessor::processBlock.
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace VocalFx
{
    /** One-pole gain ramp — used to keep additive mixes click-free. */
    struct GainRamp
    {
        void prepare (double sr, float tauMs) noexcept
        {
            const float tau = juce::jmax (tauMs * 0.001f, 1.0e-4f);
            const float dt  = 1.0f / (float) juce::jmax (sr, 1.0);
            coeff = 1.0f - std::exp (-dt / tau);
        }

        void reset (float v = 0.0f) noexcept { current = target = v; }
        void setTarget (float v)    noexcept { target = v; }

        float next() noexcept
        {
            current += coeff * (target - current);
            return current;
        }

        float current { 0.0f };
        float target  { 0.0f };
        float coeff   { 0.2f };
    };

    //==============================================================================
    /** Sub-bass channel vocoder:
          * Generates its own band-limited (polyBLEP) saw locked to a target Hz.
          * Splits carrier and mono modulator into log-spaced bands; each band
            tracks the modulator envelope and shapes the matching carrier band.
            Sum gives the saw vowel/formant character.
          * Parallel sub-LP path preserves the carrier fundamental (which is
            below the lowest vocoder band) gated by overall input envelope — so
            you keep bass weight while gaining vocoder colour. */
    class SubVocoder
    {
    public:
        static constexpr int kBands = 6;

        void prepare (double sr) noexcept
        {
            sampleRate = juce::jmax (sr, 1.0);
            invSr      = 1.0f / (float) sampleRate;

            // Saw warmth LP ~5 kHz: leaves enough harmonics for upper vocoder
            // bands to grab, while taming aliasing residue.
            sawLpAlpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5000.0f * invSr);

            // Sub fundamental path: LP ~220 Hz keeps the bottom octave or two
            // (below the lowest vocoder band) so the sub still feels weighty.
            subLpAlpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 220.0f * invSr);

            // Per-band env: fast enough for vowel transitions, slow enough to glide.
            bandAtk    = expCoeff (8.0f,  sampleRate);
            bandRel    = expCoeff (55.0f, sampleRate);
            // Overall env gates the sub fundamental path.
            overallAtk = expCoeff (5.0f,  sampleRate);
            overallRel = expCoeff (90.0f, sampleRate);

            mix.prepare (sr, 10.0f);

            // Log-spaced bands across the vowel/formant region (250 Hz – 4.5 kHz).
            constexpr float fLo = 250.0f, fHi = 4500.0f;
            constexpr float bandQ = 2.2f;
            const float bw = std::pow (fHi / fLo, 1.0f / (float) (kBands - 1));
            float fc = fLo;
            for (int b = 0; b < kBands; ++b)
            {
                const float g = std::tan (juce::MathConstants<float>::pi * fc * invSr);
                const float k = 1.0f / bandQ;
                auto& bd = bands[(size_t) b];
                bd.a1 = 1.0f / (1.0f + g * (g + k));
                bd.a2 = g * bd.a1;
                bd.a3 = g * bd.a2;
                fc *= bw;
            }

            reset();
        }

        void reset() noexcept
        {
            phase      = 0.0f;
            sawLpState = 0.0f;
            subLpState = 0.0f;
            overallEnv = 0.0f;
            currentHz  = 0.0f;
            for (auto& b : bands)
            {
                b.modIc1 = b.modIc2 = 0.0f;
                b.carIc1 = b.carIc2 = 0.0f;
                b.env    = 0.0f;
            }
            mix.reset();
        }

        /** Saw frequency (Hz). 0 stops generation but keeps phase coherent. */
        void setFrequency (float hz) noexcept
        {
            currentHz = juce::jlimit (0.0f, (float) sampleRate * 0.45f, hz);
        }

        /** Output mix in [0, 1]; ramped to avoid zipper noise. */
        void setTargetMix (float g) noexcept { mix.setTarget (juce::jlimit (0.0f, 1.0f, g)); }

        /** Vocode the internal saw against `monoModulator` (the dry input,
            mono-summed) and add the result to every channel of `buffer`. */
        void addTo (juce::AudioBuffer<float>& buffer, const float* monoModulator) noexcept
        {
            const int N   = buffer.getNumSamples();
            const int chs = buffer.getNumChannels();
            if (N <= 0 || chs <= 0 || monoModulator == nullptr) return;

            const float dt = (currentHz > 0.0f) ? juce::jlimit (1.0e-6f, 0.45f, currentHz * invSr) : 0.0f;

            for (int i = 0; i < N; ++i)
            {
                const float g_mix = mix.next();
                const float mod   = monoModulator[i];

                // --- Band-limited saw carrier ---
                float saw = 0.0f;
                if (dt > 0.0f)
                {
                    float s = 2.0f * phase - 1.0f;
                    s -= polyBlep (phase, dt);
                    sawLpState += sawLpAlpha * (s - sawLpState);
                    saw         = sawLpState;
                    phase += dt;
                    if (phase >= 1.0f) phase -= 1.0f;
                }

                // --- Channel vocoder: per-band env on mod, shape carrier, sum ---
                float vocoded = 0.0f;
                for (auto& b : bands)
                {
                    // Mod bandpass (Zavalishin TPT)
                    float v3  = mod - b.modIc2;
                    float v1m = b.a1 * b.modIc1 + b.a2 * v3;
                    float v2m = b.modIc2 + b.a2 * b.modIc1 + b.a3 * v3;
                    b.modIc1 = 2.0f * v1m - b.modIc1;
                    b.modIc2 = 2.0f * v2m - b.modIc2;
                    const float bpMod = v1m;

                    const float absMod = std::abs (bpMod);
                    const float c0     = (absMod > b.env) ? bandAtk : bandRel;
                    b.env += c0 * (absMod - b.env);

                    // Carrier bandpass
                    v3 = saw - b.carIc2;
                    float v1c = b.a1 * b.carIc1 + b.a2 * v3;
                    float v2c = b.carIc2 + b.a2 * b.carIc1 + b.a3 * v3;
                    b.carIc1 = 2.0f * v1c - b.carIc1;
                    b.carIc2 = 2.0f * v2c - b.carIc2;
                    const float bpCar = v1c;

                    vocoded += bpCar * b.env;
                }
                vocoded *= vocoderMakeup;

                // --- Sub fundamental path: LP carrier gated by overall input env ---
                const float absIn = std::abs (mod);
                const float oc    = (absIn > overallEnv) ? overallAtk : overallRel;
                overallEnv += oc * (absIn - overallEnv);

                subLpState += subLpAlpha * (saw - subLpState);
                const float subOnly = subLpState * overallEnv;

                // Blend: bass weight + vocoded vowel character on top.
                const float combined = subOnly * 0.65f + vocoded * 0.45f;

                if (g_mix > 1.0e-6f)
                {
                    const float v = combined * g_mix;
                    for (int c = 0; c < chs; ++c)
                        buffer.getWritePointer (c)[i] += v;
                }
            }
        }

    private:
        static float polyBlep (float t, float dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return (t + t) - (t * t) - 1.0f;
            }
            if (t > 1.0f - dt)
            {
                t = (t - 1.0f) / dt;
                return (t * t) + (t + t) + 1.0f;
            }
            return 0.0f;
        }

        static float expCoeff (float tauMs, double sr) noexcept
        {
            const float tau = juce::jmax (tauMs * 0.001f, 1.0e-4f);
            const float dt  = 1.0f / (float) juce::jmax (sr, 1.0);
            return 1.0f - std::exp (-dt / tau);
        }

        struct Band
        {
            float a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
            float modIc1 { 0.0f }, modIc2 { 0.0f };
            float carIc1 { 0.0f }, carIc2 { 0.0f };
            float env { 0.0f };
        };

        // Empirical sum makeup for Q=2.2 bandpasses with adjacent-band overlap.
        static constexpr float vocoderMakeup = 1.8f;

        double sampleRate { 44100.0 };
        float  invSr      { 1.0f / 44100.0f };
        float  sawLpAlpha { 0.5f };
        float  subLpAlpha { 0.03f };
        float  bandAtk    { 0.05f };
        float  bandRel    { 0.001f };
        float  overallAtk { 0.1f };
        float  overallRel { 0.0008f };

        float  phase      { 0.0f };
        float  sawLpState { 0.0f };
        float  subLpState { 0.0f };
        float  overallEnv { 0.0f };
        float  currentHz  { 0.0f };

        std::array<Band, kBands> bands {};
        GainRamp mix;
    };

    //==============================================================================
    /** Sibilance-aware de-esser. Detects sibilance by comparing the HF-band
        envelope to the full-band envelope — only triggers when high frequencies
        *dominate* the signal, not just when they're loud. Vowels never false-trigger.
        Always active; ratio threshold tightens and reduction depth grows with Robotic. */
    class DeEsser
    {
    public:
        void prepare (double sr, int numChannels) noexcept
        {
            sampleRate = juce::jmax (sr, 1.0);
            state.assign ((size_t) juce::jmax (numChannels, 1), ChState{});

            setFilter (6500.0f, 1.4f);
            attackCoeff  = expCoeff (3.0f,  sampleRate);
            releaseCoeff = expCoeff (90.0f, sampleRate);
        }

        void reset() noexcept
        {
            for (auto& s : state) s = {};
        }

        void setRobotic (float r) noexcept { robotic = juce::jlimit (0.0f, 1.0f, r); }

        /** Process in place. If `sibilanceOut` is non-null, writes a per-sample
            normalised sibilance-presence signal in [0, 1] (max across channels)
            — used downstream to swap harsh sibilance for pink-noise breath. */
        void process (juce::AudioBuffer<float>& buffer, float* sibilanceOut = nullptr) noexcept
        {
            const int N   = buffer.getNumSamples();
            const int chs = juce::jmin (buffer.getNumChannels(), (int) state.size());
            if (N <= 0 || chs <= 0) return;

            // Sibilance ratio = hfEnv / fullEnv. Triggers when HF *dominates* the band.
            // Lower threshold at high Robotic = more aggressive sibilance hunting.
            const float ratioThreshold = juce::jmap (robotic, 0.0f, 1.0f, 0.40f, 0.22f);
            const float maxReduction   = juce::jmap (robotic, 0.0f, 1.0f, 0.45f, 0.85f);
            const float invMaxRed      = 1.0f / juce::jmax (maxReduction, 1.0e-6f);
            const float ratioSpan      = juce::jmax (1.0f - ratioThreshold, 1.0e-3f);
            // Absolute noise floor on the HF band — prevents detection during near-silence.
            const float noiseFloor     = juce::Decibels::decibelsToGain (-50.0f);

            const float a1 = 1.0f / (1.0f + svfG * (svfG + svfK));
            const float a2 = svfG * a1;
            const float a3 = svfG * a2;

            if (sibilanceOut != nullptr)
                std::fill (sibilanceOut, sibilanceOut + N, 0.0f);

            for (int c = 0; c < chs; ++c)
            {
                auto& st = state[(size_t) c];
                float* x = buffer.getWritePointer (c);

                for (int i = 0; i < N; ++i)
                {
                    const float in = x[i];

                    // Zavalishin TPT state-variable filter (bandpass tap)
                    const float v3 = in - st.ic2eq;
                    const float v1 = a1 * st.ic1eq + a2 * v3;
                    const float v2 = st.ic2eq + a2 * st.ic1eq + a3 * v3;
                    st.ic1eq = 2.0f * v1 - st.ic1eq;
                    st.ic2eq = 2.0f * v2 - st.ic2eq;
                    const float bp = v1;

                    // Twin peak followers: HF band vs. full band
                    const float absBp = std::abs (bp);
                    const float cHf   = (absBp > st.hfEnv) ? attackCoeff : releaseCoeff;
                    st.hfEnv += cHf * (absBp - st.hfEnv);

                    const float absIn  = std::abs (in);
                    const float cFull  = (absIn > st.fullEnv) ? attackCoeff : releaseCoeff;
                    st.fullEnv += cFull * (absIn - st.fullEnv);

                    float reduction = 0.0f;
                    if (st.hfEnv > noiseFloor)
                    {
                        const float ratio = st.hfEnv / juce::jmax (st.fullEnv, 1.0e-6f);
                        if (ratio > ratioThreshold)
                        {
                            const float excess = juce::jlimit (0.0f, 1.0f,
                                                              (ratio - ratioThreshold) / ratioSpan);
                            reduction = excess * maxReduction;
                        }
                    }

                    // Spectral subtraction: dry minus a fraction of the isolated sibilance band.
                    x[i] = in - bp * reduction;

                    if (sibilanceOut != nullptr)
                    {
                        const float norm = reduction * invMaxRed;
                        if (norm > sibilanceOut[i])
                            sibilanceOut[i] = norm;
                    }
                }
            }
        }

    private:
        struct ChState
        {
            float ic1eq   { 0.0f };
            float ic2eq   { 0.0f };
            float hfEnv   { 0.0f };
            float fullEnv { 0.0f };
        };

        void setFilter (float fc, float q) noexcept
        {
            svfG = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
            svfK = 1.0f / juce::jmax (q, 0.05f);
        }

        static float expCoeff (float tauMs, double sr) noexcept
        {
            const float tau = juce::jmax (tauMs * 0.001f, 1.0e-4f);
            const float dt  = 1.0f / (float) juce::jmax (sr, 1.0);
            return 1.0f - std::exp (-dt / tau);
        }

        double sampleRate   { 44100.0 };
        float  svfG         { 0.5f };
        float  svfK         { 0.9f };
        float  attackCoeff  { 0.1f };
        float  releaseCoeff { 0.001f };
        float  robotic      { 0.0f };
        std::vector<ChState> state;
    };

    //==============================================================================
    /** Airy pink-noise layer: Paul Kellet's economy filter + cascaded 500 Hz HP
        (two one-poles → -12 dB/oct) so only the breathy highs remain. */
    class PinkNoiseSource
    {
    public:
        void prepare (double sr) noexcept
        {
            sampleRate = juce::jmax (sr, 1.0);
            // -12 dB/oct corner near 500 Hz — strips body, keeps the "sss" air.
            hpAlpha    = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 500.0f / (float) sampleRate);
            mix.prepare (sr, 15.0f);
            reset();
        }

        void reset() noexcept
        {
            b0 = b1 = b2 = 0.0f;
            hpLp1 = hpLp2 = 0.0f;
            rng = 0x73659832u;
            mix.reset();
        }

        void setTargetMix (float g) noexcept { mix.setTarget (juce::jlimit (0.0f, 1.0f, g)); }

        /** Add pink noise to every channel, scaled per sample by `modulator`
            (e.g. a sibilance-presence signal). Pass nullptr for a flat layer. */
        void addTo (juce::AudioBuffer<float>& buffer, const float* modulator = nullptr) noexcept
        {
            const int N   = buffer.getNumSamples();
            const int chs = buffer.getNumChannels();
            if (N <= 0 || chs <= 0) return;

            for (int i = 0; i < N; ++i)
            {
                const float g   = mix.next();
                const float mod = modulator ? modulator[i] : 1.0f;

                // xorshift-LCG hybrid → white in [-1, 1)
                rng = rng * 1664525u + 1013904223u;
                const float w = ((float) (rng >> 8) * (1.0f / 8388608.0f)) - 1.0f;

                // Paul Kellet's 3-pole economy pink filter.
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                float pink = b0 + b1 + b2 + w * 0.1848f;
                pink *= 0.11f;  // bring back to ~±1

                // Cascaded one-pole HPs at ~500 Hz (input minus LP, twice).
                hpLp1 += hpAlpha * (pink - hpLp1);
                const float hp1 = pink - hpLp1;
                hpLp2 += hpAlpha * (hp1  - hpLp2);
                const float airy = hp1 - hpLp2;

                const float amp = g * mod;
                if (amp > 1.0e-6f)
                {
                    const float v = airy * amp;
                    for (int c = 0; c < chs; ++c)
                        buffer.getWritePointer (c)[i] += v;
                }
            }
        }

    private:
        double   sampleRate { 44100.0 };
        float    hpAlpha    { 0.05f };
        float    b0 { 0.0f }, b1 { 0.0f }, b2 { 0.0f };
        float    hpLp1 { 0.0f }, hpLp2 { 0.0f };
        uint32_t rng        { 0x73659832u };
        GainRamp mix;
    };
} // namespace VocalFx
