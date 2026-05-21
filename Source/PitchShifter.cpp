/*
    PitchShifter.cpp
*/

#include "PitchShifter.h"

void PitchShifter::prepare (double sr, int blockSize, int channels)
{
    sampleRate    = sr;
    maxBlockSize  = juce::jmax (32,  blockSize);
    numChannels   = juce::jmax (1, channels);

    // Vocal-optimized: tighter grains for faster response + less smearing
    const int block    = juce::jmax (256, (int) (sampleRate * 0.055));
    const int interval = juce::jmax (64,  (int) (sampleRate * 0.014));
    stretch.configure (numChannels, block, interval, false);

    // Tonality limit preserves vocal timbre (huge quality boost)
    tonalityLimit = 3800.0f / (float) sampleRate;
    stretch.reset();

    reportedLatency = stretch.inputLatency() + stretch.outputLatency();

    scratchOut.setSize (numChannels, maxBlockSize, false, true, true);

    inPtrs .assign ((size_t) numChannels, nullptr);
    outPtrs.assign ((size_t) numChannels, nullptr);

    applyStretchConfig();
}

void PitchShifter::reset()
{
    stretch.reset();
    scratchOut.clear();
}

void PitchShifter::setPitchRatio (float ratio) noexcept
{
    pitchRatio = juce::jlimit (0.25f, 4.0f, ratio);
    applyStretchConfig();
}

void PitchShifter::setFormantSemitones (float semitones) noexcept
{
    formantSemitones = juce::jlimit (-24.0f, 24.0f, semitones);
    applyStretchConfig();
}

void PitchShifter::applyStretchConfig() noexcept
{
    stretch.setTransposeFactor (pitchRatio, tonalityLimit);
    // `true` keeps user formant offset independent of the current transpose factor.
    stretch.setFormantSemitones (formantSemitones, true);
    stretch.setFormantBase (200.0f / (float) sampleRate);
}

void PitchShifter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int blockChannels = juce::jmin (numChannels, buffer.getNumChannels());
    const int blockSamples  = buffer.getNumSamples();

    if (blockSamples <= 0 || blockChannels <= 0)
        return;

    if (scratchOut.getNumSamples() < blockSamples
        || scratchOut.getNumChannels() < blockChannels)
        scratchOut.setSize (blockChannels, blockSamples, false, false, true);

    for (int c = 0; c < blockChannels; ++c)
    {
        inPtrs[(size_t) c]  = buffer.getWritePointer (c);
        outPtrs[(size_t) c] = scratchOut.getWritePointer (c);
    }

    stretch.process (inPtrs.data(),  blockSamples,
                     outPtrs.data(), blockSamples);

    for (int c = 0; c < blockChannels; ++c)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (c),
                                           scratchOut.getReadPointer (c),
                                           blockSamples);
}
