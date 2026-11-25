#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Spectrum of the processed sound with the notes of the scale marked (gold = root)
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (HarmonizerProcessor&);
    ~SpectrumView() override { stopTimer(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HarmonizerProcessor& proc;
    lh::Fft fft;
    std::vector<double> re, im, smooth;
    std::vector<float> samples;
    static constexpr int N = 4096;
};

// one octave of piano: shows the scale and lets you click notes to build your own
class PianoView : public juce::Component, private juce::Timer
{
public:
    explicit PianoView (HarmonizerProcessor&);
    ~PianoView() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    int keyAt (juce::Point<float>) const;
    juce::Rectangle<float> keyRect (int pc) const;
    HarmonizerProcessor& proc;
    lh::Params last; bool haveLast = false;
};

class HarmonizerEditor : public juce::AudioProcessorEditor
{
public:
    explicit HarmonizerEditor (HarmonizerProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob { juce::Slider slider; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    void addKnob (const char* id, const char* text);

    HarmonizerProcessor& proc;
    SpectrumView spectrum;
    PianoView piano;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ComboBox keyBox, scaleBox, qualityBox;
    juce::Label keyLabel, scaleLabel, title, info;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, qualityAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
