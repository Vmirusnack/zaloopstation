// Built-in effects of Loop Engine. Plain C++ (no JUCE) so the engine, the tests and later the Android library share it.
// Every effect works on a stereo pair of float buffers in place. Parameters arrive in an FxSpec (a fixed array of floats),
// so the audio thread never touches strings or maps. Internal state is double precision where it matters (filters).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace le
{
constexpr double kPiD = 3.14159265358979323846;

enum class FxType { None, Eq, Comp, Delay, Limiter, Filter, Reverb, Chorus, Phaser, Trem, Dist };

struct PDef { const char* k; float def; };

// parameter tables: the position in the table is the index in FxSpec::v. Same names and defaults as the page (FX_DEFS).
inline const std::vector<PDef>& fxDefs (FxType t)
{
    static const std::vector<PDef> none, eq { { "lsF", 100 }, { "lsG", 0 }, { "m1F", 500 }, { "m1G", 0 }, { "m1Q", 1 }, { "m2F", 3000 }, { "m2G", 0 }, { "m2Q", 1 }, { "hsF", 8000 }, { "hsG", 0 } },
        comp { { "thr", -20 }, { "ratio", 3 }, { "knee", 8 }, { "range", 60 }, { "att", 10 }, { "rel", 150 }, { "hold", 0 }, { "sc", 20 }, { "gain", 0 }, { "mix", 1 }, { "style", 0 }, { "auto", 0 } },
        delay { { "time", 375 }, { "fb", 0.35f }, { "mix", 0.3f }, { "tone", 5000 } },
        lim { { "gain", 0 }, { "ceil", -1 }, { "rel", 80 } },
        filt { { "mode", 0 }, { "cut", 2000 }, { "res", 1 } },
        rev { { "size", 1.6f }, { "pre", 15 }, { "tone", 7000 }, { "mix", 0.25f } },
        cho { { "rate", 0.8f }, { "depth", 0.5f }, { "mix", 0.5f } },
        pha { { "rate", 0.4f }, { "depth", 0.7f }, { "mix", 0.6f } },
        tre { { "rate", 5 }, { "depth", 0.6f } },
        dis { { "drive", 12 }, { "tone", 5000 }, { "mix", 1 } };
    switch (t)
    {
        case FxType::Eq: return eq; case FxType::Comp: return comp; case FxType::Delay: return delay; case FxType::Limiter: return lim;
        case FxType::Filter: return filt; case FxType::Reverb: return rev; case FxType::Chorus: return cho; case FxType::Phaser: return pha;
        case FxType::Trem: return tre; case FxType::Dist: return dis; default: return none;
    }
}

inline FxType fxTypeFromName (const std::string& n)
{
    static const struct { const char* n; FxType t; } m[] = { { "eq", FxType::Eq }, { "comp", FxType::Comp }, { "delay", FxType::Delay }, { "limiter", FxType::Limiter }, { "filter", FxType::Filter },
        { "reverb", FxType::Reverb }, { "chorus", FxType::Chorus }, { "phaser", FxType::Phaser }, { "trem", FxType::Trem }, { "dist", FxType::Dist } };
    for (auto& e : m) if (n == e.n) return e.t;
    return FxType::None;
}

struct FxSpec
{
    FxType type = FxType::None; bool on = true;
    std::array<float, 16> v {};
    void init (FxType t) { type = t; v.fill (0.0f); const auto& d = fxDefs (t); for (size_t i = 0; i < d.size() && i < v.size(); ++i) v[i] = d[i].def; }
    bool setNum (const std::string& key, double x) { const auto& d = fxDefs (type); for (size_t i = 0; i < d.size(); ++i) if (key == d[i].k) { v[i] = (float) x; return true; } return false; }
    // text options (same order as the page): filter mode, compressor style and auto gain
    bool setStr (const std::string& key, const std::string& s)
    {
        if (type == FxType::Filter && key == "mode") { v[0] = s == "highpass" ? 1.0f : s == "bandpass" ? 2.0f : 0.0f; return true; }
        if (type == FxType::Comp && key == "style") { static const char* o[] = { "clean", "classic", "opto", "vocal", "mastering", "bus", "punch", "pumping" }; for (int i = 0; i < 8; ++i) if (s == o[i]) { v[10] = (float) i; return true; } return false; }
        if (type == FxType::Comp && key == "auto") { v[11] = s == "on" ? 1.0f : 0.0f; return true; }
        return false;
    }
};

inline double dbToLin (double d) { return std::pow (10.0, d / 20.0); }

struct Biquad
{
    enum Kind { LP, HP, BP, Peak, LowShelf, HighShelf };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] { 0, 0 }, z2[2] { 0, 0 };
    void set (Kind k, double sr, double f, double q, double gdb = 0)
    {
        f = std::clamp (f, 10.0, sr * 0.49); q = std::max (0.05, q);
        const double w = 2.0 * kPiD * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q), A = std::pow (10.0, gdb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (k)
        {
            case LP: B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case HP: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case BP: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case Peak: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            case LowShelf: { const double t = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * c + t); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - t);
                A0 = (A + 1) + (A - 1) * c + t; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - t; break; }
            case HighShelf: { const double t = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * c + t); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - t);
                A0 = (A + 1) - (A - 1) * c + t; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - t; break; }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    inline double run (int ch, double x) { const double y = b0 * x + z1[ch]; z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y; return y; }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

