/*
    OscilloscopeComponent.h
    -----------------------
    Time-domain L/R waveform display. Refreshed from the editor's 30 Hz timer
    with a snapshot of the processor's output buffer.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

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

    // Sun-white (Giuseppe, 2026-07-28): these carried the pre-rebrand cosmic
    // navy/cyan palette as hardcoded hex, unlike everywhere else that reads
    // named SolLookAndFeel constants. Black ink trace, matching the hub's
    // goniometer (PluginEditor.cpp sets hubScope's trace to kTitleHi too).
    juce::Colour bgColour     { SolLookAndFeel::kPanel };
    juce::Colour gridColour   { SolLookAndFeel::kOutline };
    juce::Colour traceColour  { SolLookAndFeel::kTitleHi };
};
