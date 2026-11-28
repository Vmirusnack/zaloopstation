// Built-in instruments of Loop Engine (besides Loop Synth): Kick Designer, Drum Kit and Hard Synth.
// They are ports of the instruments in the page (Web Audio) with the same parameter names, so a project sounds the same in both engines.
// Plain C++ (no JUCE). Audio thread: no allocation, no locks.
#pragma once
#include "Fx.h"
#include <cstdint>

namespace le
{
enum class InstType { None, Kick, Drums, Hard };

inline const std::vector<PDef>& instDefs (InstType t)
{
    static const std::vector<PDef> none,
        kick { { "punch", 300 }, { "sweep", 45 }, { "len", 260 }, { "click", 0.35f }, { "tail", 0.6f }, { "tlen", 450 }, { "tune", 0 }, { "drive", 0.45f }, { "clip", 0.3f }, { "mode", 0 } },
        drums { { "kit", 0 } },
        hard { { "wave", 0 }, { "uni", 3 }, { "det", 14 }, { "wid", 0.6f }, { "sub", 0 }, { "fm", 0 }, { "fmr", 2 }, { "ftype", 0 }, { "cut", 0.7f }, { "res", 1.5f }, { "fenv", 1 }, { "fdec", 200 },
               { "dive", 0 }, { "divet", 80 }, { "vib", 0 }, { "vrate", 5.5f }, { "drive", 0.3f }, { "clip", 0.2f }, { "mode", 0 }, { "atk", 4 }, { "dec", 250 }, { "sus", 0.8f }, { "rel", 0.2f } };
    switch (t) { case InstType::Kick: return kick; case InstType::Drums: return drums; case InstType::Hard: return hard; default: return none; }
}
inline InstType instTypeFromName (const std::string& n) { return n == "kick" ? InstType::Kick : n == "drums" ? InstType::Drums : n == "hard" ? InstType::Hard : InstType::None; }

struct InstSpec
{
    InstType type = InstType::None;
    std::array<float, 32> v {};
    void init (InstType t) { type = t; v.fill (0.0f); const auto& d = instDefs (t); for (size_t i = 0; i < d.size() && i < v.size(); ++i) v[i] = d[i].def; }
    bool setNum (const std::string& key, double x) { const auto& d = instDefs (type); for (size_t i = 0; i < d.size(); ++i) if (key == d[i].k) { v[i] = (float) x; return true; } return false; }
    bool setStr (const std::string& key, const std::string& s)
    {
        float x = 0;
        if (key == "mode") x = s == "hard" ? 1.0f : s == "fold" ? 2.0f : 0.0f;
        else if (key == "wave") x = s == "square" ? 1.0f : s == "triangle" ? 2.0f : s == "sine" ? 3.0f : 0.0f;
        else if (key == "ftype") x = s == "highpass" ? 1.0f : s == "bandpass" ? 2.0f : 0.0f;
        else if (key == "kit") x = s == "hard" ? 1.0f : 0.0f;
        else return false;
        return setNum (key, x);
    }
};

inline double mtofD (int p) { return 440.0 * std::pow (2.0, (p - 69) / 12.0); }
// Web Audio low-pass / high-pass take Q in dB; band-pass takes a plain Q
inline double qDb (double db) { return std::pow (10.0, db / 20.0); }

// ---- waveshaper with 4x oversampling (the shared "drive and clip" stage of the hard dance instruments) --------------------------------
class Shaper
{
public:
    Shaper() { design(); }
    void setCurve (float drive, float clip, int mode)
    {
        if (curveOk && drive == cd && clip == cc && mode == cm) return;
        cd = drive; cc = clip; cm = mode; curveOk = true;
        passthrough = drive < 0.01f && clip < 0.01f;
        const float k = mode == 2 ? 1 + drive * 7 : 1 + drive * drive * 60, lim = 1 - clip * 0.5f, pre = 1 + clip * 1.5f;
        for (int i = 0; i < kN; ++i)
        {
            const float x = (float) i / (kN - 1) * 2 - 1; float y;
            if (passthrough) y = x;
            else
            {
                if (mode == 1) y = std::max (-1.0f, std::min (1.0f, x * k)); else if (mode == 2) y = std::sin (x * k * 1.5708f); else y = std::tanh (x * k);
                y = std::max (-lim, std::min (lim, y * pre)) / lim;
            }
            curve[i] = y;
        }
    }
    void process (float* x, int n, int ch)
    {
        if (passthrough) return;
        for (int i = 0; i < n; ++i)
        {
            auto& u = up[ch]; u.push ((double) x[i]);
            double acc = 0;
            for (int ph = 0; ph < 4; ++ph)
            {
                double s = 0; for (int k = 0; k < kTaps / 4; ++k) s += u.at (k) * poly[ph][k];
                const float y = shape ((float) s);
                down[ch].push ((double) y);
                if (ph == 3) { for (int k = 0; k < kTaps; ++k) acc += down[ch].at (k) * h[k]; }
            }
            x[i] = (float) acc;
        }
    }
    void reset() { for (int c = 0; c < 2; ++c) { up[c].clear(); down[c].clear(); } }
private:
    static constexpr int kN = 4096, kTaps = 64;
    struct Ring
    {
        double d[kTaps] {}; int w = 0;
        void push (double v) { w = (w + 1) % kTaps; d[w] = v; }
        double at (int k) const { return d[(w - k + kTaps * 4) % kTaps]; }
        void clear() { for (auto& x : d) x = 0; w = 0; }
    };
    static double bessel0 (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }
    void design()
    {
        const double fc = 0.115, beta = 7.5, M = (kTaps - 1) / 2.0;     // fc in cycles per sample at the 4x rate; passband ends near 0.46 of the original Nyquist
        double sum = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double x = i - M, s = x == 0 ? 2 * fc : std::sin (2 * kPiD * fc * x) / (kPiD * x), r = 2.0 * i / (kTaps - 1) - 1, w = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
            h[i] = s * w; sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] /= sum;
        for (int ph = 0; ph < 4; ++ph) for (int k = 0; k < kTaps / 4; ++k) poly[ph][k] = h[ph + 4 * k] * 4.0;     // interpolation gain 4
    }
    float shape (float x) const
    {
        const float p = (std::max (-1.0f, std::min (1.0f, x)) + 1.0f) * 0.5f * (kN - 1); const int i = (int) p; const float f = p - (float) i;
        return i >= kN - 1 ? curve[kN - 1] : curve[i] * (1 - f) + curve[i + 1] * f;
    }
    double h[kTaps] {}, poly[4][kTaps / 4] {}; float curve[kN] {}; Ring up[2], down[2];
    float cd = -1, cc = -1; int cm = -1; bool curveOk = false, passthrough = true;
};

