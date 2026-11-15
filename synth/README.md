# Loop Synth

A wavetable synthesizer plugin (VST3) from the Loop Studio project.
Two wavetable oscillators with unison, sub oscillator, Moog-style ladder filter, two envelopes, mono/glide mode and a distortion stage.

## Build (no software to install)

1. Upload these files to a GitHub repository (keep the folder structure, including `.github/workflows/build.yml`).
2. Open the **Actions** tab. The build starts by itself (or press **Run workflow**).
3. When the run is green, open it and download the **LoopSynth-Windows** (or **LoopSynth-macOS**) file from **Artifacts**.
4. Windows: copy the folder `Loop Synth.vst3` into `C:\Program Files\Common Files\VST3`. macOS: copy it into `~/Library/Audio/Plug-Ins/VST3`.
5. Rescan plugins in your DAW. `Loop Synth.exe` / `Loop Synth.app` is the same synth as a stand-alone program.

## Sounds
Choose a preset at the top right: Zaag Lead, Raw Screech, Sub Bass, Acid Squelch, Hoover Stack, Pluck.

## macOS note
The plugin is not signed. If macOS refuses to load it, run once in Terminal:
`xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Loop Synth.vst3"`

## Folder layout
```
CMakeLists.txt
Source/        (Dsp.h, Params.h, PluginProcessor.*, PluginEditor.*)
.github/workflows/build.yml
```
