// Loop Engine core: transport, sample-accurate note sequencing, instrument tracks, mixer.
// Plain C++17, no JUCE: the same code runs inside the LoopEngine program, in tests and (later) inside plugins.
// Everything that runs on the audio thread (Renderer::process) takes no locks and allocates nothing.
#pragma once
#include "../../synth/Source/Dsp.h"
#include "Fx.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace le
{
struct Note { double start = 0, len = 1; int pitch = 60; float vel = 0.8f; };   // beats, start relative to the clip
struct Clip { double start = 0, len = 4; std::vector<Note> notes; };            // beats
struct Track
{
    int id = 0;
    std::string name;
    ls::P synth;                 // Loop Synth parameters of this track's instrument
    float gain = 1.0f;           // linear fader value
    float pan = 0.0f;            // -1 .. 1
    bool mute = false, solo = false;
    std::vector<Clip> clips;
    std::vector<FxSpec> fx;                          // inserts, before the fader
    std::vector<std::pair<int, float>> sends;        // (bus id, level), after the fader
};
struct Bus { int id = 0; std::string name; float gain = 1.0f, pan = 0.0f; bool mute = false; std::vector<FxSpec> fx; };
struct Project
{
    double bpm = 140.0;
    bool loop = false;
    double loopStart = 0.0, loopEnd = 16.0;   // beats
    float master = 1.0f;
    std::vector<Track> tracks;
    std::vector<Bus> buses;
    std::vector<FxSpec> masterFx;                    // after the master fader
};

// Loop Synth parameter by its plugin parameter id (same names as in the VST3), e.g. "a_table", "cut", "l1_rate", "m1_amt"
inline bool setSynthParam (ls::P& p, const std::string& id, float v)
{
    auto osc = [&] (ls::OscP& o, const std::string& k) -> bool
    {
        if (k == "table") o.table = (int) v; else if (k == "pos") o.pos = v; else if (k == "oct") o.oct = (int) v; else if (k == "fine") o.fine = v;
        else if (k == "level") o.level = v; else if (k == "uni") o.uni = (int) v; else if (k == "det") o.det = v; else if (k == "spread") o.spread = v; else return false;
        return true;
    };
    if (id.size() > 2 && id[1] == '_' && id[0] == 'a') return osc (p.a, id.substr (2));
    if (id.size() > 2 && id[1] == '_' && id[0] == 'b') return osc (p.b, id.substr (2));
    if (id == "sub") p.sub = v; else if (id == "rnd") p.rndPhase = v; else if (id == "ftype") p.ftype = (int) v; else if (id == "cut") p.cut = v;
    else if (id == "res") p.res = v; else if (id == "fdrive") p.fdrive = v; else if (id == "fenv") p.fenv = v; else if (id == "fkey") p.fkey = v;
    else if (id == "fa") p.fa = v; else if (id == "fd") p.fd = v; else if (id == "fs") p.fs = v; else if (id == "fr") p.fr = v;
    else if (id == "aa") p.aa = v; else if (id == "ad") p.ad = v; else if (id == "as") p.as = v; else if (id == "ar") p.ar = v;
    else if (id == "poslenv") p.posEnv = v; else if (id == "mono") p.mono = v > 0.5f; else if (id == "glide") p.glide = v; else if (id == "bend") p.bend = v;
    else if (id == "vel") p.velSens = v; else if (id == "dtype") p.dtype = (int) v; else if (id == "dist") p.dist = v; else if (id == "master") p.master = v;
    else if (id.size() >= 4 && id[0] == 'l' && id[2] == '_' && id[1] >= '1' && id[1] <= '3')
    {
        auto& l = p.lfo[id[1] - '1']; const std::string k = id.substr (3);
        if (k == "shape") l.shape = (int) v; else if (k == "rate") l.rate = v; else if (k == "sync") l.sync = v > 0.5f; else if (k == "div") l.div = (int) v; else return false;
    }
    else if (id.size() >= 5 && id[0] == 'm' && id[1] >= '1' && id[1] <= '8' && id[2] == '_')
    {
        auto& m = p.mod[id[1] - '1']; const std::string k = id.substr (3);
        if (k == "src") m.src = (int) v; else if (k == "dst") m.dst = (int) v; else if (k == "amt") m.amt = v; else return false;
    }
    else return false;
    return true;
}

class Renderer
{
public:
    static constexpr int kMaxTracks = 256, kMaxBuses = 32;

    Renderer() : tables (std::make_shared<ls::Tables>()) {}

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate; blockMax = std::max (1, maxBlock);
        accL.assign ((size_t) blockMax, 0.0); accR.assign ((size_t) blockMax, 0.0);
        smoothCoef = 1.0 - std::exp (-1.0 / (0.004 * sr));
        std::lock_guard<std::mutex> lk (mtx);
        pool.clear(); busPool.clear(); fxPool.clear(); plan.store (nullptr); plans.clear(); cur = nullptr;
    }

    // ---- message thread ------------------------------------------------------------------------------------------
    // Build and publish a new project. Running notes keep ringing on tracks that keep their id.
    void setProject (Project p)
    {
        std::lock_guard<std::mutex> lk (mtx);
        auto pl = std::make_unique<Plan>();
        pl->proj = std::move (p);
        pl->spb = 60.0 / std::max (20.0, pl->proj.bpm) * sr;
        const int nt = std::min ((int) pl->proj.tracks.size(), kMaxTracks);
        pl->proj.tracks.resize ((size_t) nt);
        pl->ev.resize ((size_t) nt);
        pl->rt.resize ((size_t) nt);
        pl->tfx.resize ((size_t) nt);
        pl->sends.resize ((size_t) nt);
        FxMap nextFx;
        const int nb = std::min ((int) pl->proj.buses.size(), kMaxBuses);
        pl->proj.buses.resize ((size_t) nb);
        pl->brt.resize ((size_t) nb); pl->bfx.resize ((size_t) nb);
        for (int b = 0; b < nb; ++b)
        {
            const auto& bs = pl->proj.buses[(size_t) b];
            auto& br = busPool[bs.id];
            if (! br) { br = std::make_unique<BusRt>(); br->l.assign ((size_t) blockMax, 0.0); br->r.assign ((size_t) blockMax, 0.0); br->fl.assign ((size_t) blockMax, 0.0f); br->fr.assign ((size_t) blockMax, 0.0f); }
            pl->brt[(size_t) b] = br.get();
            pl->bfx[(size_t) b] = fxChain (bs.fx, 2, bs.id, nextFx);
        }
        pl->mfx = fxChain (pl->proj.masterFx, 3, 0, nextFx);
        for (int i = 0; i < nt; ++i)
        {
            const auto& t = pl->proj.tracks[(size_t) i];
            auto& rt = pool[t.id];
            if (! rt) { rt = std::make_unique<TrackRt>(); rt->synth = std::make_unique<ls::Synth> (tables); rt->synth->prepare (sr); rt->sl.assign ((size_t) blockMax, 0.0f); rt->sr_.assign ((size_t) blockMax, 0.0f); }
            pl->rt[(size_t) i] = rt.get();
            pl->tfx[(size_t) i] = fxChain (t.fx, 1, t.id, nextFx);
            for (const auto& sd : t.sends)
                for (int b = 0; b < nb; ++b)
                    if (pl->proj.buses[(size_t) b].id == sd.first && sd.second > 0.0f) pl->sends[(size_t) i].push_back ({ b, sd.second });
            auto& ev = pl->ev[(size_t) i];
            for (const auto& c : t.clips)
                for (const auto& n : c.notes)
                {
                    if (n.start < 0 || n.start >= c.len) continue;
                    const double on = c.start + n.start, off = c.start + std::min (n.start + std::max (0.001, n.len), c.len);
                    ev.push_back ({ (int64_t) std::ceil (on * pl->spb - 1e-6), n.pitch, n.vel, true });
                    ev.push_back ({ (int64_t) std::ceil (off * pl->spb - 1e-6), n.pitch, 0.0f, false });
                }
            std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.sample != b.sample ? a.sample < b.sample : (a.on < b.on); });   // note-off before note-on
        }
        pl->loopStart = (int64_t) std::llround (pl->proj.loopStart * pl->spb);
        pl->loopEnd = std::max (pl->loopStart + 1, (int64_t) std::llround (pl->proj.loopEnd * pl->spb));
        fxPool = std::move (nextFx);
        const Plan* raw = pl.get();
        plans.push_back (std::move (pl));
        plan.store (raw, std::memory_order_release);
        while (plans.size() > 8) plans.erase (plans.begin());      // old plans stay alive long enough for a running block
    }
    void play (int delaySamples = 0) { playDelay.store (std::max (0, delaySamples)); playReq.store (1); }   // delay: lets the UI line the start up with its own audio latency
    void stop() { playReq.store (2); }
    void seekBeats (double beats) { seekReqBeats.store (beats); seekFlag.store (true); }
    // live notes from a MIDI keyboard or the on-screen keys (one producer thread)
    void liveNote (int trackId, int pitch, float vel, bool on)
    {
        const unsigned w = liveW.load (std::memory_order_relaxed);
        if (w - liveR.load (std::memory_order_acquire) >= kLive) return;
        live[w % kLive] = { trackId, pitch, vel, on };
        liveW.store (w + 1, std::memory_order_release);
    }
    void panic() { panicReq.store (true); }
    ls::Tables& tableBank() { return *tables; }       // user wavetables are shared by all synth tracks
    double sampleRateNow() const { return sr; }

    double positionBeats() const { return posBeats.load(); }
    bool playing() const { return isPlaying.load(); }
    // peak since the last call (0..), per track index of the current project; index -1 = master (L/R in a, b)
    void readMaster (float& l, float& r) { l = mPeakL.exchange (0.0f); r = mPeakR.exchange (0.0f); }
    float readTrackPeak (int idx) { return idx >= 0 && idx < kMaxTracks ? tPeak[idx].exchange (0.0f) : 0.0f; }

    // ---- audio thread --------------------------------------------------------------------------------------------
    void process (float* L, float* R, int n)
    {
        n = std::min (n, blockMax);
        std::fill (accL.begin(), accL.begin() + n, 0.0); std::fill (accR.begin(), accR.begin() + n, 0.0);
        const Plan* pl = plan.load (std::memory_order_acquire);
        if (pl != cur)
        {
            if (cur != nullptr && pl != nullptr && cur->spb != pl->spb) pos = (int64_t) std::llround ((double) pos / cur->spb * pl->spb);
            cur = pl; for (auto& s : sm) s = {}; for (auto* b : pl->brt) b->sm = {};
        }
        if (cur == nullptr) { std::memset (L, 0, sizeof (float) * (size_t) n); std::memset (R, 0, sizeof (float) * (size_t) n); return; }
        const Plan& P = *cur;

        if (panicReq.exchange (false)) for (auto* r : P.rt) r->synth->allOff();
        const int pr = playReq.exchange (0);
        if (pr == 1 && ! playingA) { playingA = true; delayLeft = playDelay.load(); }
        else if (pr == 2 && playingA) { playingA = false; delayLeft = 0; for (auto* r : P.rt) r->synth->allOff(); }
        if (seekFlag.exchange (false)) { pos = (int64_t) std::llround (std::max (0.0, seekReqBeats.load()) * P.spb); for (auto* r : P.rt) r->synth->allOff(); }
        isPlaying.store (playingA);

        // live notes arrive at the start of the block
        for (unsigned r = liveR.load (std::memory_order_relaxed), w = liveW.load (std::memory_order_acquire); r != w; ++r)
        {
            const LiveEv& e = live[r % kLive];
            for (size_t i = 0; i < P.rt.size(); ++i)
                if (P.proj.tracks[i].id == e.track) { if (e.on) P.rt[i]->synth->noteOn (e.pitch, e.vel, P.proj.tracks[i].synth); else P.rt[i]->synth->noteOff (e.pitch); }
            liveR.store (r + 1, std::memory_order_release);
        }

        for (auto* b : P.brt) { std::fill (b->l.begin(), b->l.begin() + n, 0.0); std::fill (b->r.begin(), b->r.begin() + n, 0.0); }
        bool anySolo = false; for (const auto& t : P.proj.tracks) anySolo = anySolo || t.solo;

        int done = 0;
        while (done < n)
        {
            int seg = n - done;
            const bool adv = playingA && delayLeft == 0;
            if (playingA && delayLeft > 0) seg = std::min (seg, delayLeft);
            else if (adv && P.proj.loop && pos < P.loopEnd) seg = (int) std::min<int64_t> (seg, P.loopEnd - pos);
            renderSegment (P, anySolo, done, seg, adv);
            if (adv) pos += seg; else if (playingA) delayLeft -= seg;
            done += seg;
            if (adv && P.proj.loop && pos >= P.loopEnd) { pos = P.loopStart; for (auto* r : P.rt) r->synth->allOff(); }
        }

        // buses: inserts, fader and pan, then into the master mix
        for (size_t bi = 0; bi < P.brt.size(); ++bi)
        {
            BusRt& b = *P.brt[bi]; const Bus& bs = P.proj.buses[bi];
            for (int i = 0; i < n; ++i) { b.fl[(size_t) i] = (float) b.l[(size_t) i]; b.fr[(size_t) i] = (float) b.r[(size_t) i]; }
            runChain (P.bfx[bi], bs.fx, b.fl.data(), b.fr.data(), n);
            const double a = (std::clamp ((double) bs.pan, -1.0, 1.0) + 1.0) * ls::kPi * 0.25, tgl = bs.mute ? 0.0 : bs.gain * std::cos (a) * 1.41421356237, tgr = bs.mute ? 0.0 : bs.gain * std::sin (a) * 1.41421356237;
            if (! b.sm.init) { b.sm.gl = tgl; b.sm.gr = tgr; b.sm.init = true; }
            for (int i = 0; i < n; ++i)
            {
                b.sm.gl += (tgl - b.sm.gl) * smoothCoef; b.sm.gr += (tgr - b.sm.gr) * smoothCoef;
                accL[(size_t) i] += b.fl[(size_t) i] * b.sm.gl; accR[(size_t) i] += b.fr[(size_t) i] * b.sm.gr;
            }
        }

        // master fader, then the master inserts (the limiter normally sits last)
        const double mt = P.proj.master; float pl_ = 0, pr_ = 0;
        for (int i = 0; i < n; ++i)
        {
            mGain += (mt - mGain) * smoothCoef;
            L[i] = (float) (accL[(size_t) i] * mGain); R[i] = (float) (accR[(size_t) i] * mGain);
        }
        runChain (P.mfx, P.proj.masterFx, L, R, n);
        for (int i = 0; i < n; ++i) { pl_ = std::max (pl_, std::fabs (L[i])); pr_ = std::max (pr_, std::fabs (R[i])); }
        maxStore (mPeakL, pl_); maxStore (mPeakR, pr_);
        posBeats.store ((double) pos / P.spb);
    }

