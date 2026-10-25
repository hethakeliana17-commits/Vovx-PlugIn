# Prompts for building and extending this plugin with an AI assistant

Use these with Claude (or any capable coding assistant). Paste the relevant files along with the prompt so the
assistant sees your real code. Keep the project **flat** (all files in one folder) so browser uploads to GitHub work.

---

## 1. Prompt to create this plugin from scratch

> You are an expert audio plugin developer. Create a complete, ready-to-build **JUCE 8 + CMake** project for a
> **vocal pitch-correction and vocal preamp VST3 effect plugin** called "Vocal Tune", plus a **GitHub Actions workflow**
> that builds it for Windows, macOS and Linux so I never have to install a compiler. I am not a programmer, so every
> file must be complete (no "..." placeholders) and compile on the first try.
>
> **Flat layout (important):** put every source file in one folder with no sub-folders, because I upload through the
> GitHub website and it drops folders. The only exception is the workflow, which must be at
> `.github/workflows/build.yml`: give it to me as `build.yml.txt` and tell me to create it with
> "Add file → Create new file" and paste it in.
>
> **Pitch correction**
> - Real-time, mono-voice, works on mono or stereo tracks, any sample rate from 44.1 to 192 kHz.
> - Pitch detection with the YIN algorithm on a down-sampled copy (about 12 kHz) with a hop of about 5 ms, a median
>   filter against octave errors, and a loudness gate. A "Voice" setting (Low / Normal / High) selects the search range.
> - Snap to the nearest note of a chosen Key (C to B) and Scale (Chromatic, Major, Minor, Major Pentatonic, Minor
>   Pentatonic, Dorian), with hysteresis so it doesn't flicker between two notes.
> - Retune Speed (0 ms = instant "hard tune" up to 400 ms), Amount (0 to 100%), Tuning of A4 (430 to 450 Hz),
>   and a Pitch Correction on/off switch.
> - Apply the correction with a **pitch-synchronous two-tap delay-line shifter** whose taps are exactly one pitch
>   period apart (so the crossfade is phase-coherent), using cubic interpolation. Where no pitch is detected the audio
>   must pass through untouched, so consonants and breaths stay natural. Report the latency to the host.
>
> **Vocal preamp (after the tuner)**
> - Input gain, high-pass filter (20 to 300 Hz, "Off" at the bottom), tube-style asymmetric tanh drive with unity
>   small-signal gain, Body (low shelf at 120 Hz), Presence (peak at 3.5 kHz), Air (high shelf at 10 kHz),
>   a one-knob compressor with automatic make-up gain, and an Output level.
>
> **Editor:** dark theme, green accent, a live note display (detected note, corrected-to note, flat/sharp cents meter,
> refreshed at 30 fps from atomics), rotary knobs grouped in labelled sections, readable values ("Instant", "80 Hz",
> "Off", "2.5 dB"), and a preset drop-down. Presets: Natural Tune, Hard Tune (robot pop), Clean Vocal Preamp,
> Warm Radio, Subtle Polish. Presets must not change Key, Scale, Voice or Tuning.
>
> **Code requirements:** put all DSP in a header (`VocalEngine.h`) with **no JUCE dependency**. Use
> `AudioProcessorValueTreeState` for all parameters. On the audio thread: no memory allocation, no locks, no string
> lookups. Write `render_test.cpp` (JUCE-free) that generates synthetic singing and verifies: in-tune notes pass
> unchanged, notes 40 to 60 cents off are corrected to within 5 cents, vibrato is flattened, amount/key/scale/off work,
> noise and silence are safe, the preamp bands measure as designed, and CPU use is far below real time.
>
> **Files to output** (each in its own code block with its path as a heading): `CMakeLists.txt` (fetch JUCE 8.0.12;
> VST3 + Standalone, plus AU on macOS; universal binaries on macOS), `VocalEngine.h`, `PluginProcessor.h/.cpp`,
> `PluginEditor.h/.cpp`, `render_test.cpp`, `build.yml.txt`, `.gitignore`, `README.md` (beginner click-by-click steps
> to upload to GitHub, download the plugin, and install it in a DAW).
>
> Finish by listing anything you could not verify, and what error output I should paste back if the build fails.

---

## 2. Prompt to fix a failed build

> My GitHub Actions build of the attached JUCE plugin failed. Here is the complete error log from the red step:
>
> ```
> (paste the log here)
> ```
>
> Explain the cause in one or two sentences, then give me the **complete corrected file(s)**, not just snippets, and
> tell me exactly which files in my repository to replace. Keep the project flat.

## 3. Prompt to change the sound or add features

Start with this, then add one request from the list below:

> Here is my working JUCE plugin (files attached). Make the change below **without breaking anything else**. Keep the
> DSP in `VocalEngine.h` free of JUCE, keep the audio thread free of allocations and locks, add any new parameters
> with unique IDs in `createLayout()` and in the editor, keep the project flat, and give me complete updated files plus
> new tests in `render_test.cpp` for the new feature.

Ideas to ask for:

- "Add a **Formant** control (-6 to +6 semitones) so I can make the voice sound more feminine or masculine without
  changing the pitch, and an **Octave / Transpose** control."
- "Let me **play the target notes from a MIDI keyboard** (MIDI input on the plugin), tuning the voice to whatever notes
  I hold, like a vocoder-style harmonizer."
- "Add **automatic key detection** that listens to the vocal and suggests the key and scale."
- "Add a **Low-latency mode** switch (about 10 ms) that trades some accuracy for speed, for live monitoring."
- "Add a **de-esser** before the compressor, with a frequency and amount control."
- "Add **2x oversampling** around the drive stage to reduce aliasing."
- "Add a **dry/wet Mix** knob and a **Humanize** control that keeps part of the natural vibrato."
- "Add a **graph** in the editor showing the detected pitch over the last 5 seconds against the notes of the scale."
- "Add a **reverb or delay** send at the end of the chain."

## 4. Prompt to use this project as a template for a different effect

> Using this project as a template (attached), turn it into a **[describe: harmonizer / vocoder / de-esser / reverb /
> amp simulator]** with these features: **[list them]**. Keep the same flat layout, CMake setup and GitHub workflow,
> change the plugin name and codes in `CMakeLists.txt` (give it a new `PLUGIN_CODE`), and give me every changed file in full.
