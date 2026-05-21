/*
    PitchDetector.h
    ---------------
    Real-time monophonic pitch detection using the YIN algorithm
    (de Cheveigné & Kawahara, 2002), with parabolic interpolation
    for sub-sample accuracy and an RMS gate to suppress unvoiced
    output.

    Inspired by adamski/pitch_detector and DamRsn/VocoderProject:
        - Use a sliding analysis ring-buffer.
        - Run YIN on overlapping windows of `analysisSize` samples.
        - Hop by `hopSize` samples (controls update rate).
        - Cache previous result if signal is too quiet / unvoiced.

    The detector is intentionally lock-free and allocation-free on
    the audio thread once `prepare()` has been called.
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>

class PitchDetector
{
public:
    PitchDetector() = default;

    /** Set up internal buffers. Call this on prepareToPlay. */
    void prepare (double sampleRate, int analysisSize = 2048, int hopSize = 256);

    /** Push one mono sample into the analysis ring-buffer.
        When enough new samples have arrived (>= hopSize), runs YIN
        and updates the latest pitch estimate. */
    void pushSample (float sample) noexcept;

    /** Push an entire block (mono). */
    void pushBlock (const float* data, int numSamples) noexcept;

    /** Returns the most recent pitch estimate in Hz.
        Returns 0.0f when the signal is too quiet / unvoiced. */
    float getPitchHz() const noexcept    { return latestPitchHz.load (std::memory_order_relaxed); }

    /** Returns the YIN clarity / confidence (0..1). Lower YIN dip = higher
        confidence. We expose 1 - dip so 1.0 means "very confident". */
    float getConfidence() const noexcept { return latestConfidence.load (std::memory_order_relaxed); }

    /** RMS gate threshold. Below this, the detector returns 0 Hz. */
    void  setSilenceThreshold (float rms) noexcept { silenceThreshold = rms; }

    /** Hard frequency limits used by YIN search. */
    void  setFrequencyRange (float minHz, float maxHz) noexcept;

private:
    void runYin();

    double sampleRate { 44100.0 };
    int    analysisSize { 2048 };
    int    hopSize      { 256 };
    int    samplesUntilNextAnalysis { 0 };

    int    minTau { 0 };
    int    maxTau { 0 };

    float  silenceThreshold { 0.005f };

    // Ring buffer of input samples
    std::vector<float> ring;
    int    writeIndex { 0 };

    // Working buffers (avoid allocations on the audio thread)
    std::vector<float> window;
    std::vector<float> diff;            // YIN difference function
    std::vector<float> cumMeanNorm;     // YIN cumulative mean normalised diff

    // Latest result, written by runYin() (audio thread), read by GUI
    std::atomic<float> latestPitchHz   { 0.0f };
    std::atomic<float> latestConfidence{ 0.0f };

    float lastGoodTau { 0.0f };
    float smoothedPitch { 0.0f };
};
