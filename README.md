# Loop Studio

A DAW for hardcore, hardstyle, uptempo, frenchcore and techno, plus a wavetable synth plugin.

| Folder | What it is | Workflow |
|---|---|---|
| `app/` | Loop Studio as an installable app: Windows (.exe installer and portable), macOS (.dmg), Android (.apk) | Build Loop Studio apps |
| `synth/` | Loop Synth: a wavetable synth plugin (VST3, Windows and macOS) | Build Loop Synth |

Open the **Actions** tab, pick a finished run and download the files from **Artifacts**.

## Updating the app
`app/www/index.html` is the whole DAW. Replace it with a new version and push: the apps are rebuilt automatically.

## Install notes
- **Windows:** run `Loop Studio-...-win.exe` (installer) or the portable one. Windows SmartScreen may warn about an unsigned app: choose More info, Run anyway.
- **macOS:** open the .dmg, drag Loop Studio to Applications. The app is unsigned: the first time, right-click it and choose Open.
- **Android:** open the .apk on the phone and allow installing from this source. Play Protect may warn because the app is not from the store.

## Loop Synth (VST3)

Native C++ sound engine (`synth/Source/Dsp.h`, no JUCE inside) wrapped by JUCE for the plugin format and window.

- Two wavetable oscillators, 4 built-in tables plus 4 user tables loaded from `.wav` files (Serum format works: 2048 samples per frame, up to 256 frames; `Load .wav...` in the OSC section). Loaded tables are saved inside the project.
- Table picture with the current position; drag on it to scrub.
- 3 LFOs (sine, triangle, saw, square, sample and hold; free or tempo synced) and an 8-slot modulation matrix (sources: LFO, filter env, amp env, velocity, key, mod wheel, random; destinations: positions, pitch, cutoff, resonance, levels, sub).
- Engine tests without JUCE: `g++ -O2 -std=c++17 synth/test/dsp_test.cpp -o dsp_test && ./dsp_test [table.wav ...]`.

## Loop Key Harmonizer (VST3)

Snaps the notes of any sound (chords and full mixes too) to a scale. Same algorithm as the Key Harmonizer insert inside Loop Studio, ported to C++ (`harmonizer/Source/Harm.h`, no JUCE inside), so the two sound the same.

- Key, scale (12 presets plus a custom scale you build by clicking the piano), Snap, Low cut, Mix, Output, Fast/High quality (latency is reported to the host).
- Live spectrum with the scale's notes marked.
- VST3 and stand-alone program for Windows and macOS (release `harmonizer-latest`).
- Engine test without JUCE: `g++ -O2 -std=c++17 harmonizer/test/harm_test.cpp -o harm_test && ./harm_test`.
