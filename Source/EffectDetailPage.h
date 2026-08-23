/*
    EffectDetailPage.h
    ------------------
    One effect's controls (63C-8).

    Nothing here is written per effect. rebind() looks the effect's control set
    up in EffectParams.h and builds exactly what it declares — a knob, a toggle
    or a dropdown per entry, attached to the matching APVTS parameter — so a
    control cannot appear on the page without existing in the audio graph, and
    adding one to the table is the whole job.

    Above them sits the slot's own Amount: how much of this effect is heard at
    this point in the chain, on top of whatever Mix the effect itself has.

    The face follows the Home page (SolPage): black on white, brand typeface,
    no panels behind anything.
*/

#pragma once

#include <JuceHeader.h>

#include "EffectParams.h"
#include "EqCurve.h"
#include "SolLookAndFeel.h"
#include "SolPage.h"
#include "SolPanel.h"

#include <memory>
#include <vector>

/** The selected effect's controls, shown BESIDE the ring rather than on a page
    of their own (Giuseppe, 2026-08-23).

    It used to be pushed onto the page stack, which meant losing sight of the
    chain the moment you went to adjust anything in it — and it left the whole
    right-hand half of the effects page empty while it did. As an inspector it
    fills that space, the chain stays visible, and the window grows or shrinks
    to whatever the selected effect actually needs. */
class EffectDetailPage final : public juce::Component
{
public:
    /** Fired when the inspector's wanted size changes, so the page above can
        ask the window to re-fit. */
    std::function<void()> onSizeChanged;

    /** Nothing selected: the inspector hides and the page shrinks to the ring. */
    bool hasEffect() const noexcept { return ! controls.empty(); }

    void clearEffect()
    {
        controls.clear();
        amountAtt.reset();
        effectName = {};
        isEq = false;
        eqCurve.unbind();
        eqCurve.setVisible (false);
        bandHeader.setVisible (false);
        setVisible (false);

        if (onSizeChanged != nullptr)
            onSizeChanged();
    }

    /** The window is sized to the effect (2026-08-22). Lo-Fi has two controls
        and Trance Gate twenty-three; giving both the same panel either wastes
        two thirds of it or crams the other into a scrollless grid that clips.

        Derived from the same constants the layout uses, so the two cannot drift
        — the page asks for exactly the room it is about to lay out into, plus
        the chrome that surrounds it. */
    juce::Point<int> preferredLogicalSize() const
    {
        // The EQ is sized to its CURVE, not to a count of controls: it shows
        // four knobs whatever happens, and twenty-one cells' worth of grid
        // would be the wrong answer by a factor of five.
        if (isEq)
            return { kEqLogicalW, kEqLogicalH };

        int mainCount = 0, filterCount = 0;

        for (const auto& c : controls)
            (c.isFilter ? filterCount : mainCount)++;

        // Grow WIDE before growing tall, and size to whichever block is
        // busier. A square-ish grid (sqrt) reads well for four controls but
        // sends Trance Gate's sixteen steps six rows deep into a tall narrow
        // window; a panel of controls wants to be a bank, not a column.
        const int busiest = juce::jmax (mainCount, filterCount);
        const int cols = juce::jlimit (2, kMaxCols, (busiest + 1) / 2);

        const int mainRows   = (juce::jmax (1, mainCount) + cols - 1) / cols;
        const int filterRows = filterCount > 0 ? (filterCount + cols - 1) / cols : 0;

        const int contentW = cols * kCellWidth;
        const int contentH = 18 + 4 + (kAmountSize + kCaptionH) + 6
                           + mainRows * kMainCellMaxH
                           + (filterRows > 0 ? 8 + kGroupHeaderH + filterRows * kFilterCellH : 0);

        return { juce::jlimit (kMinLogicalW, kMaxLogicalW, contentW + kChromeW),
                 juce::jlimit (kMinLogicalH, kMaxLogicalH, contentH + kChromeH) };
    }

