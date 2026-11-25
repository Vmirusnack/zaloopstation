#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat; using APC = juce::AudioParameterChoice; using APB = juce::AudioParameterBool;
    using PID = juce::ParameterID; using NR = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    juce::StringArray keys; for (int i = 0; i < 12; ++i) keys.add (lh::keyNames()[i]);
    juce::StringArray sc; for (auto& s : lh::scales()) sc.add (s.name);
    l.add (std::make_unique<APC> (PID { "key", 1 }, "Key", keys, 4));
    l.add (std::make_unique<APC> (PID { "scale", 1 }, "Scale", sc, 0));
    l.add (std::make_unique<APF> (PID { "snap", 1 }, "Snap", NR (0.f, 1.f), 1.f));
    l.add (std::make_unique<APF> (PID { "cut", 1 }, "Low cut", NR (40.f, 2000.f, 0.f, 0.4f), 150.f));
    l.add (std::make_unique<APF> (PID { "mix", 1 }, "Mix", NR (0.f, 1.f), 1.f));
    l.add (std::make_unique<APF> (PID { "out", 1 }, "Output", NR (-12.f, 12.f), 0.f));
    l.add (std::make_unique<APC> (PID { "quality", 1 }, "Quality", juce::StringArray { "Fast (21 ms)", "High (43 ms)" }, 0));
    const bool def[12] = { true, false, false, true, false, true, false, true, false, false, true, false };   // minor pentatonic
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<APB> (PID { "n" + juce::String (i), 1 }, "Custom note " + juce::String (i), def[i]));
    return l;
}
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

float HarmonizerProcessor::get (const char* id) const
{
    auto* v = apvts.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

lh::Params HarmonizerProcessor::currentParams() const
{
    lh::Params p;
    std::array<bool, 12> custom {};
    for (int i = 0; i < 12; ++i) custom[(size_t) i] = get (("n" + juce::String (i)).toRawUTF8()) > 0.5f;
    p.root = juce::jlimit (0, 11, (int) get ("key"));
    p.mask = lh::makeMask ((int) get ("scale"), p.root, custom);
    p.strength = get ("snap"); p.lowCut = get ("cut"); p.mix = get ("mix");
    p.outGain = std::pow (10.0f, get ("out") / 20.0f);
    return p;
}

void HarmonizerProcessor::copyLatest (float* dest, int n) const
{
    const int size = (int) ring.size(), end = ringPos.load (std::memory_order_acquire);
    n = juce::jmin (n, size);
    for (int i = 0; i < n; ++i) dest[i] = ring[(size_t) (((end - n + i) % size + size) % size)];
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    eng[0].prepare (sampleRate, 1024); eng[1].prepare (sampleRate, 2048);
    active = (int) get ("quality") > 0 ? 1 : 0;
    latencyWanted = eng[active].latency();
    setLatencySamples (latencyWanted.load());
    mono.setSize (1, juce::jmax (1, samplesPerBlock));
    std::fill (ring.begin(), ring.end(), 0.0f);
}

bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && o == l.getMainInputChannelSet();
}

void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = juce::jmin (buffer.getNumChannels(), lh::kMaxCh);
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    const int want = (int) get ("quality") > 0 ? 1 : 0;
    if (want != active)
    {
        active = want; eng[active].reset();
        latencyWanted = eng[active].latency();
        triggerAsyncUpdate();
    }
    eng[active].process (buffer.getArrayOfWritePointers(), nch, n, currentParams());

    // keep the newest output for the editor's spectrum
    const int size = (int) ring.size();
    int pos = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        float s = buffer.getSample (0, i);
        if (nch > 1) s = 0.5f * (s + buffer.getSample (1, i));
        ring[(size_t) pos] = s; if (++pos >= size) pos = 0;
    }
    ringPos.store (pos, std::memory_order_release);
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor() { return new HarmonizerEditor (*this); }

void HarmonizerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void HarmonizerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HarmonizerProcessor(); }
