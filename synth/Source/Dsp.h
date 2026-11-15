// Loop Synth: sound engine. Plain C++ (no JUCE), so it can be tested on its own.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace ls
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kTable = 2048;   // samples per table
constexpr int kFrames = 16;    // wavetable positions
constexpr int kMips = 10;      // band-limited copies (one per octave)
constexpr int kTopHarm = 512;  // harmonics in the richest copy
constexpr int kSets = 4;
constexpr int kMaxUni = 7;
constexpr int kVoices = 16;

inline double pow2 (double x) { return std::exp2 (x); }

// ---------------------------------------------------------------- wavetables
struct Tables
{
    std::vector<float> data;

    Tables() { build(); }

    const float* at (int set, int frame, int mip) const
    {
        return &data[((((size_t) set * kFrames + frame) * kMips + mip) * (kTable + 1))];
    }

    static const char* setName (int s)
    {
        static const char* n[kSets] = { "Saw - Square", "Sine - Saw", "Zaag Formant", "Digital" };
        return n[s];
    }

    // amplitude of harmonic h (sine phase) for table set `set` at morph position t (0..1)
    static double harm (int set, double t, int h)
    {
        const double hh = (double) h;
        switch (set)
        {
            case 0: return (1.0 / hh) * ((h & 1) ? 1.0 : (1.0 - t));
            case 1: return std::pow (1.0 / hh, 1.0 + (1.0 - t) * 3.5);
            case 2:
            {
                const double hc = 2.0 + 20.0 * t, w = 1.5 + 3.0 * t;
                const double bump = std::exp (-((hh - hc) * (hh - hc)) / (2.0 * w * w));
                return (1.0 / hh) * (1.0 + 5.0 * t * bump);
            }
            default:
            {
                const double m = std::fabs (std::sin (hh * 1.7 + t * kPi * (1.0 + (h % 3))));
                return std::pow (1.0 / hh, 0.7) * (0.25 + 0.75 * m);
            }
        }
    }

private:
    void build()
    {
        data.assign ((size_t) kSets * kFrames * kMips * (kTable + 1), 0.0f);
        std::vector<float> sinT (kTable);
        for (int i = 0; i < kTable; ++i) sinT[(size_t) i] = (float) std::sin (2.0 * kPi * i / kTable);
        std::vector<double> tmp (kTable);

        for (int s = 0; s < kSets; ++s)
            for (int f = 0; f < kFrames; ++f)
            {
                const double t = f / (double) (kFrames - 1);
                double scale = 1.0;
                for (int m = 0; m < kMips; ++m)
                {
                    const int maxH = std::max (1, kTopHarm >> m);
                    std::fill (tmp.begin(), tmp.end(), 0.0);
                    for (int h = 1; h <= maxH; ++h)
                    {
                        const double a = harm (s, t, h);
                        if (a == 0.0) continue;
                        for (int i = 0; i < kTable; ++i) tmp[(size_t) i] += a * sinT[(size_t) ((h * i) & (kTable - 1))];
                    }
                    if (m == 0)
                    {
                        double pk = 0;
                        for (double v : tmp) pk = std::max (pk, std::fabs (v));
                        scale = pk > 0 ? 0.9 / pk : 1.0;
                    }
                    float* d = const_cast<float*> (at (s, f, m));
                    for (int i = 0; i < kTable; ++i) d[i] = (float) (tmp[(size_t) i] * scale);
                    d[kTable] = d[0];
                }
            }
    }
};

inline float readTable (const float* t, double ph)
{
    const double x = ph * kTable;
    const int i = (int) x;
    const float f = (float) (x - i);
    return t[i] + (t[i + 1] - t[i]) * f;
}

// ---------------------------------------------------------------- parameters (one snapshot per block)
struct OscP { int table = 0; float pos = 0, fine = 0, level = 1, det = 0.2f, spread = 0.5f; int oct = 0, uni = 1; };

struct P
{
    OscP a, b;
    float sub = 0, rndPhase = 0;
    int ftype = 0;
    float cut = 20000, res = 0, fdrive = 1, fenv = 0, fkey = 0;
    float fa = 0.005f, fd = 0.3f, fs = 0.5f, fr = 0.2f;
    float aa = 0.003f, ad = 0.3f, as = 0.8f, ar = 0.15f;
    float posEnv = 0, glide = 0, bend = 2, master = 0.7f, dist = 0, velSens = 0.6f;
    int dtype = 0;
    bool mono = false;
};

