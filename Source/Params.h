#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{
struct Def { const char* id; const char* name; const char* group; };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;
    using PID = juce::ParameterID;
    using NR = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const juce::StringArray tables { "Saw - Square", "Sine - Saw", "Zaag Formant", "Digital" };

    auto osc = [&] (const juce::String& p, const juce::String& n, float level)
    {
        l.add (std::make_unique<APC> (PID { p + "_table", 1 }, n + " Table", tables, 0));
        l.add (std::make_unique<APF> (PID { p + "_pos", 1 }, n + " Position", NR (0.f, 1.f), 0.f));
        l.add (std::make_unique<API> (PID { p + "_oct", 1 }, n + " Octave", -3, 3, 0));
        l.add (std::make_unique<APF> (PID { p + "_fine", 1 }, n + " Fine", NR (-100.f, 100.f, 0.01f), 0.f));
        l.add (std::make_unique<APF> (PID { p + "_level", 1 }, n + " Level", NR (0.f, 1.f), level));
        l.add (std::make_unique<API> (PID { p + "_uni", 1 }, n + " Unison", 1, 7, 1));
        l.add (std::make_unique<APF> (PID { p + "_det", 1 }, n + " Detune", NR (0.f, 1.f), 0.25f));
        l.add (std::make_unique<APF> (PID { p + "_spread", 1 }, n + " Spread", NR (0.f, 1.f), 0.6f));
    };
    osc ("a", "A", 1.0f);
    osc ("b", "B", 0.0f);

    l.add (std::make_unique<APF> (PID { "sub", 1 }, "Sub", NR (0.f, 1.f), 0.f));
    l.add (std::make_unique<APF> (PID { "rnd", 1 }, "Random phase", NR (0.f, 1.f), 0.f));

    l.add (std::make_unique<APC> (PID { "ftype", 1 }, "Filter type", juce::StringArray { "Ladder 24", "Ladder 12", "Band", "High" }, 0));
    l.add (std::make_unique<APF> (PID { "cut", 1 }, "Cutoff", NR (20.f, 20000.f, 0.f, 0.3f), 20000.f));
    l.add (std::make_unique<APF> (PID { "res", 1 }, "Resonance", NR (0.f, 1.f), 0.f));
    l.add (std::make_unique<APF> (PID { "fdrive", 1 }, "Filter drive", NR (1.f, 4.f), 1.f));
    l.add (std::make_unique<APF> (PID { "fenv", 1 }, "Filter env amount", NR (-1.f, 1.f), 0.f));
    l.add (std::make_unique<APF> (PID { "fkey", 1 }, "Key tracking", NR (0.f, 1.f), 0.f));

    auto time = [&] (const char* id, const char* nm, float def)
    {
        l.add (std::make_unique<APF> (PID { id, 1 }, nm, NR (0.001f, 10.f, 0.f, 0.3f), def));
    };
    time ("fa", "Filter attack", 0.005f); time ("fd", "Filter decay", 0.3f);
    l.add (std::make_unique<APF> (PID { "fs", 1 }, "Filter sustain", NR (0.f, 1.f), 0.5f));
    time ("fr", "Filter release", 0.2f);
    time ("aa", "Amp attack", 0.003f); time ("ad", "Amp decay", 0.3f);
    l.add (std::make_unique<APF> (PID { "as", 1 }, "Amp sustain", NR (0.f, 1.f), 0.8f));
    time ("ar", "Amp release", 0.15f);

    l.add (std::make_unique<APF> (PID { "poslenv", 1 }, "Position by filter env", NR (-1.f, 1.f), 0.f));
    l.add (std::make_unique<APB> (PID { "mono", 1 }, "Mono", false));
    l.add (std::make_unique<APF> (PID { "glide", 1 }, "Glide", NR (0.f, 2.f, 0.f, 0.4f), 0.f));
    l.add (std::make_unique<API> (PID { "bend", 1 }, "Bend range", 0, 24, 2));
    l.add (std::make_unique<APF> (PID { "vel", 1 }, "Velocity sensitivity", NR (0.f, 1.f), 0.6f));

    l.add (std::make_unique<APC> (PID { "dtype", 1 }, "Distortion type", juce::StringArray { "Soft", "Hard", "Fold" }, 0));
    l.add (std::make_unique<APF> (PID { "dist", 1 }, "Distortion", NR (0.f, 1.f), 0.f));
    l.add (std::make_unique<APF> (PID { "master", 1 }, "Master", NR (0.f, 1.f), 0.7f));
    return l;
}
}
