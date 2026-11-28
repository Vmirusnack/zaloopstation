// g++ -O2 -std=c++17 core_test.cpp -o core_test && ./core_test [out.wav]
#include "../Source/Core.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++fails; } else std::printf("ok:   %s\n", msg); } while (0)

static le::Track synthTrack (int id, int pitch, double start, double len, int notes = 1)
{
    le::Track t; t.id = id; t.name = "t" + std::to_string(id); t.synth.a.uni = 1; t.synth.as = 1.0f; t.synth.cut = 8000;
    le::Clip c; c.start = 0; c.len = 16;
    for (int i = 0; i < notes; ++i) c.notes.push_back ({ start + i * 1.0, len, pitch, 0.9f });
    t.clips.push_back (c); return t;
}
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
static void writeWav (const char* path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = fopen (path, "wb"); if (!f) return; const uint32_t n = (uint32_t) l.size(), data = n * 4, riff = 36 + data, fmt = 16, rate = (uint32_t) sr, br = rate * 4; const uint16_t tag = 1, ch = 2, ba = 4, bits = 16;
    fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f); fwrite (&fmt, 4, 1, f); fwrite (&tag, 2, 1, f); fwrite (&ch, 2, 1, f); fwrite (&rate, 4, 1, f); fwrite (&br, 4, 1, f); fwrite (&ba, 2, 1, f); fwrite (&bits, 2, 1, f); fwrite ("data", 1, 4, f); fwrite (&data, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { int16_t a = (int16_t) std::lround (std::clamp (l[i], -1.f, 1.f) * 32767), b = (int16_t) std::lround (std::clamp (r[i], -1.f, 1.f) * 32767); fwrite (&a, 2, 1, f); fwrite (&b, 2, 1, f); }
    fclose (f);
}

