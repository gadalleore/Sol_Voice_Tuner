/*
    OscilloscopeComponent.cpp
*/

#include "OscilloscopeComponent.h"

void OscilloscopeComponent::drawBackground (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (bgColour);
    g.fillRoundedRectangle (r, 8.0f);

    g.setColour (gridColour.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    // Horizontal mid-lines for L (upper half) and R (lower half)
    const float h = (float) getHeight();
    g.setColour (gridColour.withAlpha (0.45f));
    g.drawHorizontalLine ((int) (h * 0.25f), 8.0f, (float) getWidth() - 8.0f);
    g.drawHorizontalLine ((int) (h * 0.75f), 8.0f, (float) getWidth() - 8.0f);

    g.setColour (gridColour.withAlpha (0.75f));
    g.drawHorizontalLine ((int) (h * 0.5f),  8.0f, (float) getWidth() - 8.0f);
}

void OscilloscopeComponent::paint (juce::Graphics& g)
{
    drawBackground (g);

    const int numSamples = internalBuffer.getNumSamples();
    const int numCh      = internalBuffer.getNumChannels();
    if (numSamples < 2 || numCh < 1)
        return;

    const float w  = (float) getWidth();
    const float h  = (float) getHeight();
    const float cy = h * 0.5f;
    const float halfH = (h - 20.0f) * 0.5f;

    for (int ch = 0; ch < juce::jmin (2, numCh); ++ch)
    {
        const float yBase  = (ch == 0) ? (cy - halfH * 0.5f) : (cy + halfH * 0.5f);
        const float yScale = halfH * 0.4f;

        juce::Path path;
        const auto* data = internalBuffer.getReadPointer (ch);
        const float invMax = 1.0f / (float) (numSamples - 1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = juce::jmap ((float) i * invMax, 10.0f, w - 10.0f);
            const float y = yBase - juce::jlimit (-1.0f, 1.0f, data[i]) * yScale;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        // Soft glow underneath, sharp trace on top
        g.setColour (traceGlow.withAlpha (0.35f));
        g.strokePath (path, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour (traceColour);
        g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

void OscilloscopeComponent::update (const juce::AudioBuffer<float>& source, int validSamples)
{
    const int numCh = juce::jmin (2, source.getNumChannels());
    const int numS  = (validSamples > 0) ? juce::jmin (validSamples, source.getNumSamples())
                                         : source.getNumSamples();
    if (numCh < 1 || numS <= 0)
        return;

    if (internalBuffer.getNumChannels() != numCh || internalBuffer.getNumSamples() != numS)
        internalBuffer.setSize (numCh, numS, false, true, true);

    internalBuffer.clear();
    for (int ch = 0; ch < numCh; ++ch)
        internalBuffer.copyFrom (ch, 0, source, ch, 0, numS);

    repaint();   // 63C-18: the sidebar timer drives update(); without this the trace froze
}
