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
| `CH:RANGE A`, `CH:RANGE B` | 1 V or 5 V span for that channel's CV, A and B only **(assumed: unipolar 0..1 V / 0..5 V until a real SQ-10 is metered)** |
| `CH:PORTA A`, `CH:PORTA B` | Portamento on A and B only, 0 = off, up to ~2 s time constant |
| `CH:C MODE` | Two-position toggle. CV: row C is a third CV (0..5 V) and does not set gate length (gates are 50 % of the step). TIME: the C knob of the playing step is the gate length for A and B, 5 %..95 % of the step, and `OUTPUTS:CV C` stays at 0 V |
| `CLOCK:TEMPO` | Internal clock, 0.5..32 steps per second (exponential). `CLOCK:TEMPO CV` adds 1 octave of rate per volt. Both act in INT only |
| `CLOCK:SOURCE` | INT: internal clock. EXT: steps on rising edges at the CLOCK jack; TEMPO and TEMPO CV are ignored |
| `CLOCK:DIV` | 1/8, 1/16, 1/32 = 2, 4, 8 steps per beat. Display unit only: the BPM readout next to TEMPO shows the same TEMPO parameter as BPM = steps per second x 60 / steps per beat (120 BPM at 1/16 = 8 steps/s). Dragging the readout moves TEMPO; changing DIV never changes the clock. TEMPO CV still bends around TEMPO, and the readout shows the knob's tempo, not the bent one. There is no BPM jack: the CLOCK jack takes pulses in EXT |
| `MODE:MODE` | Every mode loops until Stop. `A`: 12-step loop of row A on CV A / GATE A; the B jacks hold. `A+B`: the long sequence, one 24-step loop on CV A / GATE A (steps 1-12 = row A, 13-24 = row B, then A1); CV B holds its last value and GATE B stays low. `ALT`: one row per pass, each on its own jacks (row A on the A jacks, then row B on the B jacks, then A again). The panel legend under the switch reads `A · LOOP 12`, `A+B · LOOP 24`, `ALT · SWAP A/B` |
| `MODE:START/STOP`, `MODE:STEP`, `MODE:RESET` | Buttons (same as the matching INPUTS jacks) |
| `MIXER:LEVEL 1`, `MIXER:LEVEL 2` | Two-input mixer gains, 0..1 |

## Sequencing
- START toggles running. Every start, including one after a stop, begins at A step 1.
- Each clock tick moves one step. After step 12 the MODE decides the next row (A: A again; A+B and ALT: the other row). Nothing stops
  the sequence except START/STOP. A+B and ALT read the rows in the same order but differ in where they play: A+B puts both rows on
  the A jacks, ALT keeps each row on its own jacks. RANGE and PORTA belong to the jacks, so row B in A+B uses RANGE A and PORTA A.
- Switching to `A` while row B is playing carries on at the same step of row A.
- STEP (button or jack) moves one step even while stopped, and plays that step's gate.
- RESET (button or jack) goes to A step 1 without stopping, and restarts the internal clock so A1 gets a full step.
  **Sequence length:** patch `TRIG N+1` into `INPUTS:RESET` for an N-step loop. RESET means "back to A1" in every mode, so in ALT a
  TRIG-into-RESET loop stays on row A (A1..AN, A1..AN); it does not restart or advance the A/B swap.
- C always plays the same step number as whichever of A/B is playing.
- The channel that is playing follows its knob live; the other channel's CV holds its last value.
- One lamp per step (`STEP:1`..`STEP:12`, under the step numbers) lights for the current step, whichever row is playing.
  The A and B lamps in the CH column show which row that is. `MODE:RUN`, the red lamp under START/STOP, is lit while running.

## Timing details worth knowing
- **Settle (0.6 ms):** a new step's CV and gate wait 0.6 ms. A reset patched from a TRIG jack arrives within that time, so the skipped step
  never reaches the CV or gate outputs (test: "the skipped step 5 never reaches CV A").
- **Patch delay:** forward cables are sample-accurate. A cable that closes a feedback loop, including a module patched to itself
  (every SQ-10 to SQ-10 cable), arrives exactly **1 sample** late (`PatchGraph::kFeedbackDelay`). Only the newest cable in each loop is
  delayed: cables carry an age (when they were patched; moving a plug makes it the newest), the graph takes them oldest first, and a
  cable is delayed only if the older undelayed cables already lead back to its source. Module order does not matter. While any loop is
  patched the graph runs one sample at a time. The settle time is 29 samples at 48 kHz, so a TRIG -> RESET cable lands well inside it.
- Gate length uses the internal clock period, or the time between the last two ticks for EXT / manual stepping.

## Plugin I/O (`plugin/PluginProcessor.cpp`)
- The mixer is an audio utility only. Nothing is normalled through it from CV A or CV B.
- Host audio input L/R is normalled to `MIXER:IN 1` / `IN 2` (1.0 full scale = 5 V); patching a cable into those jacks replaces it.
  These normals belong to this plugin's processor (`PatchGraph::setNormal`), not to `Sq10Module`, so if the module is ever added to
  another graph they are off unless that host sets them.
- Main output = `MIXER:OUT` (5 V = 1.0), on both channels.
- MIDI out is a **convenience, not the patch**: GATE A -> MIDI channel 1, GATE B -> channel 2 (so A+B plays all 24 steps on channel 1), velocity 100, note taken when the gate
  opens. CV A and CV B stay in volts on the jacks. The note uses the **Hz/V curve of a Korg MS-series VCO** (`rack/HzPerVolt.h`):
  frequency is proportional to the voltage, doubling the voltage is one octave, and 1 V = 55 Hz = A1 (MIDI 33). So
  note = 33 + 12 x log2(V), rounded to the nearest semitone and clamped to 0..127; 0 V or below sends no note (a Hz/V VCO is silent
  there). Examples: 0.5 V = A0 (21), 1 V = A1 (33), 1.5 V = E2 (40), 2 V = A2 (45), 5 V = C#4 (61). The old `36 + CV x 12`
  (volts per octave) is gone. Because the knobs are not quantised, a step between semitones is rounded in MIDI but not on the jack.
- Buttons are not host-automatable; every knob and switch is.