class Fx
{
public:
    virtual ~Fx() = default;
    virtual void prepare (double sampleRate) { sr = sampleRate; }
    virtual void process (float* L, float* R, int n, const FxSpec& p) = 0;
    virtual int latency() const { return 0; }
protected:
    double sr = 48000;
    // true when the parameters differ from the last call (so coefficients are recomputed only when needed)
    bool changed (const FxSpec& p) { if (haveLast && p.v == last) return false; last = p.v; haveLast = true; return true; }
private:
    std::array<float, 16> last {}; bool haveLast = false;
};

// ---- channel EQ: low shelf, two bells, high shelf --------------------------------------------------------------------
class EqFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        if (changed (p))
        {
            b[0].set (Biquad::LowShelf, sr, p.v[0], 0.7071, p.v[1]); b[1].set (Biquad::Peak, sr, p.v[2], p.v[4], p.v[3]);
            b[2].set (Biquad::Peak, sr, p.v[5], p.v[7], p.v[6]); b[3].set (Biquad::HighShelf, sr, p.v[8], 0.7071, p.v[9]);
        }
        for (int i = 0; i < n; ++i)
        {
            double l = L[i], r = R[i];
            for (auto& q : b) { l = q.run (0, l); r = q.run (1, r); }
            L[i] = (float) l; R[i] = (float) r;
        }
    }
private:
    Biquad b[4];
};

// ---- compressor: soft knee, attack / release / hold, range, side-chain low cut, parallel mix, auto gain ---------------
class CompFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double thr = p.v[0], ratio = std::max (1.0f, p.v[1]), knee = p.v[2], range = p.v[3], hold = p.v[6] * 0.001 * sr, mix = p.v[9];
        const double att = std::exp (-1.0 / std::max (1.0, p.v[4] * 0.001 * sr)), rel = std::exp (-1.0 / std::max (1.0, p.v[5] * 0.001 * sr));
        const double makeup = p.v[8] + (p.v[11] > 0.5f ? -thr * (1.0 - 1.0 / ratio) * 0.5 : 0.0);
        if (changed (p)) { hp.set (Biquad::HP, sr, p.v[7], 0.7071); }
        const double slope = 1.0 / ratio - 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double dl = L[i], dr = R[i];
            const double sl = hp.run (0, dl), sr_ = hp.run (1, dr);
            const double lev = 20.0 * std::log10 (std::max (1e-9, std::max (std::fabs (sl), std::fabs (sr_))));
            const double over = lev - thr;
            double gr = 0;
            if (2.0 * over > knee) gr = slope * over;
            else if (2.0 * over > -knee && knee > 0) { const double x = over + knee / 2; gr = slope * x * x / (2.0 * knee); }
            gr = std::max (gr, -range);
            if (gr < env) { env = att * env + (1 - att) * gr; holdLeft = (int) hold; }
            else if (holdLeft > 0) --holdLeft;
            else env = rel * env + (1 - rel) * gr;
            const double g = dbToLin (env + makeup);
            L[i] = (float) (dl * (1 - mix) + dl * g * mix); R[i] = (float) (dr * (1 - mix) + dr * g * mix);
        }
        grDb = env;
    }
    double grDb = 0;
