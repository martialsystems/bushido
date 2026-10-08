// BUSHIDO redesign in the web engine: mirrors tests/test_redesign.cpp (BUSHIDO_Redesign.md section 5, JCS R4/R5/R9).
// Run: node web/test_bushido_redesign.js   (the C++-vs-JS sample parity check is web/test_bushido_parity.js)
const W = __dirname + "/";
const B = require(W + "bushido_dsp.js"); global.BUSHIDO_DSP = B; global.RONIN_DSP = require(W + "ronin_dsp.js"); const RACK = require(W + "rack_engine.js");
const P = B.P, J = id => B.JACKS.indexOf(id);
let fails = 0, passes = 0;
const CHECK = (c, msg) => { console.log((c ? "PASS " : "FAIL ") + msg); if (c) passes++; else fails++ };
const eqArr = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// One BUSHIDO driven by functions of the absolute sample index (like the C++ Driver feeding a PatchGraph). Presses and parameter
// changes made between run() calls land on the next sample, as a C++ press lands on the next block's first sample.
class Rig {
  constructor(sr = 48000) { this.sr = sr; this.sq = B.create(sr); this.sq.prepare(sr); this.t = 0; this.inp = {}; this.trf = null; this.block = 256 }
  patch(map) { this.inp = map }                                   // { "CLOCK:CLOCK": t => volts, ... }
  press(id) { this.sq.press(id) }
  set(i, v) { this.sq.setParam(i, v) }
  out(id) { return this.sq.portValue[J(id)] }
  run(n, each) {
    const pv = this.sq.portValue;
    for (let k = 0; k < n; k++, this.t++) {
      if (this.trf && this.t % this.block === 0) this.sq.setTransport(this.trf(this.t));   // the host hands the transport once per block
      for (const id in this.inp) pv[J(id)] = Math.fround(this.inp[id](this.t));
      this.sq.processSample();
      if (each) each(this.t);
    }
  }
}

