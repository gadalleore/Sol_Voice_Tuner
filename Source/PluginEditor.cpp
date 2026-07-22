/*
    PluginEditor.cpp
*/

#include "PluginEditor.h"

PitchCorrectorAudioProcessorEditor::PitchCorrectorAudioProcessorEditor (
    PitchCorrectorAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    homePage.onInputFx   = [this] { pageStack.push (inputFxPage); };
    homePage.onHarmonies = [this] { pageStack.push (harmoniesPage); };
    homePage.onOutputFx  = [this] { pageStack.push (outputFxPage); };

    harmoniesPage.onOpenTuning = [this] { pageStack.push (tuningPage); };

    addAndMakeVisible (pageStack);
    pageStack.setRootPage (homePage);

    setResizable (true, true);
    setResizeLimits (720, 380, 1200, 760);
    setSize (800, 460);
}

PitchCorrectorAudioProcessorEditor::~PitchCorrectorAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void PitchCorrectorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (lookAndFeel.getBackground());
}

void PitchCorrectorAudioProcessorEditor::resized()
{
    pageStack.setBounds (getLocalBounds());
}