class Inst
{
public:
    virtual ~Inst() = default;
    virtual void prepare (double sampleRate) { sr = sampleRate; }
    virtual void noteOn (int pitch, float vel, const InstSpec& p) = 0;
    virtual void noteOff (int pitch) = 0;
    virtual void allOff() = 0;
    virtual void render (float* L, float* R, int n, const InstSpec& p) = 0;     // overwrites L and R
protected:
    double sr = 48000;
};

// ---- Kick Designer: pitched body with a fast sweep, a tail oscillator, a click, then the shared shaper ----------------------------------
class KickInst : public Inst
{
public:
    void noteOn (int pitch, float vel, const InstSpec& p) override
    {
        if (cur.on) { fade = cur; fade.killing = true; }
        Voice v; v.on = true; v.g = std::pow (std::max (0.0f, vel), 1.2f);
        double tf = mtofD (pitch); while (tf > 78) tf /= 2; while (tf < 38) tf *= 2; tf *= std::pow (2.0, p.v[6] / 12.0);
        v.tf = tf; v.punch = p.v[0]; v.tauS = std::max (0.002, p.v[1] / 1000.0 / 3.0); v.tauB = p.v[2] / 1000.0 / 4.0; v.tail = p.v[4]; v.tauT = p.v[5] / 1000.0 / 4.0; v.click = p.v[3];
        v.endT = std::max (p.v[2], p.v[4] > 0.01f ? p.v[5] : 0.0f) / 1000.0 * 1.4 + 0.2;
        cur = v; (void) v;
    }
    void noteOff (int) override {}
    void allOff() override { cur.on = false; fade.on = false; }
    void render (float* L, float* R, int n, const InstSpec& p) override
    {
        std::memset (L, 0, sizeof (float) * (size_t) n);
        voiceRender (cur, L, n); voiceRender (fade, L, n);
        shaper.setCurve (p.v[7], p.v[8], (int) p.v[9]);
        shaper.process (L, n, 0);
        for (int i = 0; i < n; ++i) { L[i] *= 0.5f; R[i] = L[i]; }
    }
private:
    struct Voice { bool on = false, killing = false; double g = 1, tf = 50, punch = 300, tauS = .015, tauB = .06, tail = .6, tauT = .1, click = .35, endT = 1, t = 0, ph = 0, ph2 = 0; unsigned rng = 12345; double hpz = 0, hpx = 0, kill = 1; };
    void voiceRender (Voice& v, float* out, int n)
    {
        if (! v.on) return;
        const double dt = 1.0 / sr, killK = std::exp (-1.0 / (0.004 * sr)), hpA = std::exp (-2.0 * kPiD * 2200.0 / sr);
        for (int i = 0; i < n; ++i)
        {
            const double t = v.t;
            const double f = v.tf + (v.punch - v.tf) * std::exp (-t / v.tauS);
            v.ph += f * dt; v.ph -= std::floor (v.ph);
            double be = t < 0.002 ? t / 0.002 : t < 0.004 ? 1.0 : std::exp (-(t - 0.004) / v.tauB);
            double s = std::sin (2 * kPiD * v.ph) * be;
            if (v.tail > 0.01)
            {
                v.ph2 += v.tf * dt; v.ph2 -= std::floor (v.ph2);
                const double te = t < 0.012 ? v.tail * t / 0.012 : t < 0.02 ? v.tail : v.tail * std::exp (-(t - 0.02) / v.tauT);
                s += std::sin (2 * kPiD * v.ph2) * te;
            }
            if (v.click > 0.01 && t < 0.03)
            {
                v.rng = v.rng * 1664525u + 1013904223u; const double nz = (double) (int) v.rng / 2147483648.0;
                const double hp = hpA * (v.hpz + nz - v.hpx); v.hpz = hp; v.hpx = nz;       // one-pole high-pass at 2.2 kHz
                const double ce = t < 0.012 ? v.click * 0.8 * std::pow (0.001 / (v.click * 0.8), t / 0.012) : 0.0;
                s += hp * ce;
            }
            if (v.killing) { v.kill *= killK; s *= v.kill; if (v.kill < 1e-4) { v.on = false; break; } }
            out[i] += (float) (s * v.g);
            v.t += dt;
            if (v.t > v.endT) { v.on = false; break; }
        }
    }
    Voice cur, fade; Shaper shaper;
};

