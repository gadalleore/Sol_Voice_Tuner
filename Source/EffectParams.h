/*
    EffectParams.h
    --------------
    What every effect's controls ARE (63C-8, second half).

    One table per effect type, listing every knob, toggle and dropdown it has in
    Space Dust, with Space Dust's own ranges, skews and defaults. Three things
    read these tables and nothing else describes the controls:

      * PluginProcessor::createParameterLayout — builds the APVTS parameters;
      * EffectDetailPage                       — builds the controls and attaches
                                                 them, so a page is never written
                                                 by hand and cannot drift;
      * PluginProcessor::processBlock          — snapshots the values per block
                                                 and hands them to the effect.

    ONE SET PER CHAIN, NOT PER SLOT. In Space Dust each effect exists exactly
    once, with one panel of controls; the same is true here. A chain's Reverb has
    one set of knobs no matter which slot it sits in — the slot decides only WHERE
    in the order it runs. That is why the effects wheels forbid duplicates: two
    Reverbs in one chain would share one set of controls and read as a bug. The
    three chains (input / voice / output) each get their own full set, so the
    input chain's delay and the output chain's delay are independent.

    Parameter ids are "<chain>_<effect>_<param>", e.g. "fxIn_reverb_wetMix".

    Index order IS the wire format: each effect's `...Idx` enum below indexes
    both its table and the per-block value snapshot the adapters read. Append
    only — inserting in the middle silently rewires every saved session.
*/

#pragma once

#include <JuceHeader.h>

#include "VocalEffectBase.h"

#include <array>

namespace VocalFx
{
    /** The largest number of controls any one effect owns (Trance Gate, with
        its sixteen steps). Sizes the per-block value snapshot. */
    static constexpr int kMaxEffectParams = 24;

    /** How the detail page draws a control, and which APVTS parameter type
        backs it. */
    enum class ParamKind { Knob, Toggle, Choice };

    struct EffectParam
    {
        const char* id;          //!< unique within its effect, e.g. "wetMix"
        const char* name;        //!< shown under the control
        ParamKind   kind;
        float       min;
        float       max;
        float       def;
        float       step       = 0.01f;
        float       skew       = 1.0f;   //!< JUCE skew factor; 1 = linear
        float       skewCentre = 0.0f;   //!< if > min, overrides `skew`
        const char* choices    = nullptr;//!< '|'-separated, Choice only
        const char* unit       = "";
    };

    /** A view over one effect's table. */
    struct ParamList
    {
        const EffectParam* items = nullptr;
        int                count = 0;

        const EffectParam* begin() const noexcept { return items; }
        const EffectParam* end()   const noexcept { return items + count; }
        const EffectParam& operator[] (int i) const noexcept { return items[i]; }
    };

    //==============================================================================
    // Shared shapes. Space Dust filters every wet path the same way, so the four
    // filter controls repeat verbatim across Reverb, Delay and Grain Delay.
    //==============================================================================
    #define VOCALFX_HP_CUTOFF(defHz, sk) { "hpCut", "HP Cutoff", ParamKind::Knob, 20.0f, 20000.0f, defHz, 1.0f, sk, 0.0f, nullptr, " Hz" }
    #define VOCALFX_LP_CUTOFF(defHz, sk) { "lpCut", "LP Cutoff", ParamKind::Knob, 20.0f, 20000.0f, defHz, 1.0f, sk, 0.0f, nullptr, " Hz" }
    #define VOCALFX_HP_RES(defV)         { "hpRes", "HP Res",    ParamKind::Knob, 0.0f, 1.0f, defV }
    #define VOCALFX_LP_RES(defV)         { "lpRes", "LP Res",    ParamKind::Knob, 0.0f, 1.0f, defV }

    //==============================================================================
    // Saturate — Sol's own, not from Space Dust. Kept because it is the effect
    // the chain was built and proved against.
    //==============================================================================
    namespace SaturateIdx { enum { Drive, Mix, Count }; }

    inline constexpr EffectParam kSaturateParams[] =
    {
        { "drive", "Drive", ParamKind::Knob, 0.0f, 1.0f, 0.35f },
        { "mix",   "Mix",   ParamKind::Knob, 0.0f, 1.0f, 1.0f  },
    };
    static_assert ((int) std::size (kSaturateParams) == SaturateIdx::Count, "");

