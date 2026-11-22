// Loop Synth: sound engine. Plain C++ (no JUCE), so it can be tested on its own.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ls
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kTable = 2048;   // samples per table
constexpr int kFrames = 16;    // wavetable positions
constexpr int kMips = 10;      // band-limited copies (one per octave)
constexpr int kTopHarm = 512;  // harmonics in the richest copy
constexpr int kSets = 4;          // built-in tables
constexpr int kCustom = 4;        // user slots (tables loaded from .wav files)
constexpr int kTables = kSets + kCustom;
constexpr int kMaxFrames = 256;   // frames in a user table
constexpr int kMaxUni = 7;
constexpr int kVoices = 16;
constexpr int kNumLfo = 3;
constexpr int kNumMod = 8;
constexpr int kNumSrc = 10;       // 0 none, 1-3 LFO, 4 filter env, 5 amp env, 6 velocity, 7 key, 8 mod wheel, 9 random
constexpr int kNumDst = 11;       // 0 none, 1 A pos, 2 B pos, 3 A pitch, 4 B pitch, 5 pitch all, 6 cutoff, 7 resonance, 8 A level, 9 B level, 10 sub
constexpr double kDivBeats[8] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125 };

inline double pow2 (double x) { return std::exp2 (x); }

// ---------------------------------------------------------------- wavetables
// One table = `frames` single-cycle frames, each stored as kMips band-limited copies (one per octave).
struct TSet
{
    int frames = 1;
    int topHarm = kTopHarm;           // harmonics in mip 0
    std::string name;
    std::vector<float> data;          // [frame][mip][kTable + 1]

    const float* at (int frame, int mip) const { return &data[(((size_t) frame * kMips + mip) * (kTable + 1))]; }
    float* atW (int frame, int mip) { return &data[(((size_t) frame * kMips + mip) * (kTable + 1))]; }
};

// in-place radix-2 FFT (n power of two)
inline void fft (std::vector<double>& re, std::vector<double>& im, bool inverse)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
        const double wr = std::cos (ang), wi = std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const size_t a = i + k, b = i + k + len / 2;
                const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
    if (inverse) for (size_t i = 0; i < n; ++i) { re[i] /= (double) n; im[i] /= (double) n; }
}

// frames: nf * kTable raw samples (already normalised). Builds the band-limited copies.
inline std::unique_ptr<TSet> buildTableSet (const float* frames, int nf, const std::string& name)
{
    auto t = std::make_unique<TSet>();
    nf = std::clamp (nf, 1, kMaxFrames);
    t->frames = nf; t->topHarm = kTable / 2; t->name = name;
    t->data.assign ((size_t) nf * kMips * (kTable + 1), 0.0f);
    std::vector<double> re (kTable), im (kTable), r2 (kTable), i2 (kTable);
    for (int f = 0; f < nf; ++f)
    {
        for (int i = 0; i < kTable; ++i) { re[(size_t) i] = frames[(size_t) f * kTable + i]; im[(size_t) i] = 0; }
        fft (re, im, false);
        re[0] = 0; im[0] = 0;
        for (int m = 0; m < kMips; ++m)
        {
            const int H = (kTable / 2) >> m;
            for (int k = 0; k < kTable; ++k)
            {
                const int kk = k <= kTable / 2 ? k : kTable - k;
                if (kk <= H) { r2[(size_t) k] = re[(size_t) k]; i2[(size_t) k] = im[(size_t) k]; } else { r2[(size_t) k] = 0; i2[(size_t) k] = 0; }
            }
            fft (r2, i2, true);
            float* d = t->atW (f, m);
            for (int i = 0; i < kTable; ++i) d[i] = (float) r2[(size_t) i];
            d[kTable] = d[0];
        }
    }
    return t;
}

// ---------------------------------------------------------------- .wav reading (PCM 8/16/24/32, float 32/64; Serum "clm " frame size)
struct WavData { std::vector<float> mono; int frameSize = 0; int sampleRate = 0; };

