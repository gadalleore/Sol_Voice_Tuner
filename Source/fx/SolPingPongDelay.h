#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

//==============================================================================
/**
    Space Dust's delay, as a class.

    It is the one effect on that plugin's Effects tab with no SpaceDust*.h/.cpp
    pair — it lives inline in its PluginProcessor::processBlock, tangled up with
    that plugin's APVTS ids and playhead handling. This is the same algorithm,
    lifted into the same shape the other ported effects have, control for control:

      * Time is the 0-12 control, INVERTED (turning it up shortens the delay).
        With Sync on it indexes an 18-entry table of musical divisions — straight,
        dotted and triplet — against the host tempo, sampled through a x^2.5 curve.
        With Sync off it is a 20 ms - 2 s logarithmic sweep.
      * Feedback and Mix are percentages, as upstream. Mix also drives a small
        pre-gain (0 to +3 dB) that compensates the level the wet path costs.
      * The HP/LP filter pair runs on the wet signal only, never the dry, and is
        applied TWICE with different Q: at the user's resonance on the output tap,
        and at 0.707 inside the feedback loop, where resonance would run away.
      * Warm Saturation tanh-saturates the feedback. With it off the loop is left
        clean and only clamped as a runaway backstop — upstream found that
        saturating unconditionally coloured loud material and left quiet material
        alone, which is the wrong way round.
      * Ping-Pong sums the input to mono and cross-feeds the two lines so repeats
        alternate across the image.
      * Feedback at or below 0.1% flushes the lines and bypasses, so the tail
        genuinely stops rather than ringing on under a closed mix.
*/
class SolPingPongDelay
{
public:
    struct Parameters
    {
        bool  enabled        = false;
        bool  sync           = true;
        float rate           = 6.0f;    //!< 0-12, inverted (see above)
        float decayPercent   = 40.0f;   //!< feedback, 0-100
        float mixPercent     = 50.0f;   //!< dry/wet, 0-100
        bool  pingPong       = false;
        bool  filterOn       = false;
        float hpCutoffHz     = 100.0f;
        float hpRes          = 0.3f;    //!< 0-1, mapped to Q 0.1-5.0
        float lpCutoffHz     = 8000.0f;
        float lpRes          = 0.3f;
        bool  warmSaturation = false;

        /** Host tempo for Sync, supplied by the owner each block. */
        double bpm = 120.0;
    };

    SolPingPongDelay() = default;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        spec_ = spec;

        maxDelaySamples_ = juce::jmax (16, (int) (spec.sampleRate * 2.0) + 2);

        delayLineL_.setMaximumDelayInSamples (maxDelaySamples_);
        delayLineR_.setMaximumDelayInSamples (maxDelaySamples_);

        const juce::dsp::ProcessSpec monoSpec { spec.sampleRate, spec.maximumBlockSize, 1 };
        delayLineL_.prepare (monoSpec);
        delayLineR_.prepare (monoSpec);

        const juce::dsp::ProcessSpec stereoSpec { spec.sampleRate, spec.maximumBlockSize, 2 };
        for (auto* f : { &filterHP_, &filterLP_, &filterHPFb_, &filterLPFb_ })
            f->prepare (stereoSpec);

        filterHP_  .setType (juce::dsp::StateVariableTPTFilterType::highpass);
        filterLP_  .setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filterHPFb_.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        filterLPFb_.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

        smoothedTime_ .reset (spec.sampleRate, 0.15);   // retuning a delay glides
        smoothedDecay_.reset (spec.sampleRate, 0.05);
        smoothedMix_  .reset (spec.sampleRate, 0.05);
        smoothedHpCut_.reset (spec.sampleRate, 0.05);
        smoothedLpCut_.reset (spec.sampleRate, 0.05);
        smoothedHpQ_  .reset (spec.sampleRate, 0.05);
        smoothedLpQ_  .reset (spec.sampleRate, 0.05);

