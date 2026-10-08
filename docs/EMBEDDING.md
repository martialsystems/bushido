# Embedding the BUSHIDO engine

BUSHIDO's sequencer, patch graph and cable model are plain C++17 with no framework, so any host can run them: the BUSHIDO
plugin, JIDAI RACK, a test harness or your own app. The plugin is the simplest example. It runs one `rack::PatchGraph` with
BUSHIDO as its only module, and draws it with the JUCE components in `ui/`.

To embed it, add `rack/PatchGraph.cpp`, `rack/CableModel.cpp` (if you draw cables) and `engine/BushidoModule.cpp` to your
build, put the repository root and `third_party/jidai-common/include` on the include path, and drive the graph:

1. `addModule()` each module (not owned), then `prepare(sampleRate, maxBlock)`.
2. `setCables()` with your cables, oldest first. You can call it from any thread except the audio thread; it takes effect at the next `process()`.
3. On the audio thread, once per block: `setTransport()` on each module that follows the host clock, then `process(numSamples)`,
   then read `output(module, jack)`.
4. Set parameters with `Module::setParam()` (thread-safe) and read lamps with `indicator()`.

## Building blocks
| Folder | Framework | What it is |
|---|---|---|
| `rack/Module.h` | none | The contract every rack implements: jacks, parameters, indicators, `process`, and the host `Transport` for HOST sync. |
| `rack/PatchGraph.*` | none | Runs modules and the cables between them, in one graph. |
| `rack/CableModel.*` | none | Visible cable state: stacking, reorder, carrying, rope physics, hover push-away, cable age. |
| `rack/PitchLaw.h` | none | The two pitch laws (V/OCT and HZ/V LIN, C3 = MIDI 48): note names, QUANT and MIDI notes from volts. A thin layer over the shared header in `third_party/jidai-common`. |
| `engine/BushidoModule.*` | none | The BUSHIDO engine. |
| `engine/BushidoState.h` | none | State format and migration of saved sessions and patterns. |
| `engine/MidiOut.h` | none | The MIDI convenience out: notes from the gate events, under each row's pitch law. |
| `ui/Layout.*`, `ui/RackPanel.*` | JUCE | Draws any rack from its layout file and turns its knobs. |
| `ui/CableLayer.*` | JUCE | One overlay over all racks: draws cables and handles patching. |
| `ui/PatternScreen.*` | JUCE | Dot-matrix pattern screen and list, drawn from the layout's `screen` rects. |
| `assets/bushido_panel_bg.svg`, `assets/bushido_layout.json` | — | BUSHIDO panel art (vector) and geometry. |
| `assets/bushido_patterns.json` | — | Factory bank, INIT first, written by `panel/build_patterns.py`: parameter values by id plus cables by jack id. BYPASS lives in the processor, not the engine or the patterns. |

## Rules any host graph must keep
- **One graph.** Run every module in one `PatchGraph`. Splitting the patch over two graphs, or hosting one plugin inside another, would give cables two timing laws.
- **Jack ids** are `SECTION:LABEL` inside a module (`OUTPUTS:CV A`), and `RACK/SECTION:LABEL` across racks (`BUSHIDO/OUTPUTS:CV A`).
- **Signals are volts:** ±5 V audio and CV, 0/5 V gates and TRIGs; an input is high above 1 V and low below 0.5 V.
  Pitch CV stays in volts on the jacks; each row's pitch law (V/OCT or HZ/V LIN, see `docs/REFERENCE.md`) says what those volts mean.
- Output -> input carries signal. Output -> output or input -> input does nothing. One output into several inputs: fan-out.
  Several cables into one input: summed. Cable colour and stack order are visual only.
- **Timing:** forward cables are sample-accurate; only the newest cable in a feedback loop is delayed, by 1 sample
  (`PatchGraph::kFeedbackDelay`). Pass cables to `setCables()` oldest first (`CableSpec::age`).
- **Normals:** host audio into `MIXER:IN 1` / `IN 2` is set by the plugin's processor with `PatchGraph::setNormal`, not by the module.
  A different host graph leaves them off unless it sets them. CV A and CV B are never normalled through the mixer.