private:
    Biquad hp; double env = 0; int holdLeft = 0;
};

// ---- limiter: look-ahead peak limiter with a hard safety clip at the ceiling -------------------------------------------
class LimiterFx : public Fx
{
public:
    void prepare (double s) override { sr = s; la = std::max (16, (int) std::lround (sr * 0.0015)); dl.assign ((size_t) la, 0.0f); dr.assign ((size_t) la, 0.0f); gw.assign ((size_t) la, 1.0); gb.assign ((size_t) la, 1.0); wi = 0; sum = (double) la; rg = 1.0; }
    int latency() const override { return la; }
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double in = dbToLin (p.v[0]), ceil = dbToLin (p.v[1]), rc = std::exp (-1.0 / std::max (1.0, p.v[2] * 0.001 * sr));
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i] * in, r = R[i] * in;
            const double pk = std::max (std::fabs (l), std::fabs (r));
            const double need = pk > ceil ? ceil / pk : 1.0;
            gw[(size_t) wi] = need;
            double mn = 1.0; for (int k = 0; k < la; ++k) mn = std::min (mn, gw[(size_t) k]);
            sum += mn - gb[(size_t) wi]; gb[(size_t) wi] = mn;
            const double boxed = sum / la;
            rg = boxed < rg ? boxed : rc * rg + (1 - rc) * boxed;
            const double g = std::min (rg, boxed);
            const double ol = dl[(size_t) wi] * g, orr = dr[(size_t) wi] * g;
            dl[(size_t) wi] = (float) l; dr[(size_t) wi] = (float) r;
            wi = (wi + 1) % la;
            L[i] = (float) std::clamp (ol, -ceil, ceil); R[i] = (float) std::clamp (orr, -ceil, ceil);
        }
    }
private:
    int la = 72, wi = 0; std::vector<float> dl, dr; std::vector<double> gw, gb; double sum = 72, rg = 1;
};

// ---- stereo echo with a tone filter in the feedback loop ----------------------------------------------------------------
class DelayFx : public Fx
{
public:
    void prepare (double s) override { sr = s; buf[0].assign ((size_t) (sr * 1.2) + 4, 0.0f); buf[1] = buf[0]; w = 0; }
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double fb = std::min (0.95f, p.v[1]), mix = p.v[2], a = 1.0 - std::exp (-2.0 * kPiD * std::clamp ((double) p.v[3], 100.0, 20000.0) / sr);
        const size_t sz = buf[0].size();
        for (int i = 0; i < n; ++i)
        {
            tCur += (p.v[0] * 0.001 * sr - tCur) * 0.0005;                  // time changes glide instead of clicking
            double rp = (double) w - tCur; while (rp < 0) rp += (double) sz;
            const size_t i0 = (size_t) rp % sz, i1 = (i0 + 1) % sz; const double fr = rp - std::floor (rp);
            const double dL = buf[0][i0] * (1 - fr) + buf[0][i1] * fr, dR = buf[1][i0] * (1 - fr) + buf[1][i1] * fr;
            lpL += (dL - lpL) * a; lpR += (dR - lpR) * a;
            buf[0][w] = (float) (L[i] + lpL * fb); buf[1][w] = (float) (R[i] + lpR * fb);
            w = (w + 1) % sz;
            L[i] = (float) (L[i] * (1 - mix * 0.5) + dL * mix); R[i] = (float) (R[i] * (1 - mix * 0.5) + dR * mix);
        }
    }
private:
    std::vector<float> buf[2]; size_t w = 0; double tCur = 18000, lpL = 0, lpR = 0;
};

// ---- filter: low-pass / high-pass / band-pass ----------------------------------------------------------------------------
class FilterFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        if (changed (p)) f.set (p.v[0] > 1.5f ? Biquad::BP : p.v[0] > 0.5f ? Biquad::HP : Biquad::LP, sr, p.v[1], p.v[2]);
        for (int i = 0; i < n; ++i) { L[i] = (float) f.run (0, L[i]); R[i] = (float) f.run (1, R[i]); }
    }
