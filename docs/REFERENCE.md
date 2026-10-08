# BUSHIDO: how it behaves

Source: `engine/BushidoModule.cpp`, `rack/PitchLaw.h`, `engine/BushidoState.h`. Tests: `tests/test_engine.cpp`, `tests/test_redesign.cpp`. Everything here is pinned by a test unless marked otherwise.
Clock, transport and pitch follow the Jidai Cable Standard v1.1 (JCS R3–R7); the design notes are in `BUSHIDO_Redesign.md`.

> The panel and the engine change together: edit `panel/build_panel.py` and `engine/BushidoModule.cpp` as a pair.

## Signals
Volts in floats. Gates and TRIG outputs are 0 / 5 V. Inputs count as high above 1 V and low again below 0.5 V (JCS R3).

## Pitch laws (JCS R4)
The CV jacks are always plain volts: the knob -> volts mapping never changes, so CV A / CV B are bit-identical under either law.
A row's PITCH LAW (STEPS tab) only decides what those volts mean: note names, QUANT, MIDI, and the jack's role (ring and cable colour,
the `≠` badge when a cable expects the other law). Both laws use the same reference, **C3 = 130.81 Hz = MIDI 48** (exactly 440 x 2^(-21/12) = 130.8127826502993 Hz, jidai-common `kC3Hz`).
- **V/OCT** (default for new patches, the rack standard): **0 V = C3**, one volt per octave, note = 48 + 12 x V. RANGE 1 V spans C3..C4,
  RANGE 5 V spans C3..C8. Patch it into SHOGUN `NOTE` or RONIN `VCO:V/OCT`.
- **HZ/V LIN** (shown as LIN): **1 V = C3**, doubling the volts is one octave, note = 48 + 12 x log2(V); 0 V or below has no note.
  RANGE 5 V tops out at note 75.86. Patch it into RONIN's linear `VCO:HZ/V`.
- The old 55 Hz / A1 = MIDI 33 reference (`rack/HzPerVolt.h`) is **retired**; `rack/PitchLaw.h` replaces it everywhere.

| Volts | HZ/V LIN | V/OCT |
|---|---|---|
| 0.5 V | 36 (C2) | 54 |
| 1 V | 48 (C3) | 60 (C4) |
| 2 V | 60 (C4) | 72 |
| 4 V | 72 | 96 |
| 5 V | 76 | 108 |

## Controls (parameter id = panel id)
| Id | Meaning |
|---|---|
| `A:1`..`A:12`, `B:1`..`B:12`, `C:1`..`C:12` | Step knobs, 0..1 |
| `CH:RANGE A`, `CH:RANGE B` | 1 V or 5 V span for that channel's CV, A and B only, unipolar (0..1 V or 0..5 V) |
| `CH:PORTA A`, `CH:PORTA B` | Portamento on A and B only, 0 = off. One-pole slew in volts with τ = PORTA² x 2 s: **τ up to 2 s (99 % in 9.2 s)**. The law is kept bit-exact from v1. The STEPS tab shows the glide time as `τ = 0.50 s · 99 % in 2.30 s` (t99 = ln(100) x τ = 4.605 τ). Linear in volts, so it is a linear pitch glide on a V/OCT row and an exponential one on a LIN row |
| `CH:C MODE` | Two-position toggle. CV: row C is a third CV (0..5 V) and does not set gate length (gates are 50 % of the step). TIME: the C knob of the playing step is the gate length for A and B, 5 %..95 % of the step, and `OUTPUTS:CV C` stays at 0 V |
| `CLOCK:TEMPO` | Internal clock, 0.5..32 steps per second (exponential). `CLOCK:TEMPO CV` adds 1 octave of rate per volt. Both act in INT only |
| `CLOCK:SOURCE` | Front switch, still two positions. INT: internal clock. EXT: the clock named by `CLOCK:EXT SOURCE` (the CLOCK jack, or the DAW transport); TEMPO and TEMPO CV are ignored. In EXT the BPM readout shows the measured tempo (`EXT 118.4`, at the DIV setting) or `HOST 120`; it shows `EXT` / `HOST` until there is a tempo |
| `CLOCK:DIV` | 1/8, 1/16, 1/32 = 2, 4, 8 steps per beat. In HOST it is the step size (2, 4, 8 steps per quarter). Otherwise a display unit only: the BPM readout next to TEMPO shows the same TEMPO parameter as BPM = steps per second x 60 / steps per beat (120 BPM at 1/16 = 8 steps/s). Dragging the readout moves TEMPO; changing DIV never changes the clock. TEMPO CV still bends around TEMPO, and the readout shows the knob's tempo, not the bent one. There is no BPM jack: the CLOCK jack takes pulses in EXT |
| `MODE:MODE` | Every mode loops until Stop. `A`: 12-step loop of row A on CV A / GATE A; the B jacks hold. `A+B`: the long sequence, one 24-step loop on CV A / GATE A (steps 1-12 = row A, 13-24 = row B, then A1); CV B holds its last value and GATE B stays low. `ALT`: one row per pass, each on its own jacks (row A on the A jacks, then row B on the B jacks, then A again). The panel legend under the switch reads `A · LOOP 12`, `A+B · LOOP 24`, `ALT · SWAP A/B` |
| `MODE:START/STOP`, `MODE:STEP`, `MODE:RESET` | Buttons (same as the matching INPUTS jacks) |
| `MIXER:LEVEL 1`, `MIXER:LEVEL 2` | Two-input mixer gains, 0..1, smoothed with a 10 ms one-pole (k = −expm1(−1 / (0.010 x sr))) so moves do not click |

