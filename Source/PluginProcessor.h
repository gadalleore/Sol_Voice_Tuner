/*
    PluginProcessor.h
    -----------------
    Top-level audio processor for Sol Voice Tuner (PitchCorrectorVST lineage).

    Signal flow (63C-15 audio graph):
        input -> Input Global FX chain
        -> lead voice: pitch detect (mono) -> snap to scale or held MIDI notes
           -> ratio toward snapped pitch (Robotic = retune speed + humanize + throat formant mix)
           -> optional pitch-bend (fader x range + MIDI wheel) on transpose
           -> Signalsmith Stretch (transpose + independent formant)
           -> Voice FX chain
        -> harmony voices summing point (stub until 63C-13)
        -> Output Global FX chain -> master volume
        (bypass skips everything after detection)

    Real-time: no allocations on the audio thread after prepareToPlay().
*/

#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "ScaleQuantizer.h"
#include "VocalFx.h"
#include "EffectChain.h"

#include <array>
#include <atomic>
#include <vector>

class PitchCorrectorAudioProcessor : public juce::AudioProcessor
{
public:
    PitchCorrectorAudioProcessor();
    ~PitchCorrectorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool   acceptsMidi()         const override { return true;  }
    bool   producesMidi()        const override { return false; }
    bool   isMidiEffect()        const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()        override { return 1; }
    int  getCurrentProgram()     override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int)            override { return {}; }
    void changeProgramName (int, const juce::String&)  override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    float getDetectedPitchHz() const noexcept { return detector.getPitchHz(); }
    float getDetectionConfidence() const noexcept { return detector.getConfidence(); }
    float getSnappedTargetHz() const noexcept { return latestSnappedHz.load (std::memory_order_relaxed); }

    /** Minimum Hz treated as valid pitched input (YIN range + UI readout). */
    static constexpr float minTrackedPitchHz = 55.0f;

    int getLastMidiChannel() const noexcept { return lastMidiChannel.load (std::memory_order_relaxed); }
    int getActiveMidiNoteCount() const noexcept { return activeMidiNoteCount.load (std::memory_order_relaxed); }

    /** Thread-safe snapshot of the latest output block for the oscilloscope view. */
    const juce::AudioBuffer<float>& getScopeBuffer() const noexcept
    {
        return scopeBuffers[scopeReadIndex.load (std::memory_order_acquire)];
    }
    int getScopeValidSamples() const noexcept { return scopeValidSamples.load (std::memory_order_acquire); }

    static constexpr const char* PID_BYPASS       = "bypass";
    static constexpr const char* PID_ROBOTIC    = "robotic";
    static constexpr const char* PID_SUB        = "sub";
    static constexpr const char* PID_FORMANT    = "formant";
    static constexpr const char* PID_SCALE      = "scale";
    static constexpr const char* PID_ROOT       = "root";
    static constexpr const char* PID_MIDI_FOLLOW = "midiFollow";
    static constexpr const char* PID_PITCH_BEND  = "pitchBend";
    static constexpr const char* PID_BEND_RANGE  = "bendRange";
    static constexpr const char* PID_VOLUME      = "volumeDb";

    /** The three effect chains of the v3 audio graph (63C-15). Order is fixed:
        input global -> lead voice -> output global. */
    enum FxChainId
    {
        fxChainInput = 0,
        fxChainVoice,
        fxChainOutput,
        numFxChains
    };

    /** Param-ID prefix per chain ("fxIn" / "fxVoice" / "fxOut"). */
    static const char* fxChainPrefix (int chain) noexcept
    {
        switch (chain)
        {
            case fxChainInput:  return "fxIn";
            case fxChainVoice:  return "fxVoice";
            case fxChainOutput: return "fxOut";
            default:            return "fx";
        }
    }

    /** Human-readable chain name for parameter display. */
    static const char* fxChainDisplayName (int chain) noexcept
    {
        switch (chain)
        {
            case fxChainInput:  return "Input";
            case fxChainVoice:  return "Voice";
            case fxChainOutput: return "Output";
            default:            return "FX";
        }
    }

    /** Per-chain, per-slot type + Amount parameter IDs (e.g. "fxInType1"). */
    static juce::String fxTypeParamId (int chain, int slot)
    {
        return fxChainPrefix (chain) + juce::String ("Type") + juce::String (slot + 1);
    }
    static juce::String fxAmountParamId (int chain, int slot)
    {
        return fxChainPrefix (chain) + juce::String ("Amount") + juce::String (slot + 1);
    }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PitchDetector detector;
    PitchShifter  shifter;

    VocalFx::SubVocoder      subVocoder;
    VocalFx::DeEsser         deEsser;
    VocalFx::PinkNoiseSource pinkNoise;

    // input global / lead voice / output global (indexed by FxChainId).
    std::array<VocalFx::EffectChain, numFxChains> fxChains;

    // Cached raw-value pointers for the chains' 36 params (avoids per-block
    // String construction on the audio thread).
    using ChainParamPtrs = std::array<std::atomic<float>*, VocalFx::EffectChain::kNumSlots>;
    std::array<ChainParamPtrs, numFxChains> fxTypeParams   {};
    std::array<ChainParamPtrs, numFxChains> fxAmountParams {};

    // Per-sample mono-summed dry input — modulator for the sub vocoder.
    std::vector<float> monoInputScratch;

    // Per-sample sibilance presence published by the de-esser; gates pink noise.
    std::vector<float> sibilanceScratch;

    // Double-buffered post-processing snapshot for the editor's oscilloscope.
    std::array<juce::AudioBuffer<float>, 2> scopeBuffers;
    std::atomic<int> scopeReadIndex     { 0 };
    std::atomic<int> scopeValidSamples  { 0 };

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate { 44100.0 };
    int    currentBlockSize  { 512 };

    float smoothedRatio { 1.0f };

    /** Exponential correction in log2 space (without humanize offset). */
    float correctionLog2 { 0.0f };

    /** Prior |log2(idealRatio) - log2(smoothedRatio)|; used for attack vs release on the audio thread. */
    float prevAbsLogErr { 0.0f };

    /** Slow micro-pitch LFO + smoothed noise (log2 space); strongest when Robotic -> 0. */
    float humanizePhase { 0.0f };
    float humanizeNoise { 0.0f };
    uint32_t humanizeRng { 0xA341316Cu };

    /** Consecutive blocks on the same snapped target (for humanize gating). */
    int stableTargetBlocks { 0 };

    int lastSnappedTargetMidi { -100 };

    std::vector<int> activeMidiNotes;

    std::atomic<float> latestSnappedHz { 0.0f };
    std::atomic<int>   lastMidiChannel { 0 };
    std::atomic<int>   activeMidiNoteCount { 0 };
    /** MIDI pitch wheel as -1..+1 add-on to the Pitch Bend fader (centre = 0). */
    std::atomic<float> midiPitchBendNorm { 0.0f };

    void runPitchDetection (const juce::AudioBuffer<float>& buffer);

    /** Forwards one chain's APVTS params to its EffectChain and processes the buffer. */
    void applyFxChain (int chain, juce::AudioBuffer<float>& buffer) noexcept;

    /** 63C-13 hook: harmony voices will render and sum onto the lead voice here.
        The summing point's place in the graph (post voice chain, pre output
        chain) is fixed now so 63C-13 slots in without another restructure. */
    void mixHarmonyVoices (juce::AudioBuffer<float>& buffer) noexcept;

    void updateMidiNotes (const juce::MidiBuffer& midi);

    float computeSnappedTargetHz (float detectedHz);

    /** Move smoothedRatio toward ideal correction; `robotic` = retune speed + humanize depth. */
    float computeSmoothedRatio (float detectedHz, float snappedHz, float robotic);

    /** Formant knob + automatic "Throat" compensation from pitch ratio and Robotic. */
    float computeEffectiveFormantSemitones (float pitchRatio, float formantKnob, float robotic) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCorrectorAudioProcessor)
};
