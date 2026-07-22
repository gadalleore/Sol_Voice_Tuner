/*

    PluginProcessor.cpp

*/



#include "PluginProcessor.h"

#include "PluginEditor.h"



namespace

{

    constexpr int   kAnalysisSize = 2048;

    constexpr int   kHopSize      = 256;

    constexpr float kFormantEps   = 1.0e-4f;

    /** One-pole low-pass coefficient for one buffer: alpha ≈ 1 - exp(-dt/tau), RT-safe, bounded. */
    static float alphaFromTauSeconds (double sampleRate, int blockSamples, float tauSeconds) noexcept
    {
        if (!(sampleRate > 0.0) || blockSamples <= 0 || tauSeconds <= 1.0e-6f)
            return 1.0f;

        const float dt  = (float) blockSamples / (float) sampleRate;
        const float tau = juce::jmax (tauSeconds, 1.0e-4f);
        const float x   = dt / tau;

        if (x > 4.5f)
            return 0.99f;

        return 1.0f - std::exp (-x);
    }

}



//==============================================================================

PitchCorrectorAudioProcessor::PitchCorrectorAudioProcessor()

    : AudioProcessor (BusesProperties()

                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)

                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),

      apvts (*this, nullptr, "PARAMS", createParameterLayout())

{

    activeMidiNotes.reserve (32);

    for (int c = 0; c < numFxChains; ++c)
        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
        {
            fxTypeParams  [(size_t) c][(size_t) s] = apvts.getRawParameterValue (fxTypeParamId (c, s));
            fxAmountParams[(size_t) c][(size_t) s] = apvts.getRawParameterValue (fxAmountParamId (c, s));
        }

    chainServicer.startTimerHz (30);

}



PitchCorrectorAudioProcessor::~PitchCorrectorAudioProcessor()
{
    chainServicer.stopTimer();
}

void PitchCorrectorAudioProcessor::serviceFxChains()
{
    if (! fxChainsPrepared.load (std::memory_order_acquire))
        return;

    for (int c = 0; c < numFxChains; ++c)
        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
            fxChains[(size_t) c].serviceSlot (s, (VocalFx::EffectType)
                juce::roundToInt (fxTypeParams[(size_t) c][(size_t) s]->load()));
}



//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout

PitchCorrectorAudioProcessor::createParameterLayout()