## Tab controls (format 1)
The front panel is unchanged: every new control lives on a tab (MAIN · STEPS · CLOCK · MIDI · SETUP, a strip above the panel art;
a non-MAIN tab replaces the whole face at the same size). The ids are appended after `CLOCK:DIV`, so the v1 parameter indices never move.
| Id | Tab | Meaning |
|---|---|---|
| `CLOCK:EXT SOURCE` | CLOCK | JACK (default) / HOST. Used when the front SOURCE is at EXT. HOST is the default only for a **new rack instance created while the DAW plays**; saved patches and plugin instances keep their stored SOURCE. Stored as `SOURCE = EXT` + `EXT SOURCE = HOST` |
| `CLOCK:SETTLE` | CLOCK | TIGHT (default for new patches): a new step's CV and gate wait 2 samples. VINTAGE (migrated patches): 0.6 ms, exactly as v1 |
| `CLOCK:TRIG MODE` | CLOCK | STEP (default): each TRIG jack is high for its whole step. PULSE: a 5 ms trigger, max(1, round(0.005 x sr)) samples |
| `STEPS:LAW A`, `STEPS:LAW B` | STEPS | PITCH LAW of CV A / CV B: V/OCT (default) / HZ/V LIN, see Pitch laws |
| `STEPS:QUANT A`, `STEPS:QUANT B` | STEPS | OFF (default) / SEMI. SEMI snaps the jack's target to the nearest semitone under its law: V/OCT round(12 V) / 12; LIN 2^(round(12 log2 V) / 12) for V ≥ 2⁻⁵ V, else 0 V, never past 5 V (5 V gives note 75) |
| `MIDI:CH A`, `MIDI:CH B` | MIDI | MIDI channel 1..16 of the GATE A / GATE B notes (1 and 2 by default) |
| `MIDI:VEL A`, `MIDI:VEL B` | MIDI | 100 (default) / FROM C: velocity = round(1 + 126 x C / 5) from the playing step's row C volts. FROM C only applies when C MODE = CV (in TIME the velocity is 100) |

The STEPS tab shows the 36 step knobs as volts plus a note name under each row's law (drag a cell, or double-click it and type `2.5`,
`2.5V`, `C4`, `F#3` or `Bb2`), with COPY / PASTE / RAND / CLEAR per row (CLEAR sets 0 V). The CLOCK tab shows the measured EXT tempo,
the settle in samples and the fixed transport rules; the MIDI tab the reference and a monitor of the notes playing; SETUP the UI scale
(75 / 100 / 125 / 150 / 200 % of 1280 x 376, kept with the session) and the last load's migration report. Jack hit areas are at least
16 x 16 screen px at every scale.

## Sequencing
- START toggles running. Every start, including one after a stop, begins at A step 1 at once (JCS R5). In EXT, a clock edge on the
  START sample or the next 2 samples is step 1's own clock and is absorbed, so A1 is never skipped when RUN and CLK arrive together.
