// BUSHIDO engine for the web page: a line-for-line port of engine/Sq10Module.cpp, run one sample at a time
// so it can share one patch graph with the RONIN modules. Plain script: defines the global SQ10.
// Same module contract as the RONIN modules: numPorts(), port(i), processSample(), portValue[], inputConnected[].
var SQ10 = (function () {
  const JACKS = ["CLOCK:CLOCK", "CLOCK:TEMPO CV", "INPUTS:START/STOP", "INPUTS:STEP", "INPUTS:RESET", "MIXER:IN 1", "MIXER:IN 2",
    "OUTPUTS:CV A", "OUTPUTS:GATE A", "OUTPUTS:CV B", "OUTPUTS:GATE B", "OUTPUTS:CV C", "MIXER:OUT"];
  for (let i = 1; i <= 12; i++) JACKS.push(i + ":TRIG");
  const CLOCK_IN = 0, TEMPO_CV = 1, START_IN = 2, STEP_IN = 3, RESET_IN = 4, MIX_IN1 = 5, MIX_IN2 = 6,
        CV_A = 7, GATE_A = 8, CV_B = 9, GATE_B = 10, CV_C = 11, MIX_OUT = 12, TRIG1 = 13;
  // Port types for the shared graph. Inputs take raw volts (high above 1 V, low below 0.5 V), so they are CV.
  // GATE and TRIG are V-trig logic (0/5 V): typed Gate, so RONIN's own gate law (S-15) turns them into S-trig volts
  // when they feed a RONIN CV or audio input. Into BUSHIDO inputs they stay raw (a TRIG into RESET still works).
  function desc(i) {
    const name = JACKS[i], dir = i < CV_A ? "In" : "Out";
    let type = "CV";
    if (i === MIX_IN1 || i === MIX_IN2 || i === MIX_OUT) type = "Audio";
    if (i === GATE_A || i === GATE_B || i >= TRIG1) type = "Gate";
    return { name, type, dir, rest: 0, strigVolts: false };
  }
  const PARAMS = [];
  for (const r of "ABC") for (let i = 1; i <= 12; i++) PARAMS.push({ id: r + ":" + i, def: 0.5, positions: 0 });
  PARAMS.push({ id: "CH:PORTA A", def: 0, positions: 0 }, { id: "CH:PORTA B", def: 0, positions: 0 }, { id: "CH:RANGE A", def: 1, positions: 2 },
    { id: "CH:RANGE B", def: 1, positions: 2 }, { id: "CH:C MODE", def: 0, positions: 2 }, { id: "CLOCK:TEMPO", def: 0.5, positions: 0 },
    { id: "CLOCK:SOURCE", def: 0, positions: 2 }, { id: "MODE:MODE", def: 0.5, positions: 3 }, { id: "MODE:START/STOP", def: 0, positions: -1 },
    { id: "MODE:STEP", def: 0, positions: -1 }, { id: "MODE:RESET", def: 0, positions: -1 }, { id: "MIXER:LEVEL 1", def: 0.7, positions: 0 },
    { id: "MIXER:LEVEL 2", def: 0.7, positions: 0 }, { id: "CLOCK:DIV", def: 0.5, positions: 3 });
  const STEPS = 0, PORTA_A = 36, PORTA_B = 37, RANGE_A = 38, RANGE_B = 39, C_MODE = 40, TEMPO = 41, SOURCE = 42, MODE = 43,
        BTN_START = 44, LEVEL1 = 47, LEVEL2 = 48;
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const stepsPerSecond = t => 0.5 * Math.pow(2, t * 6);

  function create(sampleRate) {
    const m = { name: "SQ-10", JACKS, PARAMS, portValue: new Float64Array(JACKS.length), inputConnected: new Array(JACKS.length).fill(false) };
    const values = PARAMS.map(p => p.def), presses = [0, 0, 0];
    let sr = sampleRate, settle = Math.max(1, sr * 0.0006);
    let running = false, pos = -1, chan = 0, phase = 0, rate = 4, samplesInStep = 0, lastPeriod = 0.25, sinceTick = 0;
    let cvA = 0, cvB = 0, cvC = 0, tgtA = 0, tgtB = 0, gateOn = false;
    const high = [false, false, false, false];
    const p = i => values[i], mode = () => Math.round(p(MODE) * 2), outJacks = () => (mode() === 2 ? chan : 0);
    function edge(w, v) { if (!high[w] && v > 1) { high[w] = true; return true } if (high[w] && v < 0.5) high[w] = false; return false }
    function fire() { samplesInStep = 0; gateOn = true }
    function start() { running = true; pos = 0; chan = 0; phase = 0; fire() }
    function reset() { pos = 0; chan = 0; if (running) { phase = 0; fire() } else gateOn = false }
    function tick() {
      if (pos >= 0) lastPeriod = clamp(sinceTick, 0.005, 4);
      sinceTick = 0;
      if (pos < 0) { pos = 0; chan = 0; fire(); return }
      if (++pos < 12) { fire(); return }
      pos = 0; chan = mode() === 0 ? 0 : 1 - chan; fire();
    }
    const slew = porta => { const tau = porta * porta * 2; return tau < 1e-4 ? 1 : 1 - Math.exp(-1 / (tau * sr)) };

    m.numPorts = () => JACKS.length;
    m.port = desc;
    m.prepare = r => { sr = r; settle = Math.max(1, sr * 0.0006) };
    m.paramIndex = id => PARAMS.findIndex(q => q.id === id);
    m.setParam = (id, v) => {                                   // same snapping as Sq10Module::setParam
      const i = m.paramIndex(id); if (i < 0) return;
      const q = PARAMS[i];
      if (q.positions === -1) { if (v > 0.5 && values[i] <= 0.5) presses[i - BTN_START]++; }
      else if (q.positions >= 2) { const n = q.positions - 1; v = Math.round(clamp(v, 0, 1) * n) / n }
      values[i] = clamp(v, 0, 1);
    };
    m.press = id => { m.setParam(id, 1); m.setParam(id, 0) };
    m.processSample = () => {
      const pv = m.portValue;
      const rangeA = p(RANGE_A) > 0.5 ? 5 : 1, rangeB = p(RANGE_B) > 0.5 ? 5 : 1;
      const cIsTime = p(C_MODE) > 0.5, external = p(SOURCE) > 0.5;
      const pressNow = presses.slice(); presses[0] = presses[1] = presses[2] = 0;
      const doReset = edge(3, pv[RESET_IN]) || pressNow[2] > 0;
      const doStart = edge(1, pv[START_IN]) || pressNow[0] > 0;
      const doStep = edge(2, pv[STEP_IN]) || pressNow[1] > 0;
      const extClk = edge(0, pv[CLOCK_IN]);
      if (doStart) { if (running) { running = false; gateOn = false } else start() }
      if (doReset) reset();
      else {
        let t = doStep;
        if (running && !external) {
          rate = clamp(stepsPerSecond(p(TEMPO)) * Math.pow(2, pv[TEMPO_CV]), 0.05, 200);
          phase += rate / sr;
          if (phase >= 1) { phase -= 1; t = true }
        }
        if (running && external && extClk) t = true;
        if (t) tick();
      }
      sinceTick += 1 / sr; samplesInStep += 1;
      if (mode() === 0) chan = 0;
      const jk = outJacks(), settled = samplesInStep >= settle;
      if (pos >= 0 && settled) {
        const knob = p(STEPS + 12 * chan + pos);
        if (jk === 0) tgtA = knob * rangeA; else tgtB = knob * rangeB;
        cvC = p(STEPS + 24 + pos) * 5;
      }
      if (cIsTime) cvC = 0;
      cvA += (tgtA - cvA) * slew(p(PORTA_A)); cvB += (tgtB - cvB) * slew(p(PORTA_B));
      const period = (running && !external) ? 1 / rate : lastPeriod;
      const frac = (cIsTime && pos >= 0) ? 0.05 + 0.9 * p(STEPS + 24 + pos) : 0.5;
      const g = gateOn && pos >= 0 && settled && samplesInStep < settle + frac * period * sr;
      pv[CV_A] = cvA; pv[CV_B] = cvB; pv[CV_C] = cvC;
      pv[GATE_A] = g && jk === 0 ? 5 : 0; pv[GATE_B] = g && jk === 1 ? 5 : 0;
      pv[MIX_OUT] = pv[MIX_IN1] * p(LEVEL1) + pv[MIX_IN2] * p(LEVEL2);
      for (let s = 0; s < 12; s++) pv[TRIG1 + s] = pos === s ? 5 : 0;
    };
    m.state = () => ({ running, pos, chan, jk: outJacks(), cv: [cvA, cvB, cvC], gate: [m.portValue[GATE_A] > 1, m.portValue[GATE_B] > 1], mode: mode() });
    return m;
  }
  return { create, JACKS, stepsPerSecond, CV_A, CV_B, CV_C, GATE_A, GATE_B };
})();
if (typeof module !== "undefined") module.exports = SQ10;
