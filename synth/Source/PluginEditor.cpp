#include "PluginEditor.h"

// ---------------------------------------------------------------- wavetable picture
WtView::WtView (LoopSynthProcessor& p, const char* tableId, const char* posId)
    : proc (p), tableP (p.apvts.getRawParameterValue (tableId)), posP (p.apvts.getRawParameterValue (posId)),
      posParam (p.apvts.getParameter (posId))
{
    startTimerHz (15);
}

void WtView::timerCallback()
{
    const int t = (int) tableP->load();
    const float ps = posP->load();
    const ls::TSet* s = proc.tables().get (t);
    if (t != lastTable || ps != lastPos || s != lastSet) { lastTable = t; lastPos = ps; lastSet = s; repaint(); }
}

void WtView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0e1016)); g.fillRoundedRectangle (b, 6.0f);
    const ls::TSet* ts = proc.tables().get ((int) tableP->load());
    if (ts == nullptr) return;
    const float pos = juce::jlimit (0.0f, 1.0f, posP->load());
    const int nf = ts->frames;
    const int shown = juce::jmin (nf, 48);
    auto area = b.reduced (8.0f, 6.0f);
    const float skewX = area.getWidth() * 0.10f, skewY = area.getHeight() * 0.34f;
    const float w = area.getWidth() - skewX, h = area.getHeight() - skewY;
    constexpr int pts = 110;
    const float cur = nf > 1 ? pos * (float) (nf - 1) : 0.0f;
    auto framePath = [&] (int f, float d)
    {
        juce::Path pa;
        const float ox = area.getX() + d * skewX, oy = area.getBottom() - (1.0f - d) * 0.0f - d * skewY - h * 0.5f;
        const float* src = ts->at (f, 0);
        for (int i = 0; i < pts; ++i)
        {
            const float v = src[(i * (ls::kTable - 1)) / (pts - 1)];
            const float x = ox + w * i / (float) (pts - 1), y = oy - v * h * 0.42f;
            if (i == 0) pa.startNewSubPath (x, y); else pa.lineTo (x, y);
        }
        return pa;
    };
    // back to front: last frame is furthest away
    for (int k = shown - 1; k >= 0; --k)
    {
        const int f = shown == 1 ? 0 : (int) std::lround (k * (nf - 1) / (double) (shown - 1));
        const float d = nf > 1 ? f / (float) (nf - 1) : 0.0f;
        const float near = std::fabs ((float) f - cur);
        g.setColour (juce::Colour (0xff6c5aa8).withAlpha (near < 0.5f ? 0.0f : 0.20f + 0.35f * (1.0f - d)));
        g.strokePath (framePath (f, d), juce::PathStrokeType (1.0f));
    }
    const int fa = (int) std::floor (cur), fb = juce::jmin (nf - 1, fa + 1);
    const float fr = cur - (float) fa;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int f = pass == 0 ? fa : fb;
        if (pass == 1 && (fb == fa || fr < 0.02f)) break;
        const float d = nf > 1 ? f / (float) (nf - 1) : 0.0f;
        g.setColour (juce::Colour (0xffe6d8ff).withAlpha (pass == 0 ? 1.0f - fr * 0.7f : 0.3f + fr * 0.7f));
        g.strokePath (framePath (f, d), juce::PathStrokeType (pass == 0 ? 2.0f : 1.6f));
    }
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (juce::String (ts->name) + "   frame " + juce::String (juce::jmin (nf, fa + 1)) + " / " + juce::String (nf),
                getLocalBounds().reduced (8, 4), juce::Justification::topLeft);
}

void WtView::setFromX (float x)
{
    const float v = juce::jlimit (0.0f, 1.0f, (x - 8.0f) / juce::jmax (1.0f, (float) getWidth() - 16.0f));
    posParam->setValueNotifyingHost (posParam->convertTo0to1 (v));
}
void WtView::mouseDown (const juce::MouseEvent& e) { posParam->beginChangeGesture(); setFromX (e.position.x); }
void WtView::mouseDrag (const juce::MouseEvent& e) { setFromX (e.position.x); }
void WtView::mouseUp (const juce::MouseEvent&) { posParam->endChangeGesture(); }

// ---------------------------------------------------------------- editor
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

LoopSynthEditor::Combo* LoopSynthEditor::addCombo (Section& s, const juce::String& id, const char* text)
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

