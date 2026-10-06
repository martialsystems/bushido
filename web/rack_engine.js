// One patch graph for the page: BUSHIDO (SQ-10, module 0) and RONIN (MS-50, modules 1..N) run sample by sample in the
// same graph, under the MS-50's PatchGraph rules (port of Source/Modular/PatchGraph.cpp publish/fillOrder/process):
//  - a cable runs Out -> In; Audio or CV into a Gate input is refused ("that jack does not take this cable");
//  - unpatched non-gate inputs sit at their rest volts, unpatched gate inputs keep their last value, patched inputs are summed;
//  - walking cables oldest to newest, a cable that closes a loop is feedback; only the newest feedback cable is delayed
//    one sample, older feedback cables stay zero-delay (their destinations run a second time);
//  - S-15: a Gate source (logic, not strigVolts) into a non-Gate MS-50 input gives 0 V while high and +5 V while low.
// BUSHIDO inputs take plain volts (high above 1 V), so an MS-50 logic gate reaches them as 0/5 V instead.
// Jack ids are SECTION:LABEL with the instrument in front: "SQ-10/OUTPUTS:CV A", "MS-50/VCO:HZ/V".
// Plain script: needs the globals SQ10 and MS50, defines RACK. Runs in an AudioWorklet, a ScriptProcessor or Node.
var RACK = (function () {
  const ALLOWED = [[true, true, false], [true, true, false], [true, true, true]];
  const ti = t => (t === "Audio" ? 0 : t === "CV" ? 1 : 2);
  const typesAllowed = (a, b) => ALLOWED[ti(a)][ti(b)];

  // Monitor voice: BUSHIDO's own preview sound (two Hz/V VCOs on the A and B jacks), saw + detuned square into a
  // resonant low-pass with a per-note filter sweep. It listens to the jacks, so it plays exactly what the jacks carry.
  function Voice(sr) {
    let ph1 = 0, ph2 = 0, s1 = 0, s2 = 0, amp = 0, fc = 1000, fcT = 1000, prev = false, t = 0;
    const blep = (p, dt) => { if (p < dt) { p /= dt; return p + p - p * p - 1 } if (p > 1 - dt) { p = (p - 1) / dt; return p * p + p + p + 1 } return 0 };
    const kAtk = 1 / (0.004 * sr), cDec = 1 - Math.exp(-1 / (0.08 * sr)), cRel = 1 - Math.exp(-1 / (0.004 * sr)), cF = 1 - Math.exp(-1 / (0.07 * sr));
    return function (gate, volts, cut, octMul) {
      if (gate && !prev) { fc = cut * 3.2; fcT = cut * 0.7; t = 0 }
      prev = gate;
      const f = volts > 0 ? 55 * volts * octMul : 0, peak = volts > 0 ? 0.7 : 0;
      if (gate) { if (t < 0.004 * sr) amp = Math.min(peak, amp + peak * kAtk); else amp += (peak * 0.55 - amp) * cDec; t++ }
      else amp += (0 - amp) * cRel;
      if (t > 0.005 * sr) fc += (fcT - fc) * cF;
      if (amp < 1e-5 || f <= 0) return 0;
      const dt1 = Math.min(0.45, f / sr), dt2 = Math.min(0.45, f * 1.00347 / sr);   // second oscillator +6 cents
      ph1 += dt1; if (ph1 >= 1) ph1 -= 1; ph2 += dt2; if (ph2 >= 1) ph2 -= 1;
      const saw = 2 * ph1 - 1 - blep(ph1, dt1);
      let sq = ph2 < 0.5 ? 1 : -1; sq += blep(ph2, dt2); sq -= blep((ph2 + 0.5) % 1, dt2);
      const x = saw + 0.35 * sq;
      const g = Math.tan(Math.PI * Math.min(fc, sr * 0.45) / sr), k = 1 / 9, a1 = 1 / (1 + g * (g + k));   // TPT state-variable low-pass, Q 9
      const v1 = a1 * s1 + g * a1 * (x - s2), v2 = s2 + g * v1;
      s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
      return v2 * amp * 0.5;
    };
  }

  function create(sampleRate) {
    const sr = sampleRate, sq = SQ10.create(sr), rack = MS50.createRack(sr);
    const mods = [sq].concat(rack.modules), n = mods.length;
    mods.forEach(m => { m.sampleRate = sr; if (m.prepare) m.prepare(sr) });
    const desc = mods.map(m => { const a = []; for (let i = 0; i < m.numPorts(); i++) a.push(m.port(i)); return a });
    const sqParam = {}; SQ10.create(sr).PARAMS.forEach(p => { sqParam[p.id] = p.def });

    function resolve(id) {
      if (id.startsWith("SQ-10/")) { const p = SQ10.JACKS.indexOf(id.slice(6)); return p < 0 ? null : { module: 0, port: p } }
      if (id.startsWith("MS-50/")) { const j = rack.jacks[id.slice(6)]; return j ? { module: j.module + 1, port: j.port } : null }
      return null;
    }
    // Which way a cable between two jacks runs, and whether the graph takes it (same checks as attemptConnect).
    function check(a, b) {
      const A = resolve(a), B = resolve(b);
      if (!A || !B) return { ok: false, reason: "unwired" };
      const da = desc[A.module][A.port], db = desc[B.module][B.port];
      let s, d, sd, dd;
      if (da.dir === "Out" && db.dir === "In") { s = A; d = B; sd = da; dd = db }
      else if (da.dir === "In" && db.dir === "Out") { s = B; d = A; sd = db; dd = da }
      else return { ok: false, reason: da.dir === "Out" ? "two outputs" : "two inputs" };
      if (!typesAllowed(sd.type, dd.type)) return { ok: false, reason: "type", src: s, dst: d };
      return { ok: true, src: s, dst: d };
    }

    // ---- snapshot (publish + fillOrder) ----
    let cables = [], feedback = [], delayed = [], held = [], order = [], again = new Array(n).fill(false);
    const patched = mods.map(m => new Array(m.numPorts()).fill(false));
    function keptReaches(from, target, kept) {
      const seen = new Array(n).fill(false), q = [from]; seen[from] = true;
      while (q.length) { const m = q.shift(); for (let i = 0; i < cables.length; i++) { if (!kept[i] || cables[i].sm !== m) continue; const nx = cables[i].dm; if (nx === target) return true; if (!seen[nx]) { seen[nx] = true; q.push(nx) } } }
      return false;
    }
    function setCables(list) {          // list: [[jackId, jackId], ...] oldest first; illegal ones are skipped
      const prev = cables.map((c, i) => ({ c, d: delayed[i], h: held[i] }));
      cables = [];
      for (const [a, b] of list) { const r = check(a, b); if (!r.ok) continue; if (r.src.module === r.dst.module && r.src.port === r.dst.port) continue; cables.push({ sm: r.src.module, sp: r.src.port, dm: r.dst.module, dp: r.dst.port }) }
      const kept = []; let newest = -1; feedback = []; delayed = []; held = [];
      for (let i = 0; i < cables.length; i++) {
        const c = cables[i], closes = c.sm === c.dm || keptReaches(c.dm, c.sm, kept);
        feedback[i] = closes; delayed[i] = false; kept[i] = !closes; if (closes) newest = i;
      }
      if (newest >= 0) delayed[newest] = true;
      for (let i = 0; i < cables.length; i++) {
        held[i] = 0; if (!delayed[i]) continue; const c = cables[i];
        const o = prev.find(p => p.d && p.c.sm === c.sm && p.c.sp === c.sp && p.c.dm === c.dm && p.c.dp === c.dp); if (o) held[i] = o.h;
      }
      // fillOrder: Kahn's algorithm over the non-feedback edges, then any module left unplaced
      const adj = mods.map(() => []), indeg = new Array(n).fill(0);
      cables.forEach((c, i) => { if (feedback[i] || c.sm === c.dm || adj[c.sm].includes(c.dm)) return; adj[c.sm].push(c.dm); indeg[c.dm]++ });
      const q = []; for (let m = 0; m < n; m++) if (!indeg[m]) q.push(m);
      order = []; while (q.length) { const m = q.shift(); order.push(m); for (const d of adj[m]) if (--indeg[d] === 0) q.push(d) }
      if (order.length < n) for (let m = 0; m < n; m++) if (!order.includes(m)) order.push(m);
      patched.forEach(p => p.fill(false)); cables.forEach(c => { patched[c.dm][c.dp] = true });
      again = new Array(n).fill(false); cables.forEach((c, i) => { if (feedback[i] && !delayed[i]) again[c.dm] = true });
      // a module's inputConnected only changes here
      mods.forEach((m, mi) => { for (let p = 0; p < m.inputConnected.length; p++) m.inputConnected[p] = false; desc[mi].forEach((d, p) => { if (d.dir === "In") m.inputConnected[p] = patched[mi][p] }) });
      return cables.length;
    }
    function clearInputs(mi) {
      const m = mods[mi], pv = m.portValue, ds = desc[mi], pt = patched[mi];
      for (let p = 0; p < ds.length; p++) { const d = ds[p]; if (d.dir !== "In") continue; if (!pt[p]) { if (d.type !== "Gate") pv[p] = d.rest; continue } pv[p] = 0 }
    }
    function contribute(mi, withZeroDelayFeedback) {
      const pv = mods[mi].portValue;
      for (let i = 0; i < cables.length; i++) {
        const c = cables[i]; if (c.dm !== mi) continue;
        if (feedback[i] && !delayed[i] && !withZeroDelayFeedback) continue;
        let v = delayed[i] ? held[i] : mods[c.sm].portValue[c.sp];
        const sd = desc[c.sm][c.sp], dd = desc[mi][c.dp];
        if (mi > 0) { if (sd.type === "Gate" && dd.type !== "Gate" && !sd.strigVolts) v = v >= 0.5 ? 0 : 5 }   // S-15
        else if (c.sm > 0 && sd.type === "Gate") v = sd.strigVolts ? (v < 1.5 ? 5 : 0) : (v >= 0.5 ? 5 : 0);   // MS-50 gate into BUSHIDO: 0/5 V
        pv[c.dp] += v;
      }
    }
    let bypass = false;
    function processGraph() {
      for (let mi = 0; mi < n; mi++) clearInputs(mi);
      for (let k = 0; k < order.length; k++) { const mi = order[k]; contribute(mi, false); mods[mi].processSample(); if (mi === 0 && bypass) { sq.portValue[SQ10.GATE_A] = 0; sq.portValue[SQ10.GATE_B] = 0 } }
      for (let k = 0; k < order.length; k++) { const mi = order[k]; if (!again[mi]) continue; clearInputs(mi); contribute(mi, true); mods[mi].processSample(); if (mi === 0 && bypass) { sq.portValue[SQ10.GATE_A] = 0; sq.portValue[SQ10.GATE_B] = 0 } }
      for (let i = 0; i < cables.length; i++) if (delayed[i]) { const c = cables[i]; held[i] = mods[c.sm].portValue[c.sp] }
    }

    // ---- host: knobs, buttons, monitor, volume ----
    const voices = [Voice(sr), Voice(sr)];
    let monitor = "ext", oct = [2, 1], volume = 0.6, vol = 0.6, sqP = Object.assign({}, sqParam);
    function knob(id, v) {
      const k = rack.knobs[id]; if (!k) return;
      rack.setKnob(id, v);                              // host-level knobs (OUTPUT MIX / LEVEL) are applied by the rack glue
    }
    function msg(m) {
      if (m.t === "param") { sq.setParam(m.id, m.v); sqP[m.id] = m.v }
      else if (m.t === "press") sq.press(m.id);
      else if (m.t === "knob") knob(m.id, m.v);
      else if (m.t === "knobs") for (const id in m.v) knob(id, m.v[id]);
      else if (m.t === "cables") setCables(m.list);
      else if (m.t === "hold") rack.setHold(!!m.on);
      else if (m.t === "power") rack.setPower(!!m.on);
      else if (m.t === "preset") { if (rack.applyPreset) rack.applyPreset(m.i) }
      else if (m.t === "bypass") bypass = !!m.on;
      else if (m.t === "monitor") { if (m.mode) monitor = m.mode; if (m.oct) oct = m.oct }
      else if (m.t === "meter") rack.setMeterSource(m.id)
      else if (m.t === "volume") volume = m.v;
    }
    let tick = 0, mon = [0, 0];
    const every = Math.round(sr / 30), pv = sq.portValue;
    // BUSHIDO's VCOs play into RONIN's EXT IN (as a track's audio would in a DAW), straight out, or not at all.
    function voicesNow() {
      if (monitor === "off" || bypass) { voices[0](false, 0, 1000, 1); voices[1](false, 0, 1000, 1); return 0 }
      const time = sqP["CH:C MODE"] > 0.5, cut = time ? 900 : 250 + pv[SQ10.CV_C] / 5 * 5000;
      return voices[0](pv[SQ10.GATE_A] > 1, pv[SQ10.CV_A], cut, om[0]) + voices[1](pv[SQ10.GATE_B] > 1, pv[SQ10.CV_B], cut, om[1]);
    }
    let om = [16, 4];
    function render(L, R, frames) {
      om = [Math.pow(2, oct[0]), Math.pow(2, oct[1])];
      for (let s = 0; s < frames; s++) {
        const m = mon[0];                                // last sample's voices: they follow the jacks one sample late
        rack.beforeGraph(monitor === "ext" ? m : 0, monitor === "ext" ? m : 0);
        processGraph();
        mon[0] = voicesNow();
        const out = rack.afterGraph();
        let l = out[0], r = out[1];
        if (monitor === "out") { l += m * 0.4; r += m * 0.4 }   // about as loud as through RONIN
        vol += (volume - vol) * 0.001;
        L[s] = Math.max(-1, Math.min(1, l * vol)); R[s] = Math.max(-1, Math.min(1, r * vol));
        if (++tick >= every) { tick = 0; if (api.post) api.post(snapshot()) }
      }
    }
    function snapshot() {
      return { t: "state", sq: sq.state(), meter: rack.meterNeedle(), power: rack.power(), delayed: cables.filter((c, i) => delayed[i]).length };
    }
    const api = { msg, render, check, setCables, snapshot, post: null, mods, desc, resolve, sq, rack, processGraph };
    return api;
  }

  // Port types for the page (no audio): which jacks are inputs or outputs and what they carry.
  function describe(sampleRate) {
    const sq = SQ10.create(sampleRate || 48000), rack = MS50.createRack(sampleRate || 48000), out = {};
    SQ10.JACKS.forEach((id, p) => { out["SQ-10/" + id] = sq.port(p) });
    for (const id in rack.jacks) { const j = rack.jacks[id]; out["MS-50/" + id] = j ? rack.modules[j.module].port(j.port) : null }
    return { ports: out, rack };
  }
  return { create, describe, typesAllowed };
})();
if (typeof module !== "undefined") module.exports = RACK;
