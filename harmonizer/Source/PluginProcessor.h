#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Harm.h"

class HarmonizerProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Loop Key Harmonizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // current settings as the engine sees them (used by the editor to draw the scale)
    lh::Params currentParams() const;
    double sampleRateNow() const { return sr.load(); }
    // newest `n` output samples (mono), oldest first
    void copyLatest (float* dest, int n) const;

private:
    void handleAsyncUpdate() override { setLatencySamples (latencyWanted.load()); }
    float get (const char* id) const;

    lh::Engine eng[2];
    int active = 0;
    std::atomic<double> sr { 44100.0 };
    std::atomic<int> latencyWanted { 1024 };
    std::vector<float> ring = std::vector<float> (16384, 0.0f);
    std::atomic<int> ringPos { 0 };
    juce::AudioBuffer<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerProcessor)
};