LoopSynthEditor::Toggle* LoopSynthEditor::addToggle (Section& s, const juce::String& id, const char* text)
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
    auto newSection = [&] (const char* name, int pg) -> Section&
    {
        sections.push_back (std::make_unique<Section>()); auto& s = *sections.back(); s.title = name; s.page = pg; return s;
    };

    // ---- page 0: sound
    int oscIdx = 0;
    auto osc = [&] (const char* name, const char* t, const char* ps, const char* oc, const char* fn, const char* lv, const char* un, const char* dt, const char* sp)
    {
        auto& s = newSection (name, 0); s.osc = oscIdx++;
        addCombo (s, t, "Table");
        auto v = std::make_unique<WtView> (proc, t, ps); addAndMakeVisible (*v); s.view = v.get(); views.push_back (std::move (v));
        auto b = std::make_unique<juce::TextButton> ("Load .wav…"); addAndMakeVisible (*b); s.load = b.get();
        b->setTooltip ("Load a wavetable .wav (Serum format works: 2048 samples per frame, up to 256 frames) into the selected User table");
        const int idx = s.osc; b->onClick = [this, idx] { loadTable (idx); }; buttons.push_back (std::move (b));
        auto l = std::make_unique<juce::Label>(); l->setFont (juce::FontOptions (10.5f)); l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addAndMakeVisible (*l); s.tname = l.get(); labels.push_back (std::move (l));
        addKnob (s, ps, "Position"); addKnob (s, lv, "Level"); addKnob (s, oc, "Octave"); addKnob (s, fn, "Fine");
        addKnob (s, un, "Unison"); addKnob (s, dt, "Detune"); addKnob (s, sp, "Spread");
    };
    osc ("OSC A", "a_table", "a_pos", "a_oct", "a_fine", "a_level", "a_uni", "a_det", "a_spread");
    osc ("OSC B", "b_table", "b_pos", "b_oct", "b_fine", "b_level", "b_uni", "b_det", "b_spread");

    { auto& s = newSection ("SUB / MIX", 0);
      addKnob (s, "sub", "Sub"); addKnob (s, "rnd", "Rand phase"); addKnob (s, "master", "Master"); }

    { auto& s = newSection ("FILTER", 0);
      addCombo (s, "ftype", "Type");
      addKnob (s, "cut", "Cutoff"); addKnob (s, "res", "Resonance"); addKnob (s, "fdrive", "Drive");
      addKnob (s, "fenv", "Env amount"); addKnob (s, "fkey", "Key track"); addKnob (s, "poslenv", "Pos by env"); }

    { auto& s = newSection ("FILTER ENVELOPE", 0);
      addKnob (s, "fa", "Attack"); addKnob (s, "fd", "Decay"); addKnob (s, "fs", "Sustain"); addKnob (s, "fr", "Release"); }

    { auto& s = newSection ("AMP ENVELOPE", 0);
      addKnob (s, "aa", "Attack"); addKnob (s, "ad", "Decay"); addKnob (s, "as", "Sustain"); addKnob (s, "ar", "Release"); }

    { auto& s = newSection ("PLAY", 0);
      addToggle (s, "mono", "Mono");
      addKnob (s, "glide", "Glide"); addKnob (s, "bend", "Bend range"); addKnob (s, "vel", "Velocity"); }

    { auto& s = newSection ("DISTORTION", 0);
      addCombo (s, "dtype", "Type");
      addKnob (s, "dist", "Amount"); }

    // ---- page 1: modulation
    for (int i = 1; i <= 3; ++i)
    {
        auto& s = newSection (("LFO " + juce::String (i)).toRawUTF8(), 1);
        const juce::String pre = "l" + juce::String (i) + "_";
        addCombo (s, pre + "shape", "Shape");
        addToggle (s, pre + "sync", "Tempo sync");
        addCombo (s, pre + "div", "Rate");
        addKnob (s, pre + "rate", "Hz (free)");
    }
    {
        auto& s = newSection ("MODULATION MATRIX", 1);
        for (int i = 1; i <= 8; ++i)
        {
            const juce::String pre = "m" + juce::String (i) + "_";
            auto* a = addCombo (s, pre + "src", "");
            auto* d = addCombo (s, pre + "dst", "");
            auto* k = addKnob (s, pre + "amt", "");
            k->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            k->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
            s.knobs.pop_back();            // laid out by hand below
            matrix.push_back ({ a, d, k });
            s.combos.pop_back (); s.combos.pop_back();
        }
    }

    title.setText ("LOOP SYNTH", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffd9c6ff));
    addAndMakeVisible (title);

    for (auto* b : { &tabSound, &tabMod }) { addAndMakeVisible (*b); b->setClickingTogglesState (false); }
    tabSound.onClick = [this] { showPage (0); };
    tabMod.onClick = [this] { showPage (1); };

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setSize (960, 800);
    showPage (0);
    refreshNames();
}

