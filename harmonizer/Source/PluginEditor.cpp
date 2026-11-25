#include "PluginEditor.h"

namespace
{
constexpr double kFmin = 40.0, kFmax = 10000.0;
const char* const kNote[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
}

// ---------------------------------------------------------------- spectrum
SpectrumView::SpectrumView (HarmonizerProcessor& p) : proc (p)
{
    fft.setup (N); re.assign (N, 0.0); im.assign (N, 0.0); smooth.assign (N / 2 + 1, 0.0); samples.assign (N, 0.0f);
    startTimerHz (30);
}

void SpectrumView::timerCallback()
{
    proc.copyLatest (samples.data(), N);
    for (int i = 0; i < N; ++i) { re[(size_t) i] = samples[(size_t) i] * (0.5 - 0.5 * std::cos (2 * lh::kPi * i / N)); im[(size_t) i] = 0; }
    fft.run (re.data(), im.data(), false);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double m = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * 4.0 / N;
        smooth[(size_t) k] = 0.8 * smooth[(size_t) k] + 0.2 * m;
    }
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();
    g.setColour (juce::Colour (0xff0d0f15)); g.fillRoundedRectangle (b, 6.0f);
    const double L = std::log (kFmax / kFmin), sr = proc.sampleRateNow();
    auto xOf = [&] (double f) { return (float) (std::log (f / kFmin) / L * w); };
    const auto P = proc.currentParams();

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0.0f, 0.0f, xOf (juce::jmax (kFmin, (double) P.lowCut)), h);   // below Low cut the sound stays untouched

    juce::Path path; path.startNewSubPath (0, h);
    const double bin = sr / N;
    for (int x = 0; x <= (int) w; ++x)
    {
        const double f = kFmin * std::exp (L * x / w), f2 = kFmin * std::exp (L * (x + 1) / w);
        const int i0 = juce::jmax (1, (int) (f / bin)), i1 = juce::jmax (i0, (int) (f2 / bin));
        double m = 0; for (int i = i0; i <= i1 && i <= N / 2; ++i) m = juce::jmax (m, smooth[(size_t) i]);
        const double db = 20.0 * std::log10 (juce::jmax (1e-6, m));
        const float v = (float) juce::jlimit (0.0, 1.0, (db + 95.0) / 70.0);
        path.lineTo ((float) x, h - v * (h - 14.0f));
    }
    path.lineTo (w, h); path.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xd9aa82ff), 0, 0, juce::Colour (0x403c5ac8), 0, h, false));
    g.fillPath (path);

    g.setFont (juce::FontOptions (9.0f));
    for (int m = (int) std::ceil (69 + 12 * std::log2 (kFmin / 440)); m <= (int) std::floor (69 + 12 * std::log2 (kFmax / 440)); ++m)
    {
        const int pc = ((m % 12) + 12) % 12;
        if (! P.mask[(size_t) pc]) continue;
        const double f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
        const float x = std::round (xOf (f)) + 0.5f;
        const bool root = pc == P.root;
        g.setColour (root ? juce::Colour (0xf2ffd666) : juce::Colour (0x6678ffc8));
        g.drawLine (x, root ? 12.0f : 16.0f, x, h, root ? 1.5f : 1.0f);
        if (root) { g.setColour (juce::Colour (0xffffd666)); g.drawText (juce::String (kNote[pc]) + juce::String (m / 12 - 1), juce::Rectangle<float> (x - 16, 1, 32, 11), juce::Justification::centred); }
    }
}

// ---------------------------------------------------------------- piano
PianoView::PianoView (HarmonizerProcessor& p) : proc (p) { startTimerHz (15); }

void PianoView::timerCallback()
{
    const auto cur = proc.currentParams();
    if (! haveLast || cur.mask != last.mask || cur.root != last.root) { last = cur; haveLast = true; repaint(); }
}

juce::Rectangle<float> PianoView::keyRect (int pc) const
{
    const auto b = getLocalBounds().toFloat();
    const int whiteIdx[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const float ww = b.getWidth() / 7.0f;
    if (! isBlack (pc)) return { b.getX() + whiteIdx[pc] * ww, b.getY(), ww, b.getHeight() };
    return { b.getX() + (whiteIdx[pc] + 1) * ww - ww * 0.3f, b.getY(), ww * 0.6f, b.getHeight() * 0.6f };
}

int PianoView::keyAt (juce::Point<float> pt) const
{
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc) && keyRect (pc).contains (pt)) return pc;
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc) && keyRect (pc).contains (pt)) return pc;
    return -1;
}

