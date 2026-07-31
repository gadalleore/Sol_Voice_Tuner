/*
    SolLookAndFeel.h
    ----------------
    Official 63C brand LookAndFeel for "Sol Voice Tuner".

    Interim palette (cosmic navy, inherited from the former Shades branding);
    to be replaced by the 63C sun-white frutiger-aero theme (Linear 63C-10):

        background  : deep navy 0xff0a0a1f
        panels      : 0xff1a1a2f / 0xff1a1a30
        outlines    : 0xff3a3a5f
        accent arc  : 0xff00d4ff (bright cyan)
        accent glow : 0xff00b4ff
        labels      : 0xffa0d8ff
        values      : 0xff6dd5fa
        title hi    : 0xffd0f4ff
*/

#pragma once

#include <JuceHeader.h>

class SolLookAndFeel : public juce::LookAndFeel_V4
{
public:
    //--------------------------------------------------------------------------
    // Brand palette (also exposed for the editor's paint())
    //--------------------------------------------------------------------------
    // White paper, black ink (2026-07-28).
    //
    // Bright neutral surface, genuinely black text, and lens dirt as the only
    // texture.
    //
    // Names are ROLES, so retuning the values re-themes everything that reads
    // them.
    static constexpr juce::uint32 kBackground   = 0xffffffff; // pure white
    static constexpr juce::uint32 kPanel        = 0xffececea; // panel fill
    static constexpr juce::uint32 kPanelLight   = 0xffffffff;
    static constexpr juce::uint32 kOutline      = 0xffc6c6c2; // borders
    static constexpr juce::uint32 kOutlineHi    = 0xff868682; // active border

    static constexpr juce::uint32 kAccentArc    = 0xff868682; // no colour accent
    static constexpr juce::uint32 kAccentGlow   = 0xffb4b4b0;
    static constexpr juce::uint32 kAccentToggle = 0xffe2e2de;

    static constexpr juce::uint32 kLabel        = 0xff56564f; // mid grey text
    static constexpr juce::uint32 kLabelAlt     = 0xff76766f;
    static constexpr juce::uint32 kValue        = 0xff2e2e2b;
    static constexpr juce::uint32 kValueAlt     = 0xff56564f;
    static constexpr juce::uint32 kTitleHi      = 0xff0d0d0c; // black ink
    static constexpr juce::uint32 kGroupTitle   = 0xff2e2e2b;

    static constexpr juce::uint32 kKnobBodyDark = 0xffe4e4e0;
    static constexpr juce::uint32 kKnobBodyLite = 0xfffbfbfa;
    static constexpr juce::uint32 kKnobRimDark  = 0xffbdbdb8;
    static constexpr juce::uint32 kKnobRimLite  = 0xffffffff;
    static constexpr juce::uint32 kPointer      = 0xff2e2e2b;

    /** Editor sets this on `bendRangeKnob` so we can shrink only its rotary radius in `getSliderLayout`. */
    static constexpr const char* bendRangeSliderName = "SolBendRangeKnob";

    /** Marks chromatic key-note `TextButton`s: single-line fitted text + clip (default LnF uses 2 lines and can bleed into neighbours). */
    static constexpr const char* solKeyNoteButtonProperty = "solKeyNote";

    //--------------------------------------------------------------------------
    /** Brand typeface (2026-07-27, Gard). Times New Roman is present on
        Windows and macOS; on Linux the metric-compatible Liberation Serif is
        the usual substitute, and JUCE falls back to the default serif if
        neither is installed. Referenced everywhere rather than hardcoded, so
        the whole app moves together if this changes. */
    static constexpr const char* kBrandTypeface = "Times New Roman";

