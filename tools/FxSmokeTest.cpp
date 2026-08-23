/*
    FxSmokeTest.cpp
    ---------------
    Console check over the effect chain (63C-8): build every EffectType, run
    audio through it, and assert the output is finite and bounded.

    The wheel can only be exercised by hand, and by the time an effect is on the
    rim it has already been prepared, handed across a mailbox and processed —
    so a fault anywhere in that path shows up as a crash in a DAW rather than a
    message. This runs the same path headless, in both layouts Sol supports:

      * every type constructed through createEffect(), prepared, processed;
      * mono and stereo, since half the ported effects assume two channels and
        the adapter widens for them;
      * silence, a tone, and a hot signal, checking for NaN/Inf and runaway;
      * an EffectChain with all 25 slots filled, to catch anything that only
        misbehaves in series.

    Build and run:
        cmake --build build --config Release --target SolVoiceTuner_FxSmokeTest
        ./build/Release/SolVoiceTuner_FxSmokeTest.exe
*/

#include <JuceHeader.h>

#include "EffectChain.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void fail (const std::string& what)
    {
        std::printf ("  FAIL: %s\n", what.c_str());
        ++failures;
    }

    /** Always: finite. NaN or Inf is a defect at any setting, and it spreads —
        one bad sample poisons every effect after it in the chain.

        Optionally: bounded. An effect that turns a 0.7 peak into 50 has a
        runaway feedback path. This is only asked of settings a person would
        plausibly dial; at the extreme corners loudness is the user's choice
        (five cascaded resonant filters really are that loud, and Space Dust
        lets you build them too), so those passes check finiteness alone. */
    void checkBuffer (const juce::AudioBuffer<float>& b, const std::string& what,
                      bool boundLevel)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getReadPointer (c)[i];

                if (! std::isfinite (v))
                {
                    fail (what + ": non-finite sample at ch " + std::to_string (c)
                              + " idx " + std::to_string (i));
                    return;
                }

                if (boundLevel && std::abs (v) > 16.0f)
                {
                    fail (what + ": runaway level " + std::to_string (v));
                    return;
                }
            }
    }

    /** A tone whose phase carries across blocks. Restarting it every block puts
        a step discontinuity at each boundary — an impulse train that excites
        every resonance in the effect and makes ordinary settings look unstable. */
    double tonePhase = 0.0;

    void fillTone (juce::AudioBuffer<float>& b, float amp, double sr, double hz)
    {
        const double inc = juce::MathConstants<double>::twoPi * hz / sr;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto v = (float) (amp * std::sin (tonePhase));
            tonePhase += inc;

            if (tonePhase > juce::MathConstants<double>::twoPi)
                tonePhase -= juce::MathConstants<double>::twoPi;

            for (int c = 0; c < b.getNumChannels(); ++c)
                b.getWritePointer (c)[i] = v;
        }
    }

    using Values = std::array<float, VocalFx::kMaxEffectParams>;

    void fillDefaults (VocalFx::EffectType type, Values& out)
    {
        const auto list = VocalFx::effectParams (type);

        for (int i = 0; i < list.count; ++i)
            out[(size_t) i] = list[i].def;
    }

    /** Silence, then a tone, then a hot signal, through one setting. An effect
        that only misbehaves once it is driven hard should say so here. */
    void runPass (VocalFx::VocalEffect& fx, const Values& values,
                  juce::AudioBuffer<float>& buffer, double sr, double seconds,
                  const std::string& what, bool boundLevel)
    {
        const int blockSize = buffer.getNumSamples();
        const int blocks    = (int) std::ceil (sr * seconds / blockSize);

        for (int n = 0; n < blocks; ++n)
        {
            if (n < blocks / 3)          buffer.clear();
            else if (n < 2 * blocks / 3) fillTone (buffer, 0.7f, sr, 220.0);
            else                         fillTone (buffer, 1.4f, sr, 1000.0);

            fx.applyParams (values.data());
            fx.process (buffer);
            checkBuffer (buffer, what, boundLevel);

            if (failures > 0)
                return;
        }
    }

    /** One effect, one channel count.

        Every control is driven to BOTH ends of its declared range on its own,
        with the rest left at their defaults. One at a time is the point: it
        isolates each control, and it keeps the settings plausible — sweeping
        everything at once builds combinations (five cascaded resonant filters,
        say) that are legal but that nobody would dial, so a failure there would
        say nothing about whether the control is correctly wired. The all-extremes
        pass is still run at the end as a stability check, finiteness only. */
    void runOne (VocalFx::EffectType type, int channels, double sr, int blockSize)
    {
        auto fx = VocalFx::createEffect (type);
        const std::string name = VocalFx::effectTypeName (type);
        const std::string tag  = name + " / " + std::to_string (channels) + "ch";
        const auto        list = VocalFx::effectParams (type);

        if (fx == nullptr)
        {
            fail (tag + ": createEffect returned nothing");
            return;
        }

        if (fx->typeTag != type)
            fail (tag + ": typeTag not set by the factory");

        if (list.count == 0)
        {
            fail (tag + ": no controls declared in EffectParams.h");
            return;
        }

        if (list.count > VocalFx::kMaxEffectParams)
        {
            fail (tag + ": more controls than kMaxEffectParams");
            return;
        }

        fx->prepare (sr, blockSize, channels);
        fx->reset();

        juce::AudioBuffer<float> buffer (channels, blockSize);
        Values values {};

        // Defaults. Long enough to fill a 2 s delay line and let it settle.
        fillDefaults (type, values);
        runPass (*fx, values, buffer, sr, 6.0, tag + " @ defaults", true);

        if (failures > 0)
            return;

        // Each control alone, at each end of its range.
        for (int i = 0; i < list.count && failures == 0; ++i)
        {
            for (const bool high : { false, true })
            {
                fillDefaults (type, values);
                values[(size_t) i] = high ? list[i].max : list[i].min;

                runPass (*fx, values, buffer, sr, 2.5,
                         tag + " @ " + list[i].name + (high ? " max" : " min"), true);

                if (failures > 0)
                    return;
            }
        }

        // Everything at once, both ends — stability only.
        for (const bool high : { false, true })
        {
            for (int i = 0; i < list.count; ++i)
                values[(size_t) i] = high ? list[i].max : list[i].min;

            runPass (*fx, values, buffer, sr, 2.5,
                     tag + (high ? " @ all max" : " @ all min"), false);

            if (failures > 0)
                return;
        }

        std::printf ("  ok: %s (%d controls)\n", tag.c_str(), list.count);
    }

    /** Every slot of a chain filled, cycling through the types, so the whole
        series runs at once — including the message-thread servicing that hands
        instances to the audio side. */
    void runFullChain (int channels, double sr, int blockSize)
    {
        VocalFx::ChainParamValues values;
        const int numTypes = (int) VocalFx::EffectType::NumTypes;

        for (int t = 1; t < numTypes; ++t)
            fillDefaults ((VocalFx::EffectType) t, values.v[(size_t) t]);

        VocalFx::EffectChain chain;
        chain.prepare (sr, blockSize, channels);
        chain.setParamValues (&values);

        for (int s = 0; s < VocalFx::EffectChain::kNumSlots; ++s)
        {
            const auto t = (VocalFx::EffectType) (1 + (s % (numTypes - 1)));
            chain.installImmediate (s, t);
            chain.setSlotEffect (s, t);
            chain.setSlotAmount (s, 0.6f);
        }

        juce::AudioBuffer<float> buffer (channels, blockSize);
        const int blocks = (int) std::ceil (sr * 4.0 / blockSize);

        for (int n = 0; n < blocks; ++n)
        {
            fillTone (buffer, 0.6f, sr, 330.0);
            chain.process (buffer);
            checkBuffer (buffer, "full chain / " + std::to_string (channels) + "ch", true);

            if (failures > 0)
                return;
        }

        std::printf ("  ok: full 25-slot chain / %dch\n", channels);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const int numTypes = (int) VocalFx::EffectType::NumTypes;
    std::printf ("Effect types: %d (Empty + %d effects)\n", numTypes, numTypes - 1);

    for (const auto sr : { 44100.0, 48000.0 })
    {
        std::printf ("\n-- %.0f Hz --\n", sr);

        for (int t = 1; t < numTypes; ++t)      // skip Empty
            for (const int ch : { 1, 2 })
            {
                runOne ((VocalFx::EffectType) t, ch, sr, 512);

                if (failures > 0)
                    goto done;
            }

        for (const int ch : { 1, 2 })
        {
            runFullChain (ch, sr, 512);

            if (failures > 0)
                goto done;
        }
    }

done:
    std::printf ("\n%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