- STOP sends every gate and TRIG output low at once; the CVs and lamps hold.
- **HOST** (SOURCE = EXT, EXT SOURCE = HOST): the step index is floor(ppq x q) with q = 2 / 4 / 8 from DIV; a change of index is a tick,
  at its exact sample. Transport start applies the START rule and transport stop the STOP rule, so the steps follow the host and never drift
  (120 BPM at 1/16 and 48 kHz ticks every 6000 samples).
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
- **Settle (`CLOCK:SETTLE`):** a new step's CV and gate wait 2 samples (TIGHT) or 0.6 ms (VINTAGE). A reset patched from a TRIG jack is a
  feedback cable, so it lands 1 sample late, still before the gate opens at sample 2, and the skipped step never reaches the CV or gate
  outputs (test: "the skipped step 5 never reaches CV A").
- **Patch delay:** forward cables are sample-accurate. A cable that closes a feedback loop, including a module patched to itself
  (every BUSHIDO-to-BUSHIDO cable), arrives exactly **1 sample** late (`PatchGraph::kFeedbackDelay`). Only the newest cable in each loop is
  delayed: cables carry an age (when they were patched; moving a plug makes it the newest), the graph takes them oldest first, and a
  cable is delayed only if the older undelayed cables already lead back to its source. Module order does not matter. While any loop is
  patched the graph runs one sample at a time. A TRIG -> RESET cable lands at sample 1, inside even the TIGHT settle (2 samples).
- Gate length uses the internal clock period, the HOST step period, or the time between the last two ticks for EXT / manual stepping.
  The EXT period restarts at START (the stopped time is not a period); until a new one is measured it uses the last one, or the INT tempo.

## Plugin I/O (`plugin/PluginProcessor.cpp`)
- The mixer is an audio utility only. Nothing is normalled through it from CV A or CV B.
- Host audio input L/R is normalled to `MIXER:IN 1` / `IN 2` (1.0 full scale = 5 V); patching a cable into those jacks replaces it.
  These normals belong to this plugin's processor (`PatchGraph::setNormal`), not to `BushidoModule`, so if the module is ever added to
  another graph they are off unless that host sets them.
- Main output = `MIXER:OUT` (5 V = 1.0), on both channels.
- MIDI out is a **convenience, not the patch**: GATE A -> `MIDI:CH A` (1), GATE B -> `MIDI:CH B` (2), so A+B plays all 24 steps on
  channel A. A note-on is sent at the sample the gate opens and a note-off when it closes; bypass sends all notes off. Velocity is 100 or
  FROM C (see the tab controls). CV A and CV B stay in volts on the jacks. The note comes from the jack's **target** volts (never the
  slewed CV, so PORTA cannot change it) under the row's PITCH LAW: V/OCT round(48 + 12 V), HZ/V LIN round(48 + 12 log2 V) with no note at
  or below 0 V, clamped to 0..127 (JCS R4.5). Old patches' MIDI rises by 15 semitones under LIN (1 V was MIDI 33, now 48), so it matches
  RONIN. Unless QUANT is SEMI, a step between semitones is rounded in MIDI but not on the jack. In the rack, MIDI goes to RACK I/O (JCS R13).
- Buttons are not host-automatable; every knob and switch is.

## State format and migration (JCS R6, R7)
- The plugin state carries `format = 1`. A state without one is format 0 (v1) and migrates on load, before the parameters and cables
  are bound (`bushido::migrate`, `engine/BushidoState.h`):
  - `CLOCK:SETTLE` = VINTAGE and `CLOCK:TRIG MODE` = STEP, so old patches keep their timing;
  - `CLOCK:SOURCE` as stored (never flipped to HOST), `CLOCK:EXT SOURCE` = JACK, PORTA unchanged;
  - PITCH LAW per row: HZ/V LIN if that row's CV jack is cabled to a RONIN `VCO:HZ/V`, otherwise V/OCT. A row cabled to both gets LIN
    and its V/OCT cable shows the `≠` badge.
- Patterns, knob volts and cables are unchanged, so every cable carries exactly the same volts as before. The audible differences in
  old patches are the fixes only: A1 is no longer skipped on RUN + CLK, the first gate after a restart has the right length, TRIG is low
  while stopped, and MIDI notes are correct.
- Jack ids are stored in the neutral form (`BUSHIDO/...`) and read exactly as stored: there are no legacy prefix aliases, so a cable
  whose id uses an unknown prefix does not bind (it is kept in the state but not patched).
- A state from a newer format loads read-only: it is handed back unchanged when the host saves, and SETUP shows a READ-ONLY banner.
- The SETUP tab lists the last load's migration report.
