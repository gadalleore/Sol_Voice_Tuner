/*
    PitchShifter.h
    --------------
    Thin wrapper around `signalsmith::stretch::SignalsmithStretch<float>`
    that exposes a JUCE-friendly block-processing API, independent
    pitch transpose + formant (semitone) shift, and latency reporting.

    Formant shifting uses `setFormantSemitones(..., compensatePitch=true)`
    so timbre moves separately from the pitch ratio (RubberBand-style split).
*/

#pragma once

#include <JuceHeader.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>

class PitchShifter
{
public:
    PitchShifter() = default;

    /** Configure for the current host buffer size & sample rate. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Reset internal state (e.g. after host transport jump). */
    void reset();

    /** Set pitch ratio: 1.0 = no shift, 2.0 = +1 octave, 0.5 = -1 octave. */
    void setPitchRatio (float ratio) noexcept;

    /** Independent formant shift in semitones (negative = deeper, positive = brighter). */
    void setFormantSemitones (float semitones) noexcept;

    /** Process one block (in-place safe; we copy to internal buffer). */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Latency in samples that the plugin should report to the host. */
    int  getLatencySamples() const noexcept { return reportedLatency; }

private:
    void applyStretchConfig() noexcept;

    double sampleRate     { 44100.0 };
    int    maxBlockSize   { 512 };
    int    numChannels    { 2 };
    int    reportedLatency{ 0 };

    float  pitchRatio       { 1.0f };
    float  formantSemitones { 0.0f };
    float  tonalityLimit    { 0.0f };

    signalsmith::stretch::SignalsmithStretch<float> stretch;

    juce::AudioBuffer<float>      scratchOut;
    std::vector<float*>           inPtrs, outPtrs;
};