inline bool parseWav (const uint8_t* d, size_t n, WavData& out)
{
    auto u16 = [&] (size_t o) -> uint32_t { return o + 2 <= n ? (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) : 0; };
    auto u32 = [&] (size_t o) -> uint32_t { return o + 4 <= n ? (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) | ((uint32_t) d[o + 2] << 16) | ((uint32_t) d[o + 3] << 24) : 0; };
    if (n < 44 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) return false;
    size_t pos = 12, dataOff = 0, dataLen = 0;
    uint32_t tag = 0, ch = 0, bits = 0, sr = 0, ext = 0;
    bool haveFmt = false, haveData = false;
    out.frameSize = 0;
    while (pos + 8 <= n)
    {
        const uint32_t sz = u32 (pos + 4);
        const size_t b = pos + 8;
        if (std::memcmp (d + pos, "fmt ", 4) == 0) { tag = u16 (b); ch = u16 (b + 2); sr = u32 (b + 4); bits = u16 (b + 14); ext = sz >= 26 ? u16 (b + 24) : 0; haveFmt = true; }
        else if (std::memcmp (d + pos, "data", 4) == 0) { dataOff = b; dataLen = std::min ((size_t) sz, n - b); haveData = true; }
        else if (std::memcmp (d + pos, "clm ", 4) == 0)
        {
            // text like "<!>2048 00000000 wav": frame size follows "<!>"
            const size_t e = std::min (n, b + std::min ((size_t) sz, (size_t) 40));
            for (size_t i = b; i + 3 < e; ++i)
                if (d[i] == '<' && d[i + 1] == '!' && d[i + 2] == '>')
                {
                    int v = 0; size_t k = i + 3; while (k < e && d[k] == ' ') ++k;
                    while (k < e && d[k] >= '0' && d[k] <= '9') { v = v * 10 + (d[k] - '0'); ++k; }
                    out.frameSize = v; break;
                }
        }
        pos = b + sz + (sz & 1u);
    }
    if (! haveFmt || ! haveData || ch == 0 || bits == 0) return false;
    const uint32_t t = tag == 0xFFFE ? ext : tag;
    const uint32_t bps = bits >> 3;
    if ((t != 1 && t != 3) || bps == 0 || bps > 8) return false;
    const size_t frames = dataLen / ((size_t) bps * ch);
    out.sampleRate = (int) sr;
    out.mono.assign (frames, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0;
        for (uint32_t c = 0; c < ch; ++c)
        {
            const uint8_t* q = d + dataOff + (i * ch + c) * bps;
            double v;
            if (t == 3)
            {
                if (bps == 8) { uint64_t x = 0; for (int k = 7; k >= 0; --k) x = (x << 8) | q[k]; double f; std::memcpy (&f, &x, 8); v = f; }
                else { uint32_t x = (uint32_t) q[0] | ((uint32_t) q[1] << 8) | ((uint32_t) q[2] << 16) | ((uint32_t) q[3] << 24); float f; std::memcpy (&f, &x, 4); v = f; }
            }
            else if (bps == 2) v = (int16_t) ((uint16_t) q[0] | ((uint16_t) q[1] << 8)) / 32768.0;
            else if (bps == 3) { int32_t x = (int32_t) (q[0] | (q[1] << 8) | (q[2] << 16)); if (x & 0x800000) x -= 0x1000000; v = x / 8388608.0; }
            else if (bps == 4) v = (int32_t) ((uint32_t) q[0] | ((uint32_t) q[1] << 8) | ((uint32_t) q[2] << 16) | ((uint32_t) q[3] << 24)) / 2147483648.0;
            else v = (q[0] - 128) / 128.0;
            acc += v;
        }
        out.mono[i] = (float) (acc / ch);
    }
    return true;
}

// raw audio -> normalised wavetable frames (nf * kTable). Single cycles (short files) become one frame.
inline std::vector<float> framesFromAudio (const std::vector<float>& a, int frameSize, int& nf)
{
    const int L = (int) a.size();
    nf = 0;
    if (L < 64) return {};
    const int FS = frameSize >= 64 ? frameSize : kTable;
    std::vector<float> out;
    if (L >= std::max (3072, (int) (FS * 1.5)) || (frameSize > 0 && L >= FS * 2))
    {
        const int total = L / FS;
        nf = std::min (total, kMaxFrames);
        out.assign ((size_t) nf * kTable, 0.0f);
        for (int f = 0; f < nf; ++f)
        {
            const int src = total > kMaxFrames ? (int) std::lround (f * (double) (total - 1) / (kMaxFrames - 1)) : f;
            const float* o = a.data() + (size_t) src * FS;
            for (int i = 0; i < kTable; ++i)
            {
                if (FS == kTable) out[(size_t) f * kTable + i] = o[i];
                else { const double x = (double) i / kTable * FS; const int k = (int) x; const float fr = (float) (x - k); out[(size_t) f * kTable + i] = o[k] * (1 - fr) + o[std::min (FS - 1, k + 1)] * fr; }
            }
        }
    }
    else
    {
        nf = 1; out.assign ((size_t) kTable, 0.0f);
        for (int i = 0; i < kTable; ++i) { const double x = (double) i / kTable * L; const int j = (int) x; const float f = (float) (x - j); out[(size_t) i] = a[(size_t) j] * (1 - f) + a[(size_t) std::min (L - 1, j + 1)] * f; }
    }
    double mean = 0; for (float v : out) mean += v; mean /= (double) out.size();
    double pk = 1e-9; for (auto& v : out) { v = (float) (v - mean); pk = std::max (pk, (double) std::fabs (v)); }
    for (auto& v : out) v = (float) (v / pk * 0.95);
    return out;
}