private:
    Biquad f;
};

// ---- reverb: pre-delay, two diffusers, four-line feedback delay network with damping ---------------------------------------
class ReverbFx : public Fx
{
public:
    void prepare (double s) override
    {
        sr = s; static const double ms[4] = { 29.7, 37.1, 41.1, 43.7 };
        for (int i = 0; i < 4; ++i) { len[i] = (size_t) std::lround (ms[i] * 0.001 * sr); line[i].assign (len[i] + 1, 0.0f); wp[i] = 0; lp[i] = 0; }
        static const double ap[2] = { 5.0, 1.7 };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { const size_t m = (size_t) std::lround (ap[k] * 0.001 * sr * (c ? 1.13 : 1.0)); apBuf[c][k].assign (m + 1, 0.0f); apLen[c][k] = m; apW[c][k] = 0; }
        pre[0].assign ((size_t) (sr * 0.2) + 2, 0.0f); pre[1] = pre[0]; pw = 0;
    }
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double rt = std::max (0.2f, p.v[0]), mix = p.v[3], dmp = 1.0 - std::exp (-2.0 * kPiD * std::clamp ((double) p.v[2], 500.0, 20000.0) / sr);
        const size_t pd = std::min (pre[0].size() - 1, (size_t) std::lround (p.v[1] * 0.001 * sr));
        double g[4]; for (int i = 0; i < 4; ++i) g[i] = std::pow (10.0, -3.0 * (double) len[i] / (rt * sr));
        for (int i = 0; i < n; ++i)
        {
            pre[0][pw] = L[i]; pre[1][pw] = R[i];
            const size_t rp = (pw + pre[0].size() - pd) % pre[0].size();
            double in[2] = { pre[0][rp], pre[1][rp] };
            for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k)       // Schroeder all-pass diffusers
            {
                auto& b = apBuf[c][k]; const size_t w = apW[c][k]; const double d = b[w], y = -in[c] * 0.6 + d; b[w] = (float) (in[c] + d * 0.6); in[c] = y;
                apW[c][k] = (w + 1) % b.size();
            }
            pw = (pw + 1) % pre[0].size();
            double o[4]; for (int k = 0; k < 4; ++k) o[k] = line[k][wp[k]];
            double m[4] = { (o[0] + o[1] + o[2] + o[3]) * 0.5, (o[0] - o[1] + o[2] - o[3]) * 0.5, (o[0] + o[1] - o[2] - o[3]) * 0.5, (o[0] - o[1] - o[2] + o[3]) * 0.5 };
            for (int k = 0; k < 4; ++k)
            {
                lp[k] += (m[k] * g[k] - lp[k]) * dmp;
                line[k][wp[k]] = (float) (lp[k] + in[k < 2 ? 0 : 1] * 0.5);
                wp[k] = (wp[k] + 1) % line[k].size();
            }
            const double wl = (o[0] - o[1] + o[2] * 0.7 - o[3] * 0.7) * 0.6, wr = (o[1] - o[0] + o[3] * 0.7 - o[2] * 0.7) * 0.6;
            L[i] = (float) (L[i] * (1 - mix) + wl * mix); R[i] = (float) (R[i] * (1 - mix) + wr * mix);
        }
    }
private:
    size_t len[4] {}; std::vector<float> line[4]; size_t wp[4] {}; double lp[4] {};
    std::vector<float> apBuf[2][2]; size_t apLen[2][2] {}, apW[2][2] {}; std::vector<float> pre[2]; size_t pw = 0;
};

