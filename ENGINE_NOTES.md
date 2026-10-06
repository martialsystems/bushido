# SQ-10 engine: how it behaves

Source: `engine/Sq10Module.cpp`. Tests: `tests/test_engine.cpp`. Everything here is pinned by a test unless marked otherwise.

> **The panel layout is provisional.** It was built from published descriptions of the SQ-10, not from a photo. Behaviour below follows those
> descriptions; where they disagree or are silent, the choice made is marked **(assumed)**. Change `panel/build_panel.py` and this module together.

## Signals
Volts in floats. Gates and TRIG outputs are 0 / 5 V. Inputs count as high above 1 V and low again below 0.5 V.

## Controls (parameter id = panel id)
| Id | Meaning |
|---|---|
| `A:1`..`A:12`, `B:1`..`B:12`, `C:1`..`C:12` | Step knobs, 0..1 |
| `CH:RANGE A`, `CH:RANGE B` | 1 V or 5 V span for that channel's CV **(assumed: unipolar 0..1 V / 0..5 V; sources disagree)** |
| `CH:PORTA A`, `CH:PORTA B` | Portamento, 0 = off, up to ~2 s time constant |
| `CH:C MODE` | CV: C is just a third CV. TIME: C also sets gate length for A/B (5 %..95 % of the step) **(assumed switch)** |
| `CLOCK:TEMPO` | Internal clock, 0.5..32 steps per second (exponential) |
| `CLOCK:SOURCE` | INT: internal clock. EXT: steps on rising edges at the CLOCK jack |
| `MODE:MODE` | Panel marks `A` / `A+B` / `ALT`. `A`: play A once, stop. `A+B`: A then B, stop. `ALT`: A, B, A, B... forever |
| `MODE:START/STOP`, `MODE:STEP`, `MODE:RESET` | Buttons (same as the matching INPUTS jacks) |
| `MIXER:LEVEL 1`, `MIXER:LEVEL 2` | Two-input mixer gains, 0..1 |

## Sequencing
- START toggles running. Starting after the end of a sequence (or the very first time) begins at A step 1.
- Each clock tick moves one step. At the end of a row the MODE decides: stop, switch A->B, or alternate.
- STEP (button or jack) moves one step even while stopped, and plays that step's gate.
- RESET (button or jack) goes to A step 1 without stopping. **Sequence length:** patch `TRIG N+1` into `INPUTS:RESET` for an N-step loop.
- C always plays the same step number as whichever of A/B is playing.
- The channel that is playing follows its knob live; the other channel's CV holds its last value.

## Timing details worth knowing
- **Settle (0.6 ms):** a new step's CV and gate wait 0.6 ms. A reset patched from a TRIG jack arrives within that time, so the skipped step
  never reaches the CV or gate outputs (test: "the skipped step 5 never reaches CV A").
- **Patch delay:** a cable that runs "backwards" in module order (including a module patched to itself) arrives up to 16 samples late
  (`PatchGraph::kSubBlock`). Forward cables are sample-accurate.
- Gate length uses the internal clock period, or the time between the last two ticks for EXT / manual stepping.

## Plugin I/O (`plugin/PluginProcessor.cpp`)
- Host audio input L/R is normalled to `MIXER:IN 1` / `IN 2` (1.0 full scale = 5 V); patching a cable into those jacks replaces it.
- Main output = `MIXER:OUT` (5 V = 1.0), on both channels.
- MIDI out: channel A gates -> MIDI channel 1, B -> channel 2, velocity 100. Note = 36 + CV x 12 at the moment the gate opens
  **(assumed: CV treated as volts per octave; MS-series synths use Hz/V)**.
- Buttons are not host-automatable; every knob and switch is.
