/*
    SolLookAndFeel.h
    ----------------
    Official 63C brand LookAndFeel for "Sol Voice Tuner".

    NIGHT PANEL (2026-08-22). The sun-white paper-and-ink face is retired; Sol
    now reads as a modern instrument rather than a printed page. What changed
    and why:

      - Dark ground. A vocal tuner is looked AT continuously while tracking,
        usually in a dim room next to a DAW that is itself dark. A white plate
        is the brightest object on the desk and fights the host for attention;
        a dark one lets the live elements — level, pitch, spectrum — be the
        only things that glow.
      - Colour is now allowed, and carries meaning rather than decoration.
        Sol is the sun: the primary accent is a warm amber, used for VALUE
        (how much of a control is dialled in). A cool cyan is its opposite and
        marks LIVE SIGNAL (what the plugin is hearing right now). Green means
        in tune; red still means clipping, and nothing else.
      - Sans-serif. Times New Roman gave the old face its printed quality;
        against a dark ground at small sizes a serif reads as decorative and
        loses legibility, so the brand face is now a UI sans.

    Names are ROLES, not colours, so retuning the values below re-themes
    everything that reads them — including the spectrum's knock-out pass, which
    paints bars in kTitleHi and punches them back out in kBackground and so
    inverts correctly with the palette.
*/

#pragma once

#include <JuceHeader.h>

class SolLookAndFeel : public juce::LookAndFeel_V4
{
public:
    //--------------------------------------------------------------------------
    // Brand palette (also exposed for the editor's paint())
    //--------------------------------------------------------------------------
    // Surfaces: near-black with a slight cool cast, lifting in three steps.
    static constexpr juce::uint32 kBackground   = 0xff0f1216; // the plate
    static constexpr juce::uint32 kPanel        = 0xff181d23; // lifted surface
    static constexpr juce::uint32 kPanelLight   = 0xff222932; // hover / higher
    static constexpr juce::uint32 kOutline      = 0xff2e3742; // hairline border
    static constexpr juce::uint32 kOutlineHi    = 0xff8a97a6; // active / hover ink

    // Accents. kAccentArc is the value colour and the one most things reach
    // for; kAccentGlow is its warmer edge, for bloom under a bright element.
    static constexpr juce::uint32 kAccentArc    = 0xffffb03a; // sol amber — VALUE
    static constexpr juce::uint32 kAccentGlow   = 0xffff8a3d; // warmer bloom
    static constexpr juce::uint32 kAccentCool   = 0xff3fc7f4; // cyan — LIVE SIGNAL
    static constexpr juce::uint32 kAccentToggle = 0xff2a3340; // toggle body, on

    /** In tune / pass. */
    static constexpr juce::uint32 kSuccess      = 0xff45d49a;

    /** Above 0 dBFS, and nothing else. */
    static constexpr juce::uint32 kClip         = 0xffff5252;

    // Type, brightest last.
    static constexpr juce::uint32 kLabel        = 0xff97a3b2; // secondary text
    static constexpr juce::uint32 kLabelAlt     = 0xff6c7887; // tertiary text
    static constexpr juce::uint32 kValue        = 0xffe6ecf3;
    static constexpr juce::uint32 kValueAlt     = 0xff97a3b2;
    static constexpr juce::uint32 kTitleHi      = 0xfff2f6fa; // primary ink
    static constexpr juce::uint32 kGroupTitle   = 0xffe6ecf3;

    static constexpr juce::uint32 kKnobBodyDark = 0xff161b21;
    static constexpr juce::uint32 kKnobBodyLite = 0xff1f262e;
    static constexpr juce::uint32 kKnobRimDark  = 0xff2e3742;
    static constexpr juce::uint32 kKnobRimLite  = 0xff3a4552;
    static constexpr juce::uint32 kPointer      = 0xfff2f6fa;

    /** Editor sets this on `bendRangeKnob` so we can shrink only its rotary radius in `getSliderLayout`. */
    static constexpr const char* bendRangeSliderName = "SolBendRangeKnob";

    /** Marks chromatic key-note `TextButton`s: single-line fitted text + clip (default LnF uses 2 lines and can bleed into neighbours). */
    static constexpr const char* solKeyNoteButtonProperty = "solKeyNote";

    /** Knob scale ticks. Eight intervals puts a major mark at each end and one
        dead centre, which is where a centred-default control (Formant, Pan)
        wants to be readable. Below kTickMinRadius they are dropped: at that
        size the marks merge into a smudged ring and read as dirt. */
    static constexpr int   kTickCount     = 8;
    static constexpr float kTickLength    = 0.16f;   //!< fraction of the radius
    static constexpr float kTickMinRadius = 13.0f;