{

    using APF  = juce::AudioParameterFloat;

    using APB  = juce::AudioParameterBool;

    using APC  = juce::AudioParameterChoice;

    using APA  = juce::AudioParameterFloatAttributes;

    using Range = juce::NormalisableRange<float>;



    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;



    params.push_back (std::make_unique<APB> (juce::ParameterID { PID_BYPASS, 1 },

                                             "Bypass", false));



    const auto roboticAttrs = APA().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f));
    })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            auto t = text.trim().removeCharacters ("%").trim();
            return juce::jlimit (0.0f, 1.0f, (float) (t.getDoubleValue() / 100.0));
        })
        .withLabel ("%");

    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_ROBOTIC, 1 },

                                             "Robotic",

                                             Range (0.0f, 1.0f, 0.01f), 0.35f, roboticAttrs));



    // Sub carrier amount (same 0–100 % display behaviour as Robotic).
    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_SUB, 1 },

                                             "Sub",

                                             Range (0.0f, 1.0f, 0.01f), 0.0f, roboticAttrs));



    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_FORMANT, 1 },

                                             "Formant",

                                             Range (-12.0f, 12.0f, 0.01f), 0.0f));



    juce::StringArray rootNames;

    for (int i = 0; i < 12; ++i) rootNames.add (SolTune::rootChoiceLabel (i));

    params.push_back (std::make_unique<APC> (juce::ParameterID { PID_ROOT, 1 },

                                             "Key", rootNames, 0));



    juce::StringArray scaleNames;

    for (int i = 0; i < (int) SolTune::Scale::NumScales; ++i)

        scaleNames.add (SolTune::scaleName (i));

    params.push_back (std::make_unique<APC> (juce::ParameterID { PID_SCALE, 2 },

                                             "Scale", scaleNames,

                                             (int) SolTune::Scale::Major));



    params.push_back (std::make_unique<APB> (juce::ParameterID { PID_MIDI_FOLLOW, 1 },

                                             "MIDI Follow", false));



    const auto pitchBendAttrs = APA().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 100.0f));
    })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            auto t = text.trim().removeCharacters ("%").trim();
            return juce::jlimit (-1.0f, 1.0f, (float) (t.getDoubleValue() / 100.0));
        })
        .withLabel ("%");

    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_PITCH_BEND, 1 },

                                             "Pitch Bend",

                                             Range (-1.0f, 1.0f, 0.01f), 0.0f, pitchBendAttrs));



    const auto bendRangeAttrs = APA().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0f, 12.0f, v)));
    })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return (float) juce::jlimit (0, 12, text.trim().getIntValue());
        })
        .withLabel (" st");

    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_BEND_RANGE, 1 },

                                             "Bend Range",

                                             Range (0.0f, 12.0f, 1.0f), 2.0f, bendRangeAttrs));



    const auto volumeAttrs = APA().withStringFromValueFunction ([] (float v, int)
    {
        if (v <= -59.95f) return juce::String ("-inf");
        return juce::String (v, 1);
    })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            auto t = text.trim().removeCharacters ("dB").trim();
            if (t.equalsIgnoreCase ("-inf") || t.equalsIgnoreCase ("inf"))
                return -60.0f;
            return (float) juce::jlimit (-60.0, 12.0, t.getDoubleValue());
        })
        .withLabel (" dB");

    Range volumeRange (-60.0f, 12.0f, 0.1f);
    volumeRange.setSkewForCentre (-12.0f);

    params.push_back (std::make_unique<APF> (juce::ParameterID { PID_VOLUME, 1 },

                                             "Volume",

                                             volumeRange, 0.0f, volumeAttrs));



    // 63C-15 audio graph: three effect chains (input global / lead voice /
    // output global), each with per-slot effect choice + Amount macro.
    // Choice item order must match VocalFx::EffectType (append-only — see
    // EffectChain.h). Param IDs replace the single-chain fxType*/fxAmount*
    // set from 63C-7; pre-release, so no session migration is provided.
    juce::StringArray fxNames;
    for (int t = 0; t < (int) VocalFx::EffectType::NumTypes; ++t)
        fxNames.add (VocalFx::effectTypeName ((VocalFx::EffectType) t));

    for (int c = 0; c < numFxChains; ++c)
        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
        {
            const auto slotName = juce::String (fxChainDisplayName (c))
                                + " FX " + juce::String (s + 1);

            params.push_back (std::make_unique<APC> (juce::ParameterID { fxTypeParamId (c, s), 1 },
                                                     slotName, fxNames, 0));

            params.push_back (std::make_unique<APF> (juce::ParameterID { fxAmountParamId (c, s), 1 },
                                                     slotName + " Amount",
                                                     Range (0.0f, 1.0f, 0.01f), 0.5f, roboticAttrs));
        }



    return { params.begin(), params.end() };

}



//==============================================================================

void PitchCorrectorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)

{

    currentSampleRate = sampleRate;

    currentBlockSize  = samplesPerBlock;



    detector.prepare (sampleRate, kAnalysisSize, kHopSize);

    detector.setFrequencyRange (minTrackedPitchHz, 1500.0f);



    const int ch = juce::jmax (getTotalNumOutputChannels(), getTotalNumInputChannels(), 2);

    shifter.prepare (sampleRate, samplesPerBlock, ch);

    shifter.setPitchRatio (1.0f);

    shifter.setFormantSemitones (apvts.getRawParameterValue (PID_FORMANT)->load());

    subVocoder.prepare (sampleRate);
    deEsser   .prepare (sampleRate, ch);
    pinkNoise .prepare (sampleRate);

    // Chains drop all instances on prepare; re-install synchronously from the
    // params so session load has no async gap (audio is not running here).
    fxChainsPrepared.store (false, std::memory_order_release);
    for (int c = 0; c < numFxChains; ++c)
    {
        fxChains[(size_t) c].prepare (sampleRate, samplesPerBlock, ch);
        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
        {
            fxChains[(size_t) c].setSlotAmount (s, fxAmountParams[(size_t) c][(size_t) s]->load());
            fxChains[(size_t) c].installImmediate (s, (VocalFx::EffectType)
                juce::roundToInt (fxTypeParams[(size_t) c][(size_t) s]->load()));
        }
    }
    fxChainsPrepared.store (true, std::memory_order_release);

    // Dry-input mono mix (sub vocoder modulator) + sibilance presence signal.
    monoInputScratch.assign ((size_t) juce::jmax (samplesPerBlock, 1), 0.0f);
    sibilanceScratch.assign ((size_t) juce::jmax (samplesPerBlock, 1), 0.0f);

    // Double-buffered output snapshot for the oscilloscope tab.
    for (auto& b : scopeBuffers)
    {
        b.setSize (2, juce::jmax (samplesPerBlock, 1), false, true, true);
        b.clear();
    }
    scopeReadIndex.store (0);
    scopeValidSamples.store (0);



    smoothedRatio = 1.0f;
    correctionLog2 = 0.0f;
    prevAbsLogErr = 0.0f;
    humanizePhase = 0.0f;
    humanizeNoise = 0.0f;
    humanizeRng = 0xA341316Cu;
    stableTargetBlocks = 0;

    lastSnappedTargetMidi = -100;

    activeMidiNotes.clear();

    latestSnappedHz.store (0.0f);

    lastMidiChannel.store (0);

    activeMidiNoteCount.store (0);

    midiPitchBendNorm.store (0.0f, std::memory_order_relaxed);



    setLatencySamples (shifter.getLatencySamples());

}



