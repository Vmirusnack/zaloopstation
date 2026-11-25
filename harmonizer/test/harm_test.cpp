// g++ -O2 -std=c++17 harm_test.cpp -o harm_test && ./harm_test
#include "../Source/Harm.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++fails; } else std::printf("ok:   %s\n", msg); } while (0)

// dominant frequency by FFT of the last 8192 samples
static double dominant (const std::vector<float>& x, double sr)
{
    const int n = 8192; lh::Fft f; f.setup (n); std::vector<double> re (n), im (n, 0);
    for (int i = 0; i < n; ++i) re[(size_t) i] = x[x.size() - n + (size_t) i] * (0.5 - 0.5 * std::cos (2 * lh::kPi * i / n));
    f.run (re.data(), im.data(), false);
    int best = 1; double bm = 0; for (int k = 1; k < n / 2; ++k) { const double m = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]; if (m > bm) { bm = m; best = k; } }
    return best * sr / n;
}
static std::vector<float> run (double sr, int N, double freq, const lh::Params& p, double secs = 2.0)
{
    lh::Engine e; e.prepare (sr, N);
    const int total = (int) (secs * sr); std::vector<float> x ((size_t) total), y;
    for (int i = 0; i < total; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2 * lh::kPi * freq * i / sr));
    for (int i = 0; i < total; i += 256) { const int k = std::min (256, total - i); float* io[1] = { x.data() + i }; e.process (io, 1, k, p); }
    return x;
}
int main()
{
    const double sr = 48000;
    lh::Params p; p.mask = lh::makeMask (0, 9, {}); p.root = 9;   // A minor pentatonic: A C D E G
    // 466 Hz is A#4 (466.16): nearest scale notes A4 (440) / C5 (523): A#4 is closer to A4
    auto y = run (sr, 1024, 466.16, p); const double d = dominant (y, sr);
    std::printf ("      A#4 in -> %.1f Hz out\n", d);
    CHECK (std::fabs (d - 440.0) < 6.0, "A#4 is snapped to A4 (A minor pentatonic)");
    auto y2 = run (sr, 2048, 440.0, p); const double d2 = dominant (y2, sr);
    CHECK (std::fabs (d2 - 440.0) < 3.0, "a note already in the scale stays put (2048)");
    lh::Params off = p; off.strength = 0;
    auto y3 = run (sr, 1024, 466.16, off); CHECK (std::fabs (dominant (y3, sr) - 466.16) < 4.0, "strength 0 leaves pitch unchanged");
    lh::Params dry = p; dry.mix = 0; auto y4 = run (sr, 1024, 466.16, dry); CHECK (std::fabs (dominant (y4, sr) - 466.16) < 4.0, "mix 0 passes the dry sound");
    double pk = 0; for (size_t i = 24000; i < y.size(); ++i) pk = std::max (pk, (double) std::fabs (y[i]));
    CHECK (pk > 0.2 && pk < 0.9, "output level is sensible");
    // delay of the dry path must equal reported latency so mix 0..1 is phase aligned
    lh::Engine e; e.prepare (sr, 1024); CHECK (e.latency() == 1024, "latency equals fft size");
    lh::Params cm; std::array<bool, 12> cust {}; cust[0] = cust[7] = true; cm.mask = lh::makeMask (lh::customScaleIndex(), 2, cust); cm.root = 2;
    CHECK (cm.mask[2] && cm.mask[9] && !cm.mask[0], "custom scale (root + fifth) in D");
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