    explicit EffectDetailPage (juce::AudioProcessorValueTreeState& apvtsIn)
        : apvts (apvtsIn)
    {
        setVisible (false);   // nothing selected yet

        styleKnob (amount);
        addAndMakeVisible (amount);

        styleCaption (amountLabel, "Amount");
        addAndMakeVisible (amountLabel);

        styleCaption (slotLabel, {});
        slotLabel.setAlpha (0.6f);
        addAndMakeVisible (slotLabel);

        styleCaption (filterHeader, "FILTER");
        filterHeader.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (filterHeader);

        // The EQ's curve. Hidden for every other effect — this is the one
        // control set that is a SHAPE, and the only one that earns a bespoke
        // display rather than the generic grid.
        addChildComponent (eqCurve);

        eqCurve.onBandSelected = [this] (int) { resized(); repaint(); };
        eqCurve.onBandTypeChanged = [this] { updateEqEnablement(); };

        styleCaption (bandHeader, {});
        bandHeader.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (bandHeader);
    }

    /** The plate, plus the filter module's own recessed panel. Grouping has to
        be VISIBLE to do any work — a header alone just looks like a stray
        label, so the block it names gets a surface to sit on. */
    void paint (juce::Graphics& g) override
    {
        // The inspector is its own plate beside the ring.
        SolPanel::draw (g, getLocalBounds().toFloat().reduced (1.0f), false, 8.0f);

        g.setColour (juce::Colour (SolLookAndFeel::kTitleHi));
        g.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 17.0f, juce::Font::plain)));
        g.drawText (effectName, getLocalBounds().reduced (14, 8).removeFromTop (22),
                    juce::Justification::centredLeft);

        if (filterBlock.isEmpty() || ! filterHeader.isVisible())
            return;

        SolPanel::draw (g, filterBlock.toFloat()
                              .withTop ((float) filterHeader.getY())
                              .expanded (5.0f, 3.0f));
    }

    /** Other chain positions holding the same effect, whose controls these
        therefore are as well. Set before rebind(). */
    void setLinkedSlots (juce::StringArray slots) { linkedSlots = std::move (slots); }

    /** Point the page at one slot before pushing it: the slot's Amount, and the
        full control set of whatever effect that slot holds. */
    void rebind (const juce::String& amountParamId,
                 const juce::String& effectName,
                 VocalFx::EffectType type,
                 int chainIndex,
                 int slotIndex,
                 const std::function<juce::String (VocalFx::EffectType, const char*)>& paramIdFor)
    {
        this->effectName = effectName;
        setVisible (true);

        // Say when these controls are shared. An effect placed twice in one
        // chain has ONE control set (EffectParams.h), so turning a knob here
        // moves its twin too — which is only a feature if you are told.
        juce::String where = "Chain position " + juce::String (slotIndex + 1);

        if (! linkedSlots.isEmpty())
            where += "   ·   controls shared with "
                   + juce::String (linkedSlots.size() == 1 ? "position " : "positions ")
                   + linkedSlots.joinIntoString (", ");

        slotLabel.setText (where, juce::dontSendNotification);

        amountAtt.reset();
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, amountParamId, amount);

        buildControls (type, paramIdFor);

        isEq = (type == VocalFx::EffectType::FinalEQ);
        eqCurve.setVisible (isEq);
        bandHeader.setVisible (isEq);

        if (isEq)
        {
            eqCurve.bind (apvts, [&paramIdFor, type] (const juce::String& id)
                                 { return paramIdFor (type, id.toRawUTF8()); });
            updateEqEnablement();
        }
        else
        {
            eqCurve.unbind();
        }

        resized();

        if (onSizeChanged != nullptr)
            onSizeChanged();
    }