    SolLookAndFeel()
    {
        setDefaultSansSerifTypefaceName (kBrandTypeface);

        // Sliders
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (kAccentArc));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kKnobRimDark));
        setColour (juce::Slider::thumbColourId,               juce::Colour (kPointer));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (kValue));
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (0x33000000));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (0x22000000));

        // Labels
        setColour (juce::Label::textColourId,                 juce::Colour (kLabel));

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (kPanel));
        setColour (juce::ComboBox::textColourId,              juce::Colour (kLabel));
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (kOutline));
        setColour (juce::ComboBox::arrowColourId,             juce::Colour (kAccentArc));

        // Popup menus
        setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (kPanel));
        setColour (juce::PopupMenu::textColourId,             juce::Colour (kLabel));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (kAccentToggle));
        setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colour (kTitleHi));

        // Toggles
        setColour (juce::ToggleButton::textColourId,          juce::Colour (kLabel));
        setColour (juce::ToggleButton::tickColourId,          juce::Colour (kAccentArc));
        setColour (juce::ToggleButton::tickDisabledColourId,  juce::Colour (kOutline));
    }

    //--------------------------------------------------------------------------
    // Public colour accessors (so the editor's paint() can reuse them)
    //--------------------------------------------------------------------------
    juce::Colour getBackground() const noexcept { return juce::Colour (kBackground); }
    juce::Colour getPanel()      const noexcept { return juce::Colour (kPanel);      }
    juce::Colour getAccent()     const noexcept { return juce::Colour (kAccentArc);  }
    juce::Colour getAccentGlow() const noexcept { return juce::Colour (kAccentGlow); }
    juce::Colour getLabelCol()   const noexcept { return juce::Colour (kLabel);      }
    juce::Colour getValueCol()   const noexcept { return juce::Colour (kValue);      }
    juce::Colour getTitleHi()    const noexcept { return juce::Colour (kTitleHi);    }

    juce::Font getTitleFont (float h) const
    {
        return juce::Font (juce::FontOptions (kBrandTypeface, h, juce::Font::plain));
    }

    /** The `bold` argument is retained for call-site compatibility but ignored:
        Sol is unbolded Times New Roman throughout (Gard, 2026-07-28). */
    juce::Font getBodyFont (float h, bool = false) const
    {
        return juce::Font (juce::FontOptions (kBrandTypeface, h, juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label&) override          { return getBodyFont (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override    { return getBodyFont (12.0f); }

    /** ComboBox popups default to `minimumWidth = box width`; a very narrow Key control used to
        trigger multi-column menu layout bugs in some hosts. Force a sane minimum and one column. */
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box,
                                                             juce::Label& label) override
    {
        return juce::PopupMenu::Options().withTargetComponent (&box)
                                         .withItemThatMustBeVisible (box.getSelectedId())
                                         .withInitiallySelectedItem (box.getSelectedId())
                                         .withMinimumWidth (juce::jmax (180, box.getWidth()))
                                         .withMinimumNumColumns (1)
                                         .withMaximumNumColumns (1)
                                         .withStandardItemHeight (label.getHeight());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        if (button.getProperties().contains (juce::Identifier (solKeyNoteButtonProperty)))
        {
            juce::Font font (getTextButtonFont (button, button.getHeight()));
            g.setFont (font);
            g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                      : juce::TextButton::textColourOffId)
                               .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

            // Single-line `drawText` (not `drawFittedText`): avoids multi-line / scaling quirks
            // in tiny cells that can read as wrong notes on screen.
            g.saveState();
            g.reduceClipRegion (button.getLocalBounds());
            g.drawText (button.getButtonText(),
                        button.getLocalBounds().reduced (2, 1),
                        juce::Justification::centred,
                        true);
            g.restoreState();

            return;
        }

        juce::LookAndFeel_V4::drawButtonText (g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    }

    //--------------------------------------------------------------------------
    // Drop-shadow text helper (used by editor + drawToggleButton)
    //--------------------------------------------------------------------------
    void drawTextWithShadow (juce::Graphics& g, const juce::String& text,
                             juce::Rectangle<int> area,
                             juce::Justification just,
                             juce::Colour textColour,
                             float shadowOpacity = 0.3f,
                             int   shadowOffset  = 1)
    {
        g.setColour (juce::Colour (0x33000000).withAlpha (shadowOpacity));
        g.drawText (text, area.translated (shadowOffset, shadowOffset), just, false);
        g.setColour (textColour);
        g.drawText (text, area, just, false);
    }

    //--------------------------------------------------------------------------
    // Rotary slider (Space Dust style: outer glow + bevel + glowing arc + pointer)
    //--------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        juce::ignoreUnused (slider);
        const float radius  = (float) juce::jmin (width, height) * 0.5f - 4.0f;
        if (radius < 6.0f) return;

        const float cx = (float) x + (float) width  * 0.5f;
        const float cy = (float) y + (float) height * 0.5f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const juce::Colour arc  (kAccentArc);
        const juce::Colour glow (kAccentGlow);

        // 1. Outer cosmic bloom
        {
            const float gr = radius + 6.0f;
            juce::ColourGradient cg (glow.withAlpha ((juce::uint8) 30), cx, cy,
                                     glow.withAlpha ((juce::uint8) 0),  cx, cy - gr, true);
            g.setGradientFill (cg);
            g.fillEllipse (cx - gr, cy - gr, gr * 2.0f, gr * 2.0f);
        }

        // 2. Knob body (radial gradient)
        {
            juce::ColourGradient body (juce::Colour (kKnobBodyLite), cx, cy - radius * 0.35f,
                                       juce::Colour (kKnobBodyDark), cx, cy + radius * 0.8f, false);
            g.setGradientFill (body);
            g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        }

        // 3. Bevel rim
        {
            const float rim = juce::jmax (1.5f, radius * 0.06f);
            juce::ColourGradient rg (juce::Colour (kKnobRimLite), cx, cy - radius,
                                     juce::Colour (kKnobRimDark), cx, cy + radius, false);
            g.setGradientFill (rg);
            g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, rim);
        }

        // 4. Glowing value arc
        {
            const float ar = radius + 2.0f;
            const float at = juce::jmax (2.5f, radius * 0.09f);

            juce::Path glowArc;
            glowArc.addCentredArc (cx, cy, ar, ar, 0.0f, rotaryStartAngle, angle, true);
            g.setColour (glow.withAlpha ((juce::uint8) 50));
            g.strokePath (glowArc, juce::PathStrokeType (at + 4.0f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

            juce::Path crisp;
            crisp.addCentredArc (cx, cy, ar, ar, 0.0f, rotaryStartAngle, angle, true);
            g.setColour (arc);
            g.strokePath (crisp, juce::PathStrokeType (at,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // 5. Faint un-set track
        {
            const float tr = radius + 2.0f;
            const float tt = juce::jmax (1.5f, radius * 0.05f);
            juce::Path track;
            track.addCentredArc (cx, cy, tr, tr, 0.0f, angle, rotaryEndAngle, true);
            g.setColour (juce::Colour (kKnobRimDark).withAlpha (0.4f));
            g.strokePath (track, juce::PathStrokeType (tt,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // 6. Pointer
        {
            const float pl = radius * 0.55f;
            const float pt = juce::jmax (2.0f, radius * 0.07f);
            juce::Path p;
            p.addRoundedRectangle (-pt * 0.5f, -radius + 4.0f, pt, pl, pt * 0.5f);
            p.applyTransform (juce::AffineTransform::rotation (angle).translated (cx, cy));
            g.setColour (juce::Colour (kPointer));
            g.fillPath (p);
        }

        // 7. Centre dot highlight
        {
            const float dr = juce::jmax (2.0f, radius * 0.1f);
            juce::ColourGradient d (juce::Colour (0xff4a4a6a), cx, cy - dr * 0.5f,
                                    juce::Colour (kKnobBodyDark), cx, cy + dr, false);
            g.setGradientFill (d);
            g.fillEllipse (cx - dr, cy - dr, dr * 2.0f, dr * 2.0f);
        }
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);

        if (slider.getName() == bendRangeSliderName)
        {
            auto& sb = layout.sliderBounds;
            const int side = juce::jmin (sb.getWidth(), sb.getHeight());
            // Inset only the rotary paint/drag rect — label above the Slider is unchanged.
            const int inset = juce::jlimit (4, (side - 22) / 2,
                                            juce::roundToInt ((float) side * 0.13f));
            sb = sb.reduced (inset);
        }

        return layout;
    }

    //--------------------------------------------------------------------------
    // ComboBox
    //--------------------------------------------------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height,
                       bool /*isDown*/, int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                       juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);

        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        // Cyan chevron arrow
        const auto az = juce::Rectangle<int> (width - 30, 0, 20, height);
        juce::Path arrow;
        arrow.startNewSubPath ((float) az.getCentreX() - 4, (float) az.getCentreY() - 2);
        arrow.lineTo          ((float) az.getCentreX(),     (float) az.getCentreY() + 2);
        arrow.lineTo          ((float) az.getCentreX() + 4, (float) az.getCentreY() - 2);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                       .withAlpha (box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath (arrow, juce::PathStrokeType (2.0f));
    }

    //--------------------------------------------------------------------------
    // Toggle button (Space Dust style: cyan glow when on, dark when off)
    //--------------------------------------------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool /*highlighted*/, bool /*down*/) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const float corner = 3.0f;
        const bool on = button.getToggleState();

        if (on)
        {
            // Outer glow
            g.setColour (juce::Colour (0x5500aaff));
            g.fillRoundedRectangle (bounds.expanded (4.0f), corner + 2.0f);
            // Inner glow
            g.setColour (juce::Colour (0x4400d4ff));
            g.fillRoundedRectangle (bounds.expanded (2.0f), corner + 1.0f);
            // Body
            g.setColour (juce::Colour (kAccentToggle));
            g.fillRoundedRectangle (bounds, corner);
            // Bright border
            g.setColour (juce::Colour (kAccentGlow));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.5f);
        }
        else
        {
            g.setColour (juce::Colour (kPanel));
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (juce::Colour (kOutline));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        }

        // Centred bold label with shadow
        const auto textArea = bounds.reduced (4.0f).toNearestInt();
        const auto textColour = juce::Colour (kLabel).withAlpha (on ? 1.0f : 0.8f);

        g.setFont (getBodyFont (12.0f, true));
        drawTextWithShadow (g, button.getButtonText(), textArea,
                            juce::Justification::centred, textColour,
                            on ? 0.3f : 0.2f, 1);
    }
};
