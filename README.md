# SQ-10 style step sequencer (JUCE plugin, C++)

A 3 x 12 analog-style step sequencer in the look of the MS-50 project: generated panel, live knobs, and hanging patch cables.
No logo. Builds as **VST3, AU and Standalone** from one CMake project. It is its own plugin: it is not compiled into the MS-50 and does
not host or get hosted by another plugin. Its framework-free code stays reusable (see `EMBEDDING.md`).

> **Layout is provisional.** It follows published descriptions of the SQ-10 (3 x 12 knobs, A/B portamento and range, C row,
> per-step TRIG jacks, three play modes, clock/start/step/reset, two-input mixer), not a photo. To match the real panel, edit the
> column/control lists in `panel/build_panel.py`, regenerate, and rebuild. Assumptions are listed in `ENGINE_NOTES.md`.

## Folders
```
rack/      framework-free: Module contract, PatchGraph (runs modules + cables), CableModel (cable look/feel), HzPerVolt (pitch curve)
engine/    framework-free: Sq10Module (the sequencer)
ui/        JUCE: Layout (reads the panel file), RackPanel (draws a rack, turns knobs), CableLayer (cables over all racks)
plugin/    JUCE: processor + editor
panel/     build_panel.py -> assets/ (panel art and layout file)
assets/    sq10_bg@2x.png (no live parts), sq10_preview.png, sq10_layout.json
tests/     test_engine.cpp, test_cables.cpp (no JUCE needed)
```

## Build
Needs CMake 3.22+, a C++17 compiler, and a JUCE 8 checkout. By default CMake looks for `../JUCE`; otherwise pass `-DJUCE_DIR=...`.
```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ../JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release        # engine + cable tests
```
Outputs land in `build/SQ10_artefacts/Release/` (`VST3/`, `AU/` on macOS, `Standalone/`). Copy the `.vst3` to your plugin folder
(Windows: `C:\Program Files\Common Files\VST3`, macOS: `~/Library/Audio/Plug-Ins/VST3`).
On Linux you also need the usual JUCE packages (ALSA, X11, freetype, fontconfig dev headers).

Regenerating the panel needs Python 3 with `cairosvg numpy scipy pillow` and Liberation Sans: `python panel/build_panel.py`
(prints an overlap check that must end with `overlaps: []`).

## Using it
- **Knobs and switches:** drag up/down (Shift = fine), mouse wheel, double-click = default. Click a switch to step through it.
- **Buttons:** START/STOP, STEP, RESET (the INPUTS jacks do the same from a cable).
- **Cables:** pick a colour top-right. Drag from a jack to another jack. Drag a plug to move it; drop on empty space to unplug.
  Click a jack with cables to choose which to pick up, drag rows to reorder the stack, or add another. Shift-drag adds one. Esc cancels.
  Cables hang below the panel and slide away from the jack, label, or cord under the pointer.
- **MODE:** `A` loops row A (12 steps) on the A jacks. `A+B` is one 24-step sequence on the A jacks (row A, then row B), with the
  B jacks holding. `ALT` plays one row per pass on that row's own jacks, swapping A and B. All three run until START/STOP. START always begins at A step 1; RESET goes to A step 1 and keeps running.
- **C MODE** (toggle): CV = row C is a third CV. TIME = the C knob is gate length for A and B (5-95 % of the step), and CV C stays at 0 V.
- **Lamps:** one lamp per step under the step numbers shows the playing step for all three rows.
- **Sequence length:** patch `TRIG N+1` into `INPUTS > RESET` for an N-step loop.
- **In a DAW:** the plugin outputs the mixer audio and MIDI notes (A gates on MIDI channel 1, B on channel 2). Host audio input feeds
  `MIXER IN 1/2` unless something is patched there. Route its MIDI output to a synth track to hear the sequence. MIDI is a convenience:
  notes follow the Hz/V curve of an MS-series VCO (1 V = A1), see `ENGINE_NOTES.md`.

## What was verified (Linux, JUCE 8.0.4)
- Unit tests: 64 engine checks, 18 cable checks (`ctest`), including `testModeALoops`, `testModeABLoops24`, `testAltSwapsEachPass`,
  `testTrigIntoResetSkipsStep`, `testMidiUsesHzPerVolt`, `testFeedbackDelayIsOneSample`.
- Last change (loop modes, Hz/V MIDI, 1-sample feedback, step lamps, C MODE toggle): VST3 and Standalone rebuilt on Linux with JUCE 8.0.4,
  tests pass, and the Standalone opens under a virtual display with the new panel and a working C MODE toggle. pluginval and audio-device
  playback were not re-run for this change.
- VST3 and Standalone build; **pluginval strictness 5 passes** (editor, state save/restore, automation fuzzing, multi-threading).
- Real UI driven with mouse input under a virtual display: knobs and switches turn, cables patch and stack, the stack chooser
  reorders, label hover moves cables aside, and `TRIG 5 -> RESET` loops steps 1-4 in the running plugin.
- **Not verified:** the AU build and anything on macOS/Windows (needs those machines); behaviour inside specific DAWs.