// ---- Drum kit: the synthesised General MIDI drums of the page ----------------------------------------------------------------------------
class DrumInst : public Inst
{
public:
    void noteOn (int pitch, float vel, const InstSpec& p) override
    {
        const double v = std::pow (std::max (0.0f, vel), 1.3f);
        const bool hardKit = p.v[0] > 0.5f;
        Hit* h = nullptr; for (auto& x : hits) if (! x.on) { h = &x; break; }
        if (! h) { h = &hits[0]; for (auto& x : hits) if (x.age > h->age) h = &x; }
        *h = Hit(); h->on = true;
        auto pitched = [&] (double f0, double f1, double d, double peak) { Comp c; c.kind = 0; c.f = f0; c.fr = std::pow (f1 / f0, 1.0 / (d * 0.4 * sr)); c.fN = (int) (d * 0.4 * sr); setEnv (c, peak, d, 0); h->add (c); };
        auto noise = [&] (Biquad::Kind k, double fr, double d, double peak, double q, bool dbq, double at = 0, double len = -1) { Comp c; c.kind = 1; c.flt.set (k, sr, fr, dbq ? qDb (q) : q); setEnv (c, peak, d, at); c.noiseLeft = (int) ((len < 0 ? d + 0.05 : len) * sr); h->add (c); };
        auto metal = [&] (double d, double peak) { Comp c; c.kind = 2; c.flt.set (Biquad::HP, sr, 9000, qDb (1)); static const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540, 800 }; for (int i = 0; i < 6; ++i) c.mf[i] = fr[i]; setEnv (c, peak, d, 0); c.noiseLeft = (int) ((d + 0.05) * sr); h->add (c); };
        if (hardKit && pitch != 36 && pitch != 35)
        {
            if (pitch == 38 || pitch == 40) { pitched (240, 160, .14, .55 * v); noise (Biquad::BP, 2600, .24, .9 * v, .7, false); noise (Biquad::HP, 5000, .2, .35 * v, 1, true); }
            else if (pitch == 39) { for (int i = 0; i < 4; ++i) noise (Biquad::BP, 1600, i < 3 ? .011 : .32, .8 * v, .8, false, i * .009, .1); }
            else if (pitch == 42 || pitch == 44) { noise (Biquad::HP, 9500, .04, .4 * v, 1, true); metal (.035, .14 * v); }
            else if (pitch == 46) { noise (Biquad::HP, 8500, .26, .34 * v, 1, true); metal (.26, .16 * v); }
            else if (pitch == 49 || pitch == 51) { noise (Biquad::HP, 4500, 1.8, .4 * v, 1, true); metal (1.8, .18 * v); }
            else if (pitch == 37) { pitched (1200, 700, .03, .5 * v); noise (Biquad::HP, 3500, .04, .35 * v, 1, true); }
            else { const double b = tomBase (pitch, true); pitched (b * 2.2, b, .34, .85 * v); }
            return;
        }
        if (pitch == 36 || pitch == 35) { pitched (160, 44, .42, 1.1 * v); noise (Biquad::LP, 900, .02, .4 * v, 1, true); }
        else if (pitch == 38 || pitch == 40) { pitched (220, 140, .12, .5 * v); noise (Biquad::HP, 1500, .2, .75 * v, 1, true); }
        else if (pitch == 37) { pitched (900, 600, .03, .5 * v); noise (Biquad::HP, 3000, .03, .3 * v, 1, true); }
        else if (pitch == 39) { for (int i = 0; i < 3; ++i) noise (Biquad::BP, 1300, i < 2 ? .012 : .18, .7 * v, 1.2, false, i * .011, .1); }
        else if (pitch == 42 || pitch == 44) noise (Biquad::HP, 7500, .055, .45 * v, 1, true);
        else if (pitch == 46) noise (Biquad::HP, 7000, .35, .4 * v, 1, true);
        else if (pitch == 49 || pitch == 51) noise (Biquad::HP, 5200, 1.3, .45 * v, 1, true);
        else { const double b = tomBase (pitch, false); pitched (b * 1.8, b, .3, .8 * v); }
    }
    void noteOff (int) override {}
    void allOff() override { for (auto& h : hits) h.on = false; }
    void render (float* L, float* R, int n, const InstSpec&) override
    {
        std::memset (L, 0, sizeof (float) * (size_t) n);
        for (auto& h : hits) if (h.on) h.render (L, n, sr);
        for (int i = 0; i < n; ++i) R[i] = L[i];
    }
