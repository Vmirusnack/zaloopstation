#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct Preset { const char* name; std::vector<std::pair<const char*, float>> v; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Init", {} },
        { "Zaag Lead", { { "a_table", 2 }, { "a_pos", 0.55f }, { "a_uni", 5 }, { "a_det", 0.35f }, { "b_level", 0.5f }, { "b_table", 0 }, { "b_oct", -1 }, { "b_uni", 3 },
                         { "cut", 5200 }, { "res", 0.25f }, { "fenv", 0.3f }, { "fd", 0.5f }, { "as", 0.85f }, { "ar", 0.25f }, { "dist", 0.35f }, { "dtype", 0 } } },
        { "Raw Screech", { { "a_table", 2 }, { "a_pos", 0.9f }, { "a_uni", 7 }, { "a_det", 0.6f }, { "cut", 3400 }, { "res", 0.45f }, { "fenv", 0.5f }, { "fd", 0.25f }, { "fs", 0.3f },
                           { "poslenv", 0.35f }, { "dist", 0.65f }, { "dtype", 1 }, { "as", 0.9f }, { "ar", 0.2f } } },
        { "Sub Bass", { { "a_table", 1 }, { "a_pos", 0.1f }, { "sub", 0.7f }, { "cut", 420 }, { "res", 0.1f }, { "mono", 1 }, { "glide", 0.06f }, { "as", 1.0f }, { "ar", 0.1f } } },
        { "Acid Squelch", { { "a_table", 0 }, { "a_pos", 0.0f }, { "mono", 1 }, { "glide", 0.05f }, { "cut", 380 }, { "res", 0.88f }, { "fenv", 0.75f }, { "fd", 0.22f }, { "fs", 0.0f },
                            { "fdrive", 2.2f }, { "as", 0.8f }, { "ad", 0.25f }, { "ar", 0.08f } } },
        { "Hoover Stack", { { "a_table", 3 }, { "a_pos", 0.35f }, { "a_uni", 7 }, { "a_det", 0.85f }, { "a_spread", 1.0f }, { "b_level", 0.7f }, { "b_table", 0 }, { "b_uni", 7 }, { "b_det", 0.7f },
                            { "cut", 4800 }, { "res", 0.2f }, { "dist", 0.5f }, { "dtype", 2 }, { "aa", 0.01f }, { "ar", 0.35f } } },
        { "Wobble Reese", { { "a_table", 0 }, { "a_pos", 0.15f }, { "a_uni", 5 }, { "a_det", 0.45f }, { "b_level", 0.6f }, { "b_table", 3 }, { "b_oct", -1 }, { "b_uni", 3 }, { "b_det", 0.3f },
                            { "cut", 700 }, { "res", 0.35f }, { "dist", 0.45f }, { "dtype", 2 }, { "as", 1.0f }, { "ar", 0.12f },
                            { "l1_shape", 0 }, { "l1_sync", 1 }, { "l1_div", 5 }, { "m1_src", 1 }, { "m1_dst", 6 }, { "m1_amt", 0.55f }, { "m2_src", 2 }, { "m2_dst", 1 }, { "m2_amt", 0.3f }, { "l2_rate", 0.4f } } },
        { "Morph Pad", { { "a_table", 2 }, { "a_pos", 0.3f }, { "a_uni", 5 }, { "a_det", 0.3f }, { "a_spread", 1.0f }, { "cut", 3200 }, { "res", 0.15f }, { "aa", 0.5f }, { "ar", 1.2f }, { "as", 1.0f },
                         { "l1_shape", 1 }, { "l1_rate", 0.25f }, { "m1_src", 1 }, { "m1_dst", 1 }, { "m1_amt", 0.6f } } },
        { "Pluck", { { "a_table", 0 }, { "a_pos", 0.7f }, { "a_uni", 2 }, { "cut", 900 }, { "res", 0.3f }, { "fenv", 0.8f }, { "fd", 0.18f }, { "fs", 0.0f }, { "as", 0.0f }, { "ad", 0.35f }, { "ar", 0.2f } } },
    };
    return p;
}
}

