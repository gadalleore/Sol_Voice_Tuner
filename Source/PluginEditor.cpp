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
    basePlate.setStrokeWidth (kPlateStroke);   // a drawn edge, not a hairline
    basePlate.setPadding     (32.0f);
    // Solid black (Gard, 2026-07-31): the grey read as tentative next to the
    // ring and the meters, which are both full-strength ink. The close X tracks
    // this colour too, so it goes black with the border.
    basePlate.setOutlineColour (juce::Colour (SolLookAndFeel::kTitleHi));
    basePlate.setContentBleedsLeft (true);  // the wheel runs to the window edge
    // No wordmark. The product carries no name on its face — the interface is
    // the identity (Gard, 2026-07-28).
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
    wheel.setRingScale (0.62f);       // tightened to match the smaller orb
    wheel.setRingThickness (kPlateStroke);   // same line as the border
    wheel.setOrbScale (0.86f);        // 30% up on 0.60, then 10% again
    wheel.setOrbOffsetRatio (0.55f);  // scaled with the orb, so the framing the
                                      // smaller orb had is preserved — without
                                      // it the orb hangs off the left of the plate
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
    hubScope.setTraceColour (juce::Colour (SolLookAndFeel::kTitleHi));
    hubScope.setTraceAlpha (0.85f);
    hubScope.setTraceThickness (1.1f);
    wheel.setHubContent (&hubScope);

    basePlate.setContent (&wheel);

    // The wheel's labels survive the analyser's bars, knocked out white where
    // a bar crosses them. Registered here rather than in the plate because the
    // wheel is the plate's content, handed in from outside.
    basePlate.getSpectrum().addInkable (&wheel, &wheel);

    basePlate.getSpectrum().fillSamples = [this] (float* dest, int numSamples)
    {
        processorRef.readSpectrumHistory (dest, numSamples);
    };

    shell.setContent (&basePlate);

    // Not setSize: this fixes the design size AND locks the corner to uniform
    // scaling, so the window can be zoomed but never reshaped.
    shell.setLogicalSize (kShellWidth, kShellHeight);
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
    // The stub carries no name either — just a mark. Dimmed while the UI is
    // up, brighter when it has been dismissed, since then it is the only way
    // back and needs to invite a click.
    g.fillAll (juce::Colour (SolLookAndFeel::kBackground));

    const bool showing = shell.isOnDesktop();
    const auto centre  = getLocalBounds().toFloat().getCentre();

    g.setColour (juce::Colour (SolLookAndFeel::kOutlineHi)
                     .withAlpha (showing ? 0.30f : 0.85f));

    juce::Rectangle<float> mark (kStubMarkSize, kStubMarkSize);
    g.drawRect (mark.withCentre (centre), 1.2f);
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
    if (! shell.isOnDesktop())
        return;

    hubScope.update (processorRef.getScopeBuffer(),
                     processorRef.getScopeValidSamples());

    // One reader only: getAndClearMeterPeak() consumes the accumulated peak,
    // so a second poller anywhere would halve what the meters see. The shake
    // rides on the same two numbers rather than taking a second read.
    const float peakL = processorRef.getAndClearMeterPeak (0);
    const float peakR = processorRef.getAndClearMeterPeak (1);

    basePlate.getMeters().push (peakL, peakR);
    driveShake (juce::jmax (peakL, peakR));

    auto& spectrum = basePlate.getSpectrum();
    spectrum.setSampleRate (processorRef.getSampleRate());
    spectrum.pull();
}

void PitchCorrectorAudioProcessorEditor::driveShake (float peak)
{
    // Hit hard, fall away: the window should snap on a transient and drift
    // back, not wobble along behind the average level.
    shakeLevel = juce::jmax (juce::jlimit (0.0f, 1.0f, peak),
                             shakeLevel * kShakeRelease);

    // Curved so quiet passages barely register and loud ones throw it about —
    // a linear map spends most of its range on a permanent low-level jitter.
    const float amp = kShakeMax * std::pow (shakeLevel, kShakeCurve);

    const auto previous = shakeOffset;

    if (amp < kShakeFloor)
    {
        shakeOffset = {};
    }
    else
    {
        // A fresh direction every frame. Anything smoothed reads as a wobble;
        // the whole point is that it looks struck.
        const float angle = shakeRng.nextFloat() * juce::MathConstants<float>::twoPi;

        shakeOffset = { juce::roundToInt (std::cos (angle) * amp),
                        juce::roundToInt (std::sin (angle) * amp) };
    }

    shell.setShakeOffset (shakeOffset);

    // The plate has just been thrown from `previous` to `shakeOffset`, so the
    // smear trails back the other way — same sign convention the wheel's own
    // motion trails use.
    wheel.setShakeMotion ((previous - shakeOffset).toFloat());
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

    // Home, not just position: the next shake frame is measured from this.
    shell.setHomePosition (target.getPosition());
}