// ---------------------------------------------------------------- envelope
struct EnvC { double atk = 0.01, dec = 0.99, rel = 0.99; };

struct Env
{
    enum { Idle, Atk, Dec, Sus, Rel } st = Idle;
    double v = 0;
    void on() { st = Atk; }
    void off() { if (st != Idle) st = Rel; }
    inline double tick (const EnvC& c, double sus)
    {
        switch (st)
        {
            case Atk: v += c.atk; if (v >= 1.0) { v = 1.0; st = Dec; } break;
            case Dec: v = sus + (v - sus) * c.dec; if (std::fabs (v - sus) < 1e-5) { v = sus; st = Sus; } break;
            case Sus: v = sus; break;
            case Rel: v *= c.rel; if (v < 1e-5) { v = 0; st = Idle; } break;
            default: v = 0; break;
        }
        return v;
    }
};

inline EnvC makeEnvC (double a, double d, double r, double sr)
{
    EnvC c;
    c.atk = 1.0 / std::max (1.0, a * sr);
    c.dec = std::exp (-4.6 / (std::max (0.002, d) * sr));
    c.rel = std::exp (-4.6 / (std::max (0.002, r) * sr));
    return c;
}

// ---------------------------------------------------------------- ladder filter (zero-delay feedback, 4 poles)
struct Ladder
{
    double z[4] = { 0, 0, 0, 0 };

    inline double process (double x, double g, double k, double drive, int type)
    {
        const double G = g / (1.0 + g), a = 1.0 - G;
        const double G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const double S = G3 * a * z[0] + G2 * a * z[1] + G * a * z[2] + a * z[3];
        double u = (x - k * S) / (1.0 + k * G4);
        u = std::tanh (u * drive) / drive;
        double v = G * (u - z[0]);   const double o1 = v + z[0]; z[0] = o1 + v;
        v = G * (o1 - z[1]);         const double o2 = v + z[1]; z[1] = o2 + v;
        v = G * (o2 - z[2]);         const double o3 = v + z[2]; z[2] = o3 + v;
        v = G * (o3 - z[3]);         const double o4 = v + z[3]; z[3] = o4 + v;
        switch (type)
        {
            case 1: return o2;
            case 2: return 2.0 * (o2 - o4);
            case 3: return u - 4.0 * o1 + 6.0 * o2 - 4.0 * o3 + o4;
            default: return o4;
        }
    }
    void reset() { z[0] = z[1] = z[2] = z[3] = 0; }
};

// ---------------------------------------------------------------- voice
struct Voice
{
    bool active = false, gate = false, sus = false;
    int note = 60;
    float vel = 1;
    double pitch = 60, pitchT = 60;
    uint64_t age = 0;
    double phA[kMaxUni] = {}, phB[kMaxUni] = {}, phSub = 0;
    Env amp, fenv;
    Ladder lad[2];
};

struct BlockCtx
{
    EnvC ea, ef;
    double glideC = 1;
    float ratio[2][kMaxUni];
    float gl[2][kMaxUni], gr[2][kMaxUni];
    float uniNorm[2];
    double bendSemis = 0, modw = 0;
};