void PianoView::paint (juce::Graphics& g)
{
    const auto P = proc.currentParams();
    for (int pass = 0; pass < 2; ++pass)
        for (int pc = 0; pc < 12; ++pc)
        {
            if ((pass == 0) == isBlack (pc)) continue;
            auto r = keyRect (pc).reduced (0.5f);
            const bool on = P.mask[(size_t) pc], root = pc == P.root;
            if (isBlack (pc)) g.setColour (root && on ? juce::Colour (0xffd9a62a) : on ? juce::Colour (0xff7646e0) : juce::Colour (0xff0f1117));
            else g.setColour (root && on ? juce::Colour (0xffe2b232) : on ? juce::Colour (0xff9a6cf2) : juce::Colour (0xff2b2e38));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colour (0xff0c0d12)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
            if (! isBlack (pc)) { g.setColour (juce::Colours::white.withAlpha (on ? 0.85f : 0.35f)); g.setFont (juce::FontOptions (10.0f)); g.drawText (kNote[pc], r.removeFromBottom (14), juce::Justification::centred); }
        }
}

void PianoView::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc < 0) return;
    const auto P = proc.currentParams();
    // work out the current scale as intervals above the root, toggle the clicked one, and store it as the Custom scale
    std::array<bool, 12> next {};
    for (int i = 0; i < 12; ++i) next[(size_t) i] = P.mask[(size_t) ((i + P.root) % 12)];
    const int rel = ((pc - P.root) % 12 + 12) % 12;
    next[(size_t) rel] = ! next[(size_t) rel];
    int count = 0; for (bool b : next) count += b ? 1 : 0;
    if (count == 0) return;                               // keep at least one note
    for (int i = 0; i < 12; ++i)
        if (auto* prm = proc.apvts.getParameter ("n" + juce::String (i))) { prm->beginChangeGesture(); prm->setValueNotifyingHost (next[(size_t) i] ? 1.0f : 0.0f); prm->endChangeGesture(); }
    if (auto* sc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scale")))
    { sc->beginChangeGesture(); sc->setValueNotifyingHost (sc->convertTo0to1 ((float) lh::customScaleIndex())); sc->endChangeGesture(); }
}

// ---------------------------------------------------------------- editor
void HarmonizerEditor::addKnob (const char* id, const char* text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffb28cff));
    k->slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe6d8ff));
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.0f));
    k->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
}

HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p) : AudioProcessorEditor (&p), proc (p), spectrum (p), piano (p)
{
    addAndMakeVisible (spectrum); addAndMakeVisible (piano);
    for (auto& s : lh::scales()) scaleBox.addItem (s.name, scaleBox.getNumItems() + 1);
    for (int i = 0; i < 12; ++i) keyBox.addItem (kNote[i], i + 1);
    qualityBox.addItemList (juce::StringArray { "Fast (21 ms)", "High (43 ms)" }, 1);
    for (auto* c : { &keyBox, &scaleBox, &qualityBox }) addAndMakeVisible (*c);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "quality", qualityBox);
    keyLabel.setText ("Key", juce::dontSendNotification); scaleLabel.setText ("Scale", juce::dontSendNotification);
    for (auto* l : { &keyLabel, &scaleLabel }) { l->setFont (juce::FontOptions (11.0f)); l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f)); addAndMakeVisible (*l); }
    title.setText ("LOOP KEY HARMONIZER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold)); title.setColour (juce::Label::textColourId, juce::Colour (0xffd9c6ff)); addAndMakeVisible (title);
    info.setText ("Click piano keys to build your own scale. Quality changes the latency; the host compensates it.", juce::dontSendNotification);
    info.setFont (juce::FontOptions (11.0f)); info.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f)); addAndMakeVisible (info);
    addKnob ("snap", "Snap"); addKnob ("cut", "Low cut"); addKnob ("mix", "Mix"); addKnob ("out", "Output");
    setSize (760, 560);
}

void HarmonizerEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff14161d)); }

void HarmonizerEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (32);
    title.setBounds (top.removeFromLeft (300));
    qualityBox.setBounds (top.removeFromRight (160).reduced (0, 3));
    r.removeFromTop (6);
    spectrum.setBounds (r.removeFromTop (230));
    r.removeFromTop (10);
    auto row = r.removeFromTop (120);
    auto left = row.removeFromLeft (230);
    keyLabel.setBounds (left.removeFromTop (16));
    keyBox.setBounds (left.removeFromTop (26)); left.removeFromTop (8);
    scaleLabel.setBounds (left.removeFromTop (16));
    scaleBox.setBounds (left.removeFromTop (26));
    row.removeFromLeft (14);
    const int cw = row.getWidth() / 4;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = row.removeFromLeft (cw);
        knobs[i]->label.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
    r.removeFromTop (10);
    piano.setBounds (r.removeFromTop (110));
    r.removeFromTop (6);
    info.setBounds (r.removeFromTop (18));
}
