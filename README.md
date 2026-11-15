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