    //--------------------------------------------------------------------------
    /** Brand typeface. A UI sans as of the night-panel pass (2026-08-22) —
        Times New Roman gave the white plate its printed quality, but a serif
        on a dark ground reads as decorative and loses legibility at the sizes
        a control label actually gets.

        Segoe UI is present on every supported Windows install; on macOS JUCE
        falls back to the system sans (Helvetica/SF), which is the right face
        there anyway. Referenced everywhere rather than hardcoded, so the whole
        app moves together if this changes. */
    static constexpr const char* kBrandTypeface = "Segoe UI";

    SolLookAndFeel()
    {
        setDefaultSansSerifTypefaceName (kBrandTypeface);

        // Sliders
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (kAccentArc));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kOutline));
        setColour (juce::Slider::thumbColourId,               juce::Colour (kPointer));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (kValue));
        // No box behind the number — bare ink on bare plate, same as
        // everywhere else (Giuseppe, 2026-08-22 revamp).
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

        // Labels
        setColour (juce::Label::textColourId,                 juce::Colour (kLabel));

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (kPanel));
        setColour (juce::ComboBox::textColourId,              juce::Colour (kValue));
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (kOutline));
        setColour (juce::ComboBox::arrowColourId,             juce::Colour (kAccentArc));

        // Popup menus
        setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (kPanel));
        setColour (juce::PopupMenu::textColourId,             juce::Colour (kValue));
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
        Sol is unbolded Times New Roman throughout (Giuseppe, 2026-07-28). */
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
    // Rotary slider — flat ink dial (2026-08-22 revamp), same language as
    // VolumeArc: no knob body, no bevel, no glow. Unlike VolumeArc this one
    // keeps a faint full-range track, because a generic knob's zero is not
    // always its visual "nothing" (Formant defaults centred, not at a rail),
    // so a bare arc alone would leave no way to see an unset control's range.
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
        const float stroke = juce::jmax (2.0f, radius * 0.15f);
        const float ar = radius - stroke * 0.5f;

        // Engraved scale ticks outside the track, the way a real instrument
        // marks its travel — an aircraft gauge, a Diva panel. They are what
        // make a knob read as calibrated rather than as a bare ring: you can
        // see roughly where you are without reading the number underneath.
        // Only when there is room; below that they collapse into a smudge.
        if (radius >= kTickMinRadius)
        {
            const float tr0 = radius + stroke * 0.35f;
            const float tr1 = tr0 + juce::jmax (2.0f, radius * kTickLength);

            for (int i = 0; i <= kTickCount; ++i)
            {
                const float t = (float) i / (float) kTickCount;
                const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
                const float s = std::sin (a), c = std::cos (a);

                // Ends and centre are major ticks: those are the three places
                // anyone actually aims for.
                const bool major = (i == 0 || i == kTickCount || i * 2 == kTickCount);

                g.setColour (juce::Colour (major ? kOutlineHi : kOutline));
                g.drawLine (cx + s * tr0, cy - c * tr0,
                            cx + s * (major ? tr1 : tr1 - 1.0f), cy - c * (major ? tr1 : tr1 - 1.0f),
                            major ? 1.4f : 1.0f);
            }
        }

        // Recessed full-range track: the socket the value sits in.
        {
            juce::Path track;
            track.addCentredArc (cx, cy, ar, ar, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (juce::Colour (kOutline));
            g.strokePath (track, juce::PathStrokeType (stroke,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        const bool hasValue = angle - rotaryStartAngle > 0.02f;

        // Value arc in the accent, with a soft bloom under it. The bloom is
        // what makes a lit control read as emitting rather than painted — it
        // is the one place the old "no glow" rule is deliberately reversed.
        if (hasValue)
        {
            juce::Path value;
            value.addCentredArc (cx, cy, ar, ar, 0.0f, rotaryStartAngle, angle, true);

            g.setColour (juce::Colour (kAccentGlow).withAlpha (0.22f));
            g.strokePath (value, juce::PathStrokeType (stroke * 2.2f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            g.setColour (juce::Colour (kAccentArc));
            g.strokePath (value, juce::PathStrokeType (stroke,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Pointer: a hand from the hub out to the rim. Stops short of the
        // centre so the knob reads as a ring with an indicator, not a pie.
        {
            const float sinA = std::sin (angle), cosA = std::cos (angle);
            const float r0 = ar * 0.34f, r1 = ar - stroke * 0.85f;

            g.setColour (juce::Colour (hasValue ? kTitleHi : kLabel));
            g.drawLine (cx + sinA * r0, cy - cosA * r0,
                        cx + sinA * r1, cy - cosA * r1,
                        juce::jmax (1.5f, stroke * 0.5f));
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
    // ComboBox — bare word with a baseline rule and an ink chevron, no box
    // (2026-08-22 revamp): the filled rounded pill was the one place left
    // that still read as a generic form control instead of Sol's own face.
    //--------------------------------------------------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height,
                       bool /*isDown*/, int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                       juce::ComboBox& box) override
    {
        const bool hot = box.isMouseOver() || box.isMouseButtonDown();
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

        // A quiet surface rather than a bare underline: on the dark plate a
        // lone rule reads as a divider, not as something you can open.
        g.setColour (juce::Colour (hot ? kPanelLight : kPanel));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (hot ? kOutlineHi : kOutline));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        const auto az = juce::Rectangle<int> (width - 22, 0, 16, height);
        juce::Path arrow;
        arrow.startNewSubPath ((float) az.getCentreX() - 4, (float) az.getCentreY() - 2);
        arrow.lineTo          ((float) az.getCentreX(),     (float) az.getCentreY() + 2);
        arrow.lineTo          ((float) az.getCentreX() + 4, (float) az.getCentreY() - 2);
        g.setColour (juce::Colour (hot ? kTitleHi : kLabel)
                       .withAlpha (box.isEnabled() ? 1.0f : 0.35f));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    // Toggle button — MonoToggle's own rule, generalised (2026-08-22 revamp):
    // off is a bare word (no box — an unset switch has nothing to announce),
    // on is the same word knocked out white inside a solid ink block. One
    // rule now covers Input Mono, Bypass, MIDI Follow and every effect's
    // toggle-kind controls, instead of the ink block being MonoToggle's own
    // one-off and everything else keeping a cyan-glow-shaped hole where the
    // box outline still was.
    //--------------------------------------------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool /*down*/) override
    {
        const bool on = button.getToggleState();

        // No text (EffectDetailPage's per-effect toggles: a bare 24x24 tick
        // box, no caption of its own — a Label sits beside it instead): a
        // word-block has nothing to draw, so this is its own small control —
        // a square that is either just an outline or filled ink, same
        // "nothing draws what isn't there" spirit as everywhere else.
        if (button.getButtonText().isEmpty())
        {
            const auto box = button.getLocalBounds().toFloat().reduced (2.0f);
            const float a  = button.isEnabled() ? 1.0f : 0.4f;

            if (on)
            {
                g.setColour (juce::Colour (kAccentGlow).withAlpha (0.20f * a));
                g.fillRoundedRectangle (box.expanded (2.5f), 4.0f);
                g.setColour (juce::Colour (kAccentArc).withAlpha (a));
                g.fillRoundedRectangle (box, 2.5f);
            }
            else
            {
                g.setColour (juce::Colour (kPanel).withAlpha (a));
                g.fillRoundedRectangle (box, 2.5f);
                g.setColour (juce::Colour (highlighted ? kOutlineHi : kOutline).withAlpha (a));
                g.drawRoundedRectangle (box.reduced (0.5f), 2.5f, 1.2f);
            }
            return;
        }

        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (getBodyFont (13.0f), button.getButtonText(),
                              0.0f, 0.0f, (float) button.getWidth(), (float) button.getHeight(),
                              juce::Justification::centred, 1);
        const auto textBox = glyphs.getBoundingBox (0, -1, true).expanded (9.0f, 5.0f);

        // On is an accent chip, not a white slab: at this size a full-bright
        // fill on the dark plate glares, and the amber already means "engaged"
        // everywhere else.
        if (on)
        {
            g.setColour (juce::Colour (kAccentGlow).withAlpha (0.18f));
            g.fillRoundedRectangle (textBox.expanded (3.0f), 5.0f);
            g.setColour (juce::Colour (kAccentArc));
            g.fillRoundedRectangle (textBox, 3.0f);
        }
        else if (highlighted)
        {
            g.setColour (juce::Colour (kPanelLight));
            g.fillRoundedRectangle (textBox, 3.0f);
        }

        g.setColour (juce::Colour (on ? kBackground : kLabel)
                       .withAlpha (button.isEnabled() ? 1.0f : 0.5f));
        glyphs.draw (g);
    }

    //--------------------------------------------------------------------------
    // TextButton background — same ink-block rule as the toggle, so the tab
    // strip and the key-note picker read as the same family of control
    // rather than their own grey-box convention (2026-08-22 revamp). A
    // plain action button (never toggled on, e.g. "Remove voice") just
    // never shows the block and reads as a bare word throughout.
    //--------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool highlighted, bool /*down*/) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = button.getToggleState();

        if (on)
        {
            g.setColour (juce::Colour (kAccentGlow).withAlpha (0.18f));
            g.fillRoundedRectangle (bounds.expanded (2.5f), 5.5f);
            g.setColour (juce::Colour (kAccentArc));
            g.fillRoundedRectangle (bounds, 3.0f);
        }
        else
        {
            g.setColour (juce::Colour (highlighted ? kPanelLight : kPanel));
            g.fillRoundedRectangle (bounds, 3.0f);
            g.setColour (juce::Colour (highlighted ? kOutlineHi : kOutline));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
        }
    }
};