void PitchCorrectorAudioProcessor::releaseResources()

{

    shifter.reset();

    subVocoder.reset();
    deEsser   .reset();
    pinkNoise .reset();

    for (auto& chain : fxChains)
        chain.reset();

}



#ifndef JucePlugin_PreferredChannelConfigurations

bool PitchCorrectorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const

{

    const auto& mainOut = layouts.getMainOutputChannelSet();



    if (mainOut != juce::AudioChannelSet::mono()

     && mainOut != juce::AudioChannelSet::stereo())

        return false;



    return mainOut == layouts.getMainInputChannelSet();

}

#endif



//==============================================================================

void PitchCorrectorAudioProcessor::runPitchDetection (const juce::AudioBuffer<float>& buffer)

{

    const int numCh = buffer.getNumChannels();

    const int N     = buffer.getNumSamples();

    if (numCh <= 0 || N <= 0) return;



    if (numCh == 1)

    {

        detector.pushBlock (buffer.getReadPointer (0), N);

        return;

    }



    const float* l = buffer.getReadPointer (0);

    const float* r = buffer.getReadPointer (1);

    for (int i = 0; i < N; ++i)

        detector.pushSample (0.5f * (l[i] + r[i]));

}



void PitchCorrectorAudioProcessor::updateMidiNotes (const juce::MidiBuffer& midi)

{

    for (const auto meta : midi)

    {

        const auto m = meta.getMessage();

        if (m.isNoteOn())

        {

            lastMidiChannel.store (m.getChannel(), std::memory_order_relaxed);

            const int n = m.getNoteNumber();

            if (std::find (activeMidiNotes.begin(), activeMidiNotes.end(), n) == activeMidiNotes.end())

                activeMidiNotes.push_back (n);

        }

        else if (m.isNoteOff())

        {

            const int n = m.getNoteNumber();

            activeMidiNotes.erase (std::remove (activeMidiNotes.begin(), activeMidiNotes.end(), n),

                                   activeMidiNotes.end());

        }

        else if (m.isAllNotesOff() || m.isAllSoundOff())

        {

            activeMidiNotes.clear();

        }

        else if (m.isPitchWheel())

        {

            const int pw = m.getPitchWheelValue();

            const float n = juce::jlimit (-1.0f, 1.0f, ((float) pw - 8192.0f) / 8192.0f);

            midiPitchBendNorm.store (n, std::memory_order_relaxed);

        }

    }



    activeMidiNoteCount.store ((int) activeMidiNotes.size(), std::memory_order_relaxed);

}



float PitchCorrectorAudioProcessor::computeSnappedTargetHz (float detectedHz)

{

    if (detectedHz < minTrackedPitchHz)

    {

        latestSnappedHz.store (0.0f);

        return 0.0f;

    }



    const bool midiFollow = apvts.getRawParameterValue (PID_MIDI_FOLLOW)->load() > 0.5f;

    const bool useMidi    = midiFollow && ! activeMidiNotes.empty();



    float targetHz;

    if (useMidi)

        targetHz = SolTune::snapHzToMidiSet (detectedHz, activeMidiNotes);

    else

    {

        const int rootIdx = SolTune::clampChoiceIndex (
            juce::roundToInt (apvts.getRawParameterValue (PID_ROOT)->load()), 12);
        const int scaleIdx = SolTune::clampChoiceIndex (
            juce::roundToInt (apvts.getRawParameterValue (PID_SCALE)->load()),
            (int) SolTune::Scale::NumScales);

        const auto root  = (SolTune::Root)  rootIdx;
        const auto scale = (SolTune::Scale) scaleIdx;

        targetHz = SolTune::snapHzToScale (detectedHz, root, scale);

    }



    latestSnappedHz.store (targetHz);

    return targetHz;

}