struct Tables
{
    std::vector<std::unique_ptr<TSet>> builtin;
    std::atomic<const TSet*> custom[kCustom];

    Tables() { for (auto& c : custom) c.store (nullptr); build(); }

    // message thread: publish a user table; the audio thread picks it up with one pointer read.
    // The previous table stays alive for a few more swaps so a running block never sees freed memory.
    void setCustom (int slot, std::unique_ptr<TSet> t)
    {
        if (slot < 0 || slot >= kCustom) return;
        std::lock_guard<std::mutex> lk (mtx);
        const TSet* raw = t.get();
        if (own[slot]) grave.push_back (std::move (own[slot]));
        own[slot] = std::move (t);
        custom[slot].store (raw, std::memory_order_release);
        while (grave.size() > 6) grave.erase (grave.begin());
    }
    void clearCustom (int slot) { setCustom (slot, nullptr); }

    const TSet* get (int table) const
    {
        table = std::clamp (table, 0, kTables - 1);
        if (table < kSets) return builtin[(size_t) table].get();
        const TSet* c = custom[table - kSets].load (std::memory_order_acquire);
        return c ? c : builtin[0].get();
    }
    bool hasCustom (int slot) const { return slot >= 0 && slot < kCustom && custom[slot].load() != nullptr; }

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
    std::mutex mtx;
    std::unique_ptr<TSet> own[kCustom];
    std::vector<std::unique_ptr<TSet>> grave;

    void build()
    {
        std::vector<float> sinT (kTable);
        for (int i = 0; i < kTable; ++i) sinT[(size_t) i] = (float) std::sin (2.0 * kPi * i / kTable);
        std::vector<double> tmp (kTable);

        for (int s = 0; s < kSets; ++s)
        {
            auto ts = std::make_unique<TSet>();
            ts->frames = kFrames; ts->topHarm = kTopHarm; ts->name = setName (s);
            ts->data.assign ((size_t) kFrames * kMips * (kTable + 1), 0.0f);
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
                    float* d = ts->atW (f, m);
                    for (int i = 0; i < kTable; ++i) d[i] = (float) (tmp[(size_t) i] * scale);
                    d[kTable] = d[0];
                }
            }
            builtin.push_back (std::move (ts));
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
    struct Lfo { int shape = 0; float rate = 2.0f; bool sync = false; int div = 4; } lfo[kNumLfo];
    struct Mod { int src = 0, dst = 0; float amt = 0; } mod[kNumMod];
    double bpm = 120;
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
    double lfoPh[kNumLfo] = {};
    float lfoSh[kNumLfo] = {}, rnd = 0;
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
    double lfoInc[kNumLfo] = {};
    bool anyMod = false;
    int nSlots = 0, slotSrc[kNumMod] = {}, slotDst[kNumMod] = {};
    float slotAmt[kNumMod] = {};
};

class Synth
{
public:
    Synth() : tables (std::make_unique<Tables>()) {}