// ------------------------------------------------------------------ B1: START and CLOCK on the same sample
{
  const t0 = 1000, Pd = 4800;
  const r = new Rig(); r.set(P.SOURCE, 1);
  r.patch({ "CLOCK:CLOCK": t => (t >= t0 && (t - t0) % Pd < 240 ? 5 : 0), "INPUTS:START/STOP": t => (t >= t0 && t < t0 + 240 ? 5 : 0) });
  let a1 = -1, a2 = -1;
  r.run(t0 + 3 * Pd, t => { if (a1 < 0 && r.out("1:TRIG") > 1) a1 = t; if (a2 < 0 && r.out("2:TRIG") > 1) a2 = t });
  CHECK(a1 === t0, "B1: START plays A1 on its own sample");
  CHECK(a2 - a1 === Pd, "B1: a coincident EXT edge is absorbed, A1 lasts one full period (4800 samples)");
  for (const lag of [2, 3]) {
    const w = new Rig(); w.set(P.SOURCE, 1);
    w.patch({ "CLOCK:CLOCK": t => (t >= t0 + lag && (t - t0 - lag) % Pd < 240 ? 5 : 0), "INPUTS:START/STOP": t => (t >= t0 && t < t0 + 240 ? 5 : 0) });
    let b2 = -1; w.run(t0 + 2 * Pd, t => { if (b2 < 0 && w.out("2:TRIG") > 1) b2 = t });
    if (lag === 2) CHECK(b2 === t0 + lag + Pd, "B1: an edge 2 samples after START is still step 1's clock");
    else CHECK(b2 === t0 + lag, "B1: an edge 3 samples after START advances");
  }
}
// ------------------------------------------------------------------ B2: first EXT gates after a restart
{
  const Pd = 5760, startAt = [100, 20 * Pd + 100 + 480000], stopAt = 20 * Pd + 50;
  const r = new Rig(); r.set(P.SOURCE, 1);
  r.patch({ "CLOCK:CLOCK": t => (t >= 100 && (t - 100) % Pd < 100 ? 5 : 0),
            "INPUTS:START/STOP": t => (startAt.some(s => t >= s && t < s + 50) || (t >= stopAt && t < stopAt + 50) ? 5 : 0) });
  const lens = []; let hi = 0;
  r.run(startAt[1] + 6 * Pd, t => { const g = r.out("OUTPUTS:GATE A") > 1; if (g) ++hi; else if (hi) { if (t > startAt[1]) lens.push(hi); hi = 0 } });
  CHECK(lens.length >= 5 && lens.every(l => l === Pd / 2), "B2: every gate after a 10 s stop is 2880 samples (50 % of a 120 ms clock), the first one too");
}
// ------------------------------------------------------------------ B3 / R5.5: settle
for (const vintage of [0, 1]) {
  const r = new Rig(); r.set(P.SETTLE, vintage); r.set(P.STEPS + 1, 0.9); r.press("MODE:START/STOP");
  let trig = -1, gate = -1, cv = -1, prevG = true;
  r.run(30000, t => { if (trig < 0 && r.out("2:TRIG") > 1) trig = t;
    const g = r.out("OUTPUTS:GATE A") > 1; if (trig >= 0 && gate < 0 && g && !prevG) gate = t; prevG = g;
    if (trig >= 0 && cv < 0 && r.out("OUTPUTS:CV A") > 4) cv = t });
  if (!vintage) CHECK(gate - trig === 1 && cv - trig === 1, "R5.5 TIGHT (default): GATE and CV within 2 samples of TRIG");
  else CHECK(gate - trig === 28 && cv - trig === 28, "VINTAGE: v1's 0.6 ms settle (28.8 samples at 48 kHz) is kept");
}
CHECK(B.create(48000).getParam(P.SETTLE) === 0, "new patches use SETTLE = TIGHT");
CHECK(B.create(44100).settleSamples() === 2, "settleSamples() is 2 under TIGHT");
{ const m = B.create(44100); m.setParam(P.SETTLE, 1); m.processSample(); CHECK(m.settleSamples() === Math.ceil(44100 * 0.0006), "VINTAGE settle is max(1, 0.0006 sr) unrounded (26.46 -> the gate waits 27 samples at 44.1 kHz)") }

// ------------------------------------------------------------------ TRIG self-patch with TIGHT, through the rack graph (one-sample feedback delay)
{
  const e = RACK.create(48000); e.msg({ t: "devices", list: ["BUSHIDO#1"] }); e.msg({ t: "monitor", mode: "off" });
  e.msg({ t: "param", d: "BUSHIDO#1", id: "MODE:MODE", v: 0 });
  for (let i = 0; i < 12; i++) e.msg({ t: "param", d: "BUSHIDO#1", id: "A:" + (i + 1), v: (i + 1) / 12 });
  e.setCables([["BUSHIDO#1/5:TRIG", "BUSHIDO#1/INPUTS:RESET"]]); e.msg({ t: "press", d: "BUSHIDO#1", id: "MODE:START/STOP" });
  const sq = e.devices["BUSHIDO#1"].sq; let mx = 0;
  for (let i = 0; i < 48000 * 3; i++) { e.processGraph(); mx = Math.max(mx, sq.portValue[B.CV_A]) }
  CHECK(mx <= 5 * 4 / 12 + 1e-4 && sq.currentStep() < 4, "TIGHT: TRIG 5 -> RESET loops 1-4 and step 5's CV never appears");
}
// ------------------------------------------------------------------ B4: TRIG outs
CHECK(B.create(48000).getParam(P.TRIG_MODE) === 0, "testTrigModeDefaultStep: a new patch is STEP");
{ const r = new Rig(); r.press("MODE:STEP"); let hi = 0; r.run(4800, () => { hi += r.out("1:TRIG") > 1 });
  CHECK(r.sq.currentStep() === 0 && hi === 0, "B4: TRIG outs stay low while stopped (STEP button moved to step 1)") }
{ const r = new Rig(); r.press("MODE:START/STOP"); r.run(6000); r.press("MODE:START/STOP"); let hi = 0; r.run(1, () => { hi += r.out("1:TRIG") > 1 });
  CHECK(hi === 0 && r.sq.state().pos === 0, "R5.4: STOP drops TRIG on that sample; the step lamp stays lit") }
{ const r = new Rig(); let hi = 0; r.press("MODE:START/STOP"); r.run(11000, () => { hi += r.out("1:TRIG") > 1 });
  CHECK(hi === 11000, "TRIG MODE STEP: high for the whole step");
  const p = new Rig(); p.set(P.TRIG_MODE, 1); let hp = 0; p.press("MODE:START/STOP"); p.run(11000, () => { hp += p.out("1:TRIG") > 1 });
  CHECK(hp === 240, "TRIG MODE PULSE: 5 ms (240 samples at 48 kHz)");
  const w = new Rig(44100); w.set(P.TRIG_MODE, 1); let hw = 0; w.press("MODE:START/STOP"); w.run(9000, () => { hw += w.out("1:TRIG") > 1 });
  CHECK(hw === 221, "TRIG MODE PULSE: round(0.005 x 44100) = 221 samples at 44.1 kHz") }

