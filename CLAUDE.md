# Project: Sol Voice Tuner

Sol — a real-time vocal tuner (JUCE C++, VST3 + Standalone). Dark instrument panel, amber
for value and cyan for live signal; see "The face". Remote: `gadalleore/Sol_Voice_Tuner`.

## Workflow — one repo, work on main

Work happens on **main**, in this folder, in one session at a time. No worktrees, no
per-issue branches, no Linear tickets, no parallel agents — that workflow was tried and
dropped on 2026-07-31 because it cost more to coordinate than it returned.

**Never create a worktree, clone, or scratch copy beside this folder.** If a branch is ever
genuinely needed, branch in place and merge it straight back.

On Giuseppe's machine this repo sits inside a `Sol Voice Tuner Main/` folder alongside a
`Defunct Box/` of retired pre-2026-07-31 copies (old per-issue worktrees and archives, all
of them ancestors of main, plus a record of the deleted branch pointers). None of it is on
a build path and none of it is in this repo; ignore it unless something needs digging out of
history.

Note: the CMake build dir bakes in absolute paths. If this folder is ever moved, run
`build-and-launch.ps1 -Clean` (a bare `-Reconfigure` will not clear the stale cache).

## Build process: panel by panel, hooked up as we go

Sol is built **bottom-up, one element at a time**, not window-by-window:

1. Build the element or panel in isolation, on the shared base plate.
2. Hook it into the app the moment it exists — wire its parameters, put it on a page, run it.
3. Only then move to the next element.

Nothing is left as an unwired component waiting for an integration pass later. A panel issue
is not done until it renders in the running plugin and its controls do something. A control
that draws but does not touch audio is not finished — `MonoToggle` shipped with its parameter
and its DSP in the same commit, and that is the standard.

`Source/ChamferPanel.h` is the base plate every panel is built on — a rectangle with the
top-right corner sliced off, ported from Giuseppe's React/SVG prototype. Panels either call
`setContent()` to hook in a child, or subclass and override `layoutContent()`.

Run it with `build-and-launch.ps1` (add `-Reconfigure` after touching CMakeLists, `-Clean`
after moving the folder). It builds the VST3, copies it to every VST3 path on the machine,
and launches Ableton.