float PitchCorrectorAudioProcessor::computeSmoothedRatio (float detectedHz,

                                                          float snappedHz,

                                                          float robotic)

{

    if (snappedHz <= 0.0f || detectedHz < minTrackedPitchHz)

        return smoothedRatio;



    // Ratio that maps detected f0 to the snapped scale / MIDI target (100% correction goal).

    const float idealRatio = juce::jlimit (0.25f, 4.0f, snappedHz / detectedHz);



    const int snapMidi = (int) std::lround (SolTune::hzToMidi (snappedHz));

    const int prevTarget = lastSnappedTargetMidi;

    const bool crossedTargetNote = (prevTarget != -100) && (snapMidi != prevTarget);

    lastSnappedTargetMidi = snapMidi;



    robotic = juce::jlimit (0.0f, 1.0f, robotic);



    // --- Retune Speed (Antares-style): Robotic = 0 -> slow glide, Robotic = 1 -> hard lock.

    // Work in log2(ratio) so smoothing is linear in cents (exponential in frequency).

    const float logIdeal = std::log2 (idealRatio);



    if (robotic >= 0.995f)

    {

        stableTargetBlocks = 0;

        humanizePhase = 0.0f;

        humanizeNoise = 0.0f;

        smoothedRatio = idealRatio;

        correctionLog2 = logIdeal;

        prevAbsLogErr = 0.0f;

        return smoothedRatio;

    }



    float logCur = correctionLog2;

    const float err = logIdeal - logCur;

    const float magErr = std::abs (err);



    // Tighter, more professional mapping (faster natural glide + snappier robotic)
    const float tauAttackSec = std::exp (juce::jmap (robotic, 0.0f, 1.0f,
                                                     std::log (0.85f),     // was 1.1s
                                                     std::log (0.0015f))); // was 0.002s

    const float releaseStretch = juce::jmap (robotic, 0.0f, 1.0f, 2.8f, 5.5f);

    const float tauReleaseSec = tauAttackSec * releaseStretch;



    const bool closing = (prevAbsLogErr > 1.0e-5f) && (magErr <= prevAbsLogErr * 0.992f);

    const float tau = closing ? tauReleaseSec : tauAttackSec;



    const float alpha = juce::jlimit (0.003f, 0.96f,

                                      alphaFromTauSeconds (currentSampleRate, currentBlockSize, tau));



    if (crossedTargetNote)

    {

        stableTargetBlocks = 0;

        humanizePhase = 0.0f;

        humanizeNoise = 0.0f;



        // Note change: partial snap (more at high Robotic), then one-pole closes the rest.

        float snapPortion = juce::jmap (robotic, 0.0f, 1.0f, 0.35f, 0.95f);

        if (snapMidi > prevTarget)

            snapPortion = juce::jmin (0.97f, snapPortion + juce::jmap (robotic, 0.0f, 1.0f, 0.04f, 0.10f));

        else if (snapMidi < prevTarget)

            snapPortion = juce::jmax (0.22f, snapPortion - juce::jmap (robotic, 0.0f, 1.0f, 0.05f, 0.08f));



        logCur += err * snapPortion;

        logCur += alpha * (logIdeal - logCur);

    }

    else

    {

        if (snapMidi == prevTarget || prevTarget == -100)

            ++stableTargetBlocks;

        else

            stableTargetBlocks = 0;



        logCur += alpha * err;

    }



    correctionLog2 = logCur;



    // --- Humanize: tiny offset layered on top of correction (not swallowed by slow glide).

    // Quadratic fade with Robotic; strongest at 0, inaudible by ~40%.

    const float natural = 1.0f - robotic;

    const int minStableBlocks = juce::jmax (1, (int) std::lround (juce::jmap (robotic, 0.0f, 1.0f, 1.0f, 10.0f)));

    const bool settledEnough = magErr < juce::jmap (robotic, 0.0f, 1.0f, 0.030f, 0.010f);

    const bool targetStable = ! crossedTargetNote

                           && (stableTargetBlocks >= minStableBlocks

                               || (robotic < 0.22f && settledEnough && stableTargetBlocks >= 1));



    float humanizeLog2 = 0.0f;



    if (targetStable && natural > 0.04f)

    {

        const float blockSec = (float) currentBlockSize / (float) juce::jmax (currentSampleRate, 1.0);

        const float humanDepthLog2 = natural * natural * 0.0020f; // ~2.3 cents peak at Robotic 0



        humanizePhase += juce::MathConstants<float>::twoPi

                       * juce::jmap (robotic, 0.0f, 1.0f, 4.0f, 1.2f) * blockSec;

        if (humanizePhase > juce::MathConstants<float>::twoPi)

            humanizePhase -= juce::MathConstants<float>::twoPi;



        humanizeRng ^= humanizeRng << 13;

        humanizeRng ^= humanizeRng >> 17;

        humanizeRng ^= humanizeRng << 5;

        const float rawNoise = (float) (humanizeRng & 0xFFFFu) / 32767.5f - 1.0f;

        humanizeNoise += 0.07f * (rawNoise - humanizeNoise);



        humanizeLog2 = (0.88f * std::sin (humanizePhase) + 0.12f * humanizeNoise) * humanDepthLog2;

    }

    else

    {

        humanizePhase = 0.0f;

        humanizeNoise *= 0.82f;

    }



    smoothedRatio = std::pow (2.0f, correctionLog2 + humanizeLog2);

    smoothedRatio = juce::jlimit (0.25f, 4.0f, smoothedRatio);



    prevAbsLogErr = std::abs (logIdeal - correctionLog2);



    return smoothedRatio;

}