// ------------------------------------------------------------------ portamento law, bit-exact against v1 (float emulated with Math.fround)
{
  let all = true;
  for (const sr of [44100, 48000, 96000]) for (const porta of [0, 0.1, 0.5, 1]) {
    const r = new Rig(sr); r.set(P.SETTLE, 1); r.set(P.PORTA_A, porta); r.set(P.STEPS, 0.8);
    const got = []; let first = -1; r.press("MODE:START/STOP");
    r.run(6000, t => { const v = r.out("OUTPUTS:CV A"); if (first < 0 && v !== 0) first = t; if (first >= 0 && got.length < 4800) got.push(v) });
    const pf = Math.fround(porta), tau = pf * pf * 2, k = tau < 1e-4 ? 1 : Math.fround(1 - Math.exp(-1 / (tau * sr)));   // v1 L90/L124
    let cv = 0, same = got.length === 4800; const tgt = Math.fround(Math.fround(0.8) * 5);
    for (let s = 0; s < got.length && same; s++) { cv = Math.fround(cv + Math.fround(Math.fround(tgt - cv) * k)); same = Object.is(cv, got[s]) }
    all = all && same && first === Math.ceil(Math.max(1, sr * 0.0006)) - 1;
  }
  CHECK(all, "testPortaLawUnchanged: coefficient and 4800-sample trajectory bit-exact vs v1 (44.1/48/96 k, PORTA 0/0.1/0.5/1)");
}
// ------------------------------------------------------------------ B5/B6: MIDI from the target under the row's law
{
  const collect = (lawA, volts, porta, cvs) => {
    const r = new Rig(); r.set(P.LAW_A, lawA); r.set(P.PORTA_A, porta); r.set(P.MODE, 0);
    for (let i = 0; i < 12; i++) r.set(P.STEPS + i, volts[i % volts.length] / 5);
    const midi = B.createMidiOut(), notes = []; r.press("MODE:START/STOP");
    const n = Math.ceil((12000 * volts.length - 100) / 256) * 256;
    for (let done = 0; done < n; done += 256) {
      r.run(256, () => { if (cvs) cvs.push(r.out("OUTPUTS:CV A")) });
      midi.handle(r.sq, r.sq.takeGateEvents(), m => { if (m.on) notes.push(m.note) });
    }
    return notes;
  };
  CHECK(eqArr(collect(1, [0.5, 1, 2, 4, 5], 0), [36, 48, 60, 72, 76]), "testRowPitchLawMidi: LIN 0.5/1/2/4/5 V -> 36/48/60/72/76");
  CHECK(eqArr(collect(0, [0, 1, 2, 4, 5], 0), [48, 60, 72, 96, 108]), "testRowPitchLawMidi: V/OCT 0/1/2/4/5 V -> 48/60/72/96/108");
  const a = [], b = []; collect(0, [0.5, 1, 2, 4, 5], 0.3, a); collect(1, [0.5, 1, 2, 4, 5], 0.3, b);
  CHECK(a.length > 0 && eqArr(a, b), "testRowPitchLawMidi: the CV out is bit-identical under both laws");
  CHECK(eqArr(collect(1, [1, 1.5874, 2, 2.3784], 0.5), [48, 56, 60, 63, 48]), "B5: PORTA 0.5 no longer changes the MIDI note (target, not the glide)");
  { const r = new Rig(); r.set(P.LAW_A, 1); r.set(P.STEPS, 0); const midi = B.createMidiOut(); let on = 0;
    r.press("MODE:START/STOP"); r.run(256); const ev = r.sq.takeGateEvents(); midi.handle(r.sq, ev, m => { on += m.on });
    CHECK(ev.length === 1 && on === 0, "HZ/V LIN: 0 V sends no note") }
  { const m = B.create(48000), mo = B.createMidiOut(); m.setParam(P.VEL_A, 1);
    CHECK(mo.velocity(m, 0, 5) === 127 && mo.velocity(m, 0, 0) === 1 && mo.velocity(m, 0, 2.5) === 64 && mo.velocity(m, 1, 5) === 100, "VEL FROM C: round(1 + 126 C / 5) on row A only; row B stays 100");
    m.setParam(P.C_MODE, 1); CHECK(mo.velocity(m, 0, 5) === 100, "VEL FROM C needs C MODE = CV");
    CHECK(mo.channel(m, 0) === 1 && mo.channel(m, 1) === 2, "MIDI channels default to 1 (A) and 2 (B)") }
  { const r = new Rig(); r.set(P.LAW_A, 0); r.set(P.STEPS, 0.2); r.set(P.RANGE_A, 0); r.press("MODE:START/STOP"); r.run(100);   // 0.2 V V/OCT
    const n = r.sq.state().note; CHECK(n && n.note === 50 && n.jk === 0, "readout note: the page's last note-on is round(48 + 12 x 0.2 V) = 50 (D3), from the target") }
}
// ------------------------------------------------------------------ pitch helpers (rack/PitchLaw.h)
{
  const p = B.pitch;
  CHECK(p.midiNote(0, 0) === 48 && p.midiNote(1, 1) === 48 && p.midiNote(1, 0) === -1 && p.midiNote(0, 10) === 127 && p.midiNote(0, -5) === 0,
        "PitchLaw: 0 V V/OCT = 1 V LIN = MIDI 48 (C3); LIN 0 V = no note; clamped 0..127");
  CHECK(Math.abs(p.hz(0, 0) - 130.8128) < 1e-9 && Math.abs(p.hz(1, 2) - 261.6256) < 1e-9 && p.hz(1, -1) === 0, "PitchLaw: C3 = 130.8128 Hz; LIN 2 V = C4");
  CHECK(Math.abs(p.quantize(1, 5) - Math.pow(2, 27 / 12)) < 1e-12 && p.quantize(1, 0.03) === 0 && p.quantize(0, 5) === 5 && Math.abs(p.quantize(0, 0.53) - 0.5) < 1e-12,
        "PitchLaw QUANT: LIN 5 V -> note 75 (4.757 V), below 2^-5 V -> 0; V/OCT round(12 V)/12");
  CHECK(p.noteName(48) === "C3" && p.noteName(66) === "F#4" && p.noteName(0) === "C-1" && p.noteName(-1) === "--", "noteName: 48 = C3, 0 = C-1");
}
// ------------------------------------------------------------------ QUANT on the jack
{
  const r = new Rig(); r.set(P.RANGE_A, 0); r.set(P.STEPS, 0.53); r.set(P.QUANT_A, 1); r.press("MODE:START/STOP"); r.run(256);
  CHECK(Math.abs(r.out("OUTPUTS:CV A") - 0.5) < 1e-6, "QUANT SEMI on a V/OCT row: 0.53 V -> 0.5 V (F#3)");
  r.set(P.QUANT_A, 0); r.run(256); CHECK(Math.abs(r.out("OUTPUTS:CV A") - 0.53) < 1e-6, "QUANT OFF: knob volts are unchanged");
  const l = new Rig(); l.set(P.LAW_A, 1); l.set(P.QUANT_A, 1); l.set(P.STEPS, 1); l.press("MODE:START/STOP"); l.run(256);   // 5 V LIN
  CHECK(l.out("OUTPUTS:CV A") === Math.fround(Math.pow(2, 27 / 12)), "QUANT SEMI on a LIN row: 5 V -> 4.757 V (note 75, never past the rail)");
}
// ------------------------------------------------------------------ B8: mixer smoothing
{
  const r = new Rig(); r.patch({ "MIXER:IN 1": () => 5 }); r.set(P.LEVEL1, 0); r.run(256); r.set(P.LEVEL1, 1);
  const o = []; r.run(48000, () => o.push(r.out("MIXER:OUT")));
  let maxStep = 0; for (let i = 1; i < o.length; i++) maxStep = Math.max(maxStep, Math.abs(o[i] - o[i - 1]));
  CHECK(o[0] < 0.02 && maxStep < 0.02, "B8: a LEVEL jump ramps instead of stepping (no zipper)");
  CHECK(Math.abs(o[479] - 5 * (1 - Math.exp(-1))) < 0.01, "B8: 10 ms one-pole (63 % after 480 samples)");
  CHECK(o[o.length - 1] === 5, "B8: the level lands exactly (gain law unchanged)");
}
// ------------------------------------------------------------------ HOST sync
{
  const hostAt = (s, playing, bpm = 120, sr = 48000, ppq0 = 0) => ({ valid: true, playing, bpm, ppq: ppq0 + s * bpm / (60 * sr), samplePos: s });
  const ticksOf = (r, n, f) => { const ticks = []; let lh = -1; r.run(n, t => { let h = -1; for (let s = 0; s < 12; s++) if (r.out((s + 1) + ":TRIG") > 1) h = s; if (h >= 0 && h !== lh) ticks.push(t); if (f) f(t, h); lh = h }); return ticks };
  const q = new Rig(); q.set(P.SOURCE, 1); q.set(P.EXT_SOURCE, 1); q.set(P.DIV, 0.5); q.trf = s => hostAt(s, s < 180000);
  let trigWhileStopped = 0;
  const ticks = ticksOf(q, 240000, (t, h) => { if (t >= 180000 + 256 && h >= 0) ++trigWhileStopped });
  let even = ticks.length >= 29 && ticks[0] === 0; for (let k = 1; k < ticks.length; k++) even = even && ticks[k] - ticks[k - 1] === 6000;
  CHECK(even, "testHostSyncTicksOnSixteenths: 120 BPM at 1/16 ticks every 6000 samples +-0 at 48 kHz, step 1 on the transport start");
  CHECK(trigWhileStopped === 0 && !q.sq.isRunning(), "HOST: transport stop applies STOP (TRIGs low)");
  const e = new Rig(); e.set(P.SOURCE, 1); e.set(P.EXT_SOURCE, 1); e.set(P.DIV, 0); e.trf = s => hostAt(s, true, 90, 48000, 0.3);
  const t8 = ticksOf(e, 64000); let ok8 = t8.length >= 5 && t8[0] === 0; for (let k = 2; k < t8.length; k++) ok8 = ok8 && t8[k] - t8[k - 1] === 16000;
  CHECK(ok8 && t8[1] === 6400, "HOST 1/8 at 90 BPM: starting mid-step at ppq 0.3, the next tick lands on the host's next eighth (sample 6400), then every 16000");
  const n = new Rig(); n.set(P.SOURCE, 1); n.set(P.EXT_SOURCE, 1); n.press("MODE:START/STOP"); n.run(48000);
  CHECK(n.sq.currentStep() === 0 && n.sq.isRunning(), "HOST without a transport (the web page today): holds step 1 until a transport arrives, as the C++ does");
}
{ let m = B.create(48000); m.applyNewInstanceDefaults(true, true);
  CHECK(m.getParam(P.SOURCE) === 1 && m.getParam(P.EXT_SOURCE) === 1, "testHostDefaultOnlyNewRackInstancePlaying: new rack instance, transport playing -> HOST");
  m = B.create(48000); m.applyNewInstanceDefaults(true, false); CHECK(m.getParam(P.SOURCE) === 0 && m.getParam(P.EXT_SOURCE) === 0, "testHostDefaultOnlyNewRackInstancePlaying: stopped -> panel default (INT)");
  m = B.create(48000); m.applyNewInstanceDefaults(false, true); CHECK(m.getParam(P.SOURCE) === 0 && m.getParam(P.EXT_SOURCE) === 0, "testHostDefaultOnlyNewRackInstancePlaying: outside the rack -> panel (INT)");
  for (let src = 0; src < 3; src++) { const p = { "CLOCK:SOURCE": src ? 1 : 0, "CLOCK:EXT SOURCE": src === 2 ? 1 : 0 }, before = JSON.stringify(p); B.migrate(B.FORMAT, p, []);
    CHECK(JSON.stringify(p) === before, "testLoadKeepsSource: " + ["INT", "EXT-JACK", "EXT-HOST"][src] + " loads unchanged") }
  const old = { "CLOCK:SOURCE": 1 }; B.migrate(0, old, []); CHECK(old["CLOCK:SOURCE"] === 1 && old["CLOCK:EXT SOURCE"] === 0, "testLoadKeepsSource: a v1 EXT patch stays EXT-JACK (never flipped to HOST)") }