LoopSynthProcessor::LoopSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", params::createLayout())
{
    const char* ls_[4] = { "shape", "rate", "sync", "div" };
    const char* ms_[3] = { "src", "dst", "amt" };
    for (int i = 0; i < ls::kNumLfo; ++i) for (int k = 0; k < 4; ++k) lfoPtr[i][k] = apvts.getRawParameterValue ("l" + juce::String (i + 1) + "_" + ls_[k]);
    for (int i = 0; i < ls::kNumMod; ++i) for (int k = 0; k < 3; ++k) modPtr[i][k] = apvts.getRawParameterValue ("m" + juce::String (i + 1) + "_" + ms_[k]);
}

float LoopSynthProcessor::get (const char* id) const
{
    auto* v = apvts.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

void LoopSynthProcessor::readParams (ls::P& p) const
{
    auto osc = [&] (ls::OscP& o, const char* t, const char* ps, const char* oc, const char* fn, const char* lv, const char* un, const char* dt, const char* sp)
    {
        o.table = (int) get (t); o.pos = get (ps); o.oct = (int) get (oc); o.fine = get (fn); o.level = get (lv);
        o.uni = (int) get (un); o.det = get (dt); o.spread = get (sp);
    };
    osc (p.a, "a_table", "a_pos", "a_oct", "a_fine", "a_level", "a_uni", "a_det", "a_spread");
    osc (p.b, "b_table", "b_pos", "b_oct", "b_fine", "b_level", "b_uni", "b_det", "b_spread");
    p.sub = get ("sub"); p.rndPhase = get ("rnd");
    p.ftype = (int) get ("ftype"); p.cut = get ("cut"); p.res = get ("res"); p.fdrive = get ("fdrive"); p.fenv = get ("fenv"); p.fkey = get ("fkey");
    p.fa = get ("fa"); p.fd = get ("fd"); p.fs = get ("fs"); p.fr = get ("fr");
    p.aa = get ("aa"); p.ad = get ("ad"); p.as = get ("as"); p.ar = get ("ar");
    p.posEnv = get ("poslenv"); p.mono = get ("mono") > 0.5f; p.glide = get ("glide"); p.bend = get ("bend");
    p.velSens = get ("vel"); p.dtype = (int) get ("dtype"); p.dist = get ("dist"); p.master = get ("master");
    for (int i = 0; i < ls::kNumLfo; ++i)
    {
        p.lfo[i].shape = (int) lfoPtr[i][0]->load(); p.lfo[i].rate = lfoPtr[i][1]->load();
        p.lfo[i].sync = lfoPtr[i][2]->load() > 0.5f; p.lfo[i].div = (int) lfoPtr[i][3]->load();
    }
    for (int i = 0; i < ls::kNumMod; ++i)
    {
        p.mod[i].src = (int) modPtr[i][0]->load(); p.mod[i].dst = (int) modPtr[i][1]->load(); p.mod[i].amt = modPtr[i][2]->load();
    }
}

void LoopSynthProcessor::publishCustom (int slot, const std::vector<float>& frames, int nf, const juce::String& name)
{
    synth.bank().setCustom (slot, ls::buildTableSet (frames.data(), nf, name.toStdString()));
    custName[slot] = name;
    custRaw[slot].resize (frames.size());
    for (size_t i = 0; i < frames.size(); ++i) custRaw[slot][i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (frames[i] * 32767.0f));
}

bool LoopSynthProcessor::loadWavetable (int slot, const void* data, size_t size, const juce::String& name, juce::String& error)
{
    if (slot < 0 || slot >= ls::kCustom) { error = "No such slot"; return false; }
    ls::WavData wd;
    if (! ls::parseWav (static_cast<const uint8_t*> (data), size, wd)) { error = "Not a readable .wav file (PCM or float)"; return false; }
    int nf = 0;
    auto frames = ls::framesFromAudio (wd.mono, wd.frameSize, nf);
    if (nf < 1) { error = "File is too short for a wavetable"; return false; }
    publishCustom (slot, frames, nf, name);
    return true;
}

void LoopSynthProcessor::clearWavetable (int slot)
{
    if (slot < 0 || slot >= ls::kCustom) return;
    synth.bank().clearCustom (slot); custName[slot] = {}; custRaw[slot].clear();
}

void LoopSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate);
    scratch.setSize (2, juce::jmax (1, samplesPerBlock));
}

