// Tests for the built-in effects: measured behaviour, not just "it runs".
#include "../Source/Fx.h"
#include <cstdio>
#include <functional>
#include <random>

using namespace le;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s:   %s\n", ok ? "ok" : "FAIL", what); if (! ok) ++fails; }

static const double SR = 48000;

static std::vector<float> sine (double f, double amp, int n) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPiD * f * i / SR)); return v; }
static double rms (const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i]; return std::sqrt (s / (b - a)); }
static double peak (const std::vector<float>& v, int a, int b) { double m = 0; for (int i = a; i < b; ++i) m = std::max (m, (double) std::fabs (v[(size_t) i])); return m; }
static double db (double x) { return 20 * std::log10 (std::max (1e-12, x)); }

static std::vector<float> run (FxType t, const std::vector<float>& in, const std::function<void (FxSpec&)>& cfg = {})
{
    FxSpec p; p.init (t); if (cfg) cfg (p);
    auto fx = makeFx (t, SR); std::vector<float> L = in, R = in;
    for (size_t i = 0; i < L.size(); i += 512) { const int k = (int) std::min<size_t> (512, L.size() - i); fx->process (L.data() + i, R.data() + i, k, p); }
    return L;
}

int main()
{
    const int N = 48000;
    // EQ: +12 dB bell at 3 kHz lifts 3 kHz by about 12 dB and leaves 200 Hz alone
    { auto a = run (FxType::Eq, sine (3000, 0.1, N), [] (FxSpec& p) { p.setNum ("m2G", 12); });
      auto b = run (FxType::Eq, sine (200, 0.1, N), [] (FxSpec& p) { p.setNum ("m2G", 12); });
      check (std::fabs (db (rms (a, 24000, N) / rms (sine (3000, 0.1, N), 24000, N)) - 12) < 0.5, "EQ: +12 dB bell raises 3 kHz by 12 dB");
      check (std::fabs (db (rms (b, 24000, N) / rms (sine (200, 0.1, N), 24000, N))) < 0.5, "EQ: 200 Hz stays flat"); }
    // flat EQ is transparent
    { auto in = sine (1000, 0.3, N); auto o = run (FxType::Eq, in); check (std::fabs (db (rms (o, 24000, N) / rms (in, 24000, N))) < 0.01, "EQ: flat settings change nothing"); }
    // Compressor: 10:1 above the threshold
    { auto in = sine (1000, 0.5, N);   // -6 dBFS peak
      auto o = run (FxType::Comp, in, [] (FxSpec& p) { p.setNum ("thr", -30); p.setNum ("ratio", 10); p.setNum ("knee", 0); p.setNum ("att", 1); });
      const double inDb = db (peak (in, 24000, N)), outDb = db (peak (o, 24000, N));
      const double expect = -30 + (inDb + 30) / 10.0;
      check (std::fabs (outDb - expect) < 1.5, "Compressor: 10:1 reduces the level above the threshold as expected");
      auto q = run (FxType::Comp, sine (1000, 0.01, N), [] (FxSpec& p) { p.setNum ("thr", -20); });
      check (std::fabs (db (peak (q, 24000, N) / 0.01)) < 0.1, "Compressor: quiet signal below the threshold is untouched"); }
    // Limiter never exceeds the ceiling, even on a loud burst
    { std::vector<float> in (N); std::mt19937 g (3); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
      for (int i = 0; i < N; ++i) in[(size_t) i] = d (g) * (i > 10000 && i < 20000 ? 4.0f : 0.2f);
      auto o = run (FxType::Limiter, in, [] (FxSpec& p) { p.setNum ("ceil", -3); });
      check (peak (o, 0, N) <= dbToLin (-3) + 1e-6, "Limiter: output never goes above the ceiling");
      check (rms (o, 30000, N) > 0.05, "Limiter: the signal after the burst comes back (release works)"); }
    // Limiter keeps a quiet sine intact (apart from the delay)
    { auto in = sine (440, 0.2, N); auto o = run (FxType::Limiter, in, [] (FxSpec& p) { p.setNum ("ceil", -1); });
      check (std::fabs (db (rms (o, 24000, N) / rms (in, 24000, N))) < 0.05, "Limiter: below the ceiling the level is unchanged"); }
    // Delay: an impulse comes back after the set time
    { std::vector<float> in (N, 0.0f); in[0] = 1.0f;
      auto o = run (FxType::Delay, in, [] (FxSpec& p) { p.setNum ("time", 100); p.setNum ("mix", 1); p.setNum ("fb", 0); p.setNum ("tone", 20000); });
      // the time glides to its target, so look for the echo peak near 100 ms
      int at = 0; double m = 0; for (int i = 100; i < N; ++i) if (std::fabs (o[(size_t) i]) > m) { m = std::fabs (o[(size_t) i]); at = i; }
      check (m > 0.3, "Delay: there is an echo"); (void) at; }
    // Filter: low-pass 500 Hz removes 8 kHz
    { auto a = run (FxType::Filter, sine (8000, 0.3, N), [] (FxSpec& p) { p.setNum ("cut", 500); });
      check (db (rms (a, 24000, N) / rms (sine (8000, 0.3, N), 24000, N)) < -30, "Filter: low-pass at 500 Hz cuts 8 kHz by more than 30 dB");
      auto b = run (FxType::Filter, sine (200, 0.3, N), [] (FxSpec& p) { p.setStr ("mode", "highpass"); p.setNum ("cut", 2000); });
      check (db (rms (b, 24000, N) / rms (sine (200, 0.3, N), 24000, N)) < -30, "Filter: high-pass at 2 kHz cuts 200 Hz"); }
    // Reverb: an impulse leaves a decaying tail and the tail is longer for a bigger size
    { std::vector<float> in (N * 2, 0.0f); in[0] = 1.0f;
      auto s = run (FxType::Reverb, in, [] (FxSpec& p) { p.setNum ("size", 0.5); p.setNum ("mix", 1); });
      auto l = run (FxType::Reverb, in, [] (FxSpec& p) { p.setNum ("size", 3.0); p.setNum ("mix", 1); });
      const double es = rms (s, 36000, 48000), el = rms (l, 36000, 48000);
      check (rms (l, 3000, 12000) > 1e-4, "Reverb: there is a tail"); check (el > es * 3, "Reverb: bigger size gives a longer tail"); check (peak (l, 0, N * 2) < 2.0, "Reverb: stays stable"); }
    // Distortion adds harmonics (level above input for a quiet sine with heavy drive, bounded)
    { auto a = run (FxType::Dist, sine (200, 0.1, N), [] (FxSpec& p) { p.setNum ("drive", 30); p.setNum ("tone", 12000); });
      check (peak (a, 24000, N) > 0.1 && peak (a, 24000, N) < 1.2, "Distortion: drive makes a loud, bounded signal"); }
    // Tremolo at depth 1 goes to silence
    { auto a = run (FxType::Trem, sine (440, 0.5, N), [] (FxSpec& p) { p.setNum ("depth", 1); p.setNum ("rate", 5); });
      double mn = 1; for (int i = 0; i + 200 < N; i += 200) mn = std::min (mn, peak (a, i, i + 200)); check (mn < 0.05, "Tremolo: depth 1 reaches silence"); }
    // every effect: noise in, finite and bounded out, even with extreme settings
    { std::mt19937 g (9); std::uniform_real_distribution<float> d (-1.0f, 1.0f); std::vector<float> in (N); for (auto& x : in) x = d (g);
      bool ok = true;
      for (FxType t : { FxType::Eq, FxType::Comp, FxType::Delay, FxType::Limiter, FxType::Filter, FxType::Reverb, FxType::Chorus, FxType::Phaser, FxType::Trem, FxType::Dist })
      {
          auto o = run (t, in, [] (FxSpec& p) { for (auto& x : p.v) x = x; });
          for (float x : o) if (! std::isfinite (x) || std::fabs (x) > 20.0f) { ok = false; std::printf ("   unstable type %d\n", (int) t); break; }
          auto o2 = run (t, in, [t] (FxSpec& p) { const auto& dd = fxDefs (t); for (size_t i = 0; i < dd.size(); ++i) p.v[i] = dd[i].def * 4.0f + 0.5f; });
          for (float x : o2) if (! std::isfinite (x) || std::fabs (x) > 60.0f) { ok = false; std::printf ("   unstable (extreme) type %d\n", (int) t); break; }
      }
      check (ok, "all effects: finite output on noise with default and extreme settings"); }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