    //==============================================================================
    // Reverb. Type picks Schroeder (Freeverb) or Void Verb (the Dattorro plate);
    // Sol defaults to the plate, which is the one that suits a voice.
    //==============================================================================
    namespace ReverbIdx
    {
        enum { Type, WetMix, DecayTime, FilterOn, HpCut, HpRes, LpCut, LpRes, WarmSat, Count };
    }

    inline constexpr EffectParam kReverbParams[] =
    {
        { "type",      "Type",   ParamKind::Choice, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, "Schroeder|Void Verb" },
        { "wetMix",    "Mix",    ParamKind::Knob,   0.0f, 1.0f, 0.33f },
        { "decayTime", "Decay",  ParamKind::Knob,   0.0f, 640.0f, 16.0f, 0.01f, 1.0f, 64.0f, nullptr, " s" },
        { "filterOn",  "Filter", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        VOCALFX_HP_CUTOFF (100.0f,  0.3f),
        VOCALFX_HP_RES    (0.3f),
        VOCALFX_LP_CUTOFF (8000.0f, 0.3f),
        VOCALFX_LP_RES    (0.3f),
        { "warmSat",   "Warm Sat", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kReverbParams) == ReverbIdx::Count, "");

    //==============================================================================
    // Delay. Time is Space Dust's 0-12 control, read against the musical-division
    // table when Sync is on and as a 20 ms - 2 s log sweep when it is off; in both
    // cases it is INVERTED, so turning it up shortens the delay.
    //==============================================================================
    namespace DelayIdx
    {
        enum { Sync, Rate, Decay, Mix, PingPong, FilterOn, HpCut, HpRes, LpCut, LpRes, WarmSat, Count };
    }

    inline constexpr EffectParam kDelayParams[] =
    {
        { "sync",     "Sync",      ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "rate",     "Time",      ParamKind::Knob,   0.0f, 12.0f, 6.0f, 0.01f },
        { "decay",    "Feedback",  ParamKind::Knob,   0.0f, 100.0f, 40.0f, 0.1f, 1.0f, 0.0f, nullptr, " %" },
        { "mix",      "Mix",       ParamKind::Knob,   0.0f, 100.0f, 50.0f, 0.1f, 1.0f, 0.0f, nullptr, " %" },
        { "pingPong", "Ping-Pong", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "filterOn", "Filter",    ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        VOCALFX_HP_CUTOFF (100.0f,  0.3f),
        VOCALFX_HP_RES    (0.3f),
        VOCALFX_LP_CUTOFF (8000.0f, 0.3f),
        VOCALFX_LP_RES    (0.3f),
        { "warmSat",  "Warm Sat",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kDelayParams) == DelayIdx::Count, "");

    //==============================================================================
    // Grain Delay.
    //==============================================================================
    namespace GrainIdx
    {
        enum { Time, Size, Pitch, Mix, Decay, Density, Jitter, PingPong,
               FilterOn, HpCut, HpRes, LpCut, LpRes, WarmSat, Count };
    }

    inline constexpr EffectParam kGrainParams[] =
    {
        { "time",     "Time",      ParamKind::Knob,   20.0f, 2000.0f, 200.0f, 1.0f, 0.3f, 0.0f, nullptr, " ms" },
        { "size",     "Grain",     ParamKind::Knob,   10.0f, 500.0f,  50.0f,  1.0f, 0.4f, 0.0f, nullptr, " ms" },
        { "pitch",    "Pitch",     ParamKind::Knob,  -12.0f, 12.0f,   0.0f,   0.1f, 1.0f, 0.0f, nullptr, " st" },
        { "mix",      "Mix",       ParamKind::Knob,   0.0f, 100.0f,  50.0f,   0.1f, 1.0f, 0.0f, nullptr, " %" },
        { "decay",    "Decay",     ParamKind::Knob,   0.0f, 150.0f,   0.0f,   0.1f, 1.0f, 0.0f, nullptr, " %" },
        { "density",  "Density",   ParamKind::Knob,   1.0f,   8.0f,   1.0f,   0.1f },
        { "jitter",   "Jitter",    ParamKind::Knob,   0.0f, 100.0f,   0.0f,   0.1f, 1.0f, 0.0f, nullptr, " %" },
        { "pingPong", "Ping-Pong", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "filterOn", "Filter",    ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        VOCALFX_HP_CUTOFF (100.0f,  0.25f),
        VOCALFX_HP_RES    (0.5f),
        VOCALFX_LP_CUTOFF (4000.0f, 0.25f),
        VOCALFX_LP_RES    (0.5f),
        { "warmSat",  "Warm Sat",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kGrainParams) == GrainIdx::Count, "");

    //==============================================================================
    // Phaser. Script is the MXR Script/Block Logo distinction: Script has no
    // feedback path at all, Block Logo feeds the last stage back to the first.
    //==============================================================================
    namespace PhaserIdx
    {
        enum { Rate, Depth, Feedback, Script, Mix, Centre, Stages, Width, Vintage, Count };
    }

    inline constexpr EffectParam kPhaserParams[] =
    {
        { "rate",     "Rate",     ParamKind::Knob,   0.05f, 200.0f, 1.0f, 0.01f, 0.35f, 0.0f, nullptr, " Hz" },
        { "depth",    "Depth",    ParamKind::Knob,   0.0f, 1.0f, 0.7f },
        { "feedback", "Feedback", ParamKind::Knob,  -1.0f, 1.0f, 0.0f },
        { "script",   "Script",   ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "mix",      "Mix",      ParamKind::Knob,   0.0f, 1.0f, 0.5f },
        { "centre",   "Center",   ParamKind::Knob,   50.0f, 2000.0f, 400.0f, 1.0f, 0.35f, 0.0f, nullptr, " Hz" },
        { "stages",   "Stages",   ParamKind::Choice, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, "4 (Phase 90)|6 (Deeper)" },
        { "width",    "Width",    ParamKind::Knob,   0.0f, 1.0f, 0.5f },
        { "vintage",  "Vintage",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kPhaserParams) == PhaserIdx::Count, "");

    //==============================================================================
    // Flanger.
    //==============================================================================
    namespace FlangerIdx { enum { Rate, Depth, Feedback, Width, Mix, Count }; }

    inline constexpr EffectParam kFlangerParams[] =
    {
        { "rate",     "Rate",     ParamKind::Knob, 0.05f, 200.0f, 0.5f, 0.01f, 0.35f, 0.0f, nullptr, " Hz" },
        { "depth",    "Depth",    ParamKind::Knob, 0.0f, 1.0f, 0.5f },
        { "feedback", "Feedback", ParamKind::Knob, -1.0f, 1.0f, 0.0f },
        { "width",    "Width",    ParamKind::Knob, 0.0f, 1.0f, 0.5f },
        { "mix",      "Mix",      ParamKind::Knob, 0.0f, 1.0f, 0.5f },
    };
    static_assert ((int) std::size (kFlangerParams) == FlangerIdx::Count, "");

    //==============================================================================
    // Trance Gate. Steps 1-16 are the pattern; how many of them are used comes
    // from Steps (4 / 8 / 16).
    //==============================================================================
    namespace GateIdx
    {
        enum { Steps, Sync, Rate, Attack, Release, Mix, Step1, Count = Step1 + 16 };
    }

    inline constexpr EffectParam kGateParams[] =
    {
        { "steps",   "Steps",   ParamKind::Choice, 0.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.0f, "4|8|16" },
        { "sync",    "Sync",    ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "rate",    "Rate",    ParamKind::Knob,   0.0f, 12.0f, 4.0f, 0.01f },
        { "attack",  "Attack",  ParamKind::Knob,   0.1f, 50.0f, 2.0f, 0.1f, 0.4f, 0.0f, nullptr, " ms" },
        { "release", "Release", ParamKind::Knob,   0.1f, 50.0f, 5.0f, 0.1f, 0.4f, 0.0f, nullptr, " ms" },
        { "mix",     "Mix",     ParamKind::Knob,   0.0f, 1.0f, 1.0f },

        { "step1",  "1",  ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step2",  "2",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step3",  "3",  ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step4",  "4",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step5",  "5",  ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step6",  "6",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step7",  "7",  ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step8",  "8",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step9",  "9",  ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step10", "10", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step11", "11", ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step12", "12", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step13", "13", ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step14", "14", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "step15", "15", ParamKind::Toggle, 0.0f, 1.0f, 1.0f },
        { "step16", "16", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kGateParams) == GateIdx::Count, "");
    static_assert (GateIdx::Count <= kMaxEffectParams, "kMaxEffectParams too small");

    //==============================================================================
    // Bit Crush.
    //==============================================================================
    namespace CrushIdx { enum { Amount, Rate, Mix, Count }; }

    inline constexpr EffectParam kCrushParams[] =
    {
        { "amount", "Crush", ParamKind::Knob, 0.0f, 1.0f, 0.5f },
        { "rate",   "Rate",  ParamKind::Knob, 0.0f, 1.0f, 0.0f },
        { "mix",    "Mix",   ParamKind::Knob, 0.0f, 1.0f, 0.5f },
    };
    static_assert ((int) std::size (kCrushParams) == CrushIdx::Count, "");

    //==============================================================================
    // Soft Clip. Drive and Knee are 0-1 controls; the clipper maps them onto its
    // own 0.5-5.0 drive and 0.3-1.0 threshold internally, as in Space Dust.
    //==============================================================================
    namespace ClipIdx { enum { Mode, Drive, Knee, Oversample, Mix, Count }; }

    inline constexpr EffectParam kClipParams[] =
    {
        { "mode",  "Mode",  ParamKind::Choice, 0.0f, 4.0f, 0.0f, 1.0f, 1.0f, 0.0f, "Smooth|Crisp|Tube|Tape|Guitar" },
        { "drive", "Drive", ParamKind::Knob,   0.0f, 1.0f, 0.35f },
        { "knee",  "Knee",  ParamKind::Knob,   0.0f, 1.0f, 0.67f },
        { "os",    "Oversample", ParamKind::Choice, 0.0f, 3.0f, 1.0f, 1.0f, 1.0f, 0.0f, "2x|4x|8x|16x" },
        { "mix",   "Mix",   ParamKind::Knob,   0.0f, 1.0f, 1.0f },
    };
    static_assert ((int) std::size (kClipParams) == ClipIdx::Count, "");

    //==============================================================================
    // Compress.
    //==============================================================================
    namespace CompIdx
    {
        enum { Type, Threshold, Ratio, Attack, Release, Makeup, Mix, AutoRelease, SoftClip, Count };
    }

    inline constexpr EffectParam kCompParams[] =
    {
        { "type",      "Type",      ParamKind::Choice, 0.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0.0f, "SSL Bus|1176 FET|Opto" },
        { "threshold", "Threshold", ParamKind::Knob, -60.0f, 0.0f, -12.0f, 0.1f, 1.0f, 0.0f, nullptr, " dB" },
        { "ratio",     "Ratio",     ParamKind::Knob,   1.0f, 20.0f, 4.0f, 0.1f, 0.5f, 0.0f, nullptr, ":1" },
        { "attack",    "Attack",    ParamKind::Knob,   0.1f, 80.0f, 3.0f, 0.01f, 1.0f, 5.0f, nullptr, " ms" },
        { "release",   "Release",   ParamKind::Knob,   5.0f, 1200.0f, 100.0f, 0.1f, 1.0f, 100.0f, nullptr, " ms" },
        { "makeup",    "Makeup",    ParamKind::Knob,   0.0f, 24.0f, 0.0f, 0.1f, 1.0f, 0.0f, nullptr, " dB" },
        { "mix",       "Mix",       ParamKind::Knob,   0.0f, 1.0f, 1.0f },
        { "autoRel",   "Auto Rel",  ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
        { "softClip",  "Soft Clip", ParamKind::Toggle, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kCompParams) == CompIdx::Count, "");

    //==============================================================================
    // Lo-Fi — one macro upstream, one macro here.
    //==============================================================================
    namespace LofiIdx { enum { Amount, Count }; }

    inline constexpr EffectParam kLofiParams[] =
    {
        { "amount", "Amount", ParamKind::Knob, 0.0f, 1.0f, 0.0f },
    };
    static_assert ((int) std::size (kLofiParams) == LofiIdx::Count, "");

    //==============================================================================
    // Final EQ — five bands, each with its own shape.
    //==============================================================================
    namespace EqIdx
    {
        enum { B1Freq, B1Gain, B1Q, B1Type,
               B2Freq, B2Gain, B2Q, B2Type,
               B3Freq, B3Gain, B3Q, B3Type,
               B4Freq, B4Gain, B4Q, B4Type,
               B5Freq, B5Gain, B5Q, B5Type, Count };

        /** Four controls per band, in this order. */
        static constexpr int perBand = 4;
    }

    #define VOCALFX_EQ_BAND(n, defHz, defQ, defType)                                                              \
        { "b" #n "Freq", "B" #n " Freq", ParamKind::Knob, 20.0f, 20000.0f, defHz, 1.0f, 0.2f, 0.0f, nullptr, " Hz" }, \
        { "b" #n "Gain", "B" #n " Gain", ParamKind::Knob, -15.0f, 15.0f, 0.0f, 0.01f, 1.0f, 0.0f, nullptr, " dB" },   \
        { "b" #n "Q",    "B" #n " Q",    ParamKind::Knob, 0.1f, 10.0f, defQ, 0.01f, 0.3f },                           \
        { "b" #n "Type", "B" #n " Type", ParamKind::Choice, 0.0f, 4.0f, defType, 1.0f, 1.0f, 0.0f,                    \
          "Low Shelf|High Shelf|Low Pass|High Pass|Bell" }

    inline constexpr EffectParam kEqParams[] =
    {
        VOCALFX_EQ_BAND (1,    80.0f, 0.707f, 0.0f),   // Low Shelf
        VOCALFX_EQ_BAND (2,   250.0f, 1.0f,   4.0f),   // Bell
        VOCALFX_EQ_BAND (3,  1000.0f, 1.0f,   4.0f),   // Bell
        VOCALFX_EQ_BAND (4,  4000.0f, 1.0f,   4.0f),   // Bell
        VOCALFX_EQ_BAND (5, 10000.0f, 0.707f, 1.0f),   // High Shelf
    };
    static_assert ((int) std::size (kEqParams) == EqIdx::Count, "");
    static_assert (EqIdx::Count <= kMaxEffectParams, "kMaxEffectParams too small");

    //==============================================================================
    /** One effect type's whole control set. Empty for EffectType::Empty. */
    inline ParamList effectParams (EffectType t) noexcept
    {
        switch (t)
        {
            case EffectType::Saturate:   return { kSaturateParams, (int) std::size (kSaturateParams) };
            case EffectType::Reverb:     return { kReverbParams,   (int) std::size (kReverbParams)   };
            case EffectType::Delay:      return { kDelayParams,    (int) std::size (kDelayParams)    };
            case EffectType::GrainDelay: return { kGrainParams,    (int) std::size (kGrainParams)    };
            case EffectType::Phaser:     return { kPhaserParams,   (int) std::size (kPhaserParams)   };
            case EffectType::Flanger:    return { kFlangerParams,  (int) std::size (kFlangerParams)  };
            case EffectType::TranceGate: return { kGateParams,     (int) std::size (kGateParams)     };
            case EffectType::BitCrush:   return { kCrushParams,    (int) std::size (kCrushParams)    };
            case EffectType::SoftClip:   return { kClipParams,     (int) std::size (kClipParams)     };
            case EffectType::Compress:   return { kCompParams,     (int) std::size (kCompParams)     };
            case EffectType::Lofi:       return { kLofiParams,     (int) std::size (kLofiParams)     };
            case EffectType::FinalEQ:    return { kEqParams,       (int) std::size (kEqParams)       };
            case EffectType::Empty:
            case EffectType::NumTypes:   break;
        }
        return {};
    }

    /** Short id fragment used to build parameter ids, per effect type. Fixed
        for the life of the plugin: changing one orphans saved sessions. */
    inline const char* effectParamPrefix (EffectType t) noexcept
    {
        switch (t)
        {
            case EffectType::Saturate:   return "sat";
            case EffectType::Reverb:     return "reverb";
            case EffectType::Delay:      return "delay";
            case EffectType::GrainDelay: return "grain";
            case EffectType::Phaser:     return "phaser";
            case EffectType::Flanger:    return "flanger";
            case EffectType::TranceGate: return "gate";
            case EffectType::BitCrush:   return "crush";
            case EffectType::SoftClip:   return "clip";
            case EffectType::Compress:   return "comp";
            case EffectType::Lofi:       return "lofi";
            case EffectType::FinalEQ:    return "eq";
            case EffectType::Empty:
            case EffectType::NumTypes:   break;
        }
        return "none";
    }

    /** Builds the JUCE range a control declares. */
    inline juce::NormalisableRange<float> effectParamRange (const EffectParam& p)
    {
        juce::NormalisableRange<float> r (p.min, p.max, p.step, p.skew);

        if (p.skewCentre > p.min && p.skewCentre < p.max)
            r.setSkewForCentre (p.skewCentre);

        return r;
    }

    /** Per-block values for every effect of one chain, indexed
        [effect type][control index]. Filled on the audio thread from cached
        APVTS pointers, then handed to whichever effects are actually placed. */
    struct ChainParamValues
    {
        std::array<std::array<float, kMaxEffectParams>, (size_t) EffectType::NumTypes> v {};

        const float* forType (EffectType t) const noexcept
        {
            return v[(size_t) juce::jlimit (0, (int) EffectType::NumTypes - 1, (int) t)].data();
        }
    };
} // namespace VocalFx
