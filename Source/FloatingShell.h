/*
    FloatingShell.h
    ---------------
    Sol's real window: a plugin-owned, per-pixel-alpha desktop window that the
    UI lives in, instead of the rectangle the host hands us.

    Why this exists (63C-50, settled by experiment 2026-07-27):
    a host's editor rectangle is opaque — Ableton Live 10 and FL Studio 2025
    both composite it against black, so an unpainted region renders as a black
    wedge, not as see-through. A window we create ourselves is outside that
    compositing, so `ComponentPeer::windowIsSemiTransparent` gives us a layered
    window where alpha actually works and any silhouette is possible.

    The host still gets a small opaque stub editor — that is unavoidable, and
    is the same artefact Cumpressor's AU leaves behind in Logic. The stub owns
    this shell and keeps it positioned.

    Costs we take on by doing this, none of which JUCE handles for us:
      - focus and z-order are ours to manage
      - dragging and resizing are ours (there is no host title bar here)
      - the shell must be torn down with the editor or it leaks a desktop window
*/

#pragma once

#include <JuceHeader.h>

class FloatingShell final : public juce::Component
{
public:
    static constexpr int kMinWidth  = 420;
    static constexpr int kMinHeight = 260;
    static constexpr int kMaxWidth  = 1600;
    static constexpr int kMaxHeight = 1000;

    /** Size of the grab area in the bottom-right used to resize the shell. */
    static constexpr int kResizerSize = 18;

    FloatingShell()
    {
        // The reason the whole class exists: never claim to fill our bounds,
        // so anything the content leaves unpainted stays truly transparent.
        setOpaque (false);

        refuseKeyboardFocus (*this);

        constrainer.setSizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

        resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
        addAndMakeVisible (*resizer);

        setSize (800, 460);
    }

    /** Fixes the UI's design size and makes the corner a ZOOM rather than a
        reshape (Gard, 2026-07-31).

        Two halves to that. The constrainer gets a fixed aspect ratio, so the
        corner can no longer stretch the window into a different shape — width
        and height only ever move together. And the content is then laid out at
        this logical size forever and scaled to fit by a transform, so dragging
        the corner magnifies the whole interface — type, line weights, the
        chamfer, the meters — instead of leaving fixed-pixel furniture stranded
        in a larger window.

        The size limits are re-derived along the same ratio, so no limit can
        ever demand a shape the aspect lock forbids. */
    void setLogicalSize (int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        logicalWidth  = w;
        logicalHeight = h;

        const double aspect = (double) w / (double) h;

        constrainer.setFixedAspectRatio (aspect);
        constrainer.setSizeLimits (kMinWidth,  juce::roundToInt (kMinWidth  / aspect),
                                   kMaxWidth,  juce::roundToInt (kMaxWidth  / aspect));

        setSize (w, h);
        resized();
    }

    /** Puts this shell on screen as a layered top-level window. Separate from
        the constructor so the caller controls when the window appears. */
    void showOnDesktop()
    {
        if (isOnDesktop())
            return;

        // windowIgnoresKeyPresses covers macOS and Linux only — the Win32 peer
        // never reads it (checked against JUCE's own source, 2026-07-31; only
        // the NSView, UIView and X11 peers test that flag). On Windows the job
        // is done by refuseKeyboardFocus below, which is why it is called again
        // after the content tree exists.
        addToDesktop (juce::ComponentPeer::windowIsTemporary
                    | juce::ComponentPeer::windowIsSemiTransparent
                    | juce::ComponentPeer::windowIgnoresKeyPresses);
        setAlwaysOnTop (true);
        setVisible (true);

        // Anything parented since the constructor ran — the plate and
        // everything on it — has to be told as well.
        refuseKeyboardFocus (*this);
    }

    void hideFromDesktop()
    {
        if (isOnDesktop())
            removeFromDesktop();
    }

