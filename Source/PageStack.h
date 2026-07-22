/*
    PageStack.h
    -----------
    Xbox-dashboard style navigation: one page visible at a time; push() drills
    into a page, pop() goes back. Pages are owned by the caller and must
    outlive the stack; the stack only manages bounds, visibility and a simple
    slide transition (placeholder until the weighty animation pass, 63C-12).
*/

#pragma once

#include <vector>

#include <JuceHeader.h>

class PageStack final : public juce::Component,
                        private juce::Timer
{
public:
    PageStack() = default;

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

        pendingHide = pages.back();
        pages.push_back (&page);
        addAndMakeVisible (page);
        page.toFront (false);
        page.setBounds (getLocalBounds().translated (getWidth(), 0));
        animator.animateComponent (&page, getLocalBounds(), 1.0f, transitionMs, false, 1.0, 1.0);
        startTimer (transitionMs + 20);
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

        pendingHide = top;
        animator.animateComponent (top, getLocalBounds().translated (getWidth(), 0),
                                   1.0f, transitionMs, false, 1.0, 1.0);
        startTimer (transitionMs + 20);
    }

    int getDepth() const noexcept { return (int) pages.size(); }

    void resized() override
    {
        animator.cancelAllAnimations (false);
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

    /** Ensure only the top page is visible once any transition has ended.
        Iterates children (not just `pages`) so popped pages get hidden too. */
    void finalise()
    {
        pendingHide = nullptr;

        auto* top = pages.empty() ? nullptr : pages.back();

        for (auto* child : getChildren())
            child->setVisible (child == top);
    }

    std::vector<juce::Component*> pages;
    juce::ComponentAnimator animator;
    juce::Component* pendingHide = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageStack)
};
