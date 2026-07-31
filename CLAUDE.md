# Project: Sol Voice Tuner

Sol — a real-time vocal tuner (JUCE C++, VST3 + Standalone) with a frutiger-aero /
Xbox One-era look. Remote: `gadalleore/Sol_Voice_Tuner`.

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

## The face

Sun-white: black ink on a white plate, no colour except red above 0 dBFS. Prefer named
`SolLookAndFeel` constants over hardcoded colours.

- **One line weight.** `kPlateStroke` in `PluginEditor.h` draws the plate border, the ring
  around the Lissajous and the volume arc. Change it once, all three move.
- **One spacing unit.** `kColumnGap` (30px) sets the master column's rhythm; `kStackGap`
  (half it) binds the arc, its label and the toggle into one group. Elements are positioned
  by the rule, not by typed-in numbers — the meter bars have no height of their own, they
  fill what the rule leaves.
- **Movement smears.** Anything that moves drags `SolDither`'s RGB streak behind it. The
  window shake feeds its throw vector to the wheel explicitly, because shaking moves
  everything together and the ordinary motion trails would see no travel at all.
- **Nothing draws what isn't there.** The meters are invisible in silence, the volume arc has
  no empty track, the spectrum has no frame or grid.
- **The spectrum knocks type out rather than burying it.** Elements the bars cross implement
  `SpectrumStrip::Inkable` and redraw themselves white, clipped to the bars. Anything new
  that lands in the bottom 150px needs `paintInk` or it will be swallowed.

The shell is our own layered desktop window, not the host's rectangle — that is what makes
the chamfered silhouette and the shake possible. It carries `windowIgnoresKeyPresses` so the
DAW keeps the spacebar; that flag is also why no text field can work on the plate.
