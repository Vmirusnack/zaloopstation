#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp.h"
#include "Params.h"

class LoopSynthProcessor : public juce::AudioProcessor
{
public:
    LoopSynthProcessor();
    ~LoopSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Loop Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // user wavetables (.wav, Serum format works): slot 0-3 -> tables "User 1-4"
    bool loadWavetable (int slot, const void* data, size_t size, const juce::String& name, juce::String& error);
    void clearWavetable (int slot);
    juce::String wavetableName (int slot) const { return slot >= 0 && slot < ls::kCustom ? custName[slot] : juce::String(); }
    const ls::Tables& tables() const { return synth.bank(); }

private:
    std::atomic<float>* lfoPtr[ls::kNumLfo][4] {};
    std::atomic<float>* modPtr[ls::kNumMod][3] {};
    juce::String custName[ls::kCustom];
    std::vector<int16_t> custRaw[ls::kCustom];
    void publishCustom (int slot, const std::vector<float>& frames, int nf, const juce::String& name);

    float get (const char* id) const;
    void readParams (ls::P& p) const;
    void handle (const juce::MidiMessage& m);

    ls::Synth synth;
    ls::P cur;
    int program = 0;
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopSynthProcessor)
};
