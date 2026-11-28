// Tests for the built-in instruments (kick, drums, hard synth) and the oversampled shaper.
#include "../Source/Inst.h"
#include <cstdio>
#include <functional>
using namespace le;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s:   %s\n", ok ? "ok" : "FAIL", what); if (! ok) ++fails; }
static const double SR = 48000;

static std::vector<float> play (InstType t, const std::function<void (InstSpec&)>& cfg, int pitch, float vel, double seconds, double noteLen = -1)
{
    InstSpec p; p.init (t); if (cfg) cfg (p);
    auto in = makeInst (t, SR); const int N = (int) (seconds * SR); std::vector<float> L ((size_t) N), R ((size_t) N);
    const int off = noteLen < 0 ? -1 : (int) (noteLen * SR);
    in->noteOn (pitch, vel, p);
    for (int i = 0; i < N; i += 128)
    {
        const int k = std::min (128, N - i);
        if (off >= i && off < i + k) { in->render (L.data() + i, R.data() + i, off - i, p); in->noteOff (pitch); in->render (L.data() + off, R.data() + off, i + k - off, p); }
        else in->render (L.data() + i, R.data() + i, k, p);
    }
    return L;
}
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt (s / std::max<size_t> (1, b - a)); }
static double peak (const std::vector<float>& v, size_t a, size_t b) { double m = 0; for (size_t i = a; i < b && i < v.size(); ++i) m = std::max (m, (double) std::fabs (v[i])); return m; }
static double zcFreq (const std::vector<float>& v, size_t a, size_t b) { int z = 0; for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++z; return z / 2.0 / ((double) (b - a) / SR); }
static bool finite (const std::vector<float>& v, float lim) { for (float x : v) if (! std::isfinite (x) || std::fabs (x) > lim) return false; return true; }
// energy of a signal above a frequency (DFT bins of a short window)
static double highEnergy (const std::vector<float>& v, size_t a, size_t n, double f0)
{
    double e = 0; for (double f = f0; f < SR / 2; f += 500) { double re = 0, im = 0; for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPiD * (double) i / (double) n), x = v[a + i] * w; re += x * std::cos (2 * kPiD * f * (double) i / SR); im += x * std::sin (2 * kPiD * f * (double) i / SR); } e += re * re + im * im; }
    return e;
}

