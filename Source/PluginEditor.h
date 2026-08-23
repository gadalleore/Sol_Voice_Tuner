/*
    PluginEditor.h
    --------------
    Sol uses a plugin-owned floating window (63C-50), so this editor is only a
    small stub: the host gets a name plate, and the real UI lives in
    `FloatingShell` — a layered desktop window where alpha works and the plate
    can have a non-rectangular silhouette.

    The v3 paging UI (63C-6) is still compiled but detached while the interface
    is rebuilt panel by panel; see the commented block in the constructor.
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "SolLookAndFeel.h"
#include "ChamferPanel.h"
#include "FloatingShell.h"
#include "LissajousDisplay.h"
#include "PageStack.h"
#include "HomePage.h"
#include "MainPage.h"
#include "EffectsWindowPage.h"
#include "HarmoniesWindowPage.h"
#include "EdgeMeters.h"
#include "VolumeArc.h"
#include "BrandMark.h"
#include "MonoToggle.h"
#include "SpectrumStrip.h"

class PitchCorrectorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit PitchCorrectorAudioProcessorEditor (PitchCorrectorAudioProcessor&);
    ~PitchCorrectorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** One line weight for the whole face (Giuseppe, 2026-07-31): the plate's
        border, the ring around the Lissajous and the volume arc are all drawn
        at this, so the interface reads as one hand. The plate insets its path
        by half of this, so the full width is visible rather than half-clipped
        at the window edge — which is why the other two can match it directly. */
    static constexpr float kPlateStroke = 5.0f;

    //==========================================================================
    /** The base plate with the right-hand column built into it: volume over
        metering.

        Both are fixtures of the window, not of whatever is on it, so they hang
        off the plate rather than off the wheel or a page — they stay put
        through every drill-in. ChamferPanel's own `content` still carries the
        page; this only adds siblings beside it, laid out in the margin the
        content already keeps clear on the right. */
    class MeteredPlate final : public ChamferPanel
    {
    public:
        explicit MeteredPlate (juce::AudioProcessorValueTreeState& state)
        {
            addAndMakeVisible (meters);
            addAndMakeVisible (volume);
            addAndMakeVisible (inputMono);
            addAndMakeVisible (mark);
            addAndMakeVisible (spectrum);

            // What the bars knock out rather than bury. The wheel is the
            // plate's content, not ours, so the editor registers that one.
            spectrum.addInkable (&mark,   &mark);
            spectrum.addInkable (&meters, &meters);

            volume.setStrokeWeight (kPlateStroke);

            volumeAtt = std::make_unique<
                juce::AudioProcessorValueTreeState::SliderAttachment> (
                    state, PitchCorrectorAudioProcessor::PID_VOLUME, volume);

            monoAtt = std::make_unique<
                juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    state, PitchCorrectorAudioProcessor::PID_INPUT_MONO, inputMono);
        }

        EdgeMeters&    getMeters()   noexcept { return meters; }
        SpectrumStrip& getSpectrum() noexcept { return spectrum; }

    protected:
        void layoutContent (juce::Rectangle<int>) override
        {
            // Positioned against the plate itself, not the content area: the
            // strip lives in the right margin, outboard of the page.
            auto r = getLocalBounds().reduced (juce::roundToInt (getPadding()));

            // Clear of the sliced corner and the X floating in it — the cut
            // eats the full chamfer height off this edge.
            r = r.withTrimmedTop (juce::roundToInt (getChamfer()));

            // A full bar's width of air between the column and the right
            // border, on top of the plate's own padding (Giuseppe, 2026-07-31).
            r = r.withTrimmedRight (EdgeMeters::kBarWidth);

            auto strip = r.removeFromRight (EdgeMeters::kWidth);
            const int columnRight = strip.getRight();

            const int markW = VolumeArc::kWidth;
            const int markH = juce::roundToInt ((float) markW / BrandMark::kAspect);

            // Volume hangs from the chamfer by the column's own spacing unit,
            // so the top of the stack is framed exactly as the foot of it is
            // (Giuseppe, 2026-07-31). The cut is a diagonal, so the edge it hangs
            // from is measured where the dial's own right side passes under it
            // — not at the chamfer's full height, which is only ever reached in
            // the very corner.
            const int cutY = juce::jmax (0, columnRight
                                              - (getWidth() - juce::roundToInt (getChamfer())));

            // Right-aligned on the strip, but free to be wider than it — a dial
            // needs more room than two bars do. Everything below hangs off this
            // rectangle, so the whole column shares one centre line.
            const auto dial = juce::Rectangle<int> (VolumeArc::kWidth, VolumeArc::kHeight)
                                  .withRightX (columnRight)
                                  .withY (cutY + kColumnGap);
            volume.setBounds (dial);

            // Input Mono, directly under the Volume label. Sized to its own
            // type rather than to the dial — at Volume's point size the words
            // are wider than the column — and centred on the column's axis
            // instead of right-aligned, so the stack still reads as one stack.
            inputMono.setBounds (juce::Rectangle<int> (inputMono.getPreferredWidth(),
                                                       MonoToggle::kHeight)
                                     .withCentre ({ dial.getCentreX(), 0 })
                                     .withY (dial.getBottom() + kStackGap));

            // The mark stands kColumnGap off the foot of the window...
            mark.setBounds (juce::Rectangle<int> (markW, markH)
                                .withRightX  (columnRight)
                                .withBottomY (getHeight() - kColumnGap));

            // ...and the bars fill what is between: tight up under the master
            // group, a full gap clear of the mark. Their height is not a number
            // of its own — it is whatever the spacing rule leaves them.
            const int barsTop = inputMono.getBottom() + kStackGap;

            meters.setBounds (juce::Rectangle<int> (EdgeMeters::kWidth,
                                                    juce::jmax (0, mark.getY() - kColumnGap - barsTop))
                                  .withX (dial.getCentreX() - EdgeMeters::kWidth / 2)
                                  .withY (barsTop));

            // The analyser lies across the whole foot of the plate, inside the
            // border rather than over it — the frame still frames everything.
            const int inset = juce::roundToInt (getStrokeWidth());

            spectrum.setBounds (getLocalBounds()
                                    .reduced (inset, 0)
                                    .withTrimmedBottom (inset)
                                    .removeFromBottom (kSpectrumHeight));

            meters   .toFront (false);
            volume   .toFront (false);
            inputMono.toFront (false);
            mark     .toFront (false);

            // Last: the bars are drawn over the furniture and knock it back
            // out in white, so they have to be painted after all of it.
            spectrum.toFront (false);
        }

    private:
        /** The column's spacing unit (Giuseppe, 2026-07-31): the chamfer down to
            the dial, the bars down to the mark, and the mark down to the foot
            of the window. */
        static constexpr int kColumnGap = 30;

        /** Half that, used inside the master group — arc to label, label to
            toggle, toggle to bars. Tighter spacing is what binds those four
            into one object instead of four things evenly scattered down the
            side of the plate. VolumeArc::kArcLabelGap is the same figure. */
        static constexpr int kStackGap = kColumnGap / 2;

        /** How far up the plate the analyser reaches.

            A footer band, not a wash. At 150 it covered the bottom third of
            the plate, which worked when the pages were sparse — the wheel put
            almost nothing down there — but the main page is a dense control
            surface and every knob in that third would have had bars painted
            over it (or needed its own `Inkable` stencil). Slimmed to a strip
            the layout can simply sit above (2026-08-22). */
        static constexpr int kSpectrumHeight = 44;

        EdgeMeters    meters;
        VolumeArc     volume;
        MonoToggle    inputMono { "Input Mono" };
        BrandMark     mark;
        SpectrumStrip spectrum;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAtt;
    };

    /** The stub is an artefact of the floating-window technique, not UI —
        kept as small as hosts will tolerate. */
    static constexpr int   kStubWidth    = 170;
    static constexpr int   kStubHeight   = 36;
    static constexpr float kStubMarkSize = 11.0f;

    /** Opening size of the floating UI. */
    static constexpr int kShellWidth  = 750;
    static constexpr int kShellHeight = 450;

    /** Size of the sliced corner, in px. Zero gives a square plate. The close
        X is half this, so the cut has to be generous for the X to clear the
        diagonal and float free of the plate. */
    static constexpr float kChamfer = 88.0f;

    /** Refresh rate for the hub's Lissajous and the edge meters, in Hz. Doubled
        from 30 on 2026-07-31: at 30 the meters read as sluggish no matter how
        the ballistics are tuned, because a 33 ms frame is simply too coarse for
        a transient. EdgeMeters' release constants are per frame at THIS rate. */
    static constexpr int kScopeFps = 60;

    //==========================================================================
    // Shake (Giuseppe, 2026-07-31): the window is thrown around by whatever is
    // coming through it, and the plate's dither smears in the direction of the
    // throw. Deliberately overcooked to start with — these four are the dials.
    //==========================================================================
    /** Peak displacement in px at full scale. Walked down from the first pass
        at 28 (Giuseppe, 2026-07-31): halved, then another quarter off. The throw
        was right in character from the start, just too far. */
    static constexpr float kShakeMax     = 10.5f;
    static constexpr float kShakeRelease = 0.72f;   // per frame
    static constexpr float kShakeCurve   = 1.5f;    // >1 keeps quiet passages still
    static constexpr float kShakeFloor   = 0.6f;    // px below which it sits dead still

    void driveShake (float peak);

    /** Drill in from the Home wheel: the plate's content becomes the page
        stack, rooted at `page`. */
    void openPage (juce::Component& page);

    /** Back out of the stack entirely — the plate shows the Home wheel again. */
    void showHome();

    /** Resize the shell to whatever the visible page asks for (SizedPage). */
    void fitShellToPage();

    /** Only used when the shell first appears or is re-shown — never while the
        host moves its plugin window. */
    void placeShellNearStub();

    void timerCallback() override;

    PitchCorrectorAudioProcessor& processorRef;
    SolLookAndFeel lookAndFeel;

    juce::Random     shakeRng;
    float            shakeLevel = 0.0f;
    juce::Point<int> shakeOffset;

    // The real window, the plate that fills it, and the root screen on the
    // plate. The Home WHEEL was the root until 2026-08-22; `MainPage` replaced
    // it (see that file's header for why). WheelComponent is still very much
    // alive — it is what both effects chains are built on.
    FloatingShell  shell;
    MeteredPlate   basePlate { processorRef.getAPVTS() };
    MainPage       mainPage  { processorRef };

    // Declared before the pages: pages remove themselves from the stack's
    // child list on destruction, so the stack must outlive them.
    PageStack pageStack;

    HomePage            homePage;
    EffectsWindowPage   inputFxPage  { processorRef.getAPVTS(),
                                       PitchCorrectorAudioProcessor::fxChainInput,
                                       "Input Global Effects", pageStack };
    EffectsWindowPage   outputFxPage { processorRef.getAPVTS(),
                                       PitchCorrectorAudioProcessor::fxChainOutput,
                                       "Output Global Effects", pageStack };
    HarmoniesWindowPage harmoniesPage { processorRef.getAPVTS(), pageStack };

    // The 63C-18 MeterSidebar is gone from here (2026-07-31). It was never
    // attached to the floating shell, but it was still CONSTRUCTED, and its
    // 30 Hz timer kept calling getAndClearMeterPeak() — an invisible component
    // draining the peaks the visible meters need. Source/MeterSidebar.h is left
    // in the tree for the parts (volume knob, bend fader, scope) still to be
    // rebuilt as panels.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCorrectorAudioProcessorEditor)
};
