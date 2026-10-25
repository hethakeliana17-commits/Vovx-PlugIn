# Vocal Tune: pitch correction + vocal preamp (VST3)

An auto-tune style pitch corrector with a built-in vocal preamp, built with [JUCE](https://juce.com).
GitHub builds it for you, so you do not need to install a compiler. **This is the flat version:** every file sits in one
folder, so nothing is lost when you upload through the browser.

## What it does

**Pitch correction**
- Detects the singer's pitch (YIN algorithm) and moves it to the nearest note of your **key and scale**
  (Chromatic, Major, Minor, Major/Minor Pentatonic, Dorian).
- **Retune Speed:** *Instant* gives the hard "robot pop" effect; 60 to 150 ms sounds natural; 300+ ms is very gentle.
- **Amount:** how far toward the note it goes (100% = fully on pitch, 50% = halfway).
- **Tuning A4:** for songs not tuned to 440 Hz. **Voice:** Low, Normal or High sets the detection range.
- A display at the top shows the note you sing, the note it is corrected to, and a flat/sharp meter.
- Built to keep sibilants and breaths natural: where no pitch is found, the audio passes through untouched.

**Vocal preamp** (after the tuner)
Input gain, high-pass filter (rumble and plosives), tube-style drive, Body (low shelf), Presence (3.5 kHz),
Air (high shelf), compressor with automatic make-up gain, and Output level.

**Presets:** Natural Tune, Hard Tune (robot pop), Clean Vocal Preamp, Warm Radio, Subtle Polish.
Presets change the sound only. Your Key, Scale, Voice and Tuning settings are left alone.

## Get the plugin (no coding needed)

1. **Make a free account** at <https://github.com>.
2. **Create a repository:** click **+** (top right), then **New repository**. Name it `VocalTune`, choose **Public**
   (public repositories get free build time), and click **Create repository**.
3. **Upload the files:** unzip `VocalTune.zip`. On your repository page click **uploading an existing file** and drag in
   **every file from the unzipped folder**. They are all plain files, so nothing is skipped. Click **Commit changes**.
   You can ignore `build.yml.txt` for now.
4. **Create the build recipe** (GitHub needs this one file in a special folder, so create it by hand):
   - Click **Add file → Create new file**.
   - In the name box type exactly `.github/workflows/build.yml` (typing the slashes creates the folders).
   - Open `build.yml.txt` from the unzipped folder in Notepad, copy everything, and paste it into the big box.
   - Click **Commit changes**.
5. **Wait for the build:** open the **Actions** tab. A run called *Build Vocal Tune* starts by itself and takes about
   10 to 20 minutes. A green tick means success. If you don't see it, click **Build Vocal Tune** on the left, then
   **Run workflow**.
6. **Download:** click the finished run, scroll to **Artifacts** at the bottom, and download `VocalTune-Windows`
   (or `VocalTune-macOS`). Unzip it twice (a zip inside a zip). Inside is `VST3/Vocal Tune.vst3`.

## Install it

**Windows:** copy the `Vocal Tune.vst3` folder into `C:\Program Files\Common Files\VST3\`, then rescan plugins in your DAW.
In FL Studio: *Options → Manage plugins → Find installed plugins*. Windows SmartScreen may warn about the standalone app
because it is not code-signed. Choose *More info → Run anyway*.

**Mac:** copy `Vocal Tune.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (and `Vocal Tune.component` to
`~/Library/Audio/Plug-Ins/Components/` for Logic and GarageBand). Because the files are not signed or notarized, macOS may
block them. Run this once in Terminal, then rescan in your DAW:

```
xattr -cr "$HOME/Library/Audio/Plug-Ins/VST3/Vocal Tune.vst3"
codesign --force --deep -s - "$HOME/Library/Audio/Plug-Ins/VST3/Vocal Tune.vst3"
```

**Linux:** copy `Vocal Tune.vst3` to `~/.vst3/`.

The **Standalone** app lets you try it with a microphone without a DAW (use headphones to avoid feedback).

## Using it well

- **Set the key and scale to match your song.** Pitch correction can only be as musical as the scale you choose.
  If you don't know the key, use *Chromatic* (snaps to the nearest semitone, always safe but less musical).
- Put it **first on the vocal track**, on a single-voice recording. It tunes one voice at a time (no chords or harmonies).
- **Latency:** the plugin delays the audio by about 28 ms to look ahead at the pitch, and reports this to your DAW so
  recorded tracks stay aligned. For live monitoring while recording, monitor the dry mic input and add the plugin
  when mixing, or ask your DAW to use plugin delay compensation.
- Expect natural-sounding results from clean, dry vocals. Reverb, backing music or heavy noise in the recording
  makes pitch detection less reliable.

## What was tested

The sound engine has automatic tests (`render_test.cpp`) that check, on synthetic singing at 44.1, 48 and 96 kHz:
notes 40 to 60 cents off land within a few cents of the target; in-tune notes pass unchanged; vibrato is flattened;
amount, key, scale and the on/off switch work; noise and silence are safe; every preamp stage measures as designed.
On a real sung recording, the median distance from the chosen scale fell from 27 to 11 cents. The finished plugin
file is produced by GitHub's servers, so your first load in a DAW is the real test.
Run the tests yourself: `g++ -O2 -std=c++17 -I . render_test.cpp -o render_test && ./render_test`
(it also writes `demo_before.wav` and `demo_after.wav` so you can listen).

## If the build fails

Click the failed job on the Actions page, open the red step, and copy the last 20 to 30 lines of error text. Paste them
into Claude together with the prompt in `PROMPT.md` and ask for a fix.

## Files

```
CMakeLists.txt              build recipe (downloads JUCE automatically)
VocalEngine.h               the pitch detector, shifter and preamp (no JUCE needed)
PluginProcessor.h/.cpp      parameters, presets, host connection
PluginEditor.h/.cpp         the on-screen display and controls
render_test.cpp             automatic tests (optional)
build.yml.txt               paste into .github/workflows/build.yml on GitHub
PROMPT.md                   prompts for rebuilding or extending this plugin with an AI assistant
```

## License note

JUCE is dual-licensed: free under AGPLv3 (you must share your plugin's source if you distribute it) or under a paid
commercial license if you want to sell it without sharing the source. Check <https://juce.com/juce-8-licence/> before
selling a plugin built with it.

## Known limits

- Monophonic: one singing voice at a time.
- The pitch shifter is built for corrections of up to about a semitone or two. Very large shifts (octaves) will sound
  rough. Formant shifting (changing the voice character) is not included; ask for it with the prompts in `PROMPT.md`.
- Not code-signed or notarized, so Windows and macOS show security warnings.
