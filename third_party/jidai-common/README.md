# jidai-common

Header-only C++17 library shared by every Jidai unit (BUSHIDO, RONIN, SHOGUN, ORIGAMI) and the JIDAI RACK.
It is the code form of the **Jidai Cable Standard v1.1** (`JCS`, R1–R16) plus the shared DSP blocks.
No framework, no allocation in the per-sample helpers, no global state.

> **Version 1.1.1 (2026-10-08) — re-vendor.** One commit for BUSHIDO, RONIN and SHOGUN to re-vendor. Changes since 1.1.0:
> - `pitch::kC3Hz` is the exact `440·2^(−21/12)` = **130.8127826502993 Hz** (was the rounded 130.8128). Use it everywhere; never write 130.8128.
> - `pitch::lin55ToVoct(V)` is `log2(V) − 1.25` exactly (`kLin55Octaves = 1.25`, since 55/C3 = 2^(−15/12)); it was 1.9e-7 V low.
> - `TripleShaper` decides stage skipping **per block** (`planBlock(ctl, steady, vcLive)`), never per sample, so the ADAA half-sample delay cannot toggle mid-stream under modulation or VC. Without a plan no stage is skipped.
> - `AliasTable` carries a per-alias **input law** (`AliasLaw::{Identity, Lin55ToVoct, HzvLinToVoct, VoctToHzvLin, Scale}`): `add(old, canonical, law)`, `resolve(id)` → `{id, conversion}`, `conversion.convert(V)`. E.g. SHOGUN `LEAD:HZ/V` → `LEAD:NOTE` with `Lin55ToVoct` (V′ = log2 V − 1.25).
> - Headers are clean under `-Wfloat-equal` (`clampRail` uses `std::isnan`; shaper compares use `same()`), enforced by the new `headers` test.
> - `LevelComp`'s 1e-30 detector floor is documented (moves SHOGUN's output by at most 9.9e-7; kept).
> - Neutral names only: R16 level `ModularHalfLevel`; tests use the fake prefix `ACME`.

## Use it

- Vendor `include/jidai/` (copy, git subtree or FetchContent) and put `include/` on the include path, or
- `add_subdirectory(jidai-common)` and `target_link_libraries(x PRIVATE jidai::common)`.

`#include "jidai/CableStandard.h"` pulls in every `jcs` header. Code comments cite rules as `// JCS R3`.

## API

