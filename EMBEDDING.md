# Loading the SQ-10 into the MS-50 as a rack

The SQ-10 ships as its own plugin, and the same code can sit underneath the MS-50 inside the MS-50 plugin, with cables running
between the two racks. This is done by **compiling the SQ-10 code into the MS-50 plugin**, not by hosting the SQ-10 plugin file:
a plugin hosting another plugin is fragile, and the two would not share one patch.

## What to reuse
| Folder | Framework | What it is |
|---|---|---|
| `rack/Module.h` | none | The contract every rack implements (jacks, params, indicators, `process`). |
| `rack/PatchGraph.*` | none | Runs several modules and the cables between them. |
| `rack/CableModel.*` | none | Visible cable state: stacking, reorder, carrying, rope physics, hover push-away. |
| `engine/Sq10Module.*` | none | The SQ-10 engine. |
| `ui/Layout.*`, `ui/RackPanel.*` | JUCE | Draws any rack from its layout file and turns its knobs. |
| `ui/CableLayer.*` | JUCE | One overlay over all racks: draws cables and handles patching. |
| `assets/sq10_bg@2x.png`, `assets/sq10_layout.json` | — | SQ-10 panel art and geometry (provisional layout). |

## Steps in the MS-50 plugin
1. **Engine:** make each MS-50 rack (or the whole MS-50) a `rack::Module` whose jack ids match its layout file (`SECTION:LABEL`).
   Give it `name()` = `"MS-50"`.
2. **Processor:** one `rack::PatchGraph`. `addModule(&ms50)`, then `addModule(&sq10)` when the SQ-10 rack is shown.
   Module order only affects which cables get the 16-sample delay (cables into a module that runs earlier).
3. **Cables:** store them as global ids `"MS-50/VCO:SAW"`, `"SQ-10/OUTPUTS:CV A"` and convert to `rack::Cable` the way
   `Sq10Processor::applyCables()` does. Call `graph.setCables()` from the message thread; the audio thread picks it up.
4. **Editor:** stack the racks in one design space and give them one `CableLayer`:
   ```cpp
   cables.setDesignSize(1600, 640 + 640);
   cables.addRack(ms50Panel.get(), 0.0f);      // MS-50 on top
   cables.addRack(sq10Panel.get(), 640.0f);    // SQ-10 underneath
   addMouseListener(&cables, true);            // hover push-away everywhere
   ```
   Set panel bounds to match (each panel's height = its layout height x the editor scale). Keep the editor's aspect ratio fixed to the stacked size.
5. **Layout file:** the MS-50 layout (`ms50_project/assets/layout.json`) has no `dir` per jack and no `controls`/`leds` lists yet.
   `RackPanel` and `CableLayer` need the SQ-10-style schema (see `panel/build_panel.py`): add `dir`, and `controls` with `kind`.

## Rules the graph applies
- Output -> input carries signal. Output -> output or input -> input does nothing.
- One output into several inputs: same signal everywhere. Several cables into one input: summed.
- Cable colour and stack order are visual only.
- Signals are volts: keep the MS-50 on the same scale (audio about +-5 V, gates 0/5 V, 1 V high threshold).