// ---- chorus and phaser and tremolo ---------------------------------------------------------------------------------------------
class ChorusFx : public Fx
{
public:
    void prepare (double s) override { sr = s; for (auto& b : buf) b.assign ((size_t) (sr * 0.06) + 4, 0.0f); w = 0; }
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double inc = 2.0 * kPiD * p.v[0] / sr, depth = p.v[1] * 0.006 * sr, base = 0.014 * sr, mix = p.v[2]; const size_t sz = buf[0].size();
        for (int i = 0; i < n; ++i)
        {
            buf[0][w] = L[i]; buf[1][w] = R[i];
            double wet[2];
            for (int c = 0; c < 2; ++c)
            {
                const double d = base + depth * std::sin (ph + c * 1.5708), rp = (double) w + (double) sz - d; const size_t i0 = (size_t) rp % sz, i1 = (i0 + 1) % sz; const double f = rp - std::floor (rp);
                wet[c] = buf[c][i0] * (1 - f) + buf[c][i1] * f;
            }
            w = (w + 1) % sz; ph += inc; if (ph > 2 * kPiD) ph -= 2 * kPiD;
            L[i] = (float) (L[i] * (1 - mix * 0.5) + wet[0] * mix); R[i] = (float) (R[i] * (1 - mix * 0.5) + wet[1] * mix);
        }
    }
private:
    std::vector<float> buf[2]; size_t w = 0; double ph = 0;
};

class PhaserFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double inc = 2.0 * kPiD * p.v[0] / sr, depth = p.v[1], mix = p.v[2];
        for (int i = 0; i < n; ++i)
        {
            const double f = 300.0 * std::pow (2.0 / 0.3 * 8.0, 0.5 * (1.0 + std::sin (ph)) * depth) * 0.5 + 100.0, k = std::tan (kPiD * std::min (f, sr * 0.45) / sr), a = (1 - k) / (1 + k);
            ph += inc; if (ph > 2 * kPiD) ph -= 2 * kPiD;
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                double x = io[c][i] + fbv[c] * 0.4, y = x;
                for (int s = 0; s < 4; ++s) { const double o = -a * y + z[c][s]; z[c][s] = a * o + y; y = o; }
                fbv[c] = y; io[c][i] = (float) (io[c][i] * (1 - mix * 0.5) + y * mix * 0.7);
            }
        }
    }
private:
    double ph = 0, z[2][4] {}, fbv[2] {};
};

class TremFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        const double inc = 2.0 * kPiD * p.v[0] / sr, d = p.v[1];
        for (int i = 0; i < n; ++i) { const double g = 1.0 - d * 0.5 * (1.0 + std::sin (ph)); ph += inc; if (ph > 2 * kPiD) ph -= 2 * kPiD; L[i] = (float) (L[i] * g); R[i] = (float) (R[i] * g); }
    }
private:
    double ph = 0;
};

class DistFx : public Fx
{
public:
    void process (float* L, float* R, int n, const FxSpec& p) override
    {
        if (changed (p)) lp.set (Biquad::LP, sr, p.v[1], 0.7071);
        const double dr = dbToLin (p.v[0]), mix = p.v[2], comp = 1.0 / std::sqrt (std::max (1.0, dr));
        for (int i = 0; i < n; ++i)
        {
            const double l = lp.run (0, std::tanh (L[i] * dr) * comp), r = lp.run (1, std::tanh (R[i] * dr) * comp);
            L[i] = (float) (L[i] * (1 - mix) + l * mix); R[i] = (float) (R[i] * (1 - mix) + r * mix);
        }
    }
private:
    Biquad lp;
};

inline std::shared_ptr<Fx> makeFx (FxType t, double sr)
{
    std::shared_ptr<Fx> f;
    switch (t)
    {
        case FxType::Eq: f = std::make_shared<EqFx>(); break; case FxType::Comp: f = std::make_shared<CompFx>(); break; case FxType::Delay: f = std::make_shared<DelayFx>(); break;
        case FxType::Limiter: f = std::make_shared<LimiterFx>(); break; case FxType::Filter: f = std::make_shared<FilterFx>(); break; case FxType::Reverb: f = std::make_shared<ReverbFx>(); break;
        case FxType::Chorus: f = std::make_shared<ChorusFx>(); break; case FxType::Phaser: f = std::make_shared<PhaserFx>(); break; case FxType::Trem: f = std::make_shared<TremFx>(); break;
        case FxType::Dist: f = std::make_shared<DistFx>(); break; default: return nullptr;
    }
    f->prepare (sr);
    return f;
}
}