class Synth
{
public:
    Synth() : tables (std::make_unique<Tables>()) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        allOff();
        smCut = 0; smRes = -1;
    }

    void allOff()
    {
        for (auto& v : voices) { v.active = false; v.gate = false; v.sus = false; v.amp = Env(); v.fenv = Env(); v.lad[0].reset(); v.lad[1].reset(); }
        held.clear();
    }

    void setBend (double norm) { bendNorm = norm; }
    void setMod (double m) { modw = m; }

    void noteOn (int note, float vel, const P& p)
    {
        if (p.mono)
        {
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            held.push_back (note);
            Voice& v = voices[0];
            const bool legato = v.active && v.gate;
            v.note = note; v.vel = vel; v.pitchT = note; v.sus = false;
            if (! legato) { start (v, note, p, v.active && p.glide > 0.001f); }
            return;
        }
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active && x.note == note) { v = &x; break; }
        if (! v) for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v)
        {
            double best = 1e9;
            for (auto& x : voices)
            {
                const double score = (x.gate ? 10.0 : 0.0) + x.amp.v + (double) x.age * 1e-12;
                if (score < best) { best = score; v = &x; }
            }
        }
        start (*v, note, p, false);
        v->vel = vel;
    }

    void noteOff (int note)
    {
        if (voicesMono)
        {
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            Voice& v = voices[0];
            if (! held.empty()) { v.note = held.back(); v.pitchT = v.note; return; }
            if (v.gate) { if (pedal) v.sus = true; else release (v); }
            return;
        }
        for (auto& v : voices)
            if (v.active && v.gate && ! v.sus && v.note == note)
            {
                if (pedal) v.sus = true; else release (v);
            }
    }

    void sustain (bool on)
    {
        pedal = on;
        if (! on) for (auto& v : voices) if (v.sus) { v.sus = false; release (v); }
    }

    void render (float* L, float* R, int n, const P& p)
    {
        voicesMono = p.mono;
        if (smRes < 0) { smCut = std::log (p.cut); smRes = p.res; smVol = p.master; smDrv = p.fdrive; }
        BlockCtx c;
        c.ea = makeEnvC (p.aa, p.ad, p.ar, sr);
        c.ef = makeEnvC (p.fa, p.fd, p.fr, sr);
        c.glideC = p.glide > 0.001f ? 1.0 - std::exp (-1.0 / (p.glide * sr)) : 1.0;
        c.bendSemis = bendNorm * p.bend;
        c.modw = modw;
        const OscP* os[2] = { &p.a, &p.b };
        for (int o = 0; o < 2; ++o)
        {
            const int un = std::clamp (os[o]->uni, 1, kMaxUni);
            c.uniNorm[o] = (float) (1.0 / std::sqrt ((double) un));
            for (int j = 0; j < kMaxUni; ++j)
            {
                const double u = (un > 1 && j < un) ? (2.0 * j / (un - 1) - 1.0) : 0.0;
                c.ratio[o][j] = (float) pow2 (u * os[o]->det * (100.0 / 1200.0));
                const double pan = 0.5 + 0.5 * u * os[o]->spread;
                c.gl[o][j] = (float) std::cos (pan * kPi * 0.5);
                c.gr[o][j] = (float) std::sin (pan * kPi * 0.5);
            }
        }
        if (n > (int) cutA.size()) { cutA.resize ((size_t) n); resA.resize ((size_t) n); drvA.resize ((size_t) n); volA.resize ((size_t) n); }
        const double tc = 1.0 - std::exp (-1.0 / (0.004 * sr));
        const double lc = std::log (std::clamp ((double) p.cut, 20.0, 20000.0));
        for (int i = 0; i < n; ++i)
        {
            smCut += (lc - smCut) * tc; smRes += (p.res - smRes) * tc; smDrv += (p.fdrive - smDrv) * tc; smVol += (p.master - smVol) * tc;
            cutA[(size_t) i] = (float) std::exp (smCut); resA[(size_t) i] = (float) smRes; drvA[(size_t) i] = (float) smDrv; volA[(size_t) i] = (float) smVol;
        }
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        for (auto& v : voices) if (v.active) renderVoice (v, L, R, n, p, c);

        // distortion and master level
        const float d = p.dist;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (d > 0.001f)
            {
                const float g = 1.0f + d * 12.0f;
                switch (p.dtype)
                {
                    case 1: l = std::clamp (l * g, -1.0f, 1.0f) * 0.85f; r = std::clamp (r * g, -1.0f, 1.0f) * 0.85f; break;
                    case 2: l = std::sin (l * g * 1.4f) * 0.9f; r = std::sin (r * g * 1.4f) * 0.9f; break;
                    default: { const float nz = 1.0f / std::tanh (g); l = std::tanh (l * g) * nz * 0.9f; r = std::tanh (r * g) * nz * 0.9f; }
                }
            }
            const float vol = volA[(size_t) i];
            L[i] = std::clamp (l * vol, -4.0f, 4.0f); R[i] = std::clamp (r * vol, -4.0f, 4.0f);
        }
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) if (v.active) ++c; return c; }

