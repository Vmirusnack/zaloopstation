#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class LoopSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit LoopSynthEditor (LoopSynthProcessor&);
    ~LoopSynthEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        std::vector<Combo*> combos;
        std::vector<Toggle*> toggles;
        juce::Rectangle<int> area;
    };

    Knob* addKnob (Section& s, const char* id, const char* text);
    Combo* addCombo (Section& s, const char* id, const char* text);
    Toggle* addToggle (Section& s, const char* id, const char* text);

    LoopSynthProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<Section> sections;
    juce::ComboBox presetBox;
    juce::Label title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopSynthEditor)
};
