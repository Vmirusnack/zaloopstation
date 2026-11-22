// Standalone checks for the Loop Synth engine (no JUCE).  g++ -O2 -std=c++17 dsp_test.cpp -o dsp_test && ./dsp_test [wavetable.wav ...]
#include "../Source/Dsp.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++fails; } else std::printf("ok:   %s\n", msg); } while (0)

static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
static bool finite (const std::vector<float>& v, float lim = 8.0f) { for (float x : v) if (! std::isfinite (x) || std::fabs (x) > lim) return false; return true; }

static std::vector<float> renderNote (ls::Synth& s, ls::P& p, int note, double secs, int block = 256)
{
    const int n = (int) (secs * 48000);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    s.noteOn (note, 1.0f, p);
    for (int i = 0; i < n; i += block) { const int k = std::min (block, n - i); s.render (L.data() + i, R.data() + i, k, p); }
    s.noteOff (note);
    return L;
}

// brightness proxy: mean absolute sample-to-sample difference relative to level
static double bright (const std::vector<float>& v, size_t a, size_t b) { double d = 0, e = 0; for (size_t i = a + 1; i < b; ++i) { d += std::fabs (v[i] - v[i - 1]); e += std::fabs (v[i]); } return d / std::max (1e-9, e); }

int main (int argc, char** argv)
{
    // 1. built-in sound still works
    { ls::Synth s; s.prepare (48000); ls::P p; auto o = renderNote (s, p, 48, 0.5);
      CHECK (finite (o) && rms (o, 4800, 20000) > 0.05, "built-in saw table renders audio"); }

    // 2. synthetic 40-frame table with Serum "clm " chunk through the WAV reader
    {
        const int nf = 40, fs = 2048;
        std::vector<float> pcm; for (int f = 0; f < nf; ++f) for (int i = 0; i < fs; ++i) { const double ph = (double) i / fs, t = f / (double) (nf - 1); pcm.push_back ((float) (std::sin (2 * ls::kPi * ph) * (1 - t) + std::sin (2 * ls::kPi * 9 * ph) * t * 0.7)); }
        std::vector<uint8_t> w; auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) (v >> (8 * i))); }; auto put16 = [&] (uint16_t v) { w.push_back ((uint8_t) v); w.push_back ((uint8_t) (v >> 8)); };
        const char* clm = "<!>2048 00000000 wav"; const uint32_t clmLen = 20, dl = (uint32_t) pcm.size() * 4;
        w.insert (w.end(), { 'R','I','F','F' }); put32 (4 + 8 + 16 + 8 + clmLen + 8 + dl); w.insert (w.end(), { 'W','A','V','E' });
        w.insert (w.end(), { 'f','m','t',' ' }); put32 (16); put16 (3); put16 (1); put32 (44100); put32 (44100 * 4); put16 (4); put16 (32);
        w.insert (w.end(), { 'c','l','m',' ' }); put32 (clmLen); for (uint32_t i = 0; i < clmLen; ++i) w.push_back ((uint8_t) clm[i]);
        w.insert (w.end(), { 'd','a','t','a' }); put32 (dl); for (float f : pcm) { uint32_t u; std::memcpy (&u, &f, 4); put32 (u); }
        ls::WavData wd; CHECK (ls::parseWav (w.data(), w.size(), wd), "float32 wav with clm chunk parses");
        CHECK (wd.frameSize == 2048 && wd.mono.size() == pcm.size(), "frame size and length read");
        int got = 0; auto fr = ls::framesFromAudio (wd.mono, wd.frameSize, got); CHECK (got == nf, "40 frames kept");
        ls::Synth s; s.prepare (48000);
        s.bank().setCustom (0, ls::buildTableSet (fr.data(), got, "synthetic"));
        CHECK (s.bank().get (4)->frames == 40, "custom slot published with 40 frames");
        ls::P p; p.a.table = 4; p.a.pos = 0.0f; auto lo = renderNote (s, p, 48, 0.4);
        p.a.pos = 1.0f; auto hi = renderNote (s, p, 48, 0.4);
        CHECK (finite (lo) && finite (hi), "custom table output is finite");
        CHECK (bright (hi, 9600, 19000) > bright (lo, 9600, 19000) * 2.5, "position morphs to the bright frame");
    }

    // 3. LFO -> cutoff modulation changes the sound over time
    {
        ls::Synth s; s.prepare (48000); ls::P p; p.cut = 900; p.res = 0.3f; p.as = 1.0f;
        p.lfo[0].shape = 0; p.lfo[0].rate = 3.0f; p.mod[0].src = 1; p.mod[0].dst = 6; p.mod[0].amt = 0.8f;
        auto o = renderNote (s, p, 36, 1.5);
        double mn = 1e9, mx = 0; for (size_t a = 4800; a + 2400 < o.size(); a += 2400) { const double b = bright (o, a, a + 2400); mn = std::min (mn, b); mx = std::max (mx, b); }
        CHECK (finite (o), "LFO render finite");
        std::printf ("      modulated swing %.2f\n", mx / mn);
        ls::Synth s2; s2.prepare (48000); ls::P q = p; q.mod[0].amt = 0; auto o2 = renderNote (s2, q, 36, 1.5);
        double mn2 = 1e9, mx2 = 0; for (size_t a = 4800; a + 2400 < o2.size(); a += 2400) { const double b = bright (o2, a, a + 2400); mn2 = std::min (mn2, b); mx2 = std::max (mx2, b); }
        std::printf ("      unmodulated swing %.2f\n", mx2 / mn2);
        CHECK (mx / mn > 1.5 * (mx2 / mn2) && mx / mn > 1.5, "LFO modulates the filter far more than the unmodulated sound moves");
    }

    // 4. position and pitch destinations, sync LFO, S&H
    {
        ls::Synth s; s.prepare (44100); ls::P p; p.a.table = 0; p.bpm = 140; p.lfo[1].shape = 4; p.lfo[1].sync = true; p.lfo[1].div = 6;
        p.mod[0].src = 2; p.mod[0].dst = 1; p.mod[0].amt = 1.0f; p.mod[1].src = 9; p.mod[1].dst = 3; p.mod[1].amt = 0.1f; p.mod[2].src = 7; p.mod[2].dst = 10; p.mod[2].amt = 0.5f;
        auto o = renderNote (s, p, 60, 1.0); CHECK (finite (o) && rms (o, 4800, 40000) > 0.02, "S&H, key and random sources render");
    }

    // 5. many voices + stress block sizes, no NaN
    {
        ls::Synth s; s.prepare (48000); ls::P p; p.a.uni = 7; p.b.level = 1; p.b.uni = 7; p.b.table = 3; p.dist = 0.6f;
        for (int i = 0; i < 20; ++i) s.noteOn (36 + i * 2, 1.0f, p);
        std::vector<float> L (4096), R (4096); bool ok = true;
        for (int b : { 1, 7, 64, 333, 4096 }) { s.render (L.data(), R.data(), b, p); for (int i = 0; i < b; ++i) if (! std::isfinite (L[(size_t) i]) || std::fabs (L[(size_t) i]) > 4.01f) ok = false; }
        CHECK (ok, "20 notes, odd block sizes: finite and bounded");
    }

    // 6. real files given on the command line
    for (int a = 1; a < argc; ++a)
    {
        std::ifstream f (argv[a], std::ios::binary); std::vector<uint8_t> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        ls::WavData wd; const bool ok = ls::parseWav (b.data(), b.size(), wd); int nf = 0; std::vector<float> fr; if (ok) fr = ls::framesFromAudio (wd.mono, wd.frameSize, nf);
        std::printf ("file %s: parsed=%d fs=%d frames=%d\n", argv[a], (int) ok, wd.frameSize, nf);
        if (! ok || nf < 1) { ++fails; continue; }
        ls::Synth s; s.prepare (48000); s.bank().setCustom (1, ls::buildTableSet (fr.data(), nf, "file"));
        ls::P p; p.a.table = 5; p.a.pos = 0.5f; p.a.uni = 3; p.dist = 0.3f; auto o = renderNote (s, p, 40, 0.5);
        CHECK (finite (o, 4.0f) && rms (o, 4800, 20000) > 0.02, "real table renders audible, finite sound");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
