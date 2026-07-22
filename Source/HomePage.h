/*
    HomePage.h
    ----------
    Window 1 of the paging UI (63C-6): the Home wheel. Signal flows top to
    bottom — Input Global Effects at the top, Harmonies/Tuning in the middle,
    Output Global Effects at the bottom — arranged along a wheel arc per
    Gard's sketch. Placeholder vector shapes; real wheel visuals arrive with
    63C-11/63C-9.
*/

#pragma once

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class HomePage final : public juce::Component
{
public:
    std::function<void()> onInputFx, onHarmonies, onOutputFx;

    HomePage()
    {
        styleEntry (inputBtn,     [this] { if (onInputFx)   onInputFx();   });
        styleEntry (harmoniesBtn, [this] { if (onHarmonies) onHarmonies(); });
        styleEntry (outputBtn,    [this] { if (onOutputFx)  onOutputFx();  });

        styleCaption (inCaption,  "INPUT");
        styleCaption (outCaption, "OUTPUT");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 8);

        inCaption .setBounds (r.removeFromTop (18));
        outCaption.setBounds (r.removeFromBottom (18));

        wheelBounds = r.toFloat();

        // Three entries stacked on the wheel arc: top / centre / bottom.
        const int entryW = juce::jmin (280, r.getWidth() - 40);
        const int entryH = 44;
        const int cx     = r.getCentreX();

        inputBtn    .setBounds (cx - entryW / 2, r.getY() + 8,                  entryW, entryH);
        harmoniesBtn.setBounds (cx - entryW / 2, r.getCentreY() - entryH / 2,   entryW, entryH);
        outputBtn   .setBounds (cx - entryW / 2, r.getBottom() - entryH - 8,    entryW, entryH);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

        // Placeholder wheel: an arc bulging in from the left, like the sketch.
        auto b = wheelBounds;
        if (b.isEmpty())
            return;

        juce::Path arc;
        const float radius = b.getHeight() * 0.72f;
        arc.addCentredArc (b.getX() - radius * 0.35f, b.getCentreY(),
                           radius, radius, 0.0f,
                           juce::MathConstants<float>::pi * 0.25f,
                           juce::MathConstants<float>::pi * 0.75f,
                           true);

        g.setColour (juce::Colour (SolLookAndFeel::kAccentGlow).withAlpha (0.4f));
        g.strokePath (arc, juce::PathStrokeType (3.0f));
    }

private:
    void styleEntry (juce::TextButton& b, std::function<void()> fn)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (SolLookAndFeel::kPanel));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (SolLookAndFeel::kTitleHi));
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    }

    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId,
                     juce::Colour (SolLookAndFeel::kLabelAlt).withAlpha (0.6f));
        addAndMakeVisible (l);
    }

    juce::TextButton inputBtn     { "Input Global Effects" };
    juce::TextButton harmoniesBtn { "Harmonies / Tuning" };
    juce::TextButton outputBtn    { "Output Global Effects" };
    juce::Label      inCaption, outCaption;
    juce::Rectangle<float> wheelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HomePage)
};
