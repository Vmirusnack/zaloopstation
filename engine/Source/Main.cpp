// LoopEngine: the audio engine of Loop Studio as a separate program.
// It owns the sound card, plays the project it is sent and reports position and levels.
// The user interface (Electron / phone / browser) talks to it over a local TCP socket with one JSON message per line.
#include <juce_audio_devices/juce_audio_devices.h>
#include "Core.h"

namespace
{
constexpr const char* kVersion = "0.1.0";

le::Project projectFromJson (const juce::var& v)
{
    le::Project p;
    p.bpm = (double) v.getProperty ("bpm", 140.0);
    p.master = (float) (double) v.getProperty ("master", 1.0);
    if (auto* lo = v.getProperty ("loop", {}).getDynamicObject())
    { p.loop = (bool) lo->getProperty ("on"); p.loopStart = (double) lo->getProperty ("start"); p.loopEnd = (double) lo->getProperty ("end"); }
    if (auto* arr = v.getProperty ("tracks", {}).getArray())
        for (const auto& tv : *arr)
        {
            le::Track t;
            t.id = (int) tv.getProperty ("id", 0); t.name = tv.getProperty ("name", "").toString().toStdString();
            t.gain = (float) (double) tv.getProperty ("gain", 1.0); t.pan = (float) (double) tv.getProperty ("pan", 0.0);
            t.mute = (bool) tv.getProperty ("mute", false); t.solo = (bool) tv.getProperty ("solo", false);
            if (auto* so = tv.getProperty ("synth", {}).getDynamicObject())
                for (const auto& kv : so->getProperties()) le::setSynthParam (t.synth, kv.name.toString().toStdString(), (float) (double) kv.value);
            if (auto* clips = tv.getProperty ("clips", {}).getArray())
                for (const auto& cv : *clips)
                {
                    le::Clip c; c.start = (double) cv.getProperty ("start", 0.0); c.len = (double) cv.getProperty ("len", 4.0);
                    if (auto* notes = cv.getProperty ("notes", {}).getArray())
                        for (const auto& nv : *notes)
                            if (auto* a = nv.getArray(); a != nullptr && a->size() >= 3)
                                c.notes.push_back ({ (double) (*a)[1], (double) (*a)[2], (int) (*a)[0], a->size() > 3 ? (float) (double) (*a)[3] : 0.8f });
                    t.clips.push_back (std::move (c));
                }
            p.tracks.push_back (std::move (t));
        }
    return p;
}

juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv)
{
    auto* o = new juce::DynamicObject();
    for (auto& e : kv) o->setProperty (e.first, e.second);
    return juce::var (o);
}
}

class LoopEngineApp : public juce::JUCEApplicationBase, private juce::AudioIODeviceCallback
{
public:
    const juce::String getApplicationName() override { return "LoopEngine"; }
    const juce::String getApplicationVersion() override { return kVersion; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override {}
    void systemRequestedQuit() override { quit(); }
    void suspended() override {}
    void resumed() override {}
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String& cmdLine) override
    {
        int port = 47821, buffer = 256;
        auto args = juce::StringArray::fromTokens (cmdLine, true);
        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == "--port") port = args[i + 1].getIntValue();
            if (args[i] == "--buffer") buffer = args[i + 1].getIntValue();
        }
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = buffer; dm.setAudioDeviceSetup (setup, false);
        const auto err = dm.initialiseWithDefaultDevices (0, 2);
        if (err.isNotEmpty()) std::fprintf (stderr, "audio device: %s\n", err.toRawUTF8());
        auto s = dm.getAudioDeviceSetup(); s.bufferSize = buffer; dm.setAudioDeviceSetup (s, true);
        dm.addAudioCallback (this);
        server = std::make_unique<Server> (*this, port);
        server->startThread();
        std::printf ("LoopEngine %s listening on 127.0.0.1:%d\n", kVersion, port); std::fflush (stdout);
    }

    void shutdown() override
    {
        if (server) { server->stopServer(); server.reset(); }
        dm.removeAudioCallback (this);
        dm.closeAudioDevice();
    }

    // ---- audio ----
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const int maxBlock = std::max (512, d->getCurrentBufferSizeSamples() * 2);
        renderer.prepare (d->getCurrentSampleRate(), maxBlock);
        const juce::ScopedLock sl (projLock);
        if (haveProject) renderer.setProject (lastProject);
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        if (numOut < 1) return;
        scratch.resize ((size_t) n);
        renderer.process (out[0], numOut > 1 ? out[1] : scratch.data(), n);
        for (int c = 2; c < numOut; ++c) std::memset (out[c], 0, sizeof (float) * (size_t) n);
    }

    void setProject (const juce::var& v)
    {
        auto p = projectFromJson (v);
        nTracks.store ((int) p.tracks.size());
        { const juce::ScopedLock sl (projLock); lastProject = p; haveProject = true; }
        renderer.setProject (std::move (p));
    }

    le::Renderer renderer;
    juce::AudioDeviceManager dm;
    std::atomic<int> nTracks { 0 };

