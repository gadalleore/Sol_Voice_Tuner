/*
    OscilloscopeComponent.h
    -----------------------
    Time-domain L/R waveform display. Refreshed from the editor's 30 Hz timer
    with a snapshot of the processor's output buffer.
*/

#pragma once

#include <JuceHeader.h>

class OscilloscopeComponent : public juce::Component
{
public:
    OscilloscopeComponent() = default;

    void paint (juce::Graphics& g) override;
    void resized() override {}

    /** Copy a fresh audio snapshot. `validSamples` <= 0 means use full size. */
    void update (const juce::AudioBuffer<float>& source, int validSamples = -1);

private:
    void drawBackground (juce::Graphics& g);

    juce::AudioBuffer<float> internalBuffer;

    juce::Colour bgColour     { 0xff1a1a2f };  // SolLookAndFeel kPanel
    juce::Colour gridColour   { 0xff3a3a5f };  // kOutline
    juce::Colour traceColour  { 0xff00d4ff };  // kAccentArc
    juce::Colour traceGlow    { 0xff00b4ff };  // kAccentGlow
};