**Always finish a change by running that script, and let it launch Ableton**
(Giuseppe, 2026-08-23). Not `-NoLaunch`: the standalone has no audio running
through it, so a change to anything that touches sound cannot be judged there —
the meters sit at zero, the Phaser's sweep never moves, and an effect that has
gone silent looks exactly like one that is working. Deploy and open the host, so
the change can actually be heard.
A plain `cmake --build` leaves the installed VST3 on the previous build, and Giuseppe tests
in Ableton — a change that is not in `C:\Program Files\Common Files\VST3\63C\` and the three
other roots has not been delivered.

## The effect chains

Three chains — input global, lead voice, output global — of 25 ordered slots each, wired
in `PluginProcessor::applyFxChain`. The parts:

- `Source/VocalEffectBase.h` — the `EffectType` list, `effectTypeName`, and the
  `VocalEffect` interface.
- `Source/EffectParams.h` — **what every effect's controls ARE.** One table per effect,
  listing every knob, toggle and dropdown it has in Space Dust with Space Dust's own
  ranges, skews and defaults. The APVTS layout, the detail page and the per-block value
  snapshot are all built from these tables and nothing else describes the controls, so a
  knob cannot exist in the UI without existing in the audio graph.
- `Source/fx/` — Space Dust's DSP, ported from Synth VST V2 unchanged except where a
  comment says otherwise. `SolPingPongDelay.h` is the exception: Space Dust's delay lives
  inline in its processor, so there was nothing to copy and it was rebuilt here.
- `Source/SpaceDustEffects.h` — the wiring: each class reads its effect's controls out of
  the snapshot and hands them to the ported processor. `createEffect()` lives here too.
- `Source/EffectChain.h` — the chain: lazy allocation on the message thread, lock-free
  hand-off to the audio thread, click-free slot swaps.

**One control set per chain, not per slot.** In Space Dust each effect exists once with one
panel, and the same holds here: a chain's Reverb has one set of knobs no matter which slot
it sits in, and the slot decides only where in the order it runs. That is why the effects
wheels set `allowDuplicates = false` — two Reverbs in one chain would silently share one set
of controls. Per-slot sets would multiply 110 controls by 25 slots and hand the host a
parameter list nobody could use. The three chains are independent of each other.

**Every effect has its own On** (`VOCALFX_ENABLED`, appended to all twelve tables on
2026-08-22), matching Space Dust's `<fx>Enabled`. Before that an effect was on purely because
it sat in a slot, so the only way to silence one was to drag it back out and lose its
settings — which is why toggling never behaved the way it does in Space Dust. `EffectChain`
gates the slot from a ramped `enableRamp` folded into the same wet gain as the swap fade and
the slot trim, so one place covers all twelve and the mute is click-free. It looks the
control up by id (`enabledIndex`) rather than at a fixed index, because the tables are
append-only and every effect's On landed at a different position.

Two deliberate departures from Space Dust, both because the parameter has nothing to do here:

- **No Post** on Trance Gate or Bit Crush. In Space Dust it picks whether the effect runs
  before or after the synth's fixed chain; the 25 ordered slots already say exactly where.
- **No Triplet / Triplet-All** on Delay. Rate already sweeps an 18-entry table of straight,
  dotted *and* triplet divisions, so a triplet toggle would contradict the knob.

`enableRamp` resets OPEN rather than to `GainRamp`'s own default of zero: an effect whose
table carries no On would otherwise be muted forever with nothing in the UI to explain it.

The slot's **Amount** is the one control that is about the chain rather than the effect: how
much of that slot is heard, on top of whatever Mix the effect itself has. `EffectChain`
multiplies it into the same wet gain the swap fade uses, so the two cannot fight.

**Adding an effect** is: append to `EffectType` (append-only — reordering remaps saved
sessions to the wrong sound), name it in `effectTypeName`, add a control table and index
enum to `EffectParams.h`, add an adapter and a `createEffect` case. The APVTS parameters,
the wheel's palette and the detail page all follow from those. Then run the smoke test:

    cmake --build build --config Release --target SolVoiceTuner_FxSmokeTest
    ./build/Release/SolVoiceTuner_FxSmokeTest.exe

It builds every type and drives **each control to both ends of its range on its own**, with
the rest at defaults — one at a time, so a failure points at one control and the settings
stay plausible. Silence, a tone and a hot signal go through each pass, in mono and stereo,
at two sample rates, plus a chain with all 25 slots filled.

NaN or Inf fails at any setting. A runaway LEVEL fails only at plausible settings: at the
all-extremes corners the check is finiteness alone, because five cascaded resonant filters
really are that loud and Space Dust lets you build them too. Sweeping every control at once
was the first version of this test and it only produced that false alarm.

The wheel and the pages can only be judged by hand — Giuseppe does that himself in Ableton.

Space Dust's Transient is deliberately NOT ported: it is a drum-hit synthesiser triggered
by note-on, not a treatment of the voice.

## The face

**Night panel** (Giuseppe, 2026-08-22). Sol was sun-white — black ink on a white plate, no
colour except the clip red, Times New Roman throughout — for its first month. That face is
retired. It is a printed page, and Sol is an instrument you stare at while tracking, usually
in a dim room beside a DAW that is itself dark; a white plate was the brightest object on the
desk and fought the host for attention. Anything in the tree still painting black-on-white is
a leftover, not the standard.

What replaced it, and the reasoning, is at the top of `SolLookAndFeel.h`. In short:

- **Dark ground, three surfaces.** `kBackground` (the plate) → `kPanel` → `kPanelLight`,
  with `kOutline` for hairlines. Live elements are the only things that glow.
- **Colour carries meaning now, and only meaning.** `kAccentArc` (amber, "sol") is VALUE —
  how much of a control is dialled in. `kAccentCool` (cyan) is LIVE SIGNAL — what the plugin
  is hearing: the goniometer trace, the bottom of the meter gradient. `kSuccess` is in-tune,
  `kClip` is above 0 dBFS and nothing else. A colour used decoratively is a bug.
- **Sans-serif.** `kBrandTypeface` is a UI sans; a serif on a dark ground reads as decorative
  and loses legibility at control-label sizes.
- **Hover lights UP, never down.** On white, hover greyed a word — darker ink on bright paper
  reads as picked-out. Inverted onto a dark ground that same move dims the word, which reads
  as *disabled*. Hover goes to `kAccentArc`. This caught both `WheelComponent::kHoverText`
  and `SolPage`'s `BackWord`; anything new needs the same check.
- Prefer named `SolLookAndFeel` constants over hardcoded colours — the palette is roles, not
  colours, which is why inverting it re-themed the whole app from one block of constants.

- **One line weight.** `kPlateStroke` in `PluginEditor.h` draws the plate border, the ring
  around the Lissajous and the volume arc. Change it once, all three move. (The ring is the
  one place that now decouples its *colour* from full-strength ink — see below.)
- **One spacing unit.** `kColumnGap` (30px) sets the master column's rhythm; `kStackGap`
  (half it) binds the arc, its label and the toggle into one group. Elements are positioned
  by the rule, not by typed-in numbers — the meter bars have no height of their own, they
  fill what the rule leaves.
- **Movement smears.** Anything that moves drags `SolDither`'s RGB streak behind it. The
  window shake feeds its throw vector to the wheel explicitly, because shaking moves
  everything together and the ordinary motion trails would see no travel at all.
- **Nothing draws what isn't there.** The meters are invisible in silence, the volume arc has
  no empty track, the spectrum has no frame or grid. Generic rotary knobs are the deliberate
  exception — they keep a recessed full-range track, because a knob's zero is not always its
  visual "nothing" (Formant defaults centred), so a bare arc would leave no way to read an
  unset control's range.
- **The spectrum knocks type out rather than burying it.** Elements the bars cross implement
  `SpectrumStrip::Inkable` and redraw themselves, clipped to the bars. Anything new that lands
  in the bottom 150px needs `paintInk` or it will be swallowed. This survived the inversion
  untouched: the strip paints bars in `kTitleHi` and punches them back out in `kBackground`,
  so swapping the palette swapped the knock-out with it.
- **Elements the plate owns are not the page's to lay out.** Volume, Input Mono, the meters
  and the brand mark are fixtures of the WINDOW — `MeteredPlate` hangs them off the plate as
  siblings drawn *over* whatever page is showing. The wheels dodge them by bulging leftward,
  but any page that lays out across its full width must reserve that column or it will stack
  controls underneath them (`TuningWindowPage::kRightColumn`). A row of fixed-width blocks
  taken with `removeFromLeft` should also degrade proportionally — over-subscribe it and the
  overflow lands entirely on the last control, which silently stops being laid out.

### Controls

One rule, applied in `SolLookAndFeel` so a new control gets it for free instead of needing
its own paint code: **off is quiet, on is an accent chip.** `MonoToggle` (Giuseppe,
2026-07-31) established the shape of this — off is a bare word with nothing to announce, on
is that word knocked out of a solid block — and everything else was brought onto it, first as
black-on-white and then as amber on the night panel.

- **Rotary knobs**: a recessed full-range track, an amber value arc with a soft bloom under
  it, and a pointer that stops short of the hub so the knob reads as a ring with an indicator
  rather than a pie. `VolumeArc` is the same family minus the track.
- **Toggle and selected-button state**: `drawToggleButton` and `drawButtonBackground` both
  key off `getToggleState()` and fill `kAccentArc` behind `kBackground` text when on, a quiet
  panel when off. Covers Bypass, MIDI Follow, the tab strip and the key-note picker with one
  rule instead of four. Deliberately *not* a full-bright white fill — at this size that
  glares on the dark plate, and the amber already means "engaged" everywhere else. A
  `ToggleButton` built with no text (EffectDetailPage's per-effect toggles) is its own small
  case: a bare outlined square when off, a filled accent square when on — no word to knock out.
- **ComboBox** is a quiet filled surface with a hairline and a chevron. It was a bare
  underline while the plate was white; on the dark ground a lone rule reads as a divider
  rather than as something you can open.
- **The bloom is deliberate, and is the one place the old "no glow" rule is reversed.** A lit
  control needs to read as *emitting*, not painted. It is always a low-alpha `kAccentGlow`
  under the real element, never a replacement for it.
- A slider's text box only reads its transparent background/outline from `SolLookAndFeel` if
  something calls `Slider::setColour` on the instance — the box is a `Label` JUCE caches at
  first `setTextBoxStyle`, built from the tree's LookAndFeel at that moment. Pages built as
  members (constructed before the editor's `setLookAndFeel` runs) need the transparent colours
  set directly on each slider, same as `EffectDetailPage::styleKnob` already did — see
  `LegacyTunerPage.cpp`'s `clearKnobTextBoxChrome`.

## Navigation and page furniture

Pages wear the Home page's face (Giuseppe, 2026-08-16): **no title pane** — no filled
header, no divider rule, no panel behind anything — just the page's title set on the bare
plate in the brand typeface. Back is a **word**, not a bar: the full-height strip it
replaced sat at x = 0, and the plate used to let content bleed to the window edge, so it ran
underneath the border and off the glass. `SolPage::kEdgeInset` is what keeps a page off that
edge.

**Nothing crosses the frame** (2026-08-22). `setContentBleedsLeft` is off: it existed so the
half-wheel — whose centre sits on its own left edge — could run off the panel, but what it
actually produced was the orb ring and the rim labels crossing the plate's border, which
reads as a rendering fault rather than as design. A bezel things pass through is not a bezel.
Anything anchored to a component edge now has to fit inside it: `WheelComponent`'s ring is
concentric with the ORB, so it needs `orbOffsetRatio > ringOrbGap * orbScale * ringScale`
(≈0.69) or it is clipped flat and reads as a broken circle.

Items on the wheel — on the rim and in the palette both — are **bare words**. No pills, no
ovals, no borders; hover and drag read through colour alone.

### The root screen

`Source/MainPage.h` is the root. The Home **wheel** was, until 2026-08-22; see MainPage's
header for why it was replaced (short version: the wheel showed three words and hid every
control one drill-in away, which is a poor instrument to track a vocal with). `HomePage.h`
is older still — the pre-wheel full-page Home, kept only for parts not yet rebuilt.

`WheelComponent` is emphatically NOT dead: it is what both effects chains are built on. What
changed is that it is no longer the root, so the editor no longer owns one.

Clicking a nav destination calls `PitchCorrectorAudioProcessorEditor::openPage`, which swaps
the plate's content from the main page to `pageStack` rooted at that page; back on the root
page fires `PageStack::onPopFromRoot` and `showHome()` puts the main page back. Anything
registered with the spectrum as `Inkable` must therefore cope with being swapped out —
`SpectrumStrip` skips inkables that are not showing, or the bars would keep knocking out
furniture that left the plate.

The analyser is a **footer band** (`kSpectrumHeight`), not the bottom-third wash it was. A
dense control surface cannot share that space: every knob down there would have had bars
painted over it, or needed its own `Inkable` stencil. Layouts sit above it.

The shell is our own layered desktop window, not the host's rectangle — that is what makes
the chamfered silhouette and the shake possible. It carries `windowIgnoresKeyPresses` so the
DAW keeps the spacebar; that flag is also why no text field can work on the plate.
