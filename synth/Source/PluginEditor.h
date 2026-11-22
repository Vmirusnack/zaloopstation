#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// 3D-style picture of a wavetable: every frame is one line, the current position is highlighted.
// Dragging across it moves the Position parameter.
class WtView : public juce::Component, private juce::Timer
{
public:
    WtView (LoopSynthProcessor&, const char* tableId, const char* posId);
    ~WtView() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setFromX (float x);
    LoopSynthProcessor& proc;
    std::atomic<float>* tableP;
    std::atomic<float>* posP;
    juce::RangedAudioParameter* posParam;
    int lastTable = -1; float lastPos = -1; const ls::TSet* lastSet = nullptr;
};

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
        int page = 0;
        std::vector<Knob*> knobs;
        std::vector<Combo*> combos;
        std::vector<Toggle*> toggles;
        WtView* view = nullptr;
        juce::TextButton* load = nullptr;
        juce::Label* tname = nullptr;
        int osc = 0;
        juce::Rectangle<int> area;
    };
    struct MatRow { Combo* src; Combo* dst; Knob* amt; };

    Knob* addKnob (Section& s, const char* id, const char* text);
    Knob* addKnob (Section& s, const juce::String& id, const char* text) { return addKnob (s, id.toRawUTF8(), text); }
    Combo* addCombo (Section& s, const juce::String& id, const char* text);
    Toggle* addToggle (Section& s, const juce::String& id, const char* text);
    void showPage (int page);
    void loadTable (int osc);
    void refreshNames();

    LoopSynthProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<WtView>> views;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<Section>> sections;
    std::vector<MatRow> matrix;
    juce::ComboBox presetBox;
    juce::Label title;
    juce::TextButton tabSound { "SOUND" }, tabMod { "MODULATION" };
    int page = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopSynthEditor)
};
