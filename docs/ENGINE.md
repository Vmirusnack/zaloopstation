# Loop Engine: native audio engine for Loop Studio

Goal: the sound quality and stability of a desktop DAW (Logic Pro, Bitwig) under the existing Loop Studio interface.
The Web Audio engine inside the HTML app stays as the fallback for the browser / artifact.

## Architecture

```
 UI (HTML in Electron, phone app, browser)
      |  JSON lines over local TCP (127.0.0.1:47821)   [commands down, position + meters up]
 LoopEngine  (own process, owns the sound card, audio thread never waits on anything)
      |  shared memory + IPC                              [stage 5]
 plugin host processes (one per third-party VST3, like Bitwig: a crashing plugin does not take the engine down)
```

* Audio thread: no locks, no allocation, double-precision summing, per-sample smoothed faders, constant-power pan (-3 dB centre, Logic default), sample-accurate note timing, output independent of the buffer size.
* The same C++ code (`engine/Source/Core.h`, `synth/Source/Dsp.h`) is used by the engine, the VST3 plugins and the tests.

## Stages

1. **Core (done in this repo)**: transport, loop, instrument tracks (Loop Synth), mixer, live notes, TCP protocol, offline render and tests.
2. **Connect the UI**: Electron starts LoopEngine and sends the project; the arrange view, piano roll, mixer meters and transport show engine data. The Web Audio engine is used when no LoopEngine is running.
3. **Audio**: audio tracks and samples (sample-accurate clips, fades, time stretch), recording, sends and buses, plugin delay compensation, export.
4. **Built-in effects in C++**: EQ, compressor, OTT, reverb, delay, distortion, Key Harmonizer (already in C++), autotune.
5. **Third-party plugins**: VST3 hosting in the engine first, then in separate processes (Bitwig-style isolation with crash recovery).
6. **Automation and Android**: automation lanes in the engine; the same core as a native library on Android (built-in instruments and effects only; Android cannot host VST3).

## Protocol (one JSON object per line)

Commands to the engine: `hello`, `project`, `play`, `stop`, `seek {beats}`, `panic`, `note {track,pitch,vel,on}`, `devices`, `setDevice {name,buffer}`.

`project` carries `{bpm, master, loop:{on,start,end}, tracks:[{id,name,gain,pan,mute,solo,synth:{<Loop Synth parameter id>:value},clips:[{start,len,notes:[[pitch,start,len,vel]]}]}]}`; times are in beats.

From the engine: `hello` (device, sample rate, buffer, output latency), and `state` about 30 times a second: `{playing, pos (beats), master:[l,r], tracks:[peak]}`.

Run: `LoopEngine --port 47821 --buffer 256`.

## Status (stage 3 work)

* Mixer in the engine: inserts per track and bus, sends to buses (post-fader), bus fader and pan, master fader then master inserts (`engine/Source/Core.h`).
* Built-in effects in C++ (`engine/Source/Fx.h`, tests in `engine/test/fx_test.cpp`): Channel EQ, Compressor, Echo, Limiter (look-ahead, hard ceiling), Filter, Reverb, Chorus, Phaser, Tremolo, Distortion.
* Not in C++ yet (the engine skips them and tells the page): Pro EQ, OTT, tuner, Key Harmonizer, ladder filter, compressor styles other than "clean", side-chain.
* Plugin delay compensation is not done: the limiter's look-ahead (1.5 ms) is not compensated yet.

Protocol additions: `project` now also carries `buses:[{id,name,gain,pan,mute,fx}]`, `masterFx:[...]`, and per track `fx:[{type,on,p:{...}}]`, `sends:{busId:level}`. The engine answers `{t:"unsupported",effects:[...]}` when a project uses effects it does not have.