// ------------------------------------------------------------------ EXT pin and the v1 contract
{
  const v1 = ["CLOCK:CLOCK", "CLOCK:TEMPO CV", "INPUTS:START/STOP", "INPUTS:STEP", "INPUTS:RESET", "MIXER:IN 1", "MIXER:IN 2",
              "OUTPUTS:CV A", "OUTPUTS:GATE A", "OUTPUTS:CV B", "OUTPUTS:GATE B", "OUTPUTS:CV C", "MIXER:OUT"];
  for (let s = 1; s <= 12; s++) v1.push(s + ":TRIG");
  CHECK(eqArr(B.JACKS, v1), "the 25 v1 jacks keep their ids and order (no jack added, renamed or moved)");
  const ids = ["CLOCK:EXT SOURCE", "CLOCK:SETTLE", "CLOCK:TRIG MODE", "STEPS:LAW A", "STEPS:LAW B", "STEPS:QUANT A", "STEPS:QUANT B", "MIDI:CH A", "MIDI:CH B", "MIDI:VEL A", "MIDI:VEL B"];
  CHECK(B.PARAMS[P.DIV].id === "CLOCK:DIV" && B.PARAMS[P.SOURCE].id === "CLOCK:SOURCE" && B.PARAMS[P.SOURCE].positions === 2 && B.PARAMS.length === 61 &&
        eqArr(B.PARAMS.slice(P.DIV + 1).map(q => q.id), ids), "v1 params keep their indices; the 11 tab params follow DIV with the C++ ids and order");
  const edges = v => { const r = new Rig(); r.set(P.SOURCE, 1); r.press("MODE:START/STOP"); r.run(10); const t0 = r.t;
    r.patch({ "CLOCK:CLOCK": t => v[Math.floor((t - t0) / 100) % v.length] }); r.run(100 * v.length); return r.sq.currentStep() };
  CHECK(edges([0, 1.0, 0]) === 0, "testExtPinUnchanged: exactly 1.0 V is not an edge");
  CHECK(edges([0, 1.01, 0, 1.01]) === 2, "testExtPinUnchanged: above 1.0 V is an edge, re-armed below 0.5 V");
  CHECK(edges([0, 1.01, 0.6, 1.01]) === 1, "testExtPinUnchanged: 0.6 V does not re-arm (hysteresis)");
}
// ------------------------------------------------------------------ format 0 -> 1 migration (the page's patterns and rack patches)
{
  const lawOf = (cables, self) => { const p = {}; const r = B.migrate(0, p, cables, self); return [p, r] };
  { const [p] = lawOf([["BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:HZ/V"]]); CHECK(p["STEPS:LAW A"] === 1 && p["STEPS:LAW B"] === 0, "testMigrationPitchLawFromCables: row A into RONIN VCO:HZ/V -> LIN, row B -> V/OCT") }
  { const [p] = lawOf([["RONIN#1/VCO:V/OCT", "BUSHIDO#1/OUTPUTS:CV A"], ["BUSHIDO#1/OUTPUTS:CV B", "SHOGUN#1/LEAD:NOTE"]]); CHECK(p["STEPS:LAW A"] === 0 && p["STEPS:LAW B"] === 0, "testMigrationPitchLawFromCables: into RONIN V/OCT or SHOGUN NOTE -> V/OCT") }
  { const [p] = lawOf([["SQ-10#1/OUTPUTS:CV B", "MS-50#1/VCO:HZ/V"]]); CHECK(p["STEPS:LAW B"] === 1, "legacy SQ-10/MS-50 prefixes alias to BUSHIDO/RONIN") }
  { const [p, r] = lawOf([["BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"], ["BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:V/OCT"]]); CHECK(p["STEPS:LAW A"] === 1 && r.lawMismatch[0], "a row cabled to both gets LIN and flags its V/OCT cable") }
  { const [p] = lawOf([["BUSHIDO#2/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V"]], "BUSHIDO#1"); CHECK(p["STEPS:LAW A"] === 0, "another BUSHIDO's cable does not set this instance's law") }
  { const [p, r] = lawOf([["BUSHIDO/OUTPUTS:CV A", "RONIN/VCO:HZ/V", "red"]]); CHECK(p["STEPS:LAW A"] === 1 && r.lines.length > 0, "rack patch form (BUSHIDO/... RONIN/... with a colour) migrates too") }
  { const [p, r] = lawOf([]); CHECK(p["CLOCK:SETTLE"] === 1 && p["CLOCK:TRIG MODE"] === 0 && r.lines.length > 0, "format 0 -> 1: SETTLE = VINTAGE, TRIG MODE = STEP, report lines for SETUP") }
  { const p = { "A:1": 0.3 }; const r = B.migrate(7, p, []); CHECK(r.readOnly && Object.keys(p).length === 1, "a newer format loads read-only and untouched") }
  CHECK(B.canonicalJackId("SQ-10/CLOCK:CLOCK") === "BUSHIDO/CLOCK:CLOCK" && B.canonicalJackId("OUTPUTS:CV A") === "OUTPUTS:CV A", "alias table: SQ-10/ -> BUSHIDO/, bare ids unchanged");
}
// ------------------------------------------------------------------ B11: flush-to-zero of the CV state
{
  const r = new Rig(); r.set(P.PORTA_A, 0.1); r.set(P.MODE, 0); r.set(P.STEPS, 0.8); for (let i = 1; i < 12; i++) r.set(P.STEPS + i, 0);
  r.set(P.TEMPO, 0); r.press("MODE:START/STOP");                 // 0.5 steps/s: A1 at 4 V for 2 s, then 0 V for 22 s
  // Under FTZ the step (0 - cv) x k underflows to 0 before cv does, so cv parks on a tiny NORMAL float (about 1.1e-35 V), exactly
  // as the plugin does under ScopedNoDenormals (bit-exact against C++ with FTZ/DAZ in web/test_bushido_parity.js).
  let sub = 0, last = NaN, parked = -1; r.run(48000 * 8, t => { const v = r.out("OUTPUTS:CV A"); if (v !== 0 && Math.abs(v) < 1.17549435e-38) sub++; if (v !== last) { last = v; parked = t } });
  CHECK(sub === 0 && Math.abs(last) < 1e-30 && parked < 48000 * 4, "B11: the PORTA glide to 0 V never makes a subnormal float on CV A (parks at " + last.toExponential(2) + " V from sample " + parked + ")");
}
// ------------------------------------------------------------------ JCS R9 in web/rack_engine.js: every loop delayed, no module runs twice
{
  const e = RACK.create(48000); e.msg({ t: "devices", list: ["BUSHIDO#1", "BUSHIDO#2"] }); e.msg({ t: "monitor", mode: "off" });
  const s1 = e.devices["BUSHIDO#1"].sq, s2 = e.devices["BUSHIDO#2"].sq; let runs = [0, 0];
  const w1 = s1.processSample, w2 = s2.processSample; s1.processSample = () => { runs[0]++; w1() }; s2.processSample = () => { runs[1]++; w2() };
  e.setCables([["BUSHIDO#1/5:TRIG", "BUSHIDO#1/INPUTS:RESET"], ["BUSHIDO#1/OUTPUTS:GATE A", "BUSHIDO#2/INPUTS:STEP"], ["BUSHIDO#2/3:TRIG", "BUSHIDO#1/INPUTS:STEP"],
               ["BUSHIDO#2/7:TRIG", "BUSHIDO#2/INPUTS:RESET"]]);   // a self-patch, a two-device loop, then another self-patch: 3 feedback cables
  e.msg({ t: "press", d: "BUSHIDO#1", id: "MODE:START/STOP" }); e.msg({ t: "press", d: "BUSHIDO#2", id: "MODE:START/STOP" });
  const L = new Float32Array(4800), R = new Float32Array(4800); e.render(L, R, 4800);
  CHECK(runs[0] === 4800 && runs[1] === 4800, "R9: with three loops each module runs exactly once per sample (" + runs.join("/") + " runs in 4800 samples)");
  CHECK(e.snapshot().delayed === 3, "R9: every feedback cable is delayed one sample (3 of 4 cables), not only the newest");
  const f = RACK.create(48000); f.msg({ t: "devices", list: ["BUSHIDO#1"] }); f.setCables([["BUSHIDO#1/2:TRIG", "BUSHIDO#1/INPUTS:STEP"]]);
  CHECK(f.snapshot().delayed === 1, "R9: a single loop is delayed exactly as before (one cable)");
}
// ------------------------------------------------------------------ no 55 Hz / MIDI 33 left in the web build
{
  const fs = require("fs"); const bad = [];
  for (const f of ["bushido_dsp.js", "rack_engine.js", "bushido_template.html", "rack_template.html"]) {
    const s = fs.readFileSync(W + f, "utf8");
    if (/\b55\s*\*\s*(v|volts|ln|2)|33\s*\+\s*12|1 V = \$\{55|\(55\*/.test(s)) bad.push(f);
  }
  CHECK(bad.length === 0, "JCS R4: no 55 Hz / MIDI 33 pitch law left in the web sources" + (bad.length ? " (" + bad.join(", ") + ")" : ""));
}
console.log(fails ? fails + " FAILED (" + passes + " passed)" : "ALL PASSED (" + passes + ")"); process.exit(fails ? 1 : 0);
