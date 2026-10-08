# BUSHIDO is its own plugin

BUSHIDO ships as its own plugin (VST3, AU, Standalone). It is **not** compiled into RONIN, and neither plugin hosts
the other. This plugin runs exactly one `rack::PatchGraph`, with BUSHIDO as its only module.

The folders below are kept free of JUCE (except `ui/`) and free of any RONIN type, so a later compile-in could reuse them unchanged.
Nothing in this repository depends on that happening.

## What is reusable
| Folder | Framework | What it is |
|---|---|---|
| `rack/Module.h` | none | The contract every rack implements (jacks, params, indicators, `process`). |
| `rack/PatchGraph.*` | none | Runs modules and the cables between them, in one graph. |
| `rack/CableModel.*` | none | Visible cable state: stacking, reorder, carrying, rope physics, hover push-away, cable age. |
| `rack/HzPerVolt.h` | none | Hz/V pitch curve (1 V = 55 Hz, double the volts = one octave). |
| `engine/BushidoModule.*` | none | The BUSHIDO engine. |
| `ui/Layout.*`, `ui/RackPanel.*` | JUCE | Draws any rack from its layout file and turns its knobs. |
| `ui/CableLayer.*` | JUCE | One overlay over all racks: draws cables and handles patching. |
| `ui/PatternScreen.*` | JUCE | Dot-matrix pattern screen and list, drawn from the layout's `screen` rects. |
| `assets/bushido_panel_bg.svg`, `assets/bushido_layout.json` | — | BUSHIDO panel art (vector) and geometry. |
| `assets/bushido_patterns.json` | — | Factory bank (INIT only for now): parameter values by id plus cables by jack id. BYPASS lives in the processor, not the engine or the patterns. |

## Rules any host graph must keep
- **One graph.** A second `PatchGraph`, or a plugin hosted inside another, would split the patch and give cables two timing laws.
- **Jack ids** are `SECTION:LABEL` inside a module (`OUTPUTS:CV A`), and `RACK/SECTION:LABEL` across racks (`BUSHIDO/OUTPUTS:CV A`).
- **Signals are volts:** ±5 V audio and CV, 0/5 V gates and TRIGs; an input is high above 1 V and low below 0.5 V.
  Pitch CV stays in volts on the jacks; a Hz/V VCO interprets it.
- Output -> input carries signal. Output -> output or input -> input does nothing. One output into several inputs: fan-out.
  Several cables into one input: summed. Cable colour and stack order are visual only.
- **Timing:** forward cables are sample-accurate; only the newest cable in a feedback loop is delayed, by 1 sample
  (`PatchGraph::kFeedbackDelay`). Pass cables to `setCables()` oldest first (`CableSpec::age`).
- **Normals:** host audio into `MIXER:IN 1` / `IN 2` is set by this plugin's processor with `PatchGraph::setNormal`, not by the module.
  A different host graph leaves them off. CV A and CV B are never normalled through the mixer.