    /** The UI that fills the shell. Not owned — the caller keeps it alive. */
    void setContent (juce::Component* newContent)
    {
        if (content == newContent)
            return;

        if (content != nullptr)
            removeChildComponent (content);

        content = newContent;

        if (content != nullptr)
        {
            addAndMakeVisible (content);
            content->toBack();          // keep the resizer grabbable on top
            refuseKeyboardFocus (*content);
        }

        resized();
    }

    void resized() override
    {
        if (content != nullptr)
        {
            if (logicalWidth > 0 && logicalHeight > 0)
            {
                // Laid out at the design size and scaled to fit. The aspect is
                // locked, so width alone gives the factor.
                content->setBounds (0, 0, logicalWidth, logicalHeight);
                content->setTransform (juce::AffineTransform::scale (
                                           (float) getWidth() / (float) logicalWidth));
            }
            else
            {
                content->setTransform ({});
                content->setBounds (getLocalBounds());
            }
        }

        if (resizer != nullptr)
            resizer->setBounds (getWidth()  - kResizerSize,
                                getHeight() - kResizerSize,
                                kResizerSize, kResizerSize);
    }

    //--------------------------------------------------------------------------
    // Dragging — there is no host title bar on a window we own, so the surface
    // itself moves it. Children that handle their own clicks (controls) will
    // consume the event before it reaches here.
    //--------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Settle to home before the drag begins, so the position the dragger
        // starts from is the real one and not a shaken-out frame.
        setShakeOffset ({});

        dragging = true;
        dragger.startDraggingComponent (this, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        dragger.dragComponent (this, e, nullptr);
        home = getPosition();       // dragged: here is the new home
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        home = getPosition();
    }

    //--------------------------------------------------------------------------
    // Shake — the window is thrown around by whatever is coming through it
    //--------------------------------------------------------------------------
    /** Where the window sits when it is not being thrown anywhere. Everything
        that positions the shell deliberately goes through this rather than
        setTopLeftPosition, or the next shake frame would snap it back. */
    void setHomePosition (juce::Point<int> p)
    {
        home = p;
        setTopLeftPosition (home + shakeOffset);
    }

    juce::Point<int> getHomePosition() const noexcept { return home; }

    /** Displaces the window from home. Ignored mid-drag: the pointer owns the
        window's position while the user has hold of it. */
    void setShakeOffset (juce::Point<int> offset)
    {
        if (dragging || offset == shakeOffset)
            return;

        shakeOffset = offset;
        setTopLeftPosition (home + shakeOffset);
    }

private:
    /** Makes a whole subtree refuse the keyboard, which is what keeps the
        host's transport alive.

        Our own desktop window would otherwise become the keyboard's owner the
        moment it is clicked, and the DAW would stop seeing the spacebar — you
        could not start and stop the track while working the UI, which is not a
        trade anyone would make for a plugin.

        Two separate things have to be switched off, and only the second one
        matters on Windows:

          - setWantsKeyboardFocus stops a control asking for focus itself
            (Slider and Button both ask by default);
          - setMouseClickGrabsKeyboardFocus is the one that counts. JUCE's Win32
            peer answers WM_MOUSEACTIVATE with MA_NOACTIVATE when the window's
            top-level component has it cleared, so Windows never activates us on
            a click. Mouse events still arrive; focus simply never moves.

        Applied to the whole tree rather than to the handful of controls that
        happen to be clickable today, because a single component added later
        without it would quietly bring the bug back — which is exactly how this
        was missed the first time.

        The cost: no text field can ever work on this plate. Nothing here is
        typed into, and if that changes it needs its own focusable window rather
        than this one giving up the flag. */
    static void refuseKeyboardFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);

        for (auto* child : c.getChildren())
            if (child != nullptr)
                refuseKeyboardFocus (*child);
    }

    juce::Component*        content = nullptr;

    juce::Point<int> home;
    juce::Point<int> shakeOffset;
    bool             dragging = false;

    /** The size the content is laid out at, whatever the window's actual size.
        Zero until setLogicalSize(), which means "no scaling". */
    int logicalWidth  = 0;
    int logicalHeight = 0;

    juce::ComponentDragger  dragger;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingShell)
};