private:
    static double tomBase (int p, bool hard)
    {
        if (hard) { switch (p) { case 41: return 80; case 43: return 100; case 45: return 130; case 47: return 155; case 48: return 190; case 50: return 220; default: return 140; } }
        switch (p) { case 41: return 90; case 43: return 110; case 45: return 140; case 47: return 165; case 48: return 200; case 50: return 230; default: return 150; }
    }
    struct Comp
    {
        int kind = 0; double env = 0, k = 1; int left = 0, delay = 0;
        double ph = 0, f = 100, fr = 1; int fN = 0; Biquad flt; unsigned rng = 777; int noiseLeft = 0; double mf[6] {}, mph[6] {};
    };
    void setEnv (Comp& c, double peak, double d, double at) const { c.env = peak; c.k = std::pow (0.0008 / peak, 1.0 / (d * sr)); c.left = (int) (d * sr); c.delay = (int) (at * sr); }
    struct Hit
    {
        bool on = false; int age = 0; Comp c[8]; int n = 0;
        void add (const Comp& x) { if (n < 8) c[n++] = x; }
        void render (float* out, int cnt, double sr)
        {
            bool any = false;
            for (int i = 0; i < cnt; ++i)
            {
                double acc = 0;
                for (int j = 0; j < n; ++j)
                {
                    Comp& q = c[j];
                    if (q.delay > 0) { --q.delay; any = true; continue; }
                    if (q.left <= 0) continue;
                    any = true; --q.left;
                    double s = 0;
                    if (q.kind == 0) { s = std::sin (2 * kPiD * q.ph); q.ph += q.f / sr; q.ph -= std::floor (q.ph); if (q.fN > 0) { q.f *= q.fr; --q.fN; } }
                    else if (q.kind == 1) { if (q.noiseLeft > 0) { --q.noiseLeft; q.rng = q.rng * 1664525u + 1013904223u; s = q.flt.run (0, (double) (int) q.rng / 2147483648.0); } }
                    else { double m = 0; for (int s6 = 0; s6 < 6; ++s6) { q.mph[s6] += q.mf[s6] / sr; q.mph[s6] -= std::floor (q.mph[s6]); m += q.mph[s6] < 0.5 ? 1.0 : -1.0; } if (q.noiseLeft > 0) { --q.noiseLeft; s = q.flt.run (0, m); } }
                    acc += s * q.env; q.env *= q.k;
                }
                out[i] += (float) acc;
            }
            age += cnt; if (! any) on = false;
        }
    };
    Hit hits[24];
};