private:
    std::unique_ptr<Tables> tables;
    std::array<Voice, kVoices> voices;
    std::vector<int> held;
    std::vector<float> cutA, resA, drvA, volA;
    double sr = 44100, bendNorm = 0, modw = 0, smCut = 0, smRes = -1, smVol = 0.7, smDrv = 1;
    bool pedal = false, voicesMono = false;
    uint64_t counter = 0, rng = 0x9E3779B97F4A7C15ull;

    double rand01() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (double) (rng >> 11) / 9007199254740992.0; }

    void release (Voice& v) { v.gate = false; v.amp.off(); v.fenv.off(); }

    void start (Voice& v, int note, const P& p, bool keepPitch)
    {
        v.note = note; v.pitchT = note; if (! keepPitch) v.pitch = note;
        v.gate = true; v.sus = false; v.active = true; v.age = ++counter;
        for (int j = 0; j < kMaxUni; ++j) { v.phA[j] = p.rndPhase * rand01(); v.phB[j] = p.rndPhase * rand01(); }
        v.phSub = 0;
        v.amp.on(); v.fenv.on();
        v.fenv.v = 0;
        if (! v.lad[0].z[0] && ! v.lad[0].z[3]) { /* fresh voice: filters already clear */ }
    }

    static inline int mipFor (double f, double sr)
    {
        const double maxAllowed = sr * 0.5 / std::max (1.0, f);
        int m = 0;
        while (m < kMips - 1 && (kTopHarm >> m) > maxAllowed) ++m;
        return m;
    }

    inline void oscSum (const OscP& o, int idx, double* ph, double fBase, float pos, const BlockCtx& c, float& l, float& r) const
    {
        const int un = std::clamp (o.uni, 1, kMaxUni);
        const double f0 = fBase * pow2 (o.oct + o.fine / 1200.0), inv = 1.0 / sr;
        const double fp = std::clamp ((double) pos, 0.0, 1.0) * (kFrames - 1);
        int fi = (int) fp; if (fi >= kFrames - 1) fi = kFrames - 2;
        const float ff = (float) (fp - fi);
        for (int j = 0; j < un; ++j)
        {
            const double f = f0 * c.ratio[idx][j];
            ph[j] += f * inv; ph[j] -= std::floor (ph[j]);
            const int m = mipFor (f, sr);
            const float s = readTable (tables->at (o.table, fi, m), ph[j]) * (1.0f - ff) + readTable (tables->at (o.table, fi + 1, m), ph[j]) * ff;
            l += s * c.gl[idx][j]; r += s * c.gr[idx][j];
        }
        l *= c.uniNorm[idx] * o.level; r *= c.uniNorm[idx] * o.level;
    }

    void renderVoice (Voice& v, float* L, float* R, int n, const P& p, const BlockCtx& c)
    {
        const double sus = p.as, fsus = p.fs, nyq = 0.45 * sr;
        const float velG = (float) (1.0 - p.velSens + p.velSens * v.vel);
        for (int i = 0; i < n; ++i)
        {
            v.pitch += (v.pitchT - v.pitch) * c.glideC;
            const double ae = v.amp.tick (c.ea, sus), fe = v.fenv.tick (c.ef, fsus);
            if (v.amp.st == Env::Idle) { v.active = false; v.gate = false; v.lad[0].reset(); v.lad[1].reset(); return; }
            const double fBase = 440.0 * pow2 ((v.pitch + c.bendSemis - 69.0) / 12.0);
            float al = 0, ar = 0, bl = 0, br = 0;
            const float pos = (float) std::clamp (p.posEnv * fe, -1.0, 1.0);
            if (p.a.level > 0.0001f) oscSum (p.a, 0, v.phA, fBase, std::clamp (p.a.pos + pos, 0.0f, 1.0f), c, al, ar);
            if (p.b.level > 0.0001f) oscSum (p.b, 1, v.phB, fBase, std::clamp (p.b.pos + pos, 0.0f, 1.0f), c, bl, br);
            float sub = 0;
            if (p.sub > 0.0001f) { v.phSub += fBase * 0.5 / sr; v.phSub -= std::floor (v.phSub); sub = (float) std::sin (2.0 * kPi * v.phSub) * p.sub; }
            float sl = (al + bl + sub) * 0.7f, sr2 = (ar + br + sub) * 0.7f;

            const double octs = p.fenv * fe * 7.0 + p.fkey * (v.pitch - 60.0) / 12.0 + c.modw * 2.0;
            const double fc = std::clamp ((double) cutA[(size_t) i] * pow2 (octs), 20.0, nyq);
            const double g = std::tan (kPi * fc / sr), k = 4.0 * resA[(size_t) i], dr = std::max (1.0, (double) drvA[(size_t) i]);
            sl = (float) v.lad[0].process (sl, g, k, dr, p.ftype);
            sr2 = (float) v.lad[1].process (sr2, g, k, dr, p.ftype);
            const float amp = (float) ae * velG;
            L[i] += sl * amp; R[i] += sr2 * amp;
        }
    }
};
} // namespace ls
