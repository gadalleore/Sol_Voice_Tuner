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

        constrainer.setSizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

        resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
        addAndMakeVisible (*resizer);

        setSize (800, 460);
    }

    /** Puts this shell on screen as a layered top-level window. Separate from
        the constructor so the caller controls when the window appears. */
    void showOnDesktop()
    {
        if (isOnDesktop())
            return;

        addToDesktop (juce::ComponentPeer::windowIsTemporary
                    | juce::ComponentPeer::windowIsSemiTransparent);
        setAlwaysOnTop (true);
        setVisible (true);
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
        }

        resized();
    }

    void resized() override
    {
        if (content != nullptr)
            content->setBounds (getLocalBounds());

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
        dragger.startDraggingComponent (this, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        dragger.dragComponent (this, e, nullptr);
    }

private:
    juce::Component*        content = nullptr;
    juce::ComponentDragger  dragger;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingShell)
};