float PitchCorrectorAudioProcessor::computeEffectiveFormantSemitones (float pitchRatio,

                                                                     float formantKnob,

                                                                     float robotic) const noexcept

{

    robotic = juce::jlimit (0.0f, 1.0f, robotic);

    const float r = juce::jlimit (0.25f, 4.0f, pitchRatio);



    if (std::abs (r - 1.0f) < 1.0e-4f)

        return formantKnob;



    // Pitch correction in semitones (what Stretch is applying via transpose).

    const float corrSt = 12.0f * std::log2 (r);



    // Subtle "Throat" vocal-tract model: counter-shift formants against correction so

    // upward pulls stay chesty and downward pulls don't hollow out. Fades out as Robotic -> 1

    // (Signalsmith already compensates pitch; this is the extra Antares-style sweetness).

    const float throatMix = juce::jmap (robotic, 0.0f, 1.0f, 0.40f, 0.0f);

    const float throatSt = -corrSt * throatMix * 0.34f;



    // Slight cohesion at natural settings: tame extreme manual formant when throat is active.

    const float knobScale = juce::jmap (robotic, 0.0f, 1.0f, 0.94f, 1.0f);



    return juce::jlimit (-24.0f, 24.0f, formantKnob * knobScale + throatSt);

}



//==============================================================================

void PitchCorrectorAudioProcessor::applyFxChain (int chain, juce::AudioBuffer<float>& buffer) noexcept
{
    if (! juce::isPositiveAndBelow (chain, (int) numFxChains))
        return;

    auto& fx = fxChains[(size_t) chain];

    for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
    {
        fx.setSlotEffect (s, VocalFx::EffectChain::clampType (
            (VocalFx::EffectType) juce::roundToInt (fxTypeParams[(size_t) chain][(size_t) s]->load())));
        fx.setSlotAmount (s, fxAmountParams[(size_t) chain][(size_t) s]->load());
    }

    fx.process (buffer);
}

void PitchCorrectorAudioProcessor::mixHarmonyVoices (juce::AudioBuffer<float>&) noexcept
{
    // Stub: harmony voices (63C-13) will render pitch-shifted copies of the
    // lead voice and sum them here, between the voice chain and output chain.
}

//==============================================================================

void PitchCorrectorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,

                                                 juce::MidiBuffer& midi)

