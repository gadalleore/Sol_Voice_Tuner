# Sol Voice Tuner

**63C · Sol Voice Tuner**

A modern, real-time **AutoTune-style pitch corrector** built in JUCE C++.
Detects vocal pitch with the YIN algorithm, snaps it to a chosen
root + scale (or to a MIDI-keyboard chord), and re-pitches the audio
with the high-quality, header-only
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
engine — featuring formant preservation, retune-speed smoothing,
and a "Robot Mode" for hard T-Pain snapping.

A **63C** product. (Formerly branded Shades; a sun-white frutiger-aero
UI rebuild is in progress — see the Linear project for the roadmap.)

Builds as **VST3** and a **Standalone** app (Windows). macOS AU support
is wired in CMake but currently disabled.

---

## Features

- 🎤 **Live YIN pitch detection** (parabolic interpolation, RMS gate).
- 🎚 **Knobs**: Retune Speed, Correction Amount, Dry/Wet, Input Gain, Output Gain.
- 🎼 **Root + Scale selectors**: Chromatic, Major, Minor, Harmonic Minor,
  Melodic Minor, Pentatonic, Minor Pentatonic, Blues, Dorian, Phrygian,
  Lydian, Mixolydian, Locrian.
- 🎹 **MIDI input mode** — chord on your keyboard becomes the allowed
  pitch set (great for following chord changes).
- 🤖 **Robot Mode** for instant hard-snap T-Pain effect.
- 🧠 **Formant preservation** to avoid chipmunk artefacts.
- 📊 **Live UI** showing detected vs. target pitch (Hz, note name, cents)
  plus a scrolling pitch-history meter.
- 💾 **Preset system** with built-in patches (Pop Vocal, T-Pain Snap,
  Trap Drip, Blues Lead, Lo-Fi Subtle, Robot Voice, …).
- ⏱ **Correct DAW latency reporting** (set from the shifter's
  internal latency).

---

## Project layout

```
Sol Voice Tuner/
├─ CMakeLists.txt          # Top-level CMake (uses pre-cloned JUCE + Signalsmith Stretch)
├─ build-and-launch.ps1    # One-click build + copy + launch
├─ README.md
├─ external/               # Pre-cloned JUCE 8 + Signalsmith Stretch + Linear
└─ Source/
   ├─ PluginProcessor.h/.cpp   # Top-level AudioProcessor, signal routing
   ├─ PluginEditor.h/.cpp      # UI (knobs, combos, toggles, pitch meter)
   ├─ PitchDetector.h/.cpp     # YIN pitch detector (real-time, lock-free)
   ├─ PitchShifter.h/.cpp      # Wrapper around Signalsmith Stretch
   ├─ ScaleQuantizer.h         # Hz <-> MIDI helpers + scale snapping
   └─ SolLookAndFeel.h         # 63C brand theme
```

---

## Build instructions

You need **CMake ≥ 3.22** and a working C++17 compiler.
JUCE 8 and Signalsmith Stretch are fetched automatically.

### Windows — one-click (recommended)

Double-click `build-and-launch.bat`, or from PowerShell:

```powershell
cd "Sol Voice Tuner"
.\build-and-launch.ps1
```

This script will:
1. Run `cmake` (auto-fetches JUCE + Signalsmith Stretch on first run).
2. Build the **Release** VST3 incrementally.
3. Copy `Sol Voice Tuner.vst3` into the **`63C`** subfolder under
   every detected VST3 root:
   - `C:\Program Files\Common Files\VST3\63C\`
   - `~\Documents\Ableton\User Library\VST3\63C\`
   - `~\Documents\VST3\63C\` and `~\VST3\63C\` (if present).
4. Launch (or focus) Ableton Live.

Useful flags:

| Flag             | Effect                                       |
|------------------|----------------------------------------------|
| `-NoLaunch`      | Build + copy, but don't open Ableton.        |
| `-Clean`         | Delete `build/` first for a full rebuild.    |
| `-Reconfigure`   | Force CMake to re-configure.                 |

> Tip: The "Program Files" copy needs an **admin shell** to succeed.
> Right-click PowerShell → Run as administrator. The Ableton User
> Library copy works without admin.

### Windows — manual

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target SolVoiceTuner_VST3
```

The VST3 will land in
`build/SolVoiceTuner_artefacts/Release/VST3/Sol Voice Tuner.vst3`.

### Standalone

A standalone app target (`SolVoiceTuner_Standalone`) is built by
default — handy for quick testing without launching a DAW.

---

## How the algorithm works

1. **Pitch detection.**
   `PitchDetector` keeps a 2048-sample ring buffer of mono mix-down.
   Every 256 samples (~5.8 ms at 44.1 kHz) it runs the YIN difference
   function, locates the first dip below `0.10`, refines with
   parabolic interpolation, and publishes the result via an atomic
   float. An RMS gate (`silenceThreshold = 0.005`) suppresses unvoiced
   sections.

2. **Scale snap.**
   `ScaleQuantizer::snapHzToScale` converts the detected Hz to MIDI,
   then snaps to the nearest pitch class allowed by the (Root, Scale)
   pair. With **MIDI Control** on, the pitch is instead snapped to the
   nearest currently-held MIDI note.

3. **Smoothing.**
   The instantaneous ratio `target/detected` is one-pole low-passed
   with a coefficient driven by the **Retune Speed** knob.
   `Robot Mode` bypasses smoothing for hard-snap.

4. **Pitch shifting.**
   `PitchShifter` is a thin wrapper around
   `signalsmith::stretch::SignalsmithStretch<float>`. It updates the
   transposition factor each block via `setTransposeFactor`, and (when
   `Formant Preserve` is on) applies `setFormantFactor(1.0, true)` so
   formants stay anchored even at large shifts.

5. **Mixing.**
   The shifted signal is mixed with the dry signal twice:
   * once via **Correction Amount** (per-sample lerp) — controls how
     aggressive the correction sounds.
   * once via **Dry/Wet** — overall plugin balance.

---

## Performance notes

- All audio-thread code is **allocation-free** after `prepareToPlay`.
- YIN runs on a hop interval (256 samples) so CPU stays low even on
  long analysis windows.
- Signalsmith Stretch is much faster in **Release** builds (Debug is
  ~10× slower, per the library's README). Always build Release for
  serious testing.
- Plug-in latency is reported to the host via `setLatencySamples` so
  the DAW can compensate.

---

## Credits / references

- [Signalsmith-Audio/signalsmith-stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
  — pitch-shift engine.
- [adamski/pitch_detector](https://github.com/adamski/pitch_detector)
  — algorithmic inspiration for the YIN implementation.
- [DamRsn/VocoderProject](https://github.com/DamRsn/VocoderProject) and
  [dot-operator/Auto-Tuner](https://github.com/dot-operator/Auto-Tuner)
  — overall AutoTune signal-flow patterns.
- [JUCE 8](https://juce.com/) — plugin framework.

## License

Source code in this repository is MIT-licensed.
Signalsmith Stretch is MIT-licensed.
JUCE is licensed separately (free for personal/educational use; commercial
use requires a JUCE license — see <https://juce.com/get-juce>).