private:
    //==========================================================================
    /** One declared control: whichever widget its kind calls for, its caption,
        and the attachment that keeps it and the parameter in step. */
    /** Space Dust gives Reverb, Delay and Grain Delay the same four-control
        filter block plus its Warm Sat. Those describe the wet path's TONE, not
        what the effect is, so they belong behind their own header instead of
        scattered among the controls that define the sound — which is how every
        hardware unit and every plugin worth copying groups them. */
    static bool isFilterControl (const char* id) noexcept
    {
        static constexpr const char* ids[] { "filterOn", "hpCut", "hpRes", "lpCut", "lpRes", "warmSat" };

        for (auto* f : ids)
            if (id != nullptr && std::strcmp (id, f) == 0)
                return true;

        return false;
    }

    /** Which EQ band a control belongs to, or -1. The ids are "b1Freq",
        "b2Gain" and so on (EffectParams.h's VOCALFX_EQ_BAND), so the band is
        the digit — the table is the only place the numbering is declared and
        this reads it back rather than restating it. */
    static int eqBandOf (const char* id) noexcept
    {
        if (id == nullptr || id[0] != 'b' || id[1] < '1' || id[1] > '5')
            return -1;

        return id[1] - '1';
    }

    struct Control
    {
        VocalFx::ParamKind kind {};
        const char* id       = nullptr;
        bool        isFilter = false;   //!< belongs to the FILTER module
        bool        isEnable = false;   //!< the effect's On — lives in the header, not the grid
        int         eqBand   = -1;      //!< 0-4 for an EQ band control, else -1

        std::unique_ptr<juce::Slider>      knob;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox>    combo;
        std::unique_ptr<juce::Label>       caption;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   knobAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   toggleAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;

        juce::Component* widget() const
        {
            if (knob   != nullptr) return knob.get();
            if (toggle != nullptr) return toggle.get();
            return combo.get();
        }
    };

    void buildControls (VocalFx::EffectType type,
                        const std::function<juce::String (VocalFx::EffectType, const char*)>& paramIdFor)
    {
        // Attachments must die before the components they drive.
        for (auto& c : controls)
        {
            c.knobAtt.reset();
            c.toggleAtt.reset();
            c.comboAtt.reset();
        }
        controls.clear();

        for (const auto& p : VocalFx::effectParams (type))
        {
            const auto paramId = paramIdFor (type, p.id);
            if (apvts.getParameter (paramId) == nullptr)
                continue;                       // declared but not built — skip rather than crash

            Control c;
            c.kind     = p.kind;
            c.id       = p.id;
            c.isFilter = isFilterControl (p.id);
            c.isEnable = p.id != nullptr && std::strcmp (p.id, "enabled") == 0;
            c.eqBand   = eqBandOf (p.id);

            // No widget for On (Giuseppe, 2026-08-22). The PARAMETER stays —
            // EffectChain still gates the slot from it and the host can still
            // automate it — but it earns no space on the panel: an effect that
            // is in the chain is meant to be heard, and the row of On buttons
            // read as clutter. Re-exposing it is deleting these two lines.
            if (c.isEnable)
                continue;

            switch (p.kind)
            {
                case VocalFx::ParamKind::Toggle:
                    c.toggle = std::make_unique<juce::ToggleButton>();
                    styleToggle (*c.toggle);
                    addAndMakeVisible (*c.toggle);
                    c.toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        apvts, paramId, *c.toggle);
                    break;

                case VocalFx::ParamKind::Choice:
                    c.combo = std::make_unique<juce::ComboBox>();
                    styleCombo (*c.combo);

                    // Fill it from the table BEFORE attaching. ComboBoxAttachment
                    // only syncs the selected id — it never adds items — so
                    // without this every Choice control in every effect opened
                    // an empty menu: Reverb's Schroeder/Void Verb, Phaser Stages,
                    // Soft Clip Oversample, Compressor Type and all five EQ band
                    // types. The attachment must come second, or it has nothing
                    // to select and resets the parameter to the first entry.
                    if (p.choices != nullptr)
                        c.combo->addItemList (juce::StringArray::fromTokens (p.choices, "|", ""), 1);

                    addAndMakeVisible (*c.combo);
                    c.comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, paramId, *c.combo);
                    break;

                case VocalFx::ParamKind::Knob:
                default:
                    c.knob = std::make_unique<juce::Slider>();
                    styleKnob (*c.knob);
                    c.knob->setTextValueSuffix (p.unit);
                    addAndMakeVisible (*c.knob);
                    c.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, paramId, *c.knob);
                    break;
            }

            c.caption = std::make_unique<juce::Label>();

            // Only one band's knobs are on screen at a time, and which band it
            // is is said once in the header above them — so "B3 Freq" repeats
            // itself four times over. Drop the prefix the table carries for
            // the parameter list's benefit and leave the control's own name.
            styleCaption (*c.caption, c.eqBand >= 0
                                        ? juce::String (p.name).fromFirstOccurrenceOf (" ", false, false)
                                        : juce::String (p.name));
            addAndMakeVisible (*c.caption);

            controls.push_back (std::move (c));
        }
    }

    //==========================================================================
    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 10);
        area.removeFromTop (24);   // the effect name, drawn in paint()

        slotLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);

        // The slot's own Amount leads, on its own row, because it is the one
        // control that is about the CHAIN rather than about this effect.
        {
            auto row = area.removeFromTop (kAmountSize + kCaptionH);
            auto box = row.removeFromLeft (kAmountSize + 24);
            amount     .setBounds (box.removeFromTop (kAmountSize));
            amountLabel.setBounds (box);
        }

        area.removeFromTop (6);

        if (controls.empty())
        {
            filterHeader.setVisible (false);
            return;
        }

        if (isEq)
        {
            layoutEq (area);
            return;
        }

        // Two modules, not one flat grid: what the effect IS, then how its wet
        // path is filtered. A single uniform grid gave every control the same
        // weight, so a Reverb's Type and its HP resonance looked equally
        // important and the page read as a spreadsheet.
        std::vector<Control*> main, filter;

        // On is no longer singled out into a header button of its own: it sits
        // in the grid with everything else. As a header control it had to live
        // at the top right, which is exactly where the plate hangs its volume
        // dial, so the two collided (Giuseppe, 2026-08-22).
        for (auto& c : controls)
            (c.isFilter ? filter : main).push_back (&c);

        // The filter module is measured first and taken off the BOTTOM, so the
        // controls that define the effect keep the top of the page and get
        // whatever room is going.
        filterHeader.setVisible (! filter.empty());

        if (! filter.empty())
        {
            const int cols = juce::jmax (1, area.getWidth() / kCellWidth);
            const int rows = ((int) filter.size() + cols - 1) / cols;
            auto block = area.removeFromBottom (rows * kFilterCellH + kGroupHeaderH);

            filterHeader.setBounds (block.removeFromTop (kGroupHeaderH)
                                         .withTrimmedLeft (2));
            filterBlock = block;
            layoutGrid (filter, block, kFilterCellH, kFilterKnob);

            area.removeFromBottom (8);
        }
        else
        {
            filterBlock = {};
        }

        // Whatever is left goes to the effect's own controls, with the cell
        // height stretched to fill it rather than leaving a gap at the foot.
        if (! main.empty())
        {
            const int cols = juce::jmax (1, area.getWidth() / kCellWidth);
            const int rows = juce::jmax (1, ((int) main.size() + cols - 1) / cols);
            const int cellH = juce::jlimit (kFilterCellH, kMainCellMaxH, area.getHeight() / rows);

            layoutGrid (main, area, cellH, juce::jmin (kMainKnob, cellH - kCaptionH - 18));
        }
    }

    /** The EQ: curve on top, then the four knobs of whichever band is
        selected.

        Not all twenty at once. Five bands times four controls is a wall that
        tells you nothing about the filter and takes a window a third wider
        than any other effect to hold — and the curve above already shows all
        five at a glance. A parametric EQ has always worked this way: you pick
        a band, then you have Freq, Gain, Q and shape for it. The other
        sixteen controls still EXIST — their parameters and attachments are
        live, so the host can automate any of them — they are simply not on
        screen while you are working on a different band. */
    void layoutEq (juce::Rectangle<int> area)
    {
        filterHeader.setVisible (false);
        filterBlock = {};

        const int band = eqCurve.selectedBand();

        std::vector<Control*> shown;

        for (auto& c : controls)
        {
            const bool mine = (c.eqBand == band);

            if (auto* w = c.widget())  w->setVisible (mine);
            if (c.caption != nullptr)  c.caption->setVisible (mine);

            if (mine)
                shown.push_back (&c);
        }

        // The band's row of knobs is taken off the BOTTOM so the curve gets
        // every pixel that is going — it is the control, and the knobs are the
        // fine adjustment beside it.
        // Squeezed panel: give the knob row up to a third of what is left
        // rather than taking its full height and leaving the curve inverted.
        auto row = area.removeFromBottom (
            juce::jlimit (30, kEqRowH,
                          juce::jmin (kEqRowH, (area.getHeight() - kGroupHeaderH) / 3)));

        bandHeader.setText ("BAND " + juce::String (band + 1) + "   ·   "
                                + SpaceDustFinalEQ::typeChoices()[
                                      juce::jlimit (0, SpaceDustFinalEQ::numTypes - 1,
                                                    (int) eqCurve.typeOf (band))],
                            juce::dontSendNotification);
        bandHeader.setBounds (area.removeFromBottom (kGroupHeaderH).withTrimmedLeft (2));

        eqCurve.setBounds (area.withTrimmedBottom (4));

        // The row's OWN height, not kEqRowH: layoutGrid drops any cell that
        // would overflow the rectangle it is given, so a cell height larger
        // than the row silently lays out nothing at all.
        layoutGrid (shown, row, row.getHeight(), kFilterKnob + 6);
    }

    /** Low Pass and High Pass have no gain — the DSP ignores gainDb for them
        (SpaceDustFinalEQ::typeUsesGain). A live knob that changes nothing is
        worse than no knob, so it goes dead and the curve pins that band's
        handle to the 0 dB line to match. */
    void updateEqEnablement()
    {
        if (! isEq || ! eqCurve.isBound())
            return;

        for (auto& c : controls)
        {
            if (c.eqBand < 0 || c.knob == nullptr || c.id == nullptr)
                continue;

            if (juce::String (c.id).endsWith ("Gain"))
            {
                const bool usable = SpaceDustFinalEQ::typeUsesGain (eqCurve.typeOf (c.eqBand));

                c.knob->setEnabled (usable);
                c.knob->setAlpha (usable ? 1.0f : 0.35f);

                if (c.caption != nullptr)
                    c.caption->setAlpha (usable ? 1.0f : 0.35f);
            }
        }

        // The header names the shape, so it has to be redrawn when it changes.
        resized();
    }

    /** Lays a set of controls into `area` on a fixed-width grid. `knobSize`
        keeps the dials round instead of stretching them to the cell. */
    void layoutGrid (const std::vector<Control*>& list,
                     juce::Rectangle<int> area, int cellH, int knobSize)
    {
        const int cellW = kCellWidth;
        const int cols  = juce::jmax (1, area.getWidth() / cellW);

        // Centre the row: a trailing gap on the right of a half-full row is
        // what makes a panel look unfinished rather than composed.
        const int used   = juce::jmin ((int) list.size(), cols) * cellW;
        const int startX = area.getX() + juce::jmax (0, (area.getWidth() - used) / 2);

        for (size_t i = 0; i < list.size(); ++i)
        {
            const int col = (int) i % cols;
            const int row = (int) i / cols;

            juce::Rectangle<int> cell (startX + col * cellW,
                                       area.getY() + row * cellH,
                                       cellW, cellH);

            if (cell.getBottom() > area.getBottom() + 2)
                break;

            auto& c = *list[i];
            c.caption->setBounds (cell.removeFromBottom (kCaptionH));

            auto w = cell.reduced (4, 2);

            if (c.knob != nullptr)
                c.knob->setBounds (w.withSizeKeepingCentre (juce::jmin (knobSize, w.getWidth()),
                                                            juce::jmin (knobSize, w.getHeight())));
            else if (c.toggle != nullptr)
                c.toggle->setBounds (w.withSizeKeepingCentre (24, 24));
            else if (c.combo != nullptr)
                c.combo->setBounds (w.withSizeKeepingCentre (w.getWidth(), 24));
        }
    }

    //==========================================================================
    // Sun-white styling: ink on the plate, no fills, no borders.
    static void styleKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
        s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (SolLookAndFeel::kLabel));
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    static void styleToggle (juce::ToggleButton& b)
    {
        b.setColour (juce::ToggleButton::tickColourId,       juce::Colour (SolLookAndFeel::kTitleHi));
        b.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (SolLookAndFeel::kOutline));
    }

    static void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (SolLookAndFeel::kOutline));
        c.setColour (juce::ComboBox::textColourId,       juce::Colour (SolLookAndFeel::kTitleHi));
        c.setColour (juce::ComboBox::arrowColourId,      juce::Colour (SolLookAndFeel::kLabel));
    }

    static void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (SolLookAndFeel::kBrandTypeface, 11.0f, juce::Font::plain)));
        l.setColour (juce::Label::textColourId, juce::Colour (SolLookAndFeel::kLabel));
    }

    static constexpr int kAmountSize = 62;
    static constexpr int kCaptionH   = 14;
    static constexpr int kCellWidth  = 78;

    /** The effect's own controls get the bigger dial — they are what the page
        is FOR. The filter block is a sub-module and reads as one by being
        drawn smaller, not by being pushed into a corner. */
    static constexpr int kMainCellMaxH = 88;
    static constexpr int kMainKnob     = 54;
    static constexpr int kFilterCellH  = 62;
    static constexpr int kFilterKnob   = 36;
    static constexpr int kGroupHeaderH = 16;

    /** Width the plate's volume / mono / meter / mark column needs — matches
        TuningWindowPage::kRightColumn and MainPage::kRightColumn. */
    static constexpr int kRightColumn  = 110;   // unused by the inspector

    static constexpr int kMaxCols      = 6;

    /** Everything between the window edge and layoutContent's rectangle: the
        plate's padding and chamfer allowance, SolPage's edge inset, its header
        and the INPUT / OUTPUT captions. Measured against the 750x450 baseline
        rather than re-derived, because it is fixed chrome — if the plate's
        padding or SolPage's header ever change, these move with them. */
    static constexpr int kChromeW = 131;
    static constexpr int kChromeH = 148;

    /** The window stays sane whatever an effect asks for. */
    static constexpr int kMinLogicalW = 560, kMaxLogicalW = 1000;
    static constexpr int kMinLogicalH = 360, kMaxLogicalH = 660;

    juce::AudioProcessorValueTreeState& apvts;

    /** The band row's height, and the smallest curve worth drawing. Below
        about 140px a 30 dB span stops resolving a 3 dB move, and an EQ you
        cannot see a small change on is not an EQ. */
    static constexpr int kEqRowH     = 74;
    static constexpr int kEqCurveMinH = 150;

    /** What the EQ asks the window for. Wider and taller than any other
        effect, because the curve is the control and a cramped one is unusable
        — five bands across a 250px plot puts their handles on top of each
        other at the low end, where a log axis is already tightest. */
    /** Derived, not guessed: the page's own chrome (title, slot line, the
        Amount row) plus the band header and knob row plus kEqCurveMinH. Type
        a smaller number here and the curve is what gets squeezed. */
    static constexpr int kEqLogicalW = 640;
    static constexpr int kEqLogicalH = kChromeH + 20 + 24 + 18 + 4
                                     + (kAmountSize + kCaptionH) + 6
                                     + kEqCurveMinH + kGroupHeaderH + kEqRowH;

    juce::Slider amount;
    juce::Label  amountLabel, slotLabel, filterHeader, bandHeader;

    /** The Parametric EQ's curve, and whether the bound effect IS the EQ. */
    EqCurve eqCurve;
    bool    isEq = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;

    /** Where the filter module sits, so paint() can put its plate behind it. */
    juce::Rectangle<int> filterBlock;

    /** The effect currently shown, drawn as the inspector's heading. */
    juce::String effectName;

    /** Chain positions sharing this effect's controls. */
    juce::StringArray linkedSlots;

    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectDetailPage)
};
