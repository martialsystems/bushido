# BUSHIDO

**A 3 x 12 step sequencer with patch cables on the panel, from the Martial Systems Jidai Collection.**

BUSHIDO is a hands-on step sequencer for your DAW. You build lines by turning knobs, and you change how they play by pulling cables between jacks on the panel. It is made for musicians who like sequencing the analog way: set twelve steps, start the clock, and patch a trigger back into RESET until the loop feels right. In the Jidai rack its control voltages patch straight into the rest of the collection, and in any DAW it sends MIDI to your instruments.

## Features

### Steps and patterns
- **Three rows of twelve steps.** Rows A and B carry pitch CV and a gate. Row C is a third CV, or in TIME mode it sets each step's gate length from 5 to 95 percent.
- **Three play modes.** A loops twelve steps. A+B plays both rows as one 24-step line on the A jacks. ALT plays one row per pass, each on its own jacks.
- **A TRIG jack for every step.** Patch TRIG N+1 into RESET for an N-step loop, or patch any TRIG into STEP, START/STOP or (with SOURCE on EXT) CLOCK. Self-patch from the TRIG jacks only: a GATE patched back into STEP makes the sequence run away, and into CLOCK it stalls.
- **TRIG STEP or PULSE.** In STEP mode each TRIG jack stays high for its whole step. In PULSE mode it fires a 5 ms trigger.
- **Factory bank.** Bank A opens with INIT and 21 original patterns for electronic music: seven acid lines with slides and accents, plus house and rolling basslines, trance and broken arps, techno stabs, plucks and leads. Between them they use every play mode, both pitch laws, odd loop lengths, skipped steps, ratchets and swing from row C, gate-length grooves and HOST sync.
- **Patterns.** Two banks, A and B, of up to 999 patterns each. Click the pattern screen and type to search, or use the arrow keys. Right-click it for a menu of every pattern. SAVE stores the panel, including its cables, as the next number in the lit bank. Saved patterns are shared by every BUSHIDO instance on the computer, and your host sees both banks as one program list.

### Clock
- **INT, EXT or HOST.** The internal clock runs from 0.5 to 32 steps per second, and TEMPO CV bends it by one octave of rate per volt. Drag the BPM readout to set the tempo, and choose 1/8, 1/16 or 1/32 to set how many steps make a beat. EXT follows the CLOCK jack and shows the measured tempo.
- **HOST sync.** In HOST mode BUSHIDO steps on the DAW's grid of 1/8, 1/16 or 1/32 notes, read from the song position, so it never drifts. Each step changes on its exact sample. It is locked to the song position: when the transport starts, loops or jumps, BUSHIDO plays the step that falls there (counting 12 steps in A, 24 in A+B and ALT from the start of the song), and stopping the transport stops the sequence.
- **Clean starts.** Every START (button or jack) begins at A step 1 at once. On an external clock, step 1 is never skipped when START and a clock edge arrive together.
- **SETTLE TIGHT or VINTAGE.** In TIGHT mode a new step's CV and gate wait 2 samples. VINTAGE waits 0.6 ms, the timing of earlier versions.

### Pitch
- **Two pitch laws per row.** V/OCT, the Jidai rack standard, puts C3 at 0 V with one volt per octave. HZ/V LIN puts C3 at 1 V, and doubling the voltage raises the pitch one octave. Both use C3 = 130.81 Hz = MIDI 48. The law sets the note names, QUANT and MIDI. The voltage on the jack stays the same.
- **QUANT.** SEMI snaps each row to the nearest semitone under its law, without ever passing the 5 V rail.
- **RANGE.** Rows A and B each span 1 V or 5 V. Under V/OCT, 1 V covers C3 to C4 and 5 V covers C3 to C8.
- **PORTA.** A glide on rows A and B, with a time constant of up to 2 s. The STEPS tab shows the glide time.

### MIDI out
- GATE A and GATE B play notes on their own MIDI channels, 1 and 2 by default. A+B sends all 24 steps on channel A.
- Notes come from each step's target voltage under the row's pitch law, so a glide never changes the note. Note-ons and note-offs land on the exact sample where the gate opens and closes.
- Velocity is a fixed 100, or comes from row C (FROM C) when row C is in CV mode.

### Tabs
The front panel stays exactly as drawn. A strip above it opens five tabs:
- **MAIN:** the panel itself.
- **STEPS:** all 36 steps as volts with note names. Drag a cell, or type `2.5`, `2.5V`, `C4`, `F#3` or `Bb2`. Each row has COPY, PASTE, RAND and CLEAR, plus its pitch law and QUANT.
- **CLOCK:** EXT source (JACK or HOST), SETTLE, TRIG mode and the measured EXT tempo.
- **MIDI:** channel and velocity per row, and a monitor of the notes playing.
- **SETUP:** UI scale (75 to 200 percent) and the report from the last loaded session.

Every control that picks from a list (panel switches and rockers, tab settings) works the same way: click for the next setting, Shift-click for the previous one, right-click for the whole list with the current one ticked.