{

    juce::ScopedNoDenormals noDenormals;



    const int totalIn  = getTotalNumInputChannels();

    const int totalOut = getTotalNumOutputChannels();

    for (int c = totalIn; c < totalOut; ++c)

        buffer.clear (c, 0, buffer.getNumSamples());



    updateMidiNotes (midi);



    const bool  bypass = apvts.getRawParameterValue (PID_BYPASS)->load() > 0.5f;

    const float robotic   = apvts.getRawParameterValue (PID_ROBOTIC)->load();

    const float sub       = apvts.getRawParameterValue (PID_SUB)->load();

    const float formantSt = apvts.getRawParameterValue (PID_FORMANT)->load();

    const float bendParam = apvts.getRawParameterValue (PID_PITCH_BEND)->load();

    const float bendRange = apvts.getRawParameterValue (PID_BEND_RANGE)->load();

    const float volumeDb  = apvts.getRawParameterValue (PID_VOLUME)->load();



    if (bypass)
    {
        runPitchDetection (buffer);   // keep the pitch readout alive while bypassed
        return;
    }

    const int chs = buffer.getNumChannels();

    const int N   = buffer.getNumSamples();

    if (chs <= 0 || N <= 0) return;

    // === Input Global FX chain (63C-15) ===
    // First stage of the graph: shapes the signal the tuner detects and corrects.
    applyFxChain (fxChainInput, buffer);

    runPitchDetection (buffer);

    // Snapshot voice-input mono mix (post input chain, per sample) before any
    // further in-place processing.
    // Feeds the sub vocoder as its modulator; it handles its own envelope tracking.
    if ((int) monoInputScratch.size() < N)
        monoInputScratch.assign ((size_t) N, 0.0f);
    if ((int) sibilanceScratch.size() < N)
        sibilanceScratch.assign ((size_t) N, 0.0f);

    const float invChs = 1.0f / (float) juce::jmax (chs, 1);
    for (int i = 0; i < N; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < chs; ++c)
            sum += buffer.getReadPointer (c)[i];
        monoInputScratch[(size_t) i] = sum * invChs;
    }



    const float detectedHz = detector.getPitchHz();

    const float snappedHz  = computeSnappedTargetHz (detectedHz);



    const bool voiced = (detectedHz >= minTrackedPitchHz && snappedHz > 0.0f);



    if (! voiced)

    {

        lastSnappedTargetMidi = -100;

        smoothedRatio = 1.0f;

        correctionLog2 = 0.0f;

        prevAbsLogErr = 0.0f;

        stableTargetBlocks = 0;

        humanizePhase = 0.0f;

        humanizeNoise = 0.0f;

    }



    const bool wantPitch = voiced;

    const float baseRatio = wantPitch ? computeSmoothedRatio (detectedHz, snappedHz, robotic) : 1.0f;



    const float wheel = midiPitchBendNorm.load (std::memory_order_relaxed);

    const float bendNorm = juce::jlimit (-1.0f, 1.0f, bendParam + wheel);

    const float bendRatio = std::pow (2.0f, (bendNorm * bendRange) / 12.0f);

    const float ratio     = juce::jlimit (0.25f, 4.0f, baseRatio * bendRatio);



    const float effectiveFormant = computeEffectiveFormantSemitones (baseRatio, formantSt, robotic);

    // === SUPER-ROBOTIC SIZZLE: broken-up granulated digital robot (bit-crush + wavefold + grit) ===
    const bool superRobotic = robotic > 0.68f && voiced;
    if (superRobotic)
    {
        const float drive = juce::jmap (robotic, 0.68f, 1.0f, 0.0f, 1.0f);
        // Dry-dominant mix: even at full robotic the wet effect is a flavour, not a takeover.
        const float wet   = drive * 0.20f;
        const float dry   = 1.0f - wet;

        for (int c = 0; c < chs; ++c)
        {
            float* ptr = buffer.getWritePointer (c);
            for (int i = 0; i < N; ++i)
            {
                const float in = ptr[i];
                float s = in;

                // 1. Bit-crush (12-bit → ~11-bit at full robotic — barely-there digital stair-step)
                const float bits = juce::jmap (drive, 0.0f, 1.0f, 12.0f, 11.0f);
                const float q = std::pow (2.0f, bits);
                s = std::round (s * q) / q;

                // 2. Sine-shaped soft fold (smooth metallic harmonics — no triangle-fold sharp corners)
                const float foldAmount = 1.0f + drive * 0.9f;
                float folded = std::sin (s * foldAmount * juce::MathConstants<float>::halfPi);
                folded *= 1.0f / (1.0f + drive * 0.25f);

                // 3. Very light digital grit (just enough to keep it "alive")
                const float grit = (folded - s) * drive * 0.05f;

                // Final tanh on wet path rounds any remaining peaks so it can never sound clipped.
                const float wetSig = std::tanh (folded + grit);

                ptr[i] = in * dry + wetSig * wet;
            }
        }
    }

    const bool needStretch = wantPitch

                             || (std::abs (formantSt) > kFormantEps)

                             || (std::abs (effectiveFormant) > kFormantEps);



    if (needStretch)
    {
        shifter.setPitchRatio (ratio);

        shifter.setFormantSemitones (effectiveFormant);

        shifter.process (buffer);
    }

    // === Sibilance management (always on; depth grows with Robotic) ===
    // Publishes a per-sample sibilance-presence signal that gates the pink noise.
    deEsser.setRobotic (robotic);
    deEsser.process (buffer, sibilanceScratch.data());

    // === Band-limited saw sub carrier — 3 octaves below the vocal output ===
    // Pitch tracks (detectedHz*ratio) / 8 so the sub sits three octaves under
    // the vocal output. Amplitude = Sub knob × dry-input envelope, capped at
    // 0.25 so even full-Sub stays a polite underpinning.
    constexpr float kSubMaxMix = 0.25f;
    const float vocalHz = voiced ? juce::jlimit (20.0f, 4000.0f, detectedHz * ratio) : 0.0f;
    const float sawHz   = vocalHz * 0.125f;
    subVocoder.setFrequency (sawHz);
    subVocoder.setTargetMix (voiced ? sub * kSubMaxMix : 0.0f);
    subVocoder.addTo (buffer, monoInputScratch.data());

    // === Pink-noise sibilance smoothing ===
    // Gated per-sample by the de-esser's sibilance envelope: only sounds when
    // sibilance is being cut. Stays subtle even at max Robotic — it's a
    // smoother, not a layer.
    const float pinkMix = juce::jmap (robotic, 0.0f, 1.0f, 0.01f, 0.045f);
    pinkNoise.setTargetMix (pinkMix);
    pinkNoise.addTo (buffer, sibilanceScratch.data());

    // === Voice FX chain: ordered slot effects on the corrected lead voice ===
    applyFxChain (fxChainVoice, buffer);

    // === Harmony voices summing point (63C-13 renders + sums here) ===
    mixHarmonyVoices (buffer);

    // === Output Global FX chain: polish on the summed voices ===
    applyFxChain (fxChainOutput, buffer);

    // Master output volume + formant-loudness compensation.
    // Formant shifts redistribute spectral envelope energy: up-shift gets louder, down-shift quieter.
    // Counter that with ~0.5 dB per semitone so amplitude stays flat across the Formant knob.
    const float formantMakeupDb = needStretch ? -effectiveFormant * 0.5f : 0.0f;
    const float outGain = juce::Decibels::decibelsToGain (volumeDb + formantMakeupDb, -60.0f);
    if (! juce::approximatelyEqual (outGain, 1.0f))
        buffer.applyGain (outGain);

    // === Publish post-processing snapshot to the oscilloscope (lock-free) ===
    {
        const int readIdx  = scopeReadIndex.load (std::memory_order_relaxed);
        const int writeIdx = 1 - readIdx;
        auto& dest = scopeBuffers[(size_t) writeIdx];

        if (dest.getNumSamples() < N)
            dest.setSize (juce::jmax (2, chs), N, false, true, true);

        const int copyCh = juce::jmin (dest.getNumChannels(), chs);
        for (int c = 0; c < copyCh; ++c)
            dest.copyFrom (c, 0, buffer, c, 0, N);
        // Mirror mono into the second channel so the editor always sees L/R.
        if (chs == 1 && dest.getNumChannels() >= 2)
            dest.copyFrom (1, 0, buffer, 0, 0, N);

        scopeValidSamples.store (N, std::memory_order_release);
        scopeReadIndex.store    (writeIdx, std::memory_order_release);
    }
}



//==============================================================================

juce::AudioProcessorEditor* PitchCorrectorAudioProcessor::createEditor()

{

    return new PitchCorrectorAudioProcessorEditor (*this);

}



//==============================================================================

void PitchCorrectorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)

{

    if (auto state = apvts.copyState(); state.isValid())

        if (auto xml = state.createXml())

            copyXmlToBinary (*xml, destData);

}



void PitchCorrectorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)

{

    if (auto xml = getXmlFromBinary (data, sizeInBytes))

        if (xml->hasTagName (apvts.state.getType()))

            apvts.replaceState (juce::ValueTree::fromXml (*xml));

}



//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()

{

    return new PitchCorrectorAudioProcessor();

}