private:
    class Server : public juce::Thread
    {
    public:
        Server (LoopEngineApp& a, int p) : juce::Thread ("engine-server"), app (a), port (p) {}
        void stopServer() { signalThreadShouldExit(); listener.close(); if (client) client->close(); stopThread (2000); }

        void run() override
        {
            if (! listener.createListener (port, "127.0.0.1")) { std::fprintf (stderr, "cannot listen on port %d\n", port); return; }
            while (! threadShouldExit())
            {
                client.reset (listener.waitForNextConnection());
                if (client == nullptr) continue;
                serve();
                client.reset();
            }
        }

    private:
        void send (const juce::var& v) { const auto s = juce::JSON::toString (v, true) + "\n"; client->write (s.toRawUTF8(), (int) s.getNumBytesAsUTF8()); }

        void handle (const juce::var& m)
        {
            const auto cmd = m.getProperty ("cmd", "").toString();
            auto& r = app.renderer;
            if (cmd == "hello")
            {
                auto* d = app.dm.getCurrentAudioDevice();
                send (obj ({ { "t", "hello" }, { "engine", "LoopEngine" }, { "version", kVersion },
                             { "device", d ? d->getName() : juce::String() }, { "sampleRate", d ? d->getCurrentSampleRate() : 0.0 },
                             { "buffer", d ? d->getCurrentBufferSizeSamples() : 0 }, { "latencyMs", d ? 1000.0 * d->getOutputLatencyInSamples() / std::max (1.0, d->getCurrentSampleRate()) : 0.0 } }));
            }
            else if (cmd == "project") app.setProject (m.getProperty ("project", {}));
            else if (cmd == "play") r.play();
            else if (cmd == "stop") r.stop();
            else if (cmd == "panic") r.panic();
            else if (cmd == "seek") r.seekBeats ((double) m.getProperty ("beats", 0.0));
            else if (cmd == "note") r.liveNote ((int) m.getProperty ("track", 0), (int) m.getProperty ("pitch", 60), (float) (double) m.getProperty ("vel", 0.8), (bool) m.getProperty ("on", true));
            else if (cmd == "devices")
            {
                juce::Array<juce::var> list;
                if (auto* t = app.dm.getCurrentDeviceTypeObject()) for (auto& n : t->getDeviceNames (false)) list.add (n);
                send (obj ({ { "t", "devices" }, { "outputs", list } }));
            }
            else if (cmd == "setDevice")
            {
                auto s = app.dm.getAudioDeviceSetup();
                if (m.hasProperty ("name")) s.outputDeviceName = m.getProperty ("name", "").toString();
                if (m.hasProperty ("buffer")) s.bufferSize = (int) m.getProperty ("buffer", 256);
                const auto e = app.dm.setAudioDeviceSetup (s, true);
                send (obj ({ { "t", "setDevice" }, { "error", e } }));
            }
        }

        void serve()
        {
            juce::String pending; char buf[8192]; juce::uint32 lastState = 0;
            while (! threadShouldExit() && client != nullptr && client->isConnected())
            {
                if (client->waitUntilReady (true, 20) == 1)
                {
                    const int n = client->read (buf, sizeof (buf), false);
                    if (n <= 0) break;
                    pending += juce::String::fromUTF8 (buf, n);
                    if (pending.length() > 64 * 1024 * 1024) break;
                    for (int nl; (nl = pending.indexOfChar ('\n')) >= 0;)
                    {
                        const auto line = pending.substring (0, nl); pending = pending.substring (nl + 1);
                        const auto v = juce::JSON::parse (line);
                        if (v.isObject()) handle (v);
                    }
                }
                const auto now = juce::Time::getMillisecondCounter();
                if (now - lastState >= 33)
                {
                    lastState = now;
                    float ml = 0, mr = 0; app.renderer.readMaster (ml, mr);
                    juce::Array<juce::var> tr; for (int i = 0, nt = app.nTracks.load(); i < nt; ++i) tr.add ((double) app.renderer.readTrackPeak (i));
                    send (obj ({ { "t", "state" }, { "playing", app.renderer.playing() }, { "pos", app.renderer.positionBeats() }, { "master", juce::Array<juce::var> { (double) ml, (double) mr } }, { "tracks", tr } }));
                }
            }
        }

        LoopEngineApp& app; int port;
        juce::StreamingSocket listener; std::unique_ptr<juce::StreamingSocket> client;
    };

    std::unique_ptr<Server> server;
    juce::CriticalSection projLock;
    le::Project lastProject; bool haveProject = false;
    std::vector<float> scratch;
};

START_JUCE_APPLICATION (LoopEngineApp)