### Patching
- Drag from any jack to another. Cables hang, swing and stack, and you can click a jack to pick or reorder its plugs.
- Outputs fan out to several inputs, and inputs sum. Feedback loops are allowed: the newest cable in each loop is delayed by exactly one sample, and every other cable is sample-accurate.
- A two-input mixer is built in. The host's audio feeds its inputs until you patch them, and its output is the plugin's audio out.
- **BYPASS** keeps the sequencer stepping and in time while host audio passes through dry and MIDI notes stop.
- The panel is vector art and stays sharp at every size from 960 to 2560 pixels wide.

### Sessions that keep working
- Sessions and patterns saved by earlier versions are migrated on load. They keep their knobs, cables and clock source, and their timing: VINTAGE settle and STEP triggers. Each row's pitch law is set from its cables: HZ/V LIN for a row patched into RONIN's linear `VCO:HZ/V` input, V/OCT otherwise. SETUP lists what changed.
- A session from a newer version loads read-only and is saved back unchanged.

### In the Jidai rack
BUSHIDO is a device in JIDAI RACK, the Jidai Collection's rack. There it shares one patch graph and one set of cables with RONIN, the collection's semi-modular synth, and with the other Jidai devices. Patch CV A into RONIN's `VCO:V/OCT` for the rack standard or its `VCO:HZ/V` for the linear law. RONIN's outputs can also patch back into BUSHIDO's CLOCK, TEMPO CV, START/STOP, STEP, RESET and mixer inputs.

### In your browser
- [`web/bushido.html`](web/bushido.html) is a playable BUSHIDO with two built-in voices. Its sequencer matches the plugin's engine sample for sample.
- [`web/rack.html`](web/rack.html) puts any number of BUSHIDOs and RONINs in one rack with one set of cables.

Both pages open straight from disk in a current desktop browser, with no install. Saved patterns stay in that browser.

## Formats and compatibility

| | |
|---|---|
| Plugin formats | VST3 and Standalone, plus AU on macOS |
| Plugin type | Audio effect with MIDI output (manufacturer code `Mrsy`, plugin code `Bshd`) |
| Audio | Stereo or mono output, with optional stereo or mono input |
| Platforms | Built and tested on Linux. The same CMake project targets macOS and Windows. |
| Sample rate | Runs at the host's rate. Timings are set in seconds (glide, 5 ms PULSE, 0.6 ms VINTAGE settle, 10 ms mixer smoothing), so they hold at any rate. TIGHT settle is 2 samples. The engine is tested at 44.1, 48 and 96 kHz. |
| Version | 0.1.0 |

To play another instrument from BUSHIDO's MIDI, your host has to route a plugin's MIDI output to that instrument's track.

## Install

Build BUSHIDO from source (below). The finished plugins are in `build/BUSHIDO_artefacts/Release/`:

- **VST3:** copy `VST3/BUSHIDO.vst3` to your VST3 folder. That is `~/.vst3` on Linux, `~/Library/Audio/Plug-Ins/VST3` on macOS, or `C:\Program Files\Common Files\VST3` on Windows.
- **AU (macOS):** copy `AU/BUSHIDO.component` to `~/Library/Audio/Plug-Ins/Components`.
- **Standalone:** run the BUSHIDO app in `Standalone/` directly.

Then rescan plugins in your DAW and add BUSHIDO to a track as an effect.

## Build from source

You need CMake 3.22 or newer, a C++17 compiler and JUCE 8 (tested with 8.0.4). On Linux, install JUCE's Linux dependencies first; JUCE lists them in `docs/Linux Dependencies.md`.

```
git clone https://github.com/martialsystems/bushido.git
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git
cd bushido
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=../JUCE
cmake --build build --config Release
```

`JUCE_DIR` defaults to `../JUCE`, so with the layout above you can leave it out.

### Run the tests

```
ctest --test-dir build -C Release
node web/test_rack_engine.js
node web/test_bushido_redesign.js
node web/test_bushido_parity.js
```

`ctest` runs the engine, cable, sequencer and plugin suites. The Node tests cover the browser version. `test_bushido_parity.js` compiles the C++ engine with g++ (set `CXX` to use another compiler) and checks that the browser engine matches it sample for sample, factory patterns included. After you change the browser engine, or the factory bank (edit and run `python3 panel/build_patterns.py`), rebuild the pages with `python3 web/build_web.py` and `python3 web/build_rack.py`.

## The Jidai Collection

BUSHIDO is one unit of the Martial Systems Jidai Collection, alongside [RONIN](https://github.com/martialsystems/Ronin), [SHOGUN](https://github.com/martialsystems/shogun) and [ORIGAMI](https://github.com/martialsystems/origami).

## Documentation

- [`docs/REFERENCE.md`](docs/REFERENCE.md): every control, mode, timing rule and the saved-state format.
- [`docs/EMBEDDING.md`](docs/EMBEDDING.md): reusing the engine, patch graph and cable model in another host.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. See [`LICENSE`](LICENSE).

BUSHIDO is an original Martial Systems design inspired by classic Korg gear. Korg is a trademark of its owner. Martial Systems LLC is not affiliated with or endorsed by Korg.