void LoopSynthEditor::refreshNames()
{
    for (auto& s : sections)
        if (s->tname != nullptr)
        {
            const juce::String id = s->osc == 0 ? "a_table" : "b_table";
            const int t = (int) *proc.apvts.getRawParameterValue (id);
            s->tname->setText (t >= ls::kSets ? (proc.wavetableName (t - ls::kSets).isEmpty() ? "(empty: press Load .wav)" : proc.wavetableName (t - ls::kSets)) : juce::String(), juce::dontSendNotification);
        }
}

void LoopSynthEditor::showPage (int pg)
{
    page = pg;
    tabSound.setColour (juce::TextButton::buttonColourId, pg == 0 ? juce::Colour (0xff5a46a8) : juce::Colour (0xff2a2d3c));
    tabMod.setColour (juce::TextButton::buttonColourId, pg == 1 ? juce::Colour (0xff5a46a8) : juce::Colour (0xff2a2d3c));
    for (auto& s : sections)
    {
        const bool on = s->page == pg;
        for (auto* k : s->knobs) { k->slider.setVisible (on); k->label.setVisible (on); }
        for (auto* c : s->combos) { c->box.setVisible (on); c->label.setVisible (on); }
        for (auto* t : s->toggles) t->button.setVisible (on);
        if (s->view) s->view->setVisible (on);
        if (s->load) s->load->setVisible (on);
        if (s->tname) s->tname->setVisible (on);
    }
    for (auto& m : matrix) { m.src->box.setVisible (pg == 1); m.dst->box.setVisible (pg == 1); m.amt->slider.setVisible (pg == 1); m.src->label.setVisible (false); m.dst->label.setVisible (false); m.amt->label.setVisible (false); }
    resized(); repaint();
}

void LoopSynthEditor::loadTable (int osc)
{
    const juce::String tid = osc == 0 ? "a_table" : "b_table";
    int cur = (int) *proc.apvts.getRawParameterValue (tid) - ls::kSets, slot = cur;
    if (slot < 0)
    {
        slot = 0;
        for (int i = 0; i < ls::kCustom; ++i) if (! proc.tables().hasCustom (i)) { slot = i; break; }
    }
    chooser = std::make_unique<juce::FileChooser> ("Load a wavetable", juce::File(), "*.wav;*.WAV");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot, tid] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::MemoryBlock mb;
            juce::String err;
            if (! f.loadFileAsData (mb)) err = "Could not read the file";
            else proc.loadWavetable (slot, mb.getData(), mb.getSize(), f.getFileNameWithoutExtension().substring (0, 24), err);
            if (err.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wavetable", err); return; }
            if (auto* prm = proc.apvts.getParameter (tid))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (ls::kSets + slot)));
                prm->endChangeGesture();
            }
            refreshNames();
        });
}

void LoopSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161d));
    for (auto& s : sections)
    {
        if (s->page != page) continue;
        g.setColour (juce::Colour (0xff1d2030));
        g.fillRoundedRectangle (s->area.toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s->title, s->area.withHeight (20).withTrimmedLeft (10), juce::Justification::centredLeft);
    }
    if (page == 1)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (juce::FontOptions (10.5f));
        for (auto& s : sections)
            if (s->title == "MODULATION MATRIX")
            {
                const int y = s->area.getY() + 24;
                g.drawText ("Source", juce::Rectangle<int> (s->area.getX() + 12, y, 200, 14), juce::Justification::centredLeft);
                g.drawText ("Destination", juce::Rectangle<int> (s->area.getX() + 12 + 210, y, 200, 14), juce::Justification::centredLeft);
                g.drawText ("Amount", juce::Rectangle<int> (s->area.getX() + 12 + 434, y, 200, 14), juce::Justification::centredLeft);
            }
    }
}

void LoopSynthEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (170));
    tabSound.setBounds (top.removeFromLeft (90).reduced (0, 4)); top.removeFromLeft (4);
    tabMod.setBounds (top.removeFromLeft (130).reduced (0, 4));
    presetBox.setBounds (top.removeFromRight (240).reduced (0, 4));
    r.removeFromTop (6);

    auto layoutSection = [&] (Section& s)
    {
        auto inner = s.area.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4);
        if (s.view != nullptr)
        {
            auto line = inner.removeFromTop (22);
            s.load->setBounds (line.removeFromRight (84));
            line.removeFromRight (6);
            s.tname->setBounds (line.removeFromRight (150));
            auto* c = s.combos.front();
            c->label.setBounds (line.removeFromLeft (38)); c->box.setBounds (line);
            inner.removeFromTop (4);
            s.view->setBounds (inner.removeFromTop (juce::jmax (80, inner.getHeight() - 160)));
            inner.removeFromTop (4);
        }
        else
        {
            for (auto* c : s.combos)
            {
                auto line = inner.removeFromTop (22);
                c->label.setBounds (line.removeFromLeft (38));
                c->box.setBounds (line);
                inner.removeFromTop (4);
            }
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
    };

    if (page == 0)
    {
        // row 0: OSC A, OSC B (with the table pictures); row 1: sub, filter, filter env; row 2: amp env, play, distortion
        const int h0 = 330, h = (r.getHeight() - h0 - 16) / 2;
        const int rowOf[8] = { 0, 0, 1, 1, 1, 2, 2, 2 };
        const int colsOf[8] = { 2, 2, 1, 3, 2, 2, 2, 1 };
        const int rowY[3] = { r.getY(), r.getY() + h0 + 8, r.getY() + h0 + h + 16 };
        const int rowH[3] = { h0, h, h };
        int usedW[3] = { 0, 0, 0 }, totalW[3] = { 0, 0, 0 };
        int idx = 0;
        for (auto& s : sections) if (s->page == 0) { totalW[rowOf[idx]] += colsOf[idx]; ++idx; }
        idx = 0;
        for (auto& s : sections)
        {
            if (s->page != 0) continue;
            const int row = rowOf[idx];
            const int w = (r.getWidth() - 8 * ((int) std::count (rowOf, rowOf + 8, row) - 1)) * colsOf[idx] / totalW[row];
            s->area = { r.getX() + usedW[row], rowY[row], w, rowH[row] };
            usedW[row] += w + 8;
            layoutSection (*s);
            ++idx;
        }
    }
    else
    {
        const int hl = 200;
        int x = r.getX(); const int w = (r.getWidth() - 16) / 3;
        for (auto& s : sections)
        {
            if (s->page != 1) continue;
            if (s->title.startsWith ("LFO"))
            {
                s->area = { x, r.getY(), w, hl }; x += w + 8;
                auto inner = s->area.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4);
                auto c0 = s->combos[0]; auto line = inner.removeFromTop (22); c0->label.setBounds (line.removeFromLeft (42)); c0->box.setBounds (line); inner.removeFromTop (6);
                s->toggles[0]->button.setBounds (inner.removeFromTop (22)); inner.removeFromTop (4);
                auto c1 = s->combos[1]; line = inner.removeFromTop (22); c1->label.setBounds (line.removeFromLeft (42)); c1->box.setBounds (line); inner.removeFromTop (6);
                auto cell = inner.withSizeKeepingCentre (110, inner.getHeight());
                s->knobs[0]->label.setBounds (cell.removeFromTop (14)); s->knobs[0]->slider.setBounds (cell);
            }
            else
            {
                s->area = { r.getX(), r.getY() + hl + 8, r.getWidth(), 8 * 34 + 58 };
                auto rows = s->area.withTrimmedTop (42).reduced (12, 0);
                for (auto& m : matrix)
                {
                    auto line = rows.removeFromTop (34).reduced (0, 3);
                    m.src->box.setBounds (line.removeFromLeft (200)); line.removeFromLeft (10);
                    m.dst->box.setBounds (line.removeFromLeft (210)); line.removeFromLeft (14);
                    m.amt->slider.setBounds (line.removeFromLeft (juce::jmin (420, line.getWidth())));
                }
            }
        }
    }
}