bool LoopSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LoopSynthProcessor::handle (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) synth.noteOn (m.getNoteNumber(), m.getFloatVelocity(), cur);
    else if (m.isNoteOff()) synth.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.allOff();
    else if (m.isPitchWheel()) synth.setBend ((m.getPitchWheelValue() - 8192) / 8192.0);
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1) synth.setMod (m.getControllerValue() / 127.0);
        else if (m.getControllerNumber() == 64) synth.sustain (m.getControllerValue() >= 64);
    }
}

void LoopSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    readParams (cur);
    cur.bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) cur.bpm = *bpm;
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (1);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { synth.render (L + pos, R + pos, at - pos, cur); pos = at; }
        handle (meta.getMessage());
    }
    if (n > pos) synth.render (L + pos, R + pos, n - pos, cur);
}

juce::AudioProcessorEditor* LoopSynthProcessor::createEditor() { return new LoopSynthEditor (*this); }

int LoopSynthProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String LoopSynthProcessor::getProgramName (int i) { return presets()[(size_t) juce::jlimit (0, getNumPrograms() - 1, i)].name; }

void LoopSynthProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    program = index;
    // start from defaults so every preset sounds the same on any starting state
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (const auto& kv : presets()[(size_t) index].v)
        if (auto* rp = apvts.getParameter (kv.first))
            rp->setValueNotifyingHost (rp->convertTo0to1 (kv.second));
}

void LoopSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", program, nullptr);
    if (auto xml = state.createXml())
    {
        for (int i = 0; i < ls::kCustom; ++i)
            if (! custRaw[i].empty())
            {
                auto* e = xml->createNewChildElement ("CUSTOM");
                e->setAttribute ("slot", i); e->setAttribute ("name", custName[i]);
                e->setAttribute ("frames", (int) (custRaw[i].size() / ls::kTable));
                e->setAttribute ("data", juce::Base64::toBase64 (custRaw[i].data(), custRaw[i].size() * sizeof (int16_t)));
            }
        copyXmlToBinary (*xml, dest);
    }
}

void LoopSynthProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::Array<juce::XmlElement*> cust;
            for (auto* e : xml->getChildWithTagNameIterator ("CUSTOM")) cust.add (e);
            for (int i = 0; i < ls::kCustom; ++i) clearWavetable (i);
            for (auto* e : cust)
            {
                const int slot = e->getIntAttribute ("slot", -1), nf = e->getIntAttribute ("frames", 0);
                juce::MemoryOutputStream out;
                if (slot >= 0 && slot < ls::kCustom && nf >= 1 && nf <= ls::kMaxFrames && juce::Base64::convertFromBase64 (out, e->getStringAttribute ("data"))
                    && out.getDataSize() >= (size_t) nf * ls::kTable * sizeof (int16_t))
                {
                    const auto* q = static_cast<const int16_t*> (out.getData());
                    std::vector<float> fr ((size_t) nf * ls::kTable);
                    for (size_t i = 0; i < fr.size(); ++i) fr[i] = q[i] / 32767.0f;
                    publishCustom (slot, fr, nf, e->getStringAttribute ("name"));
                }
            }
            for (auto* e : cust) xml->removeChildElement (e, true);
            auto vt = juce::ValueTree::fromXml (*xml);
            program = (int) vt.getProperty ("program", 0);
            apvts.replaceState (vt);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LoopSynthProcessor(); }