private:
    struct Ev { int64_t sample; int pitch; float vel; bool on; };
    struct TrackRt { std::unique_ptr<ls::Synth> synth; std::vector<float> sl, sr_; };
    struct Smooth { double gl = 0, gr = 0; bool init = false; };
    struct BusRt { std::vector<double> l, r; std::vector<float> fl, fr; Smooth sm; };
    using FxChain = std::vector<std::shared_ptr<Fx>>;
    struct Plan
    {
        Project proj; double spb = 1; int64_t loopStart = 0, loopEnd = 1;
        std::vector<std::vector<Ev>> ev; std::vector<TrackRt*> rt;
        std::vector<FxChain> tfx, bfx; FxChain mfx; std::vector<BusRt*> brt; std::vector<std::vector<std::pair<int, float>>> sends;
    };
    using FxMap = std::unordered_map<int64_t, std::pair<FxType, std::shared_ptr<Fx>>>;
    // effect instances keep their state across project updates when the type in the same slot stays the same
    FxChain fxChain (const std::vector<FxSpec>& specs, int kind, int id, FxMap& next)
    {
        FxChain out;
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const int64_t key = ((int64_t) kind << 56) ^ ((int64_t) id << 16) ^ (int64_t) i;
            std::shared_ptr<Fx> f;
            auto it = fxPool.find (key);
            if (it != fxPool.end() && it->second.first == specs[i].type) f = it->second.second; else f = makeFx (specs[i].type, sr);
            if (f) next[key] = { specs[i].type, f };
            out.push_back (f);
        }
        return out;
    }
    static void runChain (const FxChain& c, const std::vector<FxSpec>& specs, float* l, float* r, int n)
    {
        for (size_t k = 0; k < c.size() && k < specs.size(); ++k) if (c[k] && specs[k].on) c[k]->process (l, r, n, specs[k]);
    }
    struct LiveEv { int track; int pitch; float vel; bool on; };
    static constexpr unsigned kLive = 256;

    static void maxStore (std::atomic<float>& a, float v) { float o = a.load (std::memory_order_relaxed); if (v > o) a.store (v, std::memory_order_relaxed); }

    void renderSegment (const Plan& P, bool anySolo, int off, int seg, bool adv)
    {
        const int64_t p0 = pos, p1 = pos + seg;
        for (size_t ti = 0; ti < P.rt.size(); ++ti)
        {
            const Track& t = P.proj.tracks[ti];
            TrackRt& rt = *P.rt[ti];
            float* tl = rt.sl.data(); float* tr = rt.sr_.data();
            int c = 0;
            if (adv)
            {
                const auto& ev = P.ev[ti];
                auto it = std::lower_bound (ev.begin(), ev.end(), p0, [] (const Ev& e, int64_t s) { return e.sample < s; });
                for (; it != ev.end() && it->sample < p1; ++it)
                {
                    const int at = (int) (it->sample - p0);
                    if (at > c) { rt.synth->render (tl + c, tr + c, at - c, t.synth); c = at; }
                    if (it->on) rt.synth->noteOn (it->pitch, it->vel, t.synth); else rt.synth->noteOff (it->pitch);
                }
            }
            if (seg > c) rt.synth->render (tl + c, tr + c, seg - c, t.synth);
            runChain (P.tfx[ti], t.fx, tl, tr, seg);

            // fader and pan (constant power, -3 dB at the centre like Logic), smoothed per sample
            const bool audible = ! t.mute && (! anySolo || t.solo);
            const double a = (std::clamp ((double) t.pan, -1.0, 1.0) + 1.0) * ls::kPi * 0.25;
            const double tgl = audible ? t.gain * std::cos (a) * 1.41421356237 * 0.70710678118 : 0.0, tgr = audible ? t.gain * std::sin (a) * 1.41421356237 * 0.70710678118 : 0.0;
            auto& s = sm[ti];
            if (! s.init) { s.gl = tgl; s.gr = tgr; s.init = true; }
            float pk = 0;
            for (int i = 0; i < seg; ++i)
            {
                s.gl += (tgl - s.gl) * smoothCoef; s.gr += (tgr - s.gr) * smoothCoef;
                const double l = tl[i] * s.gl, r = tr[i] * s.gr;
                accL[(size_t) (off + i)] += l; accR[(size_t) (off + i)] += r;
                for (const auto& sd : P.sends[ti]) { auto* b = P.brt[(size_t) sd.first]; b->l[(size_t) (off + i)] += l * sd.second; b->r[(size_t) (off + i)] += r * sd.second; }
                pk = std::max (pk, (float) std::max (std::fabs (l), std::fabs (r)));
            }
            maxStore (tPeak[ti], pk);
        }
    }

    double sr = 48000; int blockMax = 512; double smoothCoef = 0.005;
    std::shared_ptr<ls::Tables> tables;
    std::mutex mtx;
    std::unordered_map<int, std::unique_ptr<TrackRt>> pool;
    std::unordered_map<int, std::unique_ptr<BusRt>> busPool;
    FxMap fxPool;
    std::vector<std::unique_ptr<Plan>> plans;
    std::atomic<const Plan*> plan { nullptr };
    const Plan* cur = nullptr;
    std::vector<double> accL, accR;
    Smooth sm[kMaxTracks];
    double mGain = 1.0;
    // transport
    std::atomic<int> playReq { 0 }, playDelay { 0 }; int delayLeft = 0; std::atomic<bool> seekFlag { false }, panicReq { false }; std::atomic<double> seekReqBeats { 0 };
    std::atomic<bool> isPlaying { false }; std::atomic<double> posBeats { 0 };
    bool playingA = false; int64_t pos = 0;
    // meters
    std::atomic<float> tPeak[kMaxTracks] {}; std::atomic<float> mPeakL { 0 }, mPeakR { 0 };
    // live notes
    LiveEv live[kLive]; std::atomic<unsigned> liveW { 0 }, liveR { 0 };
};

// Render a project offline (no real-time constraints), e.g. for export or tests.
inline void renderOffline (Project p, double sampleRate, int blockSize, double seconds, std::vector<float>& outL, std::vector<float>& outR)
{
    Renderer r; r.prepare (sampleRate, blockSize); r.setProject (std::move (p)); r.play();
    const int total = (int) std::llround (seconds * sampleRate);
    outL.assign ((size_t) total, 0.0f); outR.assign ((size_t) total, 0.0f);
    for (int i = 0; i < total; i += blockSize) { const int k = std::min (blockSize, total - i); r.process (outL.data() + i, outR.data() + i, k); }
}
}