        reset();
    }

    void reset()
    {
        delayLineL_.reset();
        delayLineR_.reset();

        for (auto* f : { &filterHP_, &filterLP_, &filterHPFb_, &filterLPFb_ })
            f->reset();

        smoothedDecay_.setCurrentAndTargetValue (0.0f);
    }

    void setParameters (const Parameters& p)
    {
        params_ = p;

        smoothedTime_ .setTargetValue (delayTimeSamples (p));
        smoothedDecay_.setTargetValue (juce::jlimit (0.0f, 0.99f, p.decayPercent * 0.01f));
        smoothedMix_  .setTargetValue (juce::jlimit (0.0f, 1.0f,  p.mixPercent   * 0.01f));
        smoothedHpCut_.setTargetValue (juce::jlimit (20.0f, 20000.0f, p.hpCutoffHz));
        smoothedLpCut_.setTargetValue (juce::jlimit (20.0f, 20000.0f, p.lpCutoffHz));
        smoothedHpQ_  .setTargetValue (juce::jlimit (0.1f, 5.0f, 0.1f + p.hpRes * 4.9f));
        smoothedLpQ_  .setTargetValue (juce::jlimit (0.1f, 5.0f, 0.1f + p.lpRes * 4.9f));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! params_.enabled)
            return;

        const int N   = buffer.getNumSamples();
        const int chs = buffer.getNumChannels();
        if (N <= 0 || chs <= 0)
            return;

        const float decay = juce::jlimit (0.0f, 0.99f, params_.decayPercent * 0.01f);

        // Feedback shut: flush and bypass, so there is genuinely no tail.
        if (decay <= 0.001f)
        {
            reset();
            return;
        }

        const bool stereo = chs > 1;
        float* left  = buffer.getWritePointer (0);
        float* right = stereo ? buffer.getWritePointer (1) : left;

        // Pre-gain: 0 dB dry, +3 dB fully wet, compensating what the mix costs.
        const float drive = std::pow (10.0f, juce::jlimit (0.0f, 1.0f, params_.mixPercent * 0.01f) * 3.0f / 20.0f);
        buffer.applyGain (0, 0, N, drive);
        if (stereo)
            buffer.applyGain (1, 0, N, drive);

        for (int s = 0; s < N; ++s)
        {
            const float time  = juce::jmax (1.0f, smoothedTime_.getNextValue());
            const float fb    = juce::jlimit (0.0f, 0.99f, smoothedDecay_.getNextValue());
            const float wet   = smoothedMix_.getNextValue();
            const float dry   = 1.0f - wet;

            if (params_.filterOn)
            {
                const float hpCut = smoothedHpCut_.getNextValue();
                const float lpCut = smoothedLpCut_.getNextValue();

                filterHP_  .setCutoffFrequency (hpCut);
                filterHP_  .setResonance (smoothedHpQ_.getNextValue());
                filterLP_  .setCutoffFrequency (lpCut);
                filterLP_  .setResonance (smoothedLpQ_.getNextValue());

                // Feedback path stays at 0.707: resonance inside a loop runs away.
                filterHPFb_.setCutoffFrequency (hpCut);
                filterHPFb_.setResonance (0.707f);
                filterLPFb_.setCutoffFrequency (lpCut);
                filterLPFb_.setResonance (0.707f);
            }

            const float lIn = left[s];
            const float rIn = stereo ? right[s] : lIn;

            const float dL = delayLineL_.popSample (0, time, true);
            const float dR = delayLineR_.popSample (0, time, true);

            const float outL = filterForOutput (0, dL);
            const float outR = filterForOutput (1, dR);
            const float fbL  = filterForFeedback (0, dL);
            const float fbR  = filterForFeedback (1, dR);

            left[s] = dry * lIn + wet * saturateOutput (outL);
            if (stereo)
                right[s] = dry * rIn + wet * saturateOutput (outR);

            if (params_.pingPong)
            {
                const float monoIn = 0.5f * (lIn + rIn);
                delayLineL_.pushSample (0, saturateFeedback (monoIn + fb * fbR));
                delayLineR_.pushSample (0, fbL);
            }
            else
            {
                delayLineL_.pushSample (0, saturateFeedback (lIn + fb * fbL));
                delayLineR_.pushSample (0, saturateFeedback (rIn + fb * fbR));
            }
        }
    }