    Tables& bank() { return *tables; }
    const Tables& bank() const { return *tables; }

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
        for (int l = 0; l < kNumLfo; ++l)
        {
            const auto& lp = p.lfo[l];
            const double hz = lp.sync ? (std::max (20.0, p.bpm) / 60.0) / kDivBeats[std::clamp (lp.div, 0, 7)] : (double) lp.rate;
            c.lfoInc[l] = std::clamp (hz, 0.0, 200.0) / sr;
        }
        for (int m = 0; m < kNumMod; ++m)
            if (p.mod[m].src > 0 && p.mod[m].dst > 0 && std::fabs (p.mod[m].amt) > 0.0005f)
            { c.slotSrc[c.nSlots] = std::clamp (p.mod[m].src, 0, kNumSrc - 1); c.slotDst[c.nSlots] = std::clamp (p.mod[m].dst, 0, kNumDst - 1); c.slotAmt[c.nSlots] = p.mod[m].amt; ++c.nSlots; }
        c.anyMod = c.nSlots > 0;
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
        for (int l = 0; l < kNumLfo; ++l) { v.lfoPh[l] = 0; v.lfoSh[l] = (float) (rand01() * 2.0 - 1.0); }
        v.rnd = (float) (rand01() * 2.0 - 1.0);
        v.amp.on(); v.fenv.on();
        v.fenv.v = 0;
        if (! v.lad[0].z[0] && ! v.lad[0].z[3]) { /* fresh voice: filters already clear */ }
    }

    static inline int mipFor (double f, double sr, int topHarm)
    {
        const double maxAllowed = sr * 0.5 / std::max (1.0, f);
        int m = 0;
        while (m < kMips - 1 && (topHarm >> m) > maxAllowed) ++m;
        return m;
    }

    static inline float lfoShape (int shape, double ph, float sh)
    {
        switch (shape)
        {
            case 1: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2: return (float) (2.0 * ph - 1.0);
            case 3: return ph < 0.5 ? 1.0f : -1.0f;
            case 4: return sh;
            default: return (float) std::sin (2.0 * kPi * ph);
        }
    }

    inline void oscSum (const OscP& o, int idx, double* ph, double fBase, float pos, float level, double semis, const BlockCtx& c, float& l, float& r) const
    {
        const int un = std::clamp (o.uni, 1, kMaxUni);
        const TSet* ts = tables->get (o.table);
        const double f0 = fBase * pow2 (o.oct + o.fine / 1200.0 + semis / 12.0), inv = 1.0 / sr;
        int fi = 0, f1 = 0; float ff = 0;
        if (ts->frames > 1)
        {
            const double fp = std::clamp ((double) pos, 0.0, 1.0) * (ts->frames - 1);
            fi = (int) fp; if (fi >= ts->frames - 1) fi = ts->frames - 2;
            ff = (float) (fp - fi); f1 = fi + 1;
        }
        for (int j = 0; j < un; ++j)
        {
            const double f = f0 * c.ratio[idx][j];
            ph[j] += f * inv; ph[j] -= std::floor (ph[j]);
            const int m = mipFor (f, sr, ts->topHarm);
            const float s = readTable (ts->at (fi, m), ph[j]) * (1.0f - ff) + (ff > 0 ? readTable (ts->at (f1, m), ph[j]) * ff : 0.0f);
            l += s * c.gl[idx][j]; r += s * c.gr[idx][j];
        }
        l *= c.uniNorm[idx] * level; r *= c.uniNorm[idx] * level;
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
            float md[kNumDst] = {};
            if (c.anyMod)
            {
                float src[kNumSrc];
                src[0] = 0;
                for (int l = 0; l < kNumLfo; ++l)
                {
                    v.lfoPh[l] += c.lfoInc[l];
                    if (v.lfoPh[l] >= 1.0) { v.lfoPh[l] -= std::floor (v.lfoPh[l]); v.lfoSh[l] = (float) (rand01() * 2.0 - 1.0); }
                    src[1 + l] = lfoShape (p.lfo[l].shape, v.lfoPh[l], v.lfoSh[l]);
                }
                src[4] = (float) fe; src[5] = (float) ae; src[6] = v.vel; src[7] = (float) ((v.pitch - 60.0) / 36.0); src[8] = (float) c.modw; src[9] = v.rnd;
                for (int m = 0; m < c.nSlots; ++m) md[c.slotDst[m]] += src[c.slotSrc[m]] * c.slotAmt[m];
            }
            float al = 0, ar = 0, bl = 0, br = 0;
            const float pos = (float) std::clamp (p.posEnv * fe, -1.0, 1.0);
            const float lvA = std::clamp (p.a.level + md[8], 0.0f, 1.5f), lvB = std::clamp (p.b.level + md[9], 0.0f, 1.5f);
            if (lvA > 0.0001f) oscSum (p.a, 0, v.phA, fBase, std::clamp (p.a.pos + pos + md[1], 0.0f, 1.0f), lvA, (md[3] + md[5]) * 24.0, c, al, ar);
            if (lvB > 0.0001f) oscSum (p.b, 1, v.phB, fBase, std::clamp (p.b.pos + pos + md[2], 0.0f, 1.0f), lvB, (md[4] + md[5]) * 24.0, c, bl, br);
            float sub = 0;
            const float subLv = std::clamp (p.sub + md[10], 0.0f, 1.5f);
            if (subLv > 0.0001f) { v.phSub += fBase * 0.5 / sr; v.phSub -= std::floor (v.phSub); sub = (float) std::sin (2.0 * kPi * v.phSub) * subLv; }
            float sl = (al + bl + sub) * 0.7f, sr2 = (ar + br + sub) * 0.7f;

            const double octs = p.fenv * fe * 7.0 + p.fkey * (v.pitch - 60.0) / 12.0 + c.modw * 2.0 + md[6] * 5.0;
            const double fc = std::clamp ((double) cutA[(size_t) i] * pow2 (octs), 20.0, nyq);
            const double g = std::tan (kPi * fc / sr), k = 4.0 * std::clamp ((double) resA[(size_t) i] + md[7], 0.0, 1.05), dr = std::max (1.0, (double) drvA[(size_t) i]);
            sl = (float) v.lad[0].process (sl, g, k, dr, p.ftype);
            sr2 = (float) v.lad[1].process (sr2, g, k, dr, p.ftype);
            const float amp = (float) ae * velG;
            L[i] += sl * amp; R[i] += sr2 * amp;
        }
    }
};
} // namespace ls
