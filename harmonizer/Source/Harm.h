// Loop Key Harmonizer engine: snaps the notes of any sound (also chords and full mixes) to a scale.
// Phase vocoder: peaks of the spectrum are found, each peak and the region around it is moved to the nearest note
// of the scale, and the sound is rebuilt.  Same algorithm as the Key Harmonizer insert in Loop Studio.
// Plain C++17, no JUCE.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lh
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxCh = 2;

inline const char* const* keyNames() { static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n; }

struct ScaleDef { const char* name; const char* shortName; std::vector<int> iv; };
// same list and order as the Loop Studio insert ("Custom" is the last entry)
inline const std::vector<ScaleDef>& scales()
{
    static const std::vector<ScaleDef> s = {
        { "Minor pentatonic", "Min pent", { 0, 3, 5, 7, 10 } },
        { "Major pentatonic", "Maj pent", { 0, 2, 4, 7, 9 } },
        { "Natural minor", "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Major", "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Phrygian", "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Phrygian dominant", "Phryg dom", { 0, 1, 4, 5, 7, 8, 10 } },
        { "Harmonic minor", "Harm min", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Dorian", "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Blues", "Blues", { 0, 3, 5, 6, 7, 10 } },
        { "Whole tone", "Whole", { 0, 2, 4, 6, 8, 10 } },
        { "Root + fifth", "Root+5", { 0, 7 } },
        { "Chromatic", "Chroma", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Custom", "Custom", {} } };
    return s;
}
inline int customScaleIndex() { return (int) scales().size() - 1; }

struct Params
{
    std::array<bool, 12> mask {};   // allowed pitch classes (absolute, C = 0)
    int root = 0;
    float strength = 1.0f;          // 0..1
    float lowCut = 150.0f;          // Hz: below this the sound stays untouched
    float mix = 1.0f;
    float outGain = 1.0f;           // linear
};

// pitch classes of scale `scale` in key `root`; `custom` = intervals above the root used when scale is "Custom"
inline std::array<bool, 12> makeMask (int scale, int root, const std::array<bool, 12>& custom)
{
    std::array<bool, 12> m {};
    const auto& sc = scales();
    scale = std::clamp (scale, 0, (int) sc.size() - 1);
    if (scale == customScaleIndex()) { bool any = false; for (int i = 0; i < 12; ++i) if (custom[(size_t) i]) { m[(size_t) ((i + root) % 12)] = true; any = true; } if (! any) m[(size_t) root] = true; }
    else for (int i : sc[(size_t) scale].iv) m[(size_t) ((i + root) % 12)] = true;
    return m;
}

class Fft
{
public:
    void setup (int n)
    {
        N = n; rev.assign ((size_t) n, 0); int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = (uint32_t) r; }
        cs.resize ((size_t) n / 2); sn.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k) { cs[(size_t) k] = std::cos (2 * kPi * k / n); sn[(size_t) k] = std::sin (2 * kPi * k / n); }
    }
    void run (double* re, double* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i) { const int j = (int) rev[(size_t) i]; if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
        for (int size = 2; size <= N; size <<= 1)
        {
            const int half = size >> 1, step = N / size;
            for (int i = 0; i < N; i += size)
                for (int j = 0, k = 0; j < half; ++j, k += step)
                {
                    const double wr = cs[(size_t) k], wi = inverse ? sn[(size_t) k] : -sn[(size_t) k];
                    const int a = i + j, b = a + half;
                    const double tr = wr * re[b] - wi * im[b], ti = wr * im[b] + wi * re[b];
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
        }
    }
    int size() const { return N; }
private:
    int N = 0; std::vector<uint32_t> rev; std::vector<double> cs, sn;
};

class Engine
{
public:
    // N: 1024 (fast, about 21 ms) or 2048 (high quality, about 43 ms at 48 kHz). Latency in samples = N.
    void prepare (double sampleRate, int fftSize)
    {
        sr = sampleRate; N = fftSize; H = N >> 2; N2 = N >> 1; lat = N - H;
        fft.setup (N);
        win.resize ((size_t) N); for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * kPi * i / N);
        fr.assign ((size_t) N, 0); fi.assign ((size_t) N, 0);
        for (auto* v : { &mag, &fq, &ph, &rat, &sM, &sF, &sT }) v->assign ((size_t) N2 + 1, 0);
        pk.assign ((size_t) N2 + 1, 0);
        for (auto& c : ch) { c.inF.assign ((size_t) N, 0); c.outF.assign ((size_t) H, 0); c.acc.assign ((size_t) N, 0); c.lp.assign ((size_t) N2 + 1, 0); c.sp.assign ((size_t) N2 + 1, 0); c.dry.assign ((size_t) N, 0); c.dp = 0; c.rv = lat; }
    }
    void reset() { prepare (sr, N); }
    int latency() const { return N; }
    int lastPeaks() const { return nPeaks; }

    void process (float* const* io, int nch, int n, const Params& prm)
    {
        P = prm;
        const double g = P.outGain, mix = P.mix;
        for (int c = 0; c < nch && c < kMaxCh; ++c)
        {
            auto& s = ch[(size_t) c]; float* y = io[c];
            for (int i = 0; i < n; ++i)
            {
                const double xi = y[i];
                s.inF[(size_t) s.rv] = xi;
                const double wet = s.outF[(size_t) (s.rv - lat)], dry = s.dry[(size_t) s.dp];
                s.dry[(size_t) s.dp] = xi; s.dp = s.dp + 1 >= N ? 0 : s.dp + 1;
                y[i] = (float) ((dry * (1 - mix) + wet * mix) * g);
                if (++s.rv >= N) { s.rv = lat; frame (s); }
            }
        }
    }

private:
    struct Chan { std::vector<double> inF, outF, acc, lp, sp, dry; int dp = 0, rv = 0; };

    double snapRatio (double f) const
    {
        const double midi = 69 + 12 * std::log2 (f / 440), m0 = std::round (midi);
        double best = 1e9, tm = m0;
        for (int d = -6; d <= 6; ++d)
        {
            const int m = (int) m0 + d;
            if (P.mask[(size_t) (((m % 12) + 12) % 12)]) { const double dd = std::fabs (m - midi); if (dd < best) { best = dd; tm = m; } }
        }
        return std::pow (2.0, (tm - midi) * P.strength / 12);
    }

    void frame (Chan& c)
    {
        const double fpb = sr / N, expct = 2 * kPi * H / N, osamp = 4;
        const int cutBin = std::max (1, (int) std::lround (P.lowCut / fpb)), topBin = std::min (N2 - 1, (int) std::floor (sr * 0.45 / fpb));
        double* re = fr.data(); double* im = fi.data();
        for (int i = 0; i < N; ++i) { re[i] = c.inF[(size_t) i] * win[(size_t) i]; im[i] = 0; }
        fft.run (re, im, false);
        double mx = 0;
        for (int k = 0; k <= N2; ++k)
        {
            const double a = re[k], b = im[k], m = std::sqrt (a * a + b * b), p = std::atan2 (b, a);
            double t = p - c.lp[(size_t) k]; c.lp[(size_t) k] = p; t -= k * expct; t -= 2 * kPi * std::round (t / (2 * kPi)); t = osamp * t / (2 * kPi);
            mag[(size_t) k] = m; ph[(size_t) k] = p; fq[(size_t) k] = (k + t) * fpb; if (m > mx) mx = m;
        }
        int np = 0; const double thr = mx * 0.002 + 1e-9;
        for (int k = 2; k < N2 - 1; ++k)
        {
            const double m = mag[(size_t) k];
            if (m > thr && m > mag[(size_t) k - 1] && m >= mag[(size_t) k + 1] && m > mag[(size_t) k - 2] && m >= mag[(size_t) k + 2]) pk[(size_t) np++] = k;
        }
        nPeaks = np;
        if (! np) std::fill (rat.begin(), rat.end(), 1.0);
        else
        {
            int start = 0;
            for (int i = 0; i < np; ++i)
            {
                int end = N2 + 1;
                if (i < np - 1) { double mn = 1e30; int mk = pk[(size_t) i]; for (int k = pk[(size_t) i]; k <= pk[(size_t) i + 1]; ++k) if (mag[(size_t) k] < mn) { mn = mag[(size_t) k]; mk = k; } end = mk + 1; }
                const double f = fq[(size_t) pk[(size_t) i]];
                const double r = (f < P.lowCut || f > sr * 0.45 || P.strength <= 0) ? 1.0 : snapRatio (f);
                for (int k = start; k < end; ++k) rat[(size_t) k] = r;
                start = end;
            }
        }
        std::fill (sM.begin(), sM.end(), 0.0); std::fill (sF.begin(), sF.end(), 0.0); std::fill (sT.begin(), sT.end(), 0.0);
        for (int k = cutBin; k <= topBin; ++k)
        {
            const int d = (int) std::lround (k * rat[(size_t) k]);
            if (d < cutBin || d > topBin) continue;
            sM[(size_t) d] += mag[(size_t) k];
            if (mag[(size_t) k] >= sT[(size_t) d]) { sT[(size_t) d] = mag[(size_t) k]; sF[(size_t) d] = fq[(size_t) k] * rat[(size_t) k]; }
        }
        for (int k = 0; k <= N2; ++k)
        {
            if (k < cutBin || k > topBin) { c.sp[(size_t) k] = ph[(size_t) k]; continue; }
            if (sM[(size_t) k] > 0)
            {
                double t = (sF[(size_t) k] - k * fpb) / fpb; t = 2 * kPi * t / osamp + k * expct;
                c.sp[(size_t) k] += t; const double a = c.sp[(size_t) k];
                re[k] = sM[(size_t) k] * std::cos (a); im[k] = sM[(size_t) k] * std::sin (a);
            }
            else { c.sp[(size_t) k] += k * expct; re[k] = 0; im[k] = 0; }
        }
        for (int k = 0; k < cutBin; ++k) { const double m = mag[(size_t) k], a = ph[(size_t) k]; re[k] = m * std::cos (a); im[k] = m * std::sin (a); }
        for (int k = topBin + 1; k <= N2; ++k) { const double m = mag[(size_t) k], a = ph[(size_t) k]; re[k] = m * std::cos (a); im[k] = m * std::sin (a); }
        im[0] = 0; im[N2] = 0;
        for (int k = 1; k < N2; ++k) { re[N - k] = re[k]; im[N - k] = -im[k]; }
        fft.run (re, im, true);
        const double sc = 1.0 / (N * 1.5);
        for (int i = 0; i < N; ++i) c.acc[(size_t) i] += re[i] * win[(size_t) i] * sc;
        for (int i = 0; i < H; ++i) c.outF[(size_t) i] = c.acc[(size_t) i];
        std::copy (c.acc.begin() + H, c.acc.end(), c.acc.begin());
        std::fill (c.acc.begin() + (N - H), c.acc.end(), 0.0);
        std::copy (c.inF.begin() + H, c.inF.end(), c.inF.begin());
    }

    double sr = 48000; int N = 1024, H = 256, N2 = 512, lat = 768, nPeaks = 0;
    Params P;
    Fft fft;
    std::vector<double> win, fr, fi, mag, fq, ph, rat, sM, sF, sT;
    std::vector<int> pk;
    Chan ch[kMaxCh];
};
}