// ---- Hard Synth: unison oscillators, FM, vibrato, pitch dive, resonant filter with envelope, sub, then the shared shaper -------------------
class HardInst : public Inst
{
public:
    static constexpr int kVoices = 24;
    void noteOn (int pitch, float vel, const InstSpec& p) override
    {
        Voice* v = nullptr; for (auto& x : vs) if (! x.on) { v = &x; break; }
        if (! v) { v = &vs[0]; for (auto& x : vs) if (x.t > v->t) v = &x; }
        *v = Voice(); v->on = true; v->pitch = pitch; v->amp = 0.34 * std::pow (std::max (0.0f, vel), 1.3f); v->f = mtofD (pitch);
        v->n = std::max (1, (int) std::lround (p.v[1])); for (int i = 0; i < 9; ++i) { v->ph[i] = 0; }
        (void) p;
    }
    void noteOff (int pitch) override { for (auto& x : vs) if (x.on && ! x.rel && x.pitch == pitch) { x.rel = true; } }
    void allOff() override { for (auto& x : vs) x.on = false; }
    void render (float* L, float* R, int n, const InstSpec& p) override
    {
        std::memset (L, 0, sizeof (float) * (size_t) n); std::memset (R, 0, sizeof (float) * (size_t) n);
        for (auto& v : vs) if (v.on) voiceRender (v, L, R, n, p);
        shaper.setCurve (p.v[16], p.v[17], (int) p.v[18]);
        shaper.process (L, n, 0); shaper.process (R, n, 1);
        for (int i = 0; i < n; ++i) { L[i] *= 0.4f; R[i] *= 0.4f; }
    }
private:
    struct Voice
    {
        bool on = false, rel = false; int pitch = 60, n = 1; double amp = 0.3, f = 440, t = 0, e = 0.0001, relG = 1; bool atkDone = false;
        double ph[9] {}, phSub = 0, phM = 0, phV = 0; Biquad flt; int cnt = 0;
    };
    static double blep (double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
    static double wave (int kind, double ph, double dt)
    {
        switch (kind)
        {
            case 1: { double y = ph < 0.5 ? 1.0 : -1.0; y += blep (ph, dt); double p2 = ph + 0.5; p2 -= std::floor (p2); y -= blep (p2, dt); return y; }
            case 2: return 4.0 * std::fabs (ph - 0.5) - 1.0;
            case 3: return std::sin (2 * kPiD * ph);
            default: return 2.0 * ph - 1.0 - blep (ph, dt);
        }
    }
    void voiceRender (Voice& v, float* L, float* R, int n, const InstSpec& p)
    {
        const int kind = (int) p.v[0], ftype = (int) p.v[7];
        const double det = p.v[2], wid = p.v[3], sub = p.v[4], fm = p.v[5], fmr = p.v[6], cutP = p.v[8], res = p.v[9], fenv = p.v[10], fdec = p.v[11], dive = p.v[12], divet = p.v[13], vib = p.v[14], vrate = p.v[15];
        const double atk = std::max (0.001, p.v[19] / 1000.0), dec = std::max (0.01, p.v[20] / 1000.0 / 3.0), sus = p.v[21], rt = std::max (0.02, (double) p.v[22]);
        const double base = std::min (16000.0, 100.0 * std::pow (160.0, cutP)), fmax = std::min (20000.0, base * std::pow (2.0, fenv)), tauF = std::max (0.004, fdec / 1000.0 / 3.0);
        const double tauD = std::max (0.004, divet / 1000.0 / 3.0), gs = 1.0 / std::sqrt ((double) v.n), dt = 1.0 / sr;
        const double decK = std::exp (-1.0 / (dec * sr)), relK = std::exp (-1.0 / (rt / 4.0 * sr)), atkInc = v.amp / (atk * sr);
        double pl[9], pr[9];
        for (int i = 0; i < v.n; ++i) { const double pn = v.n > 1 ? (i / (double) (v.n - 1) * 2 - 1) * wid * (i % 2 ? 1 : -1) : 0.0, x = (pn + 1) * 0.5 * kPiD * 0.5; pl[i] = std::cos (x); pr[i] = std::sin (x); if (v.n == 1) { pl[i] = pr[i] = 1.0; } }
        for (int s = 0; s < n; ++s)
        {
            const double t = v.t;
            if ((v.cnt++ & 15) == 0)
            {
                const double fc = base + (fmax - base) * std::exp (-t / tauF);
                v.flt.set (ftype == 2 ? Biquad::BP : ftype == 1 ? Biquad::HP : Biquad::LP, sr, fc, ftype == 2 ? std::max (0.1, res) : qDb (res));
            }
            double fb = v.f; if (dive > 0) fb = v.f + (v.f * std::pow (2.0, dive / 12.0) - v.f) * std::exp (-t / tauD);
            double dev = 0;
            if (fm > 0.005) { const double mg = v.f * fm * 1.2 + (v.f * fm * 3.0 - v.f * fm * 1.2) * std::exp (-t / 0.15); v.phM += v.f * fmr * dt; v.phM -= std::floor (v.phM); dev = mg * std::sin (2 * kPiD * v.phM); }
            double vc = 0; if (vib > 0.5) { v.phV += vrate * dt; v.phV -= std::floor (v.phV); vc = vib * std::min (1.0, t / 0.12) * std::sin (2 * kPiD * v.phV); }
            double l = 0, r = 0;
            for (int i = 0; i < v.n; ++i)
            {
                const double d = v.n > 1 ? (i / (double) (v.n - 1) * 2 - 1) * det : 0.0;
                const double fi = (fb + dev) * std::pow (2.0, (d + vc) / 1200.0), inc = std::max (0.0, fi) * dt;
                v.ph[i] += inc; v.ph[i] -= std::floor (v.ph[i]);
                const double y = wave (kind, v.ph[i], std::min (0.45, inc)) * gs;
                l += y * pl[i]; r += y * pr[i];
            }
            double fl = v.flt.run (0, l), fr = v.flt.run (1, r);
            if (sub > 0.01) { v.phSub += v.f * 0.5 * dt; v.phSub -= std::floor (v.phSub); const double ss = std::sin (2 * kPiD * v.phSub) * sub * 0.8; fl += ss; fr += ss; }
            // amp envelope: linear attack, then an exponential approach to the sustain level; release is a separate multiplier
            if (! v.atkDone) { v.e += atkInc; if (v.e >= v.amp) { v.e = v.amp; v.atkDone = true; } }
            else { const double tgt = v.amp * sus; v.e = tgt + (v.e - tgt) * decK; }
            if (v.rel) { v.relG *= relK; }
            const double g = v.e * v.relG;
            L[s] += (float) (fl * g); R[s] += (float) (fr * g);
            v.t += dt;
            if (v.rel && v.relG < 1e-4) { v.on = false; break; }
            if (v.t > 60.0) { v.on = false; break; }
        }
    }
    Voice vs[kVoices]; Shaper shaper;
};

inline std::shared_ptr<Inst> makeInst (InstType t, double sr)
{
    std::shared_ptr<Inst> i;
    switch (t) { case InstType::Kick: i = std::make_shared<KickInst>(); break; case InstType::Drums: i = std::make_shared<DrumInst>(); break; case InstType::Hard: i = std::make_shared<HardInst>(); break; default: return nullptr; }
    i->prepare (sr);
    return i;
}
}