| Header | Namespace | What |
|---|---|---|
| `jidai/jcs/Detect.h` | `jidai::jcs` | `kGateLow/kGateHigh` (0/5 V, R2), `gateVolts()`, `triggerPulseSamples(sr)` (≥ 1 ms), `Schmitt` (R3: high when V > 1.0, low when V < 0.5; `process()` → `Edge::Rising/Falling/None`), `StrigDetector` (R3s: held when V < 1.0, released when V > 1.5), `strigVoltsFor(sourceHigh)` (R3s cable conversion), `kStrigRest` (+5 V) |
| `jidai/jcs/Volts.h` | `jidai::jcs` | `kNominal` 5 V, `hostToVolts(x)=5x`, `voltsToHost(v)=0.2v` (R1), `clampRail(v, over)` (±5 V hard rail, R4.4), `OverRangeLed` (R15: \|V\| > 5.5 V for > 10 ms), `PitchRailFlag` (latched until `noteOn()`), **R16 hook only**: `AudioLevel{Jidai5V, ModularHalfLevel}`, `kR16Enabled=false`, `r16BoundaryGain()` ≡ 1 |
| `jidai/jcs/Pitch.h` | `jidai::jcs::pitch` | R4: `Law{VOct=0, HzvLin=1}`, `kC3Hz=130.8127826502993` (exact 440·2^(−21/12)), `kLin55Octaves=1.25`, `kRefNote=48`, `note()`, `voltsForNote()`, `hz()`, `hzToVolts()`, `midiNote()` (from target volts, 0..127 or −1), `quantize()`, `noteName()`, `voiceHz(V, cents, a4, oct)` (receiver tuning; never the volt law), `roninHzvLinHz()` (0.05 V floor), `lin55ToVoct()` (exact migration from the retired 55 Hz law: `log2(V) − 1.25`), `clampPitch()`. Same names and semantics as BUSHIDO's `rack/PitchLaw.h`, which can become `namespace rack::pitch { using namespace jidai::jcs::pitch; }` |
| `jidai/jcs/Roles.h` | `jidai::jcs` | R14: `Role{STrig, Audio, VOct, GateClk, HzvLin, CV}`, `roleInfo()` → name, `#rrggbb`, UTF-8 glyph, luminance; `roleArgb()`; `roleFromName()`; `cableBadge(src, dst)` → `PitchLaw` (≠, R4.3), `AudioIntoClock`, `GateToStrig`; `badgeText()` |
| `jidai/jcs/JackId.h` | `jidai::jcs` | R6: `parseJackId()` → `JackId{prefix, number, section, label, form}` for `PREFIX#N/SECTION:LABEL`, `PREFIX/SECTION:LABEL`, `SECTION:LABEL`; `formatJackId()`; `isKnownPrefix()` (BUSHIDO, RONIN, SHOGUN, ORIGAMI, RACK); `isValidLocalId()`; `AliasTable` (old → canonical + input law `AliasConversion{AliasLaw, scale, offset}`, one hop, never reuses an id; `resolve(id)` → `{id, conversion, aliased}`) |
| `jidai/jcs/State.h` | `jidai::jcs` | R7: `StateHeader{format, unit}`, `MigrationChain<State>` (`add(from, step)`, `run(state, format)` → `LoadResult{Current, Migrated, FutureReadOnly, Failed}`; future formats load read-only) |
| `jidai/dsp/TripleShaper.h` | `jidai::dsp` | **The** shared triple wave shaper (SHOGUN WAVE §4.6 = ORIGAMI). `kShaperK = {4, 2, 2}`, `ShaperStage::f/F` (stage law and antiderivative), `AdaaStage` (first-order ADAA, current-sample params for both terms, `|dx| < 1e-6` fallback; a stage is a wire only when `planBlock` finds a = b = 0 for the whole block), `macroAmounts(m, c[3])` (c1 = 2m, c2 = 2m − 0.5, c3 = 2m − 1, clamped), `ShaperControls{macro, trim[3], sym[3], vcToAmt[3], vcToSym[3], modAmt[3], modSym[3]}` + `isBypass(vcLive)`, `TripleShaper::planBlock(ctl, steady, vcLive)` (once per block) + `process(x, ctl, vc)` (one VC, SHOGUN) / `process(x, ctl, vc[3])` (per-stage VC, ORIGAMI) / `processStages` / `stageParams` / `setAdaa`, `LevelComp` (20 ms detectors, ±12 dB, 5 ms smoothing, detectors floored at 1e-30), `DcBlocker` (8 Hz TPT), `toUnits(v) = v/5`, `toVolts(u) = 5u`, `same(a, b)` (intentional exact compare). Full API at the top of the header |
| `jidai/dsp/Halfband.h` | `jidai::dsp` | 93-tap exact half-band 2× pair (111.6 dB stopband, 4.5e-5 dB ripple): `Upsampler2x::process(x, y0, y1)` (even phase = x delayed 23 exactly), `Downsampler2x::process(u0, u1)`, `Halfband93::kLatencyPerDirection = 23` (so an up → effect → down chain is 46) |
| `jidai/jcs/Graph.h` | `jidai::jcs` | R9: `classifyFeedback(n, fixed, cablesOldestFirst)` (every loop-closing cable is delayed one sample), `runOrder()` (each node exactly once). R11: `pathLatency(L, audioEdges)` (`P(d) = L_d + max P(a)`), `arrivalSkew()` (Δn badges) |

## Deliberate choices

- **Jack labels may contain `/` and non-ASCII.** R6 says "no `/`", but canonical ids in the same standard (`VCO:HZ/V`, `INPUTS:START/STOP`, RONIN `EG 2:OUT −`) need it. The parser splits at the first `/` before the first `:`; sections never contain `/`.
- **No retired-prefix aliases (R6).** Only the neutral prefixes are known; a stored cable with any other prefix is kept as stored and shows as missing. Per-device `AliasTable`s cover future jack renames.
- **Neutral names only.** No third-party brand or model names appear in code or identifiers (the R16 level is `ModularHalfLevel`).
- **R16 is a hook, not a feature.** It stays identity until the user decides.

- **2× on external audio costs 46 samples, not 23.** SHOGUN generates at 2× and only decimates (23). An effect such as ORIGAMI has to upsample *and* decimate, so its true latency is 46 base samples, and that is what it reports. (A 47-tap pair would reach 23 total, at only 63.6 dB stopband.)

## Tests

`JidaiCommonTests` (the JCS headers) is built as C++17 with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
`JidaiHeaderHygiene` (the `headers` test) includes every header with `-Wconversion -Wfloat-equal -Wdouble-promotion -Werror` (clang and g++).
`JidaiDspTests` checks the shared DSP against SHOGUN's `testWave*` numbers: the stage law exactly, `F' = f`, symmetry,
the macro stagger, the exact single-stage migration, per-block stage skipping under modulation, LEVEL COMP over 200 random settings, the half-band stopband and
latency, and the static aliasing table (1031 Hz at WAVE 0.5: 1× naive −27.3 dB, 1× ADAA −45.3 dB, 2× −121.8 dB). Run it standalone
(`cmake -S jidai-common -B build && cmake --build build && ctest --test-dir build`) or as part of the jidai-rack build.
