/*
    PitchDetector.cpp
    -----------------
    YIN pitch detector implementation. See header for design notes.
*/

#include "PitchDetector.h"

void PitchDetector::prepare (double sr, int analysis, int hop)
{
    sampleRate    = sr;
    analysisSize  = juce::jmax (256, analysis);
    hopSize       = juce::jlimit (16, analysisSize / 2, hop);

    ring.assign        ((size_t) analysisSize, 0.0f);
    window.assign      ((size_t) analysisSize, 0.0f);
    diff.assign        ((size_t) analysisSize / 2, 0.0f);
    cumMeanNorm.assign ((size_t) analysisSize / 2, 0.0f);

    writeIndex                 = 0;
    samplesUntilNextAnalysis   = analysisSize; // wait until first full window

    setFrequencyRange (50.0f, 1500.0f);

    latestPitchHz.store (0.0f);
    latestConfidence.store (0.0f);
}

void PitchDetector::setFrequencyRange (float minHz, float maxHz) noexcept
{
    if (sampleRate <= 0.0)
        return;

    // tau = sampleRate / freq, so smaller tau = higher freq
    minTau = juce::jmax (2,                 (int) std::floor (sampleRate / (double) maxHz));
    maxTau = juce::jmin ((int) diff.size() - 1, (int) std::ceil  (sampleRate / (double) minHz));
    minTau = juce::jmin (minTau, maxTau - 1);
}

void PitchDetector::pushSample (float s) noexcept
{
    ring[(size_t) writeIndex] = s;
    if (++writeIndex >= analysisSize)
        writeIndex = 0;

    if (--samplesUntilNextAnalysis <= 0)
    {
        samplesUntilNextAnalysis = hopSize;
        runYin();
    }
}

void PitchDetector::pushBlock (const float* data, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        pushSample (data[i]);
}

void PitchDetector::runYin()
{
    // Linearise the ring buffer into `window` so YIN sees contiguous samples
    const int W = analysisSize;
    int idx = writeIndex;
    for (int i = 0; i < W; ++i)
    {
        window[(size_t) i] = ring[(size_t) idx];
        if (++idx >= W) idx = 0;
    }

    // Quick RMS gate -- skip computation if the signal is essentially silent
    double sumSq = 0.0;
    for (int i = 0; i < W; ++i)
        sumSq += (double) window[(size_t) i] * window[(size_t) i];

    const float rms = (float) std::sqrt (sumSq / (double) W);
    if (rms < silenceThreshold)
    {
        latestConfidence.store (0.0f);
        // keep last pitch (don't zero so the smoothed shifter ratio glides)
        return;
    }

    const int H = W / 2;

    // Step 1: difference function d(tau) = sum_i (x[i] - x[i+tau])^2
    for (int tau = 1; tau < H; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < H; ++i)
        {
            const float delta = window[(size_t) i] - window[(size_t) (i + tau)];
            sum += (double) delta * delta;
        }
        diff[(size_t) tau] = (float) sum;
    }
    diff[0] = 1.0f;

    // Step 2: cumulative mean normalised difference function
    cumMeanNorm[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau < H; ++tau)
    {
        running += diff[(size_t) tau];
        cumMeanNorm[(size_t) tau] = (float) (diff[(size_t) tau] * tau / running);
    }

    // Step 3: absolute threshold -- pick first dip below 0.1, then refine
    constexpr float yinThreshold = 0.10f;
    int    tauEstimate = -1;
    float  minVal      = 1.0f;
    int    minTauIdx   = -1;

    for (int tau = juce::jmax (2, minTau); tau <= maxTau; ++tau)
    {
        if (cumMeanNorm[(size_t) tau] < minVal)
        {
            minVal    = cumMeanNorm[(size_t) tau];
            minTauIdx = tau;
        }

        if (cumMeanNorm[(size_t) tau] < yinThreshold)
        {
            // descend to local minimum
            while (tau + 1 <= maxTau
                   && cumMeanNorm[(size_t) (tau + 1)] < cumMeanNorm[(size_t) tau])
                ++tau;

            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0)
    {
        // No clear voiced pitch -- fall back to global min if confident enough
        if (minVal < 0.4f && minTauIdx > 0)
            tauEstimate = minTauIdx;
        else
        {
            latestConfidence.store (0.0f);
            return;
        }
    }

    // Step 4: parabolic interpolation around tauEstimate for sub-sample accuracy
    float betterTau = (float) tauEstimate;
    if (tauEstimate > 1 && tauEstimate < H - 1)
    {
        const float s0 = cumMeanNorm[(size_t) (tauEstimate - 1)];
        const float s1 = cumMeanNorm[(size_t)  tauEstimate];
        const float s2 = cumMeanNorm[(size_t) (tauEstimate + 1)];
        const float denom = (s0 + s2 - 2.0f * s1);
        if (std::abs (denom) > 1.0e-9f)
            betterTau = (float) tauEstimate + 0.5f * (s0 - s2) / denom;
    }

    if (betterTau <= 0.0f)
    {
        latestConfidence.store (0.0f);
        return;
    }

    // === ENHANCEMENT: History bias + light smoothing to kill octave jumps and jitter ===
    if (tauEstimate > 0 && minVal < 0.35f) {  // only apply bias on decent confidence
        const float prevTau = lastGoodTau > 0.0f ? lastGoodTau : betterTau;
        const float tauBias = 0.18f;  // tuned for vocals
        if (std::abs(betterTau - prevTau) > 0.5f * prevTau) {  // likely octave jump
            betterTau = prevTau * (1.0f - tauBias) + betterTau * tauBias;
        }
        lastGoodTau = betterTau;
    }

    const float pitch = (float) (sampleRate / (double) betterTau);

    // Light exponential smoothing on final pitch (prevents jitter without lag)
    const float alphaDetect = 0.68f;
    smoothedPitch = alphaDetect * pitch + (1.0f - alphaDetect) * smoothedPitch;

    latestPitchHz.store    (smoothedPitch);
    latestConfidence.store (juce::jlimit (0.0f, 1.0f, 1.0f - minVal));
}
