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
    // panel at a time, and the Home WHEEL — not HomePage — is what sits on it.
    // So the drilled-in pages hang off the plate rather than off a stack that
    // owns Home too: openPage() swaps the plate's content from the wheel to the
    // stack, and back is the reverse (see onPopFromRoot below). Source/HomePage.h
    // is the older full-page Home, kept for the parts not yet rebuilt as panels.

    // Back on the ROOT page means "leave the stack entirely". This fires from
    // inside the back bar's mouse callback, which is why the plate is not
    // re-parented until the message loop comes round again.
    pageStack.onPopFromRoot = [safeThis = juce::Component::SafePointer<PitchCorrectorAudioProcessorEditor> (this)]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->showHome();
        });
    };

    // The chamfer is back: in our own layered window the cut corner genuinely
    // shows through, which it could not do inside the host's opaque rect.
    // Set this to 0 for a square plate.
    basePlate.setChamfer     (kChamfer);
    basePlate.setStrokeWidth (kPlateStroke);   // a drawn edge, not a hairline
    basePlate.setPadding     (32.0f);
    // The plate's silhouette against whatever is behind the layered window.
    // Not full-strength ink on the night panel (2026-08-22): at kPlateStroke
    // a near-white edge is the brightest thing on screen and frames the UI
    // harder than anything inside it. A lifted grey states the shape and
    // stops there. The close X tracks this colour too.
    basePlate.setOutlineColour (juce::Colour (SolLookAndFeel::kOutlineHi));
    // Content stays INSIDE the frame (2026-08-22). This used to bleed left so
    // the half-wheel — whose centre sits on its own left edge — could run off
    // the panel. What that actually produced was the orb ring and the rim
    // labels crossing the plate's border, which reads as a rendering fault
    // rather than a design: a bezel that things pass through is not a bezel.
    // Every professional plugin frame is unbroken, so the wheel is inset with
    // everything else and its geometry follows.
    basePlate.setContentBleedsLeft (false);
    // No wordmark. The product carries no name on its face — the interface is
    // the identity (Giuseppe, 2026-07-28).
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

    // The window follows the page (2026-08-22): a page that implements
    // SizedPage says how much room it wants, and the shell takes that as its
    // new design size, keeping whatever zoom is already applied.
    pageStack.onTopPageChanged = [this] { fitShellToPage(); };

    // The root screen. Its three nav destinations are what the Home wheel's
    // three items used to be.
    inputFxPage.onSizeWanted  = [this] { fitShellToPage(); };
    outputFxPage.onSizeWanted = [this] { fitShellToPage(); };

    // Choosing an effect moves the window in two legs — the panel retracts to
    // the bare ring, then opens out at the new effect's size — and the second
    // leg starts when the first ARRIVES. Both pages are told; the one that
    // isn't mid-selection ignores it.
    shell.onResizeSettled = [this]
    {
        inputFxPage .windowSettled();
        outputFxPage.windowSettled();
    };

    mainPage.onInputFx   = [this] { openPage (inputFxPage);   };
    mainPage.onHarmonies = [this] { openPage (harmoniesPage); };
    mainPage.onOutputFx  = [this] { openPage (outputFxPage);  };

    basePlate.setContent (&mainPage);

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
    shell.setContent (nullptr);
    shell.hideFromDesktop();
    shell.setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void PitchCorrectorAudioProcessorEditor::openPage (juce::Component& page)
{
    // Each drill-in starts a fresh stack rooted at the page that was opened, so
    // the back bar walks that page's own children (an effects wheel -> one
    // effect's detail page) and then leaves for Home.
    pageStack.clear();
    pageStack.setRootPage (page);
    basePlate.setContent (&pageStack);
}

void PitchCorrectorAudioProcessorEditor::showHome()
{
    basePlate.setContent (&mainPage);
    pageStack.clear();

    // Back to the root screen's own size — clear() detaches every page without
    // firing onTopPageChanged, since there is no new top page to report.
    shell.setLogicalSize (kShellWidth, kShellHeight);
}

void PitchCorrectorAudioProcessorEditor::fitShellToPage()
{
    auto* top = pageStack.getTopPage();

    // A page with no opinion gets the base size, so leaving one that asked for
    // something bigger always returns the window rather than stranding it.
    auto want = juce::Point<int> (kShellWidth, kShellHeight);

    if (auto* sized = dynamic_cast<SizedPage*> (top))
        want = sized->preferredLogicalSize();

    shell.setLogicalSize (want.x, want.y);
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
    // user put it (Giuseppe, 2026-07-28).
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

    // The root screen's pitch display rides this clock too, rather than
    // owning a timer of its own — one clock for the whole UI.
    mainPage.tick();

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

    juce::ignoreUnused (previous);

    shell.setShakeOffset (shakeOffset);

    // The plate is still thrown by the audio, but nothing on the root screen
    // subscribes to the throw vector any more: the Home wheel's labels were
    // what smeared, and the wheel is no longer the root (2026-08-22). The FX
    // wheels still smear on their own pages, from their own motion. If a
    // future root element wants the throw, hand it (previous - shakeOffset).
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
