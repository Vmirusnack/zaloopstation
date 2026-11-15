#include "PluginEditor.h"

LoopSynthEditor::Knob* LoopSynthEditor::addKnob (Section& s, const char* id, const char* text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffb28cff));
    k->slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe6d8ff));
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.0f));
    k->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    auto* raw = k.get();
    knobs.push_back (std::move (k));
    s.knobs.push_back (raw);
    return raw;
}

LoopSynthEditor::Combo* LoopSynthEditor::addCombo (Section& s, const char* id, const char* text)
{
    auto c = std::make_unique<Combo>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->box.addItemList (p->choices, 1);
    c->label.setText (text, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centredLeft);
    c->label.setFont (juce::FontOptions (11.0f));
    c->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (c->box); addAndMakeVisible (c->label);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    auto* raw = c.get();
    combos.push_back (std::move (c));
    s.combos.push_back (raw);
    return raw;
}

LoopSynthEditor::Toggle* LoopSynthEditor::addToggle (Section& s, const char* id, const char* text)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (text);
    addAndMakeVisible (t->button);
    t->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t->button);
    auto* raw = t.get();
    toggles.push_back (std::move (t));
    s.toggles.push_back (raw);
    return raw;
}

LoopSynthEditor::LoopSynthEditor (LoopSynthProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    sections.reserve (8);   // pointers into the vector are not kept, but keep it stable anyway
    auto osc = [&] (const char* name, const char* t, const char* ps, const char* oc, const char* fn, const char* lv, const char* un, const char* dt, const char* sp)
    {
        sections.emplace_back(); auto& s = sections.back(); s.title = name;
        addCombo (s, t, "Table");
        addKnob (s, ps, "Position"); addKnob (s, lv, "Level"); addKnob (s, oc, "Octave"); addKnob (s, fn, "Fine");
        addKnob (s, un, "Unison"); addKnob (s, dt, "Detune"); addKnob (s, sp, "Spread");
    };
    osc ("OSC A", "a_table", "a_pos", "a_oct", "a_fine", "a_level", "a_uni", "a_det", "a_spread");
    osc ("OSC B", "b_table", "b_pos", "b_oct", "b_fine", "b_level", "b_uni", "b_det", "b_spread");

    { sections.emplace_back(); auto& s = sections.back(); s.title = "SUB / MIX";
      addKnob (s, "sub", "Sub"); addKnob (s, "rnd", "Rand phase"); addKnob (s, "master", "Master"); }

    { sections.emplace_back(); auto& s = sections.back(); s.title = "FILTER";
      addCombo (s, "ftype", "Type");
      addKnob (s, "cut", "Cutoff"); addKnob (s, "res", "Resonance"); addKnob (s, "fdrive", "Drive");
      addKnob (s, "fenv", "Env amount"); addKnob (s, "fkey", "Key track"); addKnob (s, "poslenv", "Pos by env"); }

    { sections.emplace_back(); auto& s = sections.back(); s.title = "FILTER ENVELOPE";
      addKnob (s, "fa", "Attack"); addKnob (s, "fd", "Decay"); addKnob (s, "fs", "Sustain"); addKnob (s, "fr", "Release"); }

    { sections.emplace_back(); auto& s = sections.back(); s.title = "AMP ENVELOPE";
      addKnob (s, "aa", "Attack"); addKnob (s, "ad", "Decay"); addKnob (s, "as", "Sustain"); addKnob (s, "ar", "Release"); }

    { sections.emplace_back(); auto& s = sections.back(); s.title = "PLAY";
      addToggle (s, "mono", "Mono");
      addKnob (s, "glide", "Glide"); addKnob (s, "bend", "Bend range"); addKnob (s, "vel", "Velocity"); }

    { sections.emplace_back(); auto& s = sections.back(); s.title = "DISTORTION";
      addCombo (s, "dtype", "Type");
      addKnob (s, "dist", "Amount"); }

    title.setText ("LOOP SYNTH", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffd9c6ff));
    addAndMakeVisible (title);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setSize (960, 700);
}

void LoopSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161d));
    for (auto& s : sections)
    {
        g.setColour (juce::Colour (0xff1d2030));
        g.fillRoundedRectangle (s.area.toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.title, s.area.withHeight (20).withTrimmedLeft (10), juce::Justification::centredLeft);
    }
}

void LoopSynthEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (200));
    presetBox.setBounds (top.removeFromRight (240).reduced (0, 4));
    r.removeFromTop (6);

    // row 1: OSC A, OSC B (4 columns x 2 rows of controls each); row 2: sub, filter, filter env; row 3: amp env, play, distortion
    const int rowH = (r.getHeight() - 16) / 3;
    const int rowOf[8] = { 0, 0, 1, 1, 1, 2, 2, 2 };
    const int colsOf[8] = { 2, 2, 1, 3, 2, 2, 2, 1 };   // kept as a rough width weight
    int rowStart[3] = { r.getY(), r.getY() + rowH + 8, r.getY() + 2 * (rowH + 8) };
    int usedW[3] = { 0, 0, 0 };
    int totalW[3] = { 0, 0, 0 };
    for (size_t i = 0; i < sections.size(); ++i) totalW[rowOf[i]] += colsOf[i];

    for (size_t i = 0; i < sections.size(); ++i)
    {
        auto& s = sections[i];
        const int row = rowOf[i];
        const int w = (r.getWidth() - 8 * ((int) std::count (rowOf, rowOf + 8, row) - 1)) * colsOf[i] / totalW[row];
        s.area = { r.getX() + usedW[row], rowStart[row], w, rowH };
        usedW[row] += w + 8;

        auto inner = s.area.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4);
        for (auto* c : s.combos)
        {
            auto line = inner.removeFromTop (22);
            c->label.setBounds (line.removeFromLeft (38));
            c->box.setBounds (line);
            inner.removeFromTop (4);
        }
        for (auto* t : s.toggles) { t->button.setBounds (inner.removeFromTop (22)); inner.removeFromTop (2); }
        const int n = (int) s.knobs.size();
        if (n > 0)
        {
            const int perRow = n > 4 ? (n + 1) / 2 : n;
            const int rows = (n + perRow - 1) / perRow;
            const int cellW = inner.getWidth() / perRow, cellH = inner.getHeight() / rows;
            for (int k = 0; k < n; ++k)
            {
                auto cell = juce::Rectangle<int> (inner.getX() + (k % perRow) * cellW, inner.getY() + (k / perRow) * cellH, cellW, cellH);
                s.knobs[(size_t) k]->label.setBounds (cell.removeFromTop (14));
                s.knobs[(size_t) k]->slider.setBounds (cell);
            }
        }
    }
}
