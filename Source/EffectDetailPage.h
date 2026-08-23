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
#include "SolLookAndFeel.h"
#include "SolPage.h"
#include "SolPanel.h"

#include <memory>
#include <vector>

class EffectDetailPage final : public SolPage,
                               public SizedPage
{
public:
    /** The window is sized to the effect (2026-08-22). Lo-Fi has two controls
        and Trance Gate twenty-three; giving both the same panel either wastes
        two thirds of it or crams the other into a scrollless grid that clips.

        Derived from the same constants the layout uses, so the two cannot drift
        — the page asks for exactly the room it is about to lay out into, plus
        the chrome that surrounds it. */
    juce::Point<int> preferredLogicalSize() const override
    {
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

        const int contentW = cols * kCellWidth + kRightColumn;
        const int contentH = 18 + 4 + (kAmountSize + kCaptionH) + 6
                           + mainRows * kMainCellMaxH
                           + (filterRows > 0 ? 8 + kGroupHeaderH + filterRows * kFilterCellH : 0);

        return { juce::jlimit (kMinLogicalW, kMaxLogicalW, contentW + kChromeW),
                 juce::jlimit (kMinLogicalH, kMaxLogicalH, contentH + kChromeH) };
    }

    EffectDetailPage (juce::AudioProcessorValueTreeState& apvtsIn, PageStack& stackToUse)
        : SolPage (stackToUse, "Effect"), apvts (apvtsIn)
    {
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
    }

    /** The plate, plus the filter module's own recessed panel. Grouping has to
        be VISIBLE to do any work — a header alone just looks like a stray
        label, so the block it names gets a surface to sit on. */
    void paint (juce::Graphics& g) override
    {
        SolPage::paint (g);

        if (filterBlock.isEmpty() || ! filterHeader.isVisible())
            return;

        SolPanel::draw (g, filterBlock.toFloat()
                              .withTop ((float) filterHeader.getY())
                              .expanded (5.0f, 3.0f));
    }

    /** Point the page at one slot before pushing it: the slot's Amount, and the
        full control set of whatever effect that slot holds. */
    void rebind (const juce::String& amountParamId,
                 const juce::String& effectName,
                 VocalFx::EffectType type,
                 int chainIndex,
                 int slotIndex,
                 const std::function<juce::String (VocalFx::EffectType, const char*)>& paramIdFor)
    {
        setTitle (effectName);
        slotLabel.setText ("Chain position " + juce::String (slotIndex + 1),
                           juce::dontSendNotification);

        amountAtt.reset();
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, amountParamId, amount);

        buildControls (type, paramIdFor);
        resized();
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

    struct Control
    {
        VocalFx::ParamKind kind {};
        const char* id       = nullptr;
        bool        isFilter = false;   //!< belongs to the FILTER module
        bool        isEnable = false;   //!< the effect's On — lives in the header, not the grid

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
            styleCaption (*c.caption, p.name);
            addAndMakeVisible (*c.caption);

            controls.push_back (std::move (c));
        }
    }

    //==========================================================================
    void layoutContent (juce::Rectangle<int> area) override
    {
        // Keep clear of the plate's right-hand column. Volume, Input Mono, the
        // meters and the mark are fixtures of the WINDOW, drawn over whatever
        // page is showing (see TuningWindowPage) — a page that lays out across
        // its full width stacks controls underneath them.
        area = area.withTrimmedRight (kRightColumn);

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
    static constexpr int kRightColumn  = 110;

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

    juce::Slider amount;
    juce::Label  amountLabel, slotLabel, filterHeader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;

    /** Where the filter module sits, so paint() can put its plate behind it. */
    juce::Rectangle<int> filterBlock;

    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectDetailPage)
};
