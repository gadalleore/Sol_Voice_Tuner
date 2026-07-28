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

    // The shell is its own desktop window, NOT a child of this editor, so it
    // does not inherit the editor's LookAndFeel. (Font *resolution* still goes
    // through the global default LookAndFeel, which is why every call site
    // names SolLookAndFeel::kBrandTypeface explicitly.)
    shell.setLookAndFeel (&lookAndFeel);

    // ── Panel-by-panel rebuild (2026-07-27) ──────────────────────────────
    // The base plate (63C-48) is the root view while the UI is rebuilt one
    // panel at a time. The v3 paging UI below is intact and still compiled —
    // it is simply not attached. To bring it back, restore this block and
    // parent it to the shell instead of the plate:
    //
    //     homePage.onInputFx   = [this] { pageStack.push (inputFxPage); };
    //     homePage.onHarmonies = [this] { pageStack.push (harmoniesPage); };
    //     homePage.onOutputFx  = [this] { pageStack.push (outputFxPage); };
    //     harmoniesPage.onOpenTuning = [this] { pageStack.push (tuningPage); };
    //     pageStack.setRootPage (homePage);

    // The chamfer is back: in our own layered window the cut corner genuinely
    // shows through, which it could not do inside the host's opaque rect.
    // Set this to 0 for a square plate.
    basePlate.setChamfer     (kChamfer);
    basePlate.setStrokeWidth (5.0f);
    basePlate.setPadding     (32.0f);
    basePlate.setWatermark   ("Sol Voice Tuner");
    basePlate.setPanelFill   (juce::Colour (SolLookAndFeel::kBackground));

    // The X in the chamfer closes the whole thing.
    //
    // Standalone: quit the application outright — the shell IS the app window.
    // Plugin: a plugin cannot close the host's window, so the best available
    // equivalent is to dismiss the UI; clicking the host's stub restores it.
    basePlate.onClose = [this]
    {
        if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
            {
                app->systemRequestedQuit();
                return;
            }
        }

        shell.hideFromDesktop();
        repaint();
    };

    // The Home wheel, lifted onto the plate. The wheel is a pure view — slot
    // state comes back through these hooks — so re-hosting it is just a matter
    // of supplying the model. Drill-in targets are stubbed until the panels
    // they open exist.
    wheel.setNumSlots (3);
    wheel.setPillSize (400.0f, 54.0f);
    wheel.setItemFontHeight (28.0f);
    wheel.setRingScale (0.84f);     // arcs reach out close under the labels
    wheel.emptyTypeId    = -1;      // every slot is always occupied
    wheel.itemsDraggable = false;   // Home items are fixed drill-ins

    wheel.getSlotType  = [] (int slot) { return slot; };
    wheel.nameProvider = [] (int typeId) -> juce::String
    {
        switch (typeId)
        {
            case 0:  return "Input Global Effects";
            case 1:  return "Harmonies / Tuning";
            case 2:  return "Output Global Effects";
            default: return {};
        }
    };
    wheel.onSlotClicked = [] (int) { /* panels land here as they are built */ };

    // Black goniometer trace in the wheel's hub, fed from the processor's
    // scope snapshot — the panel's first live element.
    hubScope.setTraceColour (juce::Colours::black);
    hubScope.setTraceAlpha (0.8f);
    hubScope.setTraceThickness (1.1f);
    wheel.setHubContent (&hubScope);

    basePlate.setContent (&wheel);

    shell.setContent (&basePlate);
    shell.setSize (kShellWidth, kShellHeight);
    shell.showOnDesktop();
    placeShellNearStub();

    // The host only ever sees this stub.
    setSize (kStubWidth, kStubHeight);

    startTimerHz (kScopeFps);
}

PitchCorrectorAudioProcessorEditor::~PitchCorrectorAudioProcessorEditor()
{
    stopTimer();

    // Order matters: drop the content before the shell leaves the desktop, and
    // never leave a desktop window behind when the host closes the editor.
    basePlate.onClose = nullptr;
    wheel.setHubContent (nullptr);
    shell.setContent (nullptr);
    shell.hideFromDesktop();
    shell.setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void PitchCorrectorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface,
                                              12.5f, juce::Font::plain)));
    g.drawText (shell.isOnDesktop() ? "Sol Voice Tuner"
                                    : "Sol Voice Tuner  -  click to show",
                getLocalBounds(), juce::Justification::centred, false);
}

void PitchCorrectorAudioProcessorEditor::resized()
{
    // Nothing to lay out — the stub is just a name plate.
    //
    // The shell is deliberately NOT repositioned here. It is a window in its
    // own right: moving the host's plugin window must leave the UI where the
    // user put it (Gard, 2026-07-28).
}

void PitchCorrectorAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    // Bring the UI back after the chamfer X has dismissed it.
    if (! shell.isOnDesktop())
    {
        shell.showOnDesktop();
        placeShellNearStub();
        repaint();
    }
}

void PitchCorrectorAudioProcessorEditor::timerCallback()
{
    if (shell.isOnDesktop())
        hubScope.update (processorRef.getScopeBuffer(),
                         processorRef.getScopeValidSamples());
}

void PitchCorrectorAudioProcessorEditor::placeShellNearStub()
{
    const auto stub = getScreenBounds();

    juce::Rectangle<int> target (stub.getX(), stub.getBottom() + 10,
                                 shell.getWidth(), shell.getHeight());

    // Keep the whole window on screen. Without this the shell can be pushed
    // off the left or bottom edge by wherever the host put its plugin window,
    // and the plate gets clipped.
    if (auto* display = juce::Desktop::getInstance().getDisplays()
                            .getDisplayForRect (stub, true))
        target = target.constrainedWithin (display->userArea);

    shell.setTopLeftPosition (target.getPosition());
}
