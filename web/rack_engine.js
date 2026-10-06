// One patch graph for the page's rack: any number of BUSHIDOs and RONINs, in rack order, run sample by sample in the
// same graph, under RONIN's PatchGraph rules (port of Source/Modular/PatchGraph.cpp publish/fillOrder/process), the same
// rules as JIDAI RACK's RackGraph:
//  - a cable runs Out -> In; Audio or CV into a Gate input is refused ("that jack does not take this cable");
//  - unpatched non-gate inputs sit at their rest volts, unpatched gate inputs keep their last value, patched inputs are summed
//    (outputs fan out, inputs sum);
//  - walking cables oldest to newest, a cable that closes a loop is feedback; only the newest feedback cable is delayed
//    one sample, older feedback cables stay zero-delay (their destinations run a second time);
//  - S-15: a Gate source (logic, not strigVolts) into a non-Gate RONIN input gives 0 V while high and +5 V while low.
// BUSHIDO inputs take plain volts (high above 1 V), so a RONIN logic gate reaches them as 0/5 V instead.
// A device is named by its kind and number, as in JIDAI RACK: "SQ-10#1" is BUSHIDO 1, "MS-50#2" is RONIN 2. Jack ids are
// SECTION:LABEL behind the device: "SQ-10#1/OUTPUTS:CV A", "MS-50#1/VCO:HZ/V".
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

  const isB = key => key.startsWith("SQ-10#");

  function create(sampleRate) {
    const sr = sampleRate;
    const sqParam = {}; SQ10.create(sr).PARAMS.forEach(p => { sqParam[p.id] = p.def });

    // ---- devices, in rack order. Each one owns a run of modules in the graph: a BUSHIDO one, a RONIN sixteen. ----
    const devs = {};                     // key -> device
    let rackOrder = [], mods = [], desc = [], kindR = [], owner = [], n = 0;
    function makeDevice(key) {
      if (isB(key)) {
        const sq = SQ10.create(sr); sq.sampleRate = sr; if (sq.prepare) sq.prepare(sr);
        return { key, b: true, sq, modules: [sq], P: Object.assign({}, sqParam), bypass: false, voices: [Voice(sr), Voice(sr)] };
      }
      const rack = MS50.createRack(sr);
      rack.modules.forEach(m => { m.sampleRate = sr; if (m.prepare) m.prepare(sr) });
      return { key, b: false, rack, modules: rack.modules };
    }
    function setDevices(keys) {          // keys in rack order; new ones are made, missing ones dropped
      const next = {};
      for (const k of keys) next[k] = devs[k] || makeDevice(k);
      for (const k in devs) delete devs[k];
      Object.assign(devs, next);
      rackOrder = keys.filter((k, i) => keys.indexOf(k) === i).map(k => devs[k]);
      mods = []; desc = []; kindR = []; owner = [];
      for (const d of rackOrder) {
        d.base = mods.length;
        for (const m of d.modules) { const a = []; for (let i = 0; i < m.numPorts(); i++) a.push(m.port(i)); mods.push(m); desc.push(a); kindR.push(!d.b); owner.push(d) }
      }
      n = mods.length;
      setCables(cableList);
    }

    function resolve(id) {
      const slash = id.indexOf("/"); if (slash < 0) return null;
      const d = devs[id.slice(0, slash)], jack = id.slice(slash + 1); if (!d) return null;
      if (d.b) { const p = SQ10.JACKS.indexOf(jack); return p < 0 ? null : { module: d.base, port: p } }
      const j = d.rack.jacks[jack]; return j ? { module: d.base + j.module, port: j.port } : null;
    }
    // Which way a cable between two jacks runs, and whether the graph takes it (same checks as attemptConnect).
    function check(a, b) {
      const A = resolve(a), B = resolve(b);
      if (!A || !B) return { ok: false, reason: "unwired" };
      const da = desc[A.module][A.port], db = desc[B.module][B.port];
      let s, d, sd, dd;
      if (da.dir === "Out" && db.dir === "In") { s = A; d = B; sd = da; dd = db; s.id = a; d.id = b }
      else if (da.dir === "In" && db.dir === "Out") { s = B; d = A; sd = db; dd = da; s.id = b; d.id = a }
      else return { ok: false, reason: da.dir === "Out" ? "two outputs" : "two inputs" };
      if (!typesAllowed(sd.type, dd.type)) return { ok: false, reason: "type", src: s, dst: d };
      return { ok: true, src: s, dst: d };
    }

    // ---- snapshot (publish + fillOrder) ----
    let cableList = [], cables = [], feedback = [], delayed = [], held = [], order = [], again = [], patched = [];
    function keptReaches(from, target, kept) {
      const seen = new Array(n).fill(false), q = [from]; seen[from] = true;
      while (q.length) { const m = q.shift(); for (let i = 0; i < cables.length; i++) { if (!kept[i] || cables[i].sm !== m) continue; const nx = cables[i].dm; if (nx === target) return true; if (!seen[nx]) { seen[nx] = true; q.push(nx) } } }
      return false;
    }
    function setCables(list) {          // list: [[jackId, jackId], ...] oldest first; illegal ones and ones to missing devices are skipped
      cableList = list;
      const prev = cables.map((c, i) => ({ c, d: delayed[i], h: held[i] }));
      cables = [];
      for (const [a, b] of list) { const r = check(a, b); if (!r.ok) continue; if (r.src.module === r.dst.module && r.src.port === r.dst.port) continue; cables.push({ sm: r.src.module, sp: r.src.port, dm: r.dst.module, dp: r.dst.port, s: r.src.id, d: r.dst.id }) }
      const kept = []; let newest = -1; feedback = []; delayed = []; held = [];
      for (let i = 0; i < cables.length; i++) {
        const c = cables[i], closes = c.sm === c.dm || keptReaches(c.dm, c.sm, kept);
        feedback[i] = closes; delayed[i] = false; kept[i] = !closes; if (closes) newest = i;
      }
      if (newest >= 0) delayed[newest] = true;
      for (let i = 0; i < cables.length; i++) {      // a delayed cable that was delayed before keeps its held sample
        held[i] = 0; if (!delayed[i]) continue; const c = cables[i];
        const o = prev.find(p => p.d && p.c.s === c.s && p.c.d === c.d); if (o) held[i] = o.h;
      }
      // fillOrder: Kahn's algorithm over the non-feedback edges, then any module left unplaced
      const adj = mods.map(() => []), indeg = new Array(n).fill(0);
      cables.forEach((c, i) => { if (feedback[i] || c.sm === c.dm || adj[c.sm].includes(c.dm)) return; adj[c.sm].push(c.dm); indeg[c.dm]++ });
      const q = []; for (let m = 0; m < n; m++) if (!indeg[m]) q.push(m);
      order = []; while (q.length) { const m = q.shift(); order.push(m); for (const d of adj[m]) if (--indeg[d] === 0) q.push(d) }
      if (order.length < n) for (let m = 0; m < n; m++) if (!order.includes(m)) order.push(m);
      patched = mods.map(m => new Array(m.numPorts()).fill(false)); cables.forEach(c => { patched[c.dm][c.dp] = true });
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
        if (kindR[mi]) { if (sd.type === "Gate" && dd.type !== "Gate" && !sd.strigVolts) v = v >= 0.5 ? 0 : 5 }   // S-15
        else if (kindR[c.sm] && sd.type === "Gate") v = sd.strigVolts ? (v < 1.5 ? 5 : 0) : (v >= 0.5 ? 5 : 0);   // RONIN gate into BUSHIDO: 0/5 V
        pv[c.dp] += v;
      }
    }
    function run(mi) { mods[mi].processSample(); const d = owner[mi]; if (d.b && d.bypass) { d.sq.portValue[SQ10.GATE_A] = 0; d.sq.portValue[SQ10.GATE_B] = 0 } }
    function processGraph() {
      for (let mi = 0; mi < n; mi++) clearInputs(mi);
      for (let k = 0; k < order.length; k++) { const mi = order[k]; contribute(mi, false); run(mi) }
      for (let k = 0; k < order.length; k++) { const mi = order[k]; if (!again[mi]) continue; clearInputs(mi); contribute(mi, true); run(mi) }
      for (let i = 0; i < cables.length; i++) if (delayed[i]) { const c = cables[i]; held[i] = mods[c.sm].portValue[c.sp] }
    }

    // ---- host: knobs, buttons, monitor, volume ----
    let monitor = "ext", oct = [2, 1], volume = 0.6, vol = 0.6;
    function msg(m) {
      if (m.t === "devices") { setDevices(m.list); return }
      if (m.t === "cables") { setCables(m.list); return }
      if (m.t === "monitor") { if (m.mode) monitor = m.mode; if (m.oct) oct = m.oct; return }
      if (m.t === "volume") { volume = m.v; return }
      const d = devs[m.d]; if (!d) return;
      if (d.b) {
        if (m.t === "param") { d.sq.setParam(m.id, m.v); d.P[m.id] = m.v }
        else if (m.t === "press") d.sq.press(m.id);
        else if (m.t === "bypass") d.bypass = !!m.on;
        return;
      }
      const rack = d.rack;
      if (m.t === "knob") rack.setKnob(m.id, m.v);     // host-level knobs (OUTPUT MIX / LEVEL) are applied by the rack glue
      else if (m.t === "knobs") for (const id in m.v) rack.setKnob(id, m.v[id]);
      else if (m.t === "hold") rack.setHold(!!m.on);
      else if (m.t === "power") rack.setPower(!!m.on);
      else if (m.t === "preset") { if (rack.applyPreset) rack.applyPreset(m.i) }
      else if (m.t === "meter") rack.setMeterSource(m.id);
    }
    let tick = 0, mon = 0, om = [16, 4];
    const every = Math.round(sr / 30);
    // Each BUSHIDO's own two VCOs. Together they play into the first RONIN's EXT IN (as a track's audio feeds the first
    // RONIN in JIDAI RACK), straight out, or not at all. With no RONIN on the rack, "into EXT IN" plays them straight out.
    function voicesNow() {
      let sum = 0;
      for (const d of rackOrder) {
        if (!d.b) continue;
        const v = d.voices, pv = d.sq.portValue;
        if (monitor === "off" || d.bypass) { v[0](false, 0, 1000, 1); v[1](false, 0, 1000, 1); continue }
        const time = d.P["CH:C MODE"] > 0.5, cut = time ? 900 : 250 + pv[SQ10.CV_C] / 5 * 5000;
        sum += v[0](pv[SQ10.GATE_A] > 1, pv[SQ10.CV_A], cut, om[0]) + v[1](pv[SQ10.GATE_B] > 1, pv[SQ10.CV_B], cut, om[1]);
      }
      return sum;
    }
    function render(L, R, frames) {
      om = [Math.pow(2, oct[0]), Math.pow(2, oct[1])];
      const ronins = rackOrder.filter(d => !d.b), toExt = monitor === "ext" && ronins.length > 0, dry = monitor === "out" || (monitor === "ext" && !ronins.length);
      for (let s = 0; s < frames; s++) {
        const m = mon;                                   // last sample's voices: they follow the jacks one sample late
        ronins.forEach((d, k) => d.rack.beforeGraph(toExt && k === 0 ? m : 0, toExt && k === 0 ? m : 0));
        processGraph();
        mon = voicesNow();
        let l = 0, r = 0;
        for (const d of ronins) { const out = d.rack.afterGraph(); l += out[0]; r += out[1] }
        if (dry) { l += m * 0.4; r += m * 0.4 }          // about as loud as through RONIN
        vol += (volume - vol) * 0.001;
        L[s] = Math.max(-1, Math.min(1, l * vol)); R[s] = Math.max(-1, Math.min(1, r * vol));
        if (++tick >= every) { tick = 0; if (api.post) api.post(snapshot()) }
      }
    }
    function snapshot() {
      const devices = {};
      for (const d of rackOrder) devices[d.key] = d.b ? { sq: d.sq.state() } : { meter: d.rack.meterNeedle(), power: d.rack.power() };
      return { t: "state", devices, delayed: cables.filter((c, i) => delayed[i]).length };
    }
    const api = { msg, render, check, setCables, setDevices, snapshot, post: null, resolve, processGraph, devices: devs, get mods() { return mods } };
    return api;
  }

  // Port types for the page (no audio): which jacks are inputs or outputs and what they carry, by jack id without
  // the device ("OUTPUTS:CV A", "VCO:HZ/V"), for each kind.
  function describe(sampleRate) {
    const sq = SQ10.create(sampleRate || 48000), rack = MS50.createRack(sampleRate || 48000), B = {}, R = {};
    SQ10.JACKS.forEach((id, p) => { B[id] = sq.port(p) });
    for (const id in rack.jacks) { const j = rack.jacks[id]; R[id] = j ? rack.modules[j.module].port(j.port) : null }
    return { B, R, rack, port(id) { const k = id.indexOf("/"), dev = id.slice(0, k), jack = id.slice(k + 1); return (isB(dev) ? B : R)[jack] || null } };
  }
  return { create, describe, typesAllowed };
})();
if (typeof module !== "undefined") module.exports = RACK;
