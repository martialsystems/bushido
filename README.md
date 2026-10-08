# BUSHIDO

**A 3 x 12 analog step sequencer with real patch cables. Part of the Jidai Collection by Martial Systems.**

BUSHIDO is a step sequencer you play by turning knobs and pulling cables. Three rows of twelve steps send control voltages, gates and per-step triggers, and every jack on the panel can be patched. Loop a 12-step line, chain two rows into 24 steps, or swap rows each pass. Then patch a trigger back into RESET for odd-length loops, or turn row C into per-step gate length. It runs in your DAW as a plugin and sends MIDI, so it can drive any synth you own.

## Highlights

- **Three rows, twelve steps.** Rows A and B carry pitch CV and gates. Row C is a third CV or, in TIME mode, the gate length of each step (5 to 95 percent).
- **Three play modes.** A loops 12 steps. A+B plays one 24-step line. ALT swaps rows A and B on every pass, each on its own jacks.
- **Real patching.** Cables hang, swing and stack on the jacks. Any output can feed several inputs, inputs sum, and feedback loops are allowed.
- **Per-step triggers.** Every step has its own TRIG jack. Patch TRIG 6 into RESET and you have a 5-step loop.
- **Clock your way.** Internal tempo with a draggable BPM readout and 1/8, 1/16 or 1/32 division, or step it from an external clock.
- **Portamento and range** for rows A and B, 1 V or 5 V spans, and a two-input mixer.
- **Patterns.** Two banks of up to 999 patterns. Type to search, save to the next free number. Bank A ships with an INIT pattern; the factory set is being rewritten.
- **MIDI out.** Row A plays on MIDI channel 1 and row B on channel 2, so you can drive any instrument in your DAW.
- **BYPASS** keeps the sequencer running with its gates muted, so you can drop it in and out without losing time.
- **Sharp at any size.** The panel is vector art, so it scales cleanly in your DAW.

## Formats

VST3, AU and Standalone, built from one CMake project. Version 0.1 is a beta: it has been built and tested on Linux. macOS and Windows builds come from the same source.

## Try it in your browser

`web/bushido.html` is a playable BUSHIDO, and `web/rack.html` patches BUSHIDO straight into RONIN, its Jidai Collection partner synth. Both open from disk in any modern browser.

## Quick start

1. Press **START/STOP**.
2. Turn the row A knobs to set the notes. Each lamp under the step numbers lights as its step plays.
3. Patch **OUTPUTS > CV A** and **GATE A** to your synth, or route BUSHIDO's MIDI output to a synth track.
4. Click the **PATTERN** screen to browse and search patterns. Use **SAVE** to keep your own.

Knobs turn when you drag up or down (Shift for fine moves), scroll, or double-click to reset. To patch, drag from one jack to another. Drop a plug on empty space to unplug it, or click a jack to pick from its stack.

## Build from source

You need CMake 3.22+, a C++17 compiler and JUCE 8 (CMake looks for `../JUCE`, or pass `-DJUCE_DIR=...`).

```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ../JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Plugins land in `build/BUSHIDO_artefacts/Release/`. Copy the `.vst3` to your plugin folder: `C:\Program Files\Common Files\VST3` on Windows or `~/Library/Audio/Plug-Ins/VST3` on macOS. On Linux, install the usual JUCE dev packages first (ALSA, X11, freetype, fontconfig).

The full behaviour reference is in `docs/REFERENCE.md`. `docs/EMBEDDING.md` covers reusing the engine and patch graph in another host.

## The Jidai Collection

The Jidai Collection is Martial Systems' line of patchable instruments. Its pieces share one patch format and one cable feel.

- **BUSHIDO**: the step sequencer.
- **RONIN**: a semi-modular synthesizer and effect, built to be sequenced by BUSHIDO.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. See `LICENSE`.

BUSHIDO is inspired by classic Korg gear. Korg is a trademark of its owner. Martial Systems is not affiliated with or endorsed by Korg.
