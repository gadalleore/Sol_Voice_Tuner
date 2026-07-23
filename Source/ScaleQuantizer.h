/*
    ScaleQuantizer.h
    ----------------
    Pure helper functions for converting Hz <-> MIDI note and snapping
    a detected pitch to the nearest note belonging to a chosen
    root + scale (or a MIDI-driven set of allowed notes).

    Header-only -- no JUCE dependency, can be unit-tested in isolation.
*/

#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace SolTune
{
    // ------------------------------------------------------------------------
    // Roots
    // ------------------------------------------------------------------------
    /** Maps any integer (e.g. MIDI pitch class, root index) to 0..11. */
    constexpr int pitchClass12 (int x) noexcept
    {
        const int pc = x % 12;
        return pc < 0 ? pc + 12 : pc;
    }

    /** Clamp a root / scale choice index from APVTS (denormalised 0..N-1). */
    constexpr int clampChoiceIndex (int index, int count) noexcept
    {
        return count <= 0 ? 0 : (index < 0 ? 0 : (index >= count ? count - 1 : index));
    }

    enum class Root : int
    {
        C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B
    };

    /** Sharp spelling for pitch-class / key labels (UI, host choice list, readouts). */
    inline const char* rootName (int rootIndex) noexcept
    {
        switch (pitchClass12 (rootIndex))
        {
            case 0:  return "C";
            case 1:  return "C#";
            case 2:  return "D";
            case 3:  return "D#";
            case 4:  return "E";
            case 5:  return "F";
            case 6:  return "F#";
            case 7:  return "G";
            case 8:  return "G#";
            case 9:  return "A";
            case 10: return "A#";
            default: return "B";
        }
    }

    /** Same as `rootName` — key / root display uses sharps throughout the plug-in. */
    inline const char* rootChoiceLabel (int rootIndex) { return rootName (rootIndex); }

    // ------------------------------------------------------------------------
    // Scales (12-bit pitch class masks, bit 0 = root)
    // UI order matches the Scale dropdown (indices are saved in APVTS).
    // ------------------------------------------------------------------------
    enum class Scale : int
    {
        Chromatic = 0,
        Major,
        Minor,
        Pentatonic,       // major pentatonic
        Blues,            // minor blues (hexatonic)
        Dorian,
        PhrygianDominant, // Spanish / Egyptian dominant (5th mode of harmonic minor)
        Mixolydian,
        Lydian,
        HarmonicMinor,
        // New scales appended below — do NOT reorder above, existing presets
        // store the integer index of the choice parameter.
        Phrygian,
        Locrian,
        MinorPentatonic,
        MelodicMinor,     // ascending form (jazz minor)
        MajorBlues,       // major hexatonic blues
        HungarianMinor,   // a.k.a. Gypsy Minor
        WholeTone,
        NumScales
    };

    inline const char* scaleName (int s)
    {
        static constexpr const char* names[(int) Scale::NumScales] =
        {
            "Chromatic",
            "Major",
            "Minor",
            "Major Pentatonic",
            "Minor Blues",
            "Dorian",
            "Phrygian Dominant (Egyptian / Dub)",
            "Mixolydian",
            "Lydian",
            "Harmonic Minor",
            "Phrygian",
            "Locrian",
            "Minor Pentatonic",
            "Melodic Minor",
            "Major Blues",
            "Hungarian Minor",
            "Whole Tone"
        };
        const int i = s < 0 ? 0 : (s >= (int) Scale::NumScales ? (int) Scale::NumScales - 1 : s);
        return names[i];
    }

    /** 12-bit pitch class mask for each scale, root = bit 0. */
    inline uint16_t scaleMask (Scale s)
    {
        switch (s)
        {
            case Scale::Chromatic:        return 0b111111111111;
            case Scale::Major:            return 0b101010110101; // C D E F G A B
            case Scale::Minor:            return 0b010110101101; // natural minor
            case Scale::Pentatonic:       return 0b001010010101; // C D E G A
            case Scale::Blues:            return 0b010011101001; // + b5
            case Scale::Dorian:           return 0b011010101101; // natural 6 / b7 (C D Eb F G A Bb)
            case Scale::PhrygianDominant: return (uint16_t) 0b10110110011; // 1 b2 3 4 5 b6 b7
            case Scale::Mixolydian:       return (uint16_t) 0b011010110101; // major + b7 (0,2,4,5,7,9,10)
            case Scale::Lydian:           return (uint16_t) 0b101011010101; // major + #4
            case Scale::HarmonicMinor:    return (uint16_t) 0b100110101101; // natural 7 on minor (0,2,3,5,7,8,11)
            case Scale::Phrygian:         return (uint16_t) 0b010110101011; // 1 b2 b3 4 5 b6 b7 (0,1,3,5,7,8,10)
            case Scale::Locrian:          return (uint16_t) 0b010101101011; // 1 b2 b3 4 b5 b6 b7 (0,1,3,5,6,8,10)
            case Scale::MinorPentatonic:  return (uint16_t) 0b010010101001; // 1 b3 4 5 b7 (0,3,5,7,10)
            case Scale::MelodicMinor:     return (uint16_t) 0b101010101101; // 1 2 b3 4 5 6 7 (0,2,3,5,7,9,11)
            case Scale::MajorBlues:       return (uint16_t) 0b001010011101; // 1 2 b3 3 5 6 (0,2,3,4,7,9)
            case Scale::HungarianMinor:   return (uint16_t) 0b100111001101; // 1 2 b3 #4 5 b6 7 (0,2,3,6,7,8,11)
            case Scale::WholeTone:        return (uint16_t) 0b010101010101; // 1 2 3 #4 #5 b7 (0,2,4,6,8,10)
            default:                      return 0b111111111111;
        }
    }

    // ------------------------------------------------------------------------
    // Pitch <-> MIDI helpers (A4 = 69 = 440 Hz)
    // ------------------------------------------------------------------------
    inline float hzToMidi (float hz) noexcept
    {
        if (hz <= 0.0f) return 0.0f;
        return 69.0f + 12.0f * std::log2 (hz / 440.0f);
    }

    inline float midiToHz (float midi) noexcept
    {
        return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
    }

    inline std::string midiNoteName (int midi)
    {
        const int pc     = ((midi % 12) + 12) % 12;
        const int octave = (midi / 12) - 1;
        char buf[8] = {0};
        std::snprintf (buf, sizeof (buf), "%s%d", rootName (pc), octave);
        return std::string (buf);
    }

    // ------------------------------------------------------------------------
    // Allowed-note set
    // ------------------------------------------------------------------------
    /** Returns a 12-bit pitch class mask, rotated by `root` semitones. */
    inline uint16_t allowedPitchClasses (Root root, Scale scale)
    {
        const uint16_t base = scaleMask (scale);
        const int      r    = pitchClass12 ((int) root);
        const uint16_t lo   = (uint16_t) ((base << r) & 0xFFFu);
        const uint16_t hi   = (uint16_t) ((base >> (12 - r)) & 0xFFFu);
        return (uint16_t) (lo | hi);
    }

    /** Snap `midiPitch` to the nearest allowed note inside `mask`.
        Returns the snapped MIDI pitch as a float (integer-valued). */
    inline float snapMidiToMask (float midiPitch, uint16_t mask)
    {
        if (mask == 0) return midiPitch;

        const int center = (int) std::round (midiPitch);
        for (int radius = 0; radius < 12; ++radius)
        {
            for (int sign : { 0, +1, -1 })
            {
                if (radius == 0 && sign != 0) continue;
                const int candidate = center + sign * radius;
                const int pc        = ((candidate % 12) + 12) % 12;
                if (mask & (1u << pc))
                    return (float) candidate;
            }
        }
        return midiPitch;
    }

    /** Transpose `midiPitch` by `steps` SCALE DEGREES within `mask`
        (12-bit pitch-class mask). Positive = up, negative = down. A full
        chromatic mask treats each step as one semitone (the "dumb harmonizer");
        any other scale walks diatonic degrees so a fixed step number yields a
        musically-correct interval that changes quality with the note. The start
        note is snapped into the scale first. Returns the resulting MIDI note. */
    inline int transposeBySteps (int midiPitch, int steps, uint16_t mask)
    {
        if (mask == 0 || (mask & 0x0FFFu) == 0x0FFFu)
            return midiPitch + steps;                       // chromatic: literal semitones

        int note = (int) std::lround (snapMidiToMask ((float) midiPitch, mask));
        if (steps == 0)
            return note;

        const int dir = steps > 0 ? 1 : -1;
        for (int remaining = std::abs (steps); remaining > 0; --remaining)
        {
            note += dir;
            for (int guard = 0;
                 guard < 12 && ! (mask & (1u << (((note % 12) + 12) % 12)));
                 ++guard)
                note += dir;
        }
        return note;
    }

    /** Snap a frequency in Hz to the nearest scale note, returning Hz. */
    inline float snapHzToScale (float hz, Root root, Scale scale)
    {
        if (hz <= 0.0f) return 0.0f;
        const float midi    = hzToMidi (hz);
        const float snapped = snapMidiToMask (midi, allowedPitchClasses (root, scale));
        return midiToHz (snapped);
    }

    /** Snap to the closest note in an externally-supplied set of MIDI notes
        (used for MIDI-controlled allowed pitches). */
    inline float snapHzToMidiSet (float hz, const std::vector<int>& midiNotes)
    {
        if (hz <= 0.0f || midiNotes.empty()) return hz;

        const float midi = hzToMidi (hz);
        int   best = midiNotes.front();
        float bestDist = std::abs ((float) best - midi);
        for (int n : midiNotes)
        {
            const float d = std::abs ((float) n - midi);
            if (d < bestDist) { bestDist = d; best = n; }
        }
        return midiToHz ((float) best);
    }
}
