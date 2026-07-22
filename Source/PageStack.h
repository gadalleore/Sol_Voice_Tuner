/*
    PageStack.h
    -----------
    Xbox-dashboard style navigation: one page visible at a time; push() drills
    into a page, pop() goes back. Pages are owned by the caller and must
    outlive the stack; the stack only manages bounds, visibility and a simple
    slide transition (placeholder until the weighty animation pass, 63C-12).

    63C-17 bleed-through fix: finalise() now also runs the moment the
    animator finishes (ChangeListener) rather than only on a timer that a
    rapid push/pop sequence could keep postponing, interrupted transitions
    snap components to their final position (cancelAllAnimations(true)),
    and the stack paints an opaque background so a mid-transition gap can
    never show stale pixels underneath.
*/

#pragma once

#include <vector>

#include <JuceHeader.h>

#include "SolLookAndFeel.h"

class PageStack final : public juce::Component,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    PageStack()
    {
        setOpaque (true);
        animator.addChangeListener (this);
    }

    ~PageStack() override
    {
        animator.removeChangeListener (this);
    }

    static constexpr int transitionMs = 200;

    void setRootPage (juce::Component& page)
    {
        jassert (pages.empty());
        pages.push_back (&page);
        addAndMakeVisible (page);
        resized();
    }

    void push (juce::Component& page)
    {
        if (pages.empty())
        {
            setRootPage (page);
            return;
        }

        pages.push_back (&page);
        addAndMakeVisible (page);
        page.toFront (false);
        page.setBounds (getLocalBounds().translated (getWidth(), 0));
        animator.animateComponent (&page, getLocalBounds(), 1.0f, transitionMs, false, 1.0, 1.0);
        startTimer (transitionMs + 40);   // backstop; ChangeListener finalises sooner
    }

    void pop()
    {
        if (pages.size() < 2)
            return;

        auto* top = pages.back();
        pages.pop_back();

        auto* below = pages.back();
        below->setVisible (true);
        below->setBounds (getLocalBounds());
        below->toBehind (top);

        animator.animateComponent (top, getLocalBounds().translated (getWidth(), 0),
                                   1.0f, transitionMs, false, 1.0, 1.0);
        startTimer (transitionMs + 40);
    }

    int getDepth() const noexcept { return (int) pages.size(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (SolLookAndFeel::kBackground));
    }

    void resized() override
    {
        animator.cancelAllAnimations (true);   // snap to final positions
        finalise();

        for (auto* p : pages)
            p->setBounds (getLocalBounds());
    }

private:
    void timerCallback() override
    {
        stopTimer();
        finalise();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // The animator broadcasts when its animation set changes; once idle,
        // the transition is over — finalise immediately (timer is backstop).
        if (! animator.isAnimating())
        {
            stopTimer();
            finalise();
        }
    }

    /** Ensure only the top page is visible once any transition has ended.
        Iterates children (not just `pages`) so popped pages get hidden too. */
    void finalise()
    {
        auto* top = pages.empty() ? nullptr : pages.back();

        for (auto* child : getChildren())
            child->setVisible (child == top);

        if (top != nullptr)
            top->setBounds (getLocalBounds());
    }

    std::vector<juce::Component*> pages;
    juce::ComponentAnimator animator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageStack)
};