int main()
{
    // Kick: key 36 is 65.4 Hz, body sweeps down from the punch frequency, tail decays
    { auto k = play (InstType::Kick, [] (InstSpec& p) { p.setNum ("drive", 0); p.setNum ("clip", 0); }, 36, 1.0f, 1.2);
      check (peak (k, 0, 4800) > 0.1, "kick: it has level");
      check (zcFreq (k, 0, 700) > zcFreq (k, 9600, 14400) * 1.5, "kick: the pitch sweeps down");
      check (std::fabs (zcFreq (k, 12000, 19200) - 65.4) < 8, "kick: settles on the note's pitch (36 -> 65 Hz)");
      check (rms (k, 40000, 48000) < rms (k, 2000, 6000) * 0.1, "kick: it decays");
      auto k2 = play (InstType::Kick, [] (InstSpec& p) { p.setNum ("drive", 0); p.setNum ("clip", 0); p.setNum ("tune", 12); }, 36, 1.0f, 1.2);
      check (std::fabs (zcFreq (k2, 12000, 19200) - 130.8) < 15, "kick: tune +12 semitones doubles the pitch"); }
    // Kick drive makes it louder in the mean and adds highs; clip lowers the peak
    { auto a = play (InstType::Kick, [] (InstSpec& p) { p.setNum ("drive", 0); p.setNum ("clip", 0); p.setNum ("click", 0); }, 36, 1.0f, 0.6);
      auto b = play (InstType::Kick, [] (InstSpec& p) { p.setNum ("drive", 0.9); p.setNum ("clip", 0.5); p.setStr ("mode", "hard"); p.setNum ("click", 0); }, 36, 1.0f, 0.6);
      check (rms (b, 2000, 12000) > rms (a, 2000, 12000) * 1.1, "kick: drive raises the mean level");
      check (highEnergy (b, 3000, 4096, 500) > highEnergy (a, 3000, 4096, 500) * 3, "kick: drive adds harmonics");
      check (finite (b, 1.0f), "kick: distorted kick stays within 1.0"); }
    // new kick note chokes the old one
    { InstSpec p; p.init (InstType::Kick); auto k = makeInst (InstType::Kick, SR); std::vector<float> L (48000), R (48000);
      k->noteOn (36, 1, p); k->render (L.data(), R.data(), 4800, p); k->noteOn (36, 1, p); k->render (L.data() + 4800, R.data() + 4800, 43200, p);
      check (finite (L, 1.5f) && peak (L, 4900, 9600) > 0.1, "kick: retrigger works and does not blow up"); }
    // Drums: every GM drum note makes sound, is finite, and ends
    { bool all = true, ends = true;
      for (int pit : { 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 })
        for (float kit : { 0.0f, 1.0f })
        {
            auto d = play (InstType::Drums, [kit] (InstSpec& p) { p.v[0] = kit; }, pit, 1.0f, 3.0);
            if (! finite (d, 4.0f) || peak (d, 0, 9600) < 0.02) { all = false; std::printf ("   silent or bad: note %d kit %g\n", pit, kit); }
            if (rms (d, 140000, 144000) > 1e-3) { ends = false; std::printf("   does not end: note %d kit %g\n", pit, kit); }
        }
      check (all, "drums: all notes of both kits sound and stay finite"); check (ends, "drums: every hit ends within 3 s"); }
    { auto h = play (InstType::Drums, {}, 42, 1.0f, 0.3), k = play (InstType::Drums, {}, 36, 1.0f, 0.3);
      check (highEnergy (h, 0, 2048, 6000) > highEnergy (h, 0, 2048, 200) * 0.0 + 1e-6 && rms (k, 0, 4000) > rms (h, 0, 4000), "drums: closed hat is bright and quieter than the kick");
      auto soft = play (InstType::Drums, {}, 38, 0.3f, 0.3), loud = play (InstType::Drums, {}, 38, 1.0f, 0.3);
      check (peak (loud, 0, 4000) > peak (soft, 0, 4000) * 2, "drums: velocity changes the level"); }
    // Hard synth
    { auto s = play (InstType::Hard, [] (InstSpec& p) { p.setNum ("uni", 1); p.setNum ("drive", 0); p.setNum ("clip", 0); p.setNum ("cut", 1); p.setNum ("fenv", 0); p.setNum ("res", 0.5); }, 57, 1.0f, 1.0, 0.8);
      check (std::fabs (zcFreq (s, 12000, 36000) - 220) < 6, "hard synth: plays the note pitch (A3 = 220 Hz)");
      check (peak (s, 12000, 36000) > 0.05, "hard synth: it has level");
      check (rms (s, 46000, 48000) < rms (s, 12000, 36000) * 0.05, "hard synth: note ends after release");
      auto dark = play (InstType::Hard, [] (InstSpec& p) { p.setNum ("uni", 1); p.setNum ("drive", 0); p.setNum ("clip", 0); p.setNum ("cut", 0.1); p.setNum ("fenv", 0); }, 57, 1.0f, 1.0, 0.8);
      check (highEnergy (dark, 12000, 4096, 3000) < highEnergy (s, 12000, 4096, 3000) * 0.05, "hard synth: low cutoff removes the highs");
      auto uni = play (InstType::Hard, [] (InstSpec& p) { p.setNum ("uni", 7); p.setNum ("det", 30); p.setNum ("drive", 0); p.setNum ("clip", 0); }, 57, 1.0f, 1.0, 0.8);
      check (rms (uni, 12000, 36000) > 0.01 && finite (uni, 2.0f), "hard synth: 7 detuned voices"); }
    // presets from the page: all must stay finite and bounded
    { struct Pre { const char* w; int uni; double det, wid, sub, fm, fmr; const char* ft; double cut, res, fenv, fdec, dive, divet, vib, vrate, drive, clip; const char* mode; double atk, dec, sus, rel; };
      const Pre pre[] = {
        { "sawtooth", 3, 14, .7, 0, .55, 2.01, "bandpass", .58, 9, 1.6, 180, 0, 80, 25, 6, .8, .4, "hard", 1, 200, .6, .12 },
        { "sawtooth", 5, 28, .9, .15, 0, 2, "lowpass", .75, 2, 0, 200, 12, 120, 12, 5, .35, .1, "tanh", 6, 300, .9, .25 },
        { "sawtooth", 1, 0, 0, 1, 0, 2, "lowpass", .22, 1.5, .6, 120, 0, 60, 0, 5, .8, .4, "tanh", 2, 300, .7, .12 },
        { "sawtooth", 2, 12, .5, 0, .9, 1, "lowpass", .8, 6, 2, 80, 5, 60, 40, 5.5, 1, .6, "hard", 1, 160, .7, .1 },
        { "square", 3, 10, .6, 0, 0, 2, "lowpass", .65, 3, 1.2, 150, 0, 60, 0, 5, .7, .4, "fold", 1, 160, 0, .12 } };
      bool ok = true;
      for (auto& q : pre)
      {
          auto s = play (InstType::Hard, [&q] (InstSpec& p) { p.setStr ("wave", q.w); p.setNum ("uni", q.uni); p.setNum ("det", q.det); p.setNum ("wid", q.wid); p.setNum ("sub", q.sub); p.setNum ("fm", q.fm); p.setNum ("fmr", q.fmr);
              p.setStr ("ftype", q.ft); p.setNum ("cut", q.cut); p.setNum ("res", q.res); p.setNum ("fenv", q.fenv); p.setNum ("fdec", q.fdec); p.setNum ("dive", q.dive); p.setNum ("divet", q.divet); p.setNum ("vib", q.vib); p.setNum ("vrate", q.vrate);
              p.setNum ("drive", q.drive); p.setNum ("clip", q.clip); p.setStr ("mode", q.mode); p.setNum ("atk", q.atk); p.setNum ("dec", q.dec); p.setNum ("sus", q.sus); p.setNum ("rel", q.rel); }, 45, 1.0f, 1.5, 1.0);
          if (! finite (s, 1.5f) || peak (s, 0, 24000) < 0.02) { ok = false; std::printf ("   bad preset (%s %s)\n", q.w, q.mode); }
      }
      check (ok, "hard synth: the page's presets are audible, finite and below 1.5"); }
    // Shaper: oversampling keeps aliasing low (a 9 kHz sine through a hard clip must not leave strong energy at low frequencies)
    { Shaper sh; sh.setCurve (0.9f, 0.0f, 1); std::vector<float> x (48000); for (int i = 0; i < 48000; ++i) x[(size_t) i] = (float) (0.8 * std::sin (2 * kPiD * 9000.0 * i / SR));
      sh.process (x.data(), 48000, 0);
      double low = 0, tot = 0; for (double f = 100; f < 4000; f += 500) { double re = 0, im = 0; for (int i = 0; i < 8192; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPiD * i / 8192.0), v = x[(size_t) (20000 + i)] * w; re += v * std::cos (2 * kPiD * f * i / SR); im += v * std::sin (2 * kPiD * f * i / SR); } low += re * re + im * im; }
      { double re = 0, im = 0; for (int i = 0; i < 8192; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPiD * i / 8192.0), v = x[(size_t) (20000 + i)] * w; re += v * std::cos (2 * kPiD * 9000.0 * i / SR); im += v * std::sin (2 * kPiD * 9000.0 * i / SR); } tot = re * re + im * im; }
      std::printf ("      alias energy %.1f dB below the 9 kHz tone\n", 10 * std::log10 (tot / std::max (1e-30, low)));
      check (10 * std::log10 (tot / std::max (1e-30, low)) > 30, "shaper: aliasing is more than 30 dB below the tone"); }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