private:
    using Line = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

    float filterForOutput (int ch, float x) noexcept
    {
        if (! params_.filterOn) return x;
        return filterLP_.processSample (ch, filterHP_.processSample (ch, x));
    }

    float filterForFeedback (int ch, float x) noexcept
    {
        if (! params_.filterOn) return x;
        return filterLPFb_.processSample (ch, filterHPFb_.processSample (ch, x));
    }

    float saturateOutput (float filtered) const noexcept
    {
        if (! params_.warmSaturation) return filtered;

        // Drive from the filter resonances, kept conservative so the wet path
        // never comes out louder than it went in.
        const float hpQ  = juce::jlimit (0.1f, 5.0f, 0.1f + params_.hpRes * 4.9f);
        const float lpQ  = juce::jlimit (0.1f, 5.0f, 0.1f + params_.lpRes * 4.9f);
        const float gain = 1.0f + (hpQ + lpQ) * 0.15f;

        return std::tanh (juce::jlimit (-1.5f, 1.5f, filtered) * gain);
    }

    float saturateFeedback (float raw) const noexcept
    {
        if (params_.warmSaturation)
            return std::tanh (juce::jlimit (-2.0f, 2.0f, raw));

        // Clean: clamp only as a runaway backstop, at a ceiling high enough
        // (+/-4, about +12 dBFS) to stay transparent for anything musical.
        return juce::jlimit (-4.0f, 4.0f, raw);
    }

    /** Space Dust's Time mapping, unchanged. */
    float delayTimeSamples (const Parameters& p) const noexcept
    {
        const float clamped  = juce::jlimit (0.0f, 12.0f, p.rate);
        const float inverted = 12.0f - clamped;      // knob up = shorter delay

        float samples;

        if (p.sync)
        {
            const double tempo         = p.bpm > 0.0 ? p.bpm : 120.0;
            const double samplesPerBeat = spec_.sampleRate * 60.0 / tempo;

            // 18 divisions: straight, dotted and triplets, sampled through a
            // x^2.5 curve so the short end gets most of the travel.
            const double normalised = juce::jlimit (0.0, 1.0, (double) inverted / 12.0);
            const double curved     = std::pow (normalised, 2.5);

            static constexpr double multipliers[18] = {
                8.0, 6.0, 4.0, 2.6666666666666665, 2.0, 1.3333333333333333,
                1.0, 0.6666666666666666, 0.5, 0.3333333333333333, 0.25,
                0.16666666666666666, 0.125, 0.08333333333333333, 0.0625,
                0.0510204081632653, 0.03125, 0.03125
            };

            const int index = juce::jlimit (0, 17, (int) std::round (curved * 17.0));
            samples = (float) (samplesPerBeat * (1.0 / multipliers[index]));
        }
        else
        {
            // 20 ms - 2 s, logarithmic.
            const float norm  = juce::jlimit (0.0f, 1.0f, inverted / 12.0f);
            const float logMs = std::log (20.0f) + norm * (std::log (2000.0f) - std::log (20.0f));
            const float ms    = juce::jlimit (20.0f, 2000.0f, std::exp (logMs));

            samples = ms * (float) spec_.sampleRate / 1000.0f;
        }

        return juce::jlimit (1.0f, (float) (maxDelaySamples_ - 2), samples);
    }

    juce::dsp::ProcessSpec spec_ { 44100.0, 512, 2 };
    Parameters params_;

    Line delayLineL_, delayLineR_;
    int  maxDelaySamples_ { 0 };

    juce::dsp::StateVariableTPTFilter<float> filterHP_,   filterLP_;
    juce::dsp::StateVariableTPTFilter<float> filterHPFb_, filterLPFb_;

    juce::SmoothedValue<float> smoothedTime_  { 22050.0f };
    juce::SmoothedValue<float> smoothedDecay_ { 0.4f };
    juce::SmoothedValue<float> smoothedMix_   { 0.5f };
    juce::SmoothedValue<float> smoothedHpCut_ { 100.0f };
    juce::SmoothedValue<float> smoothedLpCut_ { 8000.0f };
    juce::SmoothedValue<float> smoothedHpQ_   { 0.707f };
    juce::SmoothedValue<float> smoothedLpQ_   { 0.707f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolPingPongDelay)
};