int main (int argc, char** argv)
{
    const double sr = 48000;
    // 1. a note at beat 2 (140 bpm) starts at the right sample, not before
    {
        le::Project p; p.bpm = 120; p.tracks.push_back (synthTrack (1, 57, 2.0, 1.0));
        std::vector<float> L, R; le::renderOffline (p, sr, 256, 3.0, L, R);
        const size_t at = (size_t) (2.0 * 60.0 / 120.0 * sr);     // 48000
        CHECK (rms (L, 0, at - 64) < 1e-6, "silence before the note");
        CHECK (rms (L, at + 200, at + 4000) > 0.01, "note sounds from beat 2");
        size_t first = 0; while (first < L.size() && std::fabs (L[first]) < 1e-5) ++first;
        std::printf ("      first audible sample %zu (note at %zu)\n", first, at);
        CHECK (first >= at && first < at + 64, "note starts within a few samples of its time");
        CHECK (rms (L, at + 36000, at + 40000) < rms (L, at + 200, at + 4000) * 0.2, "note ends after its length (release tail)");
    }
    // 2. same result for different block sizes (engine must not depend on the audio buffer size)
    {
        le::Project p; p.bpm = 140; p.loop = true; p.loopStart = 0; p.loopEnd = 4;
        p.tracks.push_back (synthTrack (1, 45, 0.0, 0.5, 4)); p.tracks.push_back (synthTrack (2, 69, 0.5, 0.25, 3)); p.tracks[1].pan = -0.4f;
        std::vector<float> A, Ar, B, Br; le::renderOffline (p, sr, 64, 4.0, A, Ar); le::renderOffline (p, sr, 480, 4.0, B, Br);
        double md = 0, mx = 0; for (size_t i = 0; i < A.size(); ++i) { md = std::max (md, (double) std::fabs (A[i] - B[i])); mx = std::max (mx, (double) std::fabs (A[i])); }
        std::printf ("      max difference %.2e of peak %.3f\n", md, mx);
        CHECK (mx > 0.05 && md < 1e-3, "64-sample and 480-sample blocks sound the same");
    }
    // 3. looping: the pattern repeats and nothing sticks
    {
        le::Project p; p.bpm = 120; p.loop = true; p.loopStart = 0; p.loopEnd = 2; p.tracks.push_back (synthTrack (1, 60, 0.0, 0.5));
        std::vector<float> L, R; le::renderOffline (p, sr, 333, 6.0, L, R);
        const size_t lp = (size_t) (1.0 * sr);   // 2 beats at 120 bpm = 1 s
        CHECK (rms (L, 100, 3000) > 0.01 && rms (L, lp + 100, lp + 3000) > 0.01 && rms (L, 4 * lp + 100, 4 * lp + 3000) > 0.01, "note retriggers every loop");
        CHECK (rms (L, lp / 2 + 20000, lp - 2000) < 0.01, "silence between notes in the loop");
    }
    // 4. mute, solo, pan law
    {
        le::Project p; p.bpm = 120; p.tracks.push_back (synthTrack (1, 57, 0.0, 2.0)); p.tracks.push_back (synthTrack (2, 64, 0.0, 2.0));
        std::vector<float> L, R; le::renderOffline (p, sr, 256, 1.0, L, R); const double both = rms (L, 24000, 40000);
        p.tracks[0].mute = true; le::renderOffline (p, sr, 256, 1.0, L, R); const double one = rms (L, 24000, 40000);
        CHECK (one > 0.01 && one < both, "mute removes a track");
        p.tracks[0].mute = false; p.tracks[1].solo = true; le::renderOffline (p, sr, 256, 1.0, L, R); const double solo = rms (L, 24000, 40000);
        CHECK (std::fabs (solo - one) < 0.3 * one + 1e-4, "solo plays only that track");
        p.tracks[1].solo = false; p.tracks.resize (1); p.tracks[0].pan = 1.0f; le::renderOffline (p, sr, 256, 1.0, L, R);
        CHECK (rms (L, 24000, 40000) < 0.02 * rms (R, 24000, 40000), "hard right pan silences the left channel");
        p.tracks[0].pan = 0.0f; le::renderOffline (p, sr, 256, 1.0, L, R); const double c = rms (L, 24000, 40000);
        p.tracks[0].pan = -1.0f; le::renderOffline (p, sr, 256, 1.0, L, R); const double hard = rms (L, 24000, 40000);
        std::printf ("      centre %.4f hard-left %.4f (ratio %.2f, ideal 1.41)\n", c, hard, hard / c);
        CHECK (hard / c > 1.3 && hard / c < 1.5, "constant-power pan: centre is 3 dB below a hard-panned channel");
    }
    // 5. parameters by plugin id, long/odd blocks, 64 tracks
    {
        ls::P sp; CHECK (le::setSynthParam (sp, "a_table", 2) && le::setSynthParam (sp, "cut", 1234) && le::setSynthParam (sp, "l1_sync", 1) && le::setSynthParam (sp, "m3_dst", 6) && ! le::setSynthParam (sp, "nope", 1), "synth parameters are set by plugin id");
        CHECK (sp.a.table == 2 && sp.cut == 1234 && sp.lfo[0].sync && sp.mod[2].dst == 6, "values land in the right fields");
        le::Project p; p.bpm = 160; for (int i = 0; i < 64; ++i) p.tracks.push_back (synthTrack (i + 1, 36 + i % 24, i * 0.25, 0.5, 8));
        std::vector<float> L, R; le::renderOffline (p, sr, 1000, 4.0, L, R);
        bool ok = true; double pk = 0; for (float v : L) { if (! std::isfinite (v)) ok = false; pk = std::max (pk, (double) std::fabs (v)); }
        CHECK (ok && pk > 0.1, "64 tracks render finite audio");
    }
    // 6. project swap while playing keeps notes ringing and does not crash
    {
        le::Renderer r; r.prepare (sr, 256); le::Project p; p.bpm = 120; p.tracks.push_back (synthTrack (1, 57, 0.0, 8.0)); r.setProject (p); r.play();
        std::vector<float> L (256), R (256); double e1 = 0, e2 = 0;
        for (int i = 0; i < 100; ++i) { r.process (L.data(), R.data(), 256); if (i == 99) for (float v : L) e1 += (double) v * v; }
        p.tracks[0].gain = 0.5f; r.setProject (p);
        for (int i = 0; i < 100; ++i) { r.process (L.data(), R.data(), 256); if (i == 99) for (float v : L) e2 += (double) v * v; }
        CHECK (e1 > 0 && e2 > 0 && e2 < e1, "edit while playing: same track keeps playing, new gain applies");
    }
    // 7. delayed start (lines the engine up with the UI's own latency)
    {
        le::Renderer r; r.prepare (sr, 256); le::Project p; p.bpm = 120; p.tracks.push_back (synthTrack (1, 57, 0.0, 2.0)); r.setProject (p); r.play (4800);
        std::vector<float> L (256), R (256), all; for (int i = 0; i < 60; ++i) { r.process (L.data(), R.data(), 256); all.insert (all.end(), L.begin(), L.end()); }
        size_t first = 0; while (first < all.size() && std::fabs (all[first]) < 1e-5) ++first;
        std::printf ("      delayed start: first sample %zu (asked 4800)\n", first);
        CHECK (first >= 4800 && first < 4800 + 64, "play with a delay starts the song that many samples later");
    }
    // 8. mixer: send to a bus, bus fader, insert on a track, master limiter
    {
        auto mk = [&] { le::Project p; p.bpm = 120; p.tracks.push_back (synthTrack (1, 57, 0.0, 1.0)); return p; };
        std::vector<float> L, R;
        auto base = mk(); le::renderOffline (base, sr, 256, 1.5, L, R); const double rBase = rms (L, 2000, 20000);
        // a send at 0.5 to a bus at unity adds half of the track again
        auto p = mk(); le::Bus b; b.id = 7; p.buses.push_back (b); p.tracks[0].sends.push_back ({ 7, 0.5f });
        le::renderOffline (p, sr, 256, 1.5, L, R); CHECK (std::fabs (rms (L, 2000, 20000) / rBase - 1.5) < 0.05, "send to a bus adds the sent level");
        p.buses[0].mute = true; le::renderOffline (p, sr, 256, 1.5, L, R); CHECK (std::fabs (rms (L, 2000, 20000) / rBase - 1.0) < 0.02, "a muted bus adds nothing");
        // a track insert (low-pass 200 Hz) takes the level of a bright note down
        auto q = mk(); le::FxSpec f; f.init (le::FxType::Filter); f.setNum ("cut", 60); q.tracks[0].fx.push_back (f);
        le::renderOffline (q, sr, 256, 1.5, L, R); CHECK (rms (L, 2000, 20000) < rBase * 0.7, "track insert is applied");
        q.tracks[0].fx[0].on = false; le::renderOffline (q, sr, 256, 1.5, L, R); CHECK (std::fabs (rms (L, 2000, 20000) / rBase - 1.0) < 0.001, "a bypassed insert changes nothing");
        // master limiter with a ceiling of -12 dB keeps the output below it even with the fader up
        auto m = mk(); m.master = 4.0f; le::FxSpec lim; lim.init (le::FxType::Limiter); lim.setNum ("ceil", -12); m.masterFx.push_back (lim);
        le::renderOffline (m, sr, 256, 1.5, L, R); double pk = 0; for (float v : L) pk = std::max (pk, (double) std::fabs (v));
        std::printf ("      master limiter peak %.4f (ceiling %.4f)\n", pk, std::pow (10.0, -12.0 / 20.0));
        CHECK (pk <= std::pow (10.0, -12.0 / 20.0) + 1e-5 && pk > 0.1, "master limiter holds the ceiling");
        // same result for different block sizes with effects
        auto e = mk(); le::FxSpec rv; rv.init (le::FxType::Reverb); rv.setNum ("mix", 0.4); e.tracks[0].fx.push_back (rv);
        std::vector<float> A, Ar, B2, Br; le::renderOffline (e, sr, 64, 1.5, A, Ar); le::renderOffline (e, sr, 480, 1.5, B2, Br);
        double md = 0; for (size_t i = 0; i < A.size(); ++i) md = std::max (md, (double) std::fabs (A[i] - B2[i]));
        CHECK (md < 1e-3, "effects give the same result for different block sizes");
    }
    // 9. built-in instruments on tracks: a kick track and a drum track play at their beats, and mix with a Loop Synth track
    {
        le::Project p; p.bpm = 120;
        le::Track k; k.id = 1; k.inst.init (le::InstType::Kick); le::Clip c; c.len = 4; c.notes.push_back ({ 1.0, 0.25, 36, 1.0f }); k.clips.push_back (c);
        le::Track d; d.id = 2; d.inst.init (le::InstType::Drums); le::Clip c2; c2.len = 4; c2.notes.push_back ({ 2.0, 0.25, 38, 1.0f }); d.clips.push_back (c2);
        p.tracks = { k, d, synthTrack (3, 57, 3.0, 0.5) };
        std::vector<float> L, R; le::renderOffline (p, sr, 256, 3.0, L, R);
        CHECK (rms (L, 0, 20000) < 1e-6, "instrument tracks are silent before their first note");
        CHECK (rms (L, 24000, 28000) > 0.02, "kick track sounds at beat 1");
        CHECK (rms (L, 48000, 51000) > 0.01, "drum track sounds at beat 2");
        CHECK (rms (L, 72000, 76000) > 0.005, "synth track mixes with instrument tracks");
        std::vector<float> A, Ar, B, Br; le::renderOffline (p, sr, 64, 3.0, A, Ar); le::renderOffline (p, sr, 480, 3.0, B, Br);
        double md = 0; for (size_t i = 0; i < A.size(); ++i) md = std::max (md, (double) std::fabs (A[i] - B[i]));
        CHECK (md < 1e-3, "instrument tracks give the same result for different block sizes");
    }
    if (argc > 1)
    {
        le::Project p; p.bpm = 150; p.loop = true; p.loopEnd = 4; auto b = synthTrack (1, 38, 0.0, 0.4, 4); b.synth.a.table = 1; b.synth.cut = 600; b.gain = 0.9f;
        auto l = synthTrack (2, 69, 0.5, 0.2, 3); l.pan = 0.3f; l.synth.a.table = 2; l.synth.a.uni = 5; l.synth.a.det = 0.4f; l.gain = 0.5f; p.tracks = { b, l };
        std::vector<float> L, R; le::renderOffline (p, sr, 256, 6.0, L, R); writeWav (argv[1], L, R, (int) sr); std::printf ("wrote %s\n", argv[1]);
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
