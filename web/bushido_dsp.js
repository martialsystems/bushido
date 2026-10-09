// BUSHIDO engine for the web page: a line-for-line port of engine/BushidoModule.cpp (with rack/PitchLaw.h, engine/MidiOut.h and
// engine/BushidoState.h), run one sample at a time so it can share one patch graph with the RONIN modules. Plain script: defines
// the global BUSHIDO_DSP. Same module contract as the RONIN modules: numPorts(), port(i), processSample(), portValue[], inputConnected[].
// Clock, transport and pitch follow the Jidai Cable Standard v1.1 (JCS R4, R5); see docs/REFERENCE.md.
// Parity: the C++ keeps parameters, CV and outputs in float and the clock in double. This port rounds to float (Math.fround) at
// the same places, so for the same inputs it gives the same outputs sample for sample (tests: web/test_bushido_redesign.js).
// Denormals (B11): the plugin runs under ScopedNoDenormals (FTZ/DAZ). Here the float CV state is flushed to 0 below the float
// subnormal threshold (|x| < 1.17549435e-38), which is the same result as the plugin, not a new snap.
var BUSHIDO_DSP = (function () {
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
  const f32 = Math.fround;
  // Indices 0..DIV are the v1 front panel and never move. Everything after DIV lives on a tab (CLOCK, STEPS, MIDI).
  const PARAMS = [];
  for (const r of "ABC") for (let i = 1; i <= 12; i++) PARAMS.push({ id: r + ":" + i, def: 0.5, positions: 0 });
  PARAMS.push({ id: "CH:PORTA A", def: 0, positions: 0 }, { id: "CH:PORTA B", def: 0, positions: 0 }, { id: "CH:RANGE A", def: 1, positions: 2 },
    { id: "CH:RANGE B", def: 1, positions: 2 }, { id: "CH:C MODE", def: 0, positions: 2 }, { id: "CLOCK:TEMPO", def: 0.5, positions: 0 },
    { id: "CLOCK:SOURCE", def: 0, positions: 2 }, { id: "MODE:MODE", def: 0.5, positions: 3 }, { id: "MODE:START/STOP", def: 0, positions: -1 },
    { id: "MODE:STEP", def: 0, positions: -1 }, { id: "MODE:RESET", def: 0, positions: -1 }, { id: "MIXER:LEVEL 1", def: 0.7, positions: 0 },
    { id: "MIXER:LEVEL 2", def: 0.7, positions: 0 }, { id: "CLOCK:DIV", def: 0.5, positions: 3 });
  PARAMS.push(                                                    // tab controls (the front panel is unchanged)
    { id: "CLOCK:EXT SOURCE", def: 0, positions: 2 },             // JACK / HOST, used when the front SOURCE switch is at EXT
    { id: "CLOCK:SETTLE", def: 0, positions: 2 },                 // TIGHT (2 samples, new patches) / VINTAGE (0.6 ms, migrated patches)
    { id: "CLOCK:TRIG MODE", def: 0, positions: 2 },              // STEP (whole step, default) / PULSE (5 ms)
    { id: "STEPS:LAW A", def: 0, positions: 2 }, { id: "STEPS:LAW B", def: 0, positions: 2 },       // V/OCT (default) / HZ/V LIN
    { id: "STEPS:QUANT A", def: 0, positions: 2 }, { id: "STEPS:QUANT B", def: 0, positions: 2 },   // OFF / SEMI
    { id: "MIDI:CH A", def: 0, positions: 16 }, { id: "MIDI:CH B", def: f32(1 / 15), positions: 16 }, // channel 1..16 (A on 1, B on 2)
    { id: "MIDI:VEL A", def: 0, positions: 2 }, { id: "MIDI:VEL B", def: 0, positions: 2 });        // 100 / FROM C
  const STEPS = 0, PORTA_A = 36, PORTA_B = 37, RANGE_A = 38, RANGE_B = 39, C_MODE = 40, TEMPO = 41, SOURCE = 42, MODE = 43,
        BTN_START = 44, BTN_STEP = 45, BTN_RESET = 46, LEVEL1 = 47, LEVEL2 = 48, DIV = 49,
        EXT_SOURCE = 50, SETTLE = 51, TRIG_MODE = 52, LAW_A = 53, LAW_B = 54, QUANT_A = 55, QUANT_B = 56,
        MIDI_CH_A = 57, MIDI_CH_B = 58, VEL_A = 59, VEL_B = 60, NUM_PARAMS = 61, NUM_V1_PARAMS = DIV + 1;
  const P = { STEPS, PORTA_A, PORTA_B, RANGE_A, RANGE_B, C_MODE, TEMPO, SOURCE, MODE, BTN_START, BTN_STEP, BTN_RESET, LEVEL1, LEVEL2, DIV,
              EXT_SOURCE, SETTLE, TRIG_MODE, LAW_A, LAW_B, QUANT_A, QUANT_B, MIDI_CH_A, MIDI_CH_B, VEL_A, VEL_B, NUM_PARAMS, NUM_V1_PARAMS };
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const lround = x => (x < 0 ? -Math.round(-x) : Math.round(x));  // C++ std::round / std::lround: halves away from zero
  const FLT_MIN = 1.17549435e-38;
  const ftz = x => (Math.abs(x) < FLT_MIN ? 0 : x);               // float op under FTZ/DAZ: a subnormal result is 0
  const f32z = x => ftz(f32(x));

  // Tempo in two units (BushidoModule.h). TEMPO is the internal clock in steps per second; DIV (1/8, 1/16, 1/32) only sets the
  // BPM readout's unit (2, 4 or 8 steps per beat) and the HOST step size. It never changes the INT clock.
  const stepsPerSecond = t => 0.5 * Math.pow(2, t * 6);           // 0.5..32 steps/s
  const stepsPerBeat = d => 2 << lround(f32(clamp(d, 0, 1) * 2));
  const bpm = (t, d) => stepsPerSecond(t) * 60 / stepsPerBeat(d);
  const tempoForBpm = (b, d) => f32(clamp(Math.log2(Math.max(1e-6, b * stepsPerBeat(d) / 60 / 0.5)) / 6, 0, 1));

  // ---- rack/PitchLaw.h (JCS R4). 0 V = C3 = 130.8127826502993 Hz (jidai-common kC3Hz, exactly 440 x 2^(-21/12)) = MIDI 48. The old 55 Hz / MIDI 33 reference is retired. ----
  const pitch = (function () {
    const Law = { VOct: 0, HzvLin: 1 }, kC3Hz = 130.8127826502993, kRefNote = 48, kRail = 5, kLinFloor = 1 / 32;
    const note = (law, v) => (law === Law.VOct ? kRefNote + 12 * v : v > 0 ? kRefNote + 12 * Math.log2(v) : NaN);
    const hz = (law, v) => (law === Law.VOct ? kC3Hz * Math.pow(2, v) : v > 0 ? kC3Hz * v : 0);
    function midiNote(law, v) { const n = note(law, v); if (Number.isNaN(n)) return -1; const r = lround(n); return r < 0 ? 0 : r > 127 ? 127 : r }
    function quantize(law, v) {                                   // QUANT SEMI, never past the +-5 V rail
      if (law === Law.VOct) { let s = lround(12 * v); if (s / 12 > kRail) s -= 1; if (s / 12 < -kRail) s += 1; return s / 12 }
      if (v < kLinFloor) return 0;
      let s = lround(12 * Math.log2(v)); if (Math.pow(2, s / 12) > kRail) s -= 1;
      return Math.pow(2, s / 12);
    }
    const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const noteName = n => (n < 0 ? "--" : NAMES[n % 12] + (Math.floor(n / 12) - 1));   // 48 = "C3"
    return { Law, kC3Hz, kRefNote, kRail, kLinFloor, note, hz, midiNote, quantize, noteName };
  })();

  // ---- engine/MidiOut.h: the MIDI convenience out, from the gate events (note from the TARGET volts under the row's law). ----
  function createMidiOut() {
    const note = [-1, -1], chan = [1, 2];
    const channel = (m, j) => 1 + lround(f32(m.getParam(j === 0 ? MIDI_CH_A : MIDI_CH_B) * 15));
    function velocity(m, j, cvC) {
      const fromC = m.getParam(j === 0 ? VEL_A : VEL_B) > 0.5 && m.getParam(C_MODE) < 0.5;
      if (!fromC) return 100;
      const v = lround(1 + 126 * cvC / 5); return v < 1 ? 1 : v > 127 ? 127 : v;
    }
    function handle(m, ev, emit) {                                // emit({sample, channel, note, velocity, on}) in order
      for (const g of ev) {
        const j = g.jack;
        if (g.on) {
          if (note[j] >= 0) emit({ sample: g.sample, channel: chan[j], note: note[j], velocity: 0, on: false });
          note[j] = pitch.midiNote(m.law(j), g.target); chan[j] = channel(m, j);
          if (note[j] >= 0) emit({ sample: g.sample, channel: chan[j], note: note[j], velocity: velocity(m, j, g.cvC), on: true });
        } else if (note[j] >= 0) { emit({ sample: g.sample, channel: chan[j], note: note[j], velocity: 0, on: false }); note[j] = -1 }
      }
    }
    function allOff(sample, emit) { for (let j = 0; j < 2; j++) if (note[j] >= 0) { emit({ sample, channel: chan[j], note: note[j], velocity: 0, on: false }); note[j] = -1 } }
    return { handle, allOff, channel, velocity };
  }

  // ---- engine/BushidoState.h: state format and migration (JCS R6, R7). A pattern without `format` is v1 (format 0). ----
  const FORMAT = 1;
  // Jack ids are read exactly as stored (JCS R6): no prefix aliases, so an unknown prefix simply binds to nothing.
  function splitJack(gid) {                                       // "BUSHIDO#1/OUTPUTS:CV A" -> ["BUSHIDO", "OUTPUTS:CV A"]
    const colon = gid.indexOf(":"), slash = gid.indexOf("/");     // the prefix ends at the first "/" before the first ":"
    if (slash < 0 || (colon >= 0 && slash > colon)) return ["", gid];   // bare, e.g. "INPUTS:START/STOP"
    let dev = gid.slice(0, slash); const hash = dev.indexOf("#"); if (hash >= 0) dev = dev.slice(0, hash);
    return [dev, gid.slice(slash + 1)];
  }
  // params: {id: value}, changed in place. cables: [[jackId, jackId], ...] (a third entry, the colour, is ignored).
  // self: this instance's rack key ("BUSHIDO#1"), or "" when the file is this unit's own (bare ids).
  function migrate(fromFormat, params, cables, self) {
    self = self || "";
    const r = { fromFormat, readOnly: false, lines: [], law: [0, 0], lawMismatch: [false, false] };
    if (fromFormat > FORMAT) { r.readOnly = true; r.lines.push("Saved by a newer BUSHIDO (format " + fromFormat + "): loaded read-only"); return r }
    if (fromFormat >= FORMAT) return r;
    // 0 -> 1. Patterns, knob volts, PORTA and SOURCE are untouched, so every cable carries exactly the same volts.
    params["CLOCK:SETTLE"] = 1; r.lines.push("SETTLE = VINTAGE (0.6 ms, as before)");
    params["CLOCK:TRIG MODE"] = 0; r.lines.push("TRIG MODE = STEP");
    params["CLOCK:EXT SOURCE"] = 0;                               // SOURCE stays as stored; never flipped to HOST
    const cvJack = ["OUTPUTS:CV A", "OUTPUTS:CV B"];
    const mine = (end, raw) => {
      if (!end[0]) return true;                                   // bare id: this unit's own file
      if (end[0] !== "BUSHIDO") return false;
      if (!self) return true;                                     // outside the rack: every BUSHIDO end is this one
      return raw.startsWith(self + "/") || (self === "BUSHIDO#1" && raw.startsWith("BUSHIDO/"));
    };
    for (let row = 0; row < 2; row++) {
      let toLin = false, toVoct = false;
      for (const cab of cables || []) {
        const a = cab[0], b = cab[1], ea = splitJack(a), eb = splitJack(b);
        for (let side = 0; side < 2; side++) {
          const src = side === 0 ? ea : eb, dst = side === 0 ? eb : ea, srcRaw = side === 0 ? a : b;
          if (src[1] !== cvJack[row] || !mine(src, srcRaw)) continue;
          if (dst[0] === "RONIN" && dst[1] === "VCO:HZ/V") toLin = true;
          else if ((dst[0] === "RONIN" && dst[1] === "VCO:V/OCT") || (dst[0] === "SHOGUN" && dst[1].includes(":NOTE"))) toVoct = true;
        }
      }
      r.law[row] = toLin ? 1 : 0; r.lawMismatch[row] = toLin && toVoct;
      params[row === 0 ? "STEPS:LAW A" : "STEPS:LAW B"] = toLin ? 1 : 0;
      r.lines.push("Row " + (row === 0 ? "A" : "B") + " PITCH LAW = " + (toLin ? "HZ/V LIN (cabled to RONIN VCO:HZ/V)" : "V/OCT")
                   + (r.lawMismatch[row] ? "; its V/OCT cable shows the mismatch badge" : ""));
    }
    return r;
  }

  const MAX_EVENTS = 128;

  function create(sampleRate) {
    const m = { name: "BUSHIDO", JACKS, PARAMS, portValue: new Float64Array(JACKS.length), inputConnected: new Array(JACKS.length).fill(false) };
    const values = PARAMS.map(q => f32(q.def)), presses = [0, 0, 0];
    let sr = sampleRate || 48000;
    let running = false, pos = -1, chan = 0, phase = 0, rate = 4;  // internal clock, steps per second
    let samplesInStep = 0, sinceTick = 0, settle = 2;
    let cvA = 0, cvB = 0, cvC = 0, tgtA = 0, tgtB = 0, gateOn = false;   // float-valued, like the C++
    // EXT period (JCS R5.2): measured between ticks, restarted at START
    let havePeriod = false, haveAnyPeriod = false, extPeriod = 0.25;
    let sampleCount = 0, absorbUntil = -1000;                     // START absorbs an EXT edge on its sample and the next 2
    let lastTempoCv = 1e30, lastTempoRate = -1;                  // INT clock: exp2 only when TEMPO CV moves
    let transport = { valid: false, playing: false, bpm: 120, ppq: 0, samplePos: 0 }, hostOffset = 0, hostPlayingPrev = false, hostStep = 0;
    let lvl1 = 0, lvl2 = 0, kMix = 1, levelsPrimed = false;       // mixer LEVEL smoothing, 10 ms
    let events = [];                                              // gate events for MIDI
    const gatePrev = [false, false], high = [false, false, false, false, false];
    let lastNote = null;                                          // the last note-on, for the page's readout
    const p = i => values[i], mode = () => lround(f32(p(MODE) * 2)), outJacks = () => (mode() === 2 ? chan : 0);
    const law = j => (p(j === 0 ? LAW_A : LAW_B) > 0.5 ? pitch.Law.HzvLin : pitch.Law.VOct);
    function edge(w, v) { if (!high[w] && v > 1) { high[w] = true; return true } if (high[w] && v < 0.5) high[w] = false; return false }   // JCS R3
    function fire() { samplesInStep = 0; gateOn = true }          // gate (and CV) start after the settle time
    function start(n) {                                           // JCS R5.2: every start plays A step 1 now
      running = true; pos = 0; chan = 0; phase = 0; fire();
      sinceTick = 0; havePeriod = false;                          // the stopped time is not a clock period
      absorbUntil = n + 2;                                        // an EXT edge on this sample or the next 2 is step 1's own clock
    }
    // HOST song position: song step k of the unpatched sequence, 12 steps (A) or 24 (A+B, ALT: row A then row B) from song step 0.
    function locate(k) {
      const len = mode() === 0 ? 12 : 24, s = ((k % len) + len) % len;
      chan = Math.floor(s / 12); pos = s % 12; phase = 0; fire();
      sinceTick = 0; havePeriod = false;                          // a jump is not a clock period
    }
    function stop() { running = false; gateOn = false }           // JCS R5.4: gates and TRIGs go low now; lamps and CV hold
    function reset() { pos = 0; chan = 0; if (running) { phase = 0; fire() } else gateOn = false }
    function tick() {
      if (havePeriod) { extPeriod = clamp(sinceTick, 0.005, 4); haveAnyPeriod = true }   // gate length for EXT and STEP
      havePeriod = true; sinceTick = 0;
      if (pos < 0) { pos = 0; chan = 0; fire(); return }
      if (++pos < 12) { fire(); return }
      pos = 0; chan = mode() === 0 ? 0 : 1 - chan; fire();
    }
    // PORTA law, kept bit-exact from v1 (user decision): tau = PORTA^2 x 2 s, k in double, the update in float.
    const slew = porta => { const tau = porta * porta * 2; return tau < 1e-4 ? 1 : f32(1 - Math.exp(-1 / (tau * sr))) };

    m.numPorts = () => JACKS.length;
    m.port = desc;
    m.prepare = r => {
      sr = r; kMix = -Math.expm1(-1 / (0.010 * sr)); levelsPrimed = false;
      sampleCount = 0; absorbUntil = -1000; events = []; lastTempoCv = 1e30; lastTempoRate = -1;
    };
    m.paramIndex = id => (typeof id === "number" ? id : PARAMS.findIndex(q => q.id === id));
    m.setParam = (id, v) => {                                     // same snapping as BushidoModule::setParam; id or index
      const i = m.paramIndex(id); if (i < 0 || i >= NUM_PARAMS) return;
      const q = PARAMS[i]; v = f32(v);
      if (q.positions === -1) { if (v > 0.5 && values[i] <= 0.5) presses[i - BTN_START]++ }
      else if (q.positions >= 2) { const n = q.positions - 1; v = f32(lround(f32(clamp(v, 0, 1) * n)) / n) }
      values[i] = clamp(v, 0, 1);
    };
    m.getParam = id => values[m.paramIndex(id)];
    m.press = id => { m.setParam(id, 1); m.setParam(id, 0) };
    m.law = law;
    m.setTransport = t => { transport = Object.assign({ valid: false, playing: false, bpm: 120, ppq: 0, samplePos: 0 }, t); hostOffset = 0 };
    // HOST is the default clock only for a NEW instance created in the rack while the transport plays.
    m.applyNewInstanceDefaults = (inRack, playing) => { if (inRack && playing) { m.setParam(SOURCE, 1); m.setParam(EXT_SOURCE, 1) } };
    // Gate rises and falls on the CV/GATE pairs: {sample, jack, on, target, cvC}. The page has no MIDI port; this is the hook.
    m.takeGateEvents = () => { const e = events; events = []; return e };
    m.samplesProcessed = () => sampleCount;
    m.measuredExtPeriod = () => (haveAnyPeriod ? extPeriod : 0);
    m.settleSamples = () => Math.ceil(settle);
    m.currentStep = () => pos;
    m.isRunning = () => running;

    m.processSample = () => {
      const pv = m.portValue;
      const rangeA = p(RANGE_A) > 0.5 ? 5 : 1, rangeB = p(RANGE_B) > 0.5 ? 5 : 1;
      const cIsTime = p(C_MODE) > 0.5, external = p(SOURCE) > 0.5;
      const host = external && p(EXT_SOURCE) > 0.5, jackClock = external && !host;
      const tempoRate = stepsPerSecond(p(TEMPO));
      const kA = slew(p(PORTA_A)), kB = slew(p(PORTA_B));
      settle = p(SETTLE) > 0.5 ? Math.max(1, sr * 0.0006) : 2;   // VINTAGE keeps v1's exact 0.6 ms; TIGHT is 2 samples (JCS R5.5)
      const pulse = p(TRIG_MODE) > 0.5, pulseLen = Math.max(1, lround(0.005 * sr));
      const lawA = law(0), lawB = law(1), quantA = p(QUANT_A) > 0.5, quantB = p(QUANT_B) > 0.5;
      const l1 = p(LEVEL1), l2 = p(LEVEL2);
      if (!levelsPrimed) { lvl1 = l1; lvl2 = l2; levelsPrimed = true }
      const q = stepsPerBeat(p(DIV));                             // HOST: steps per quarter, 2 / 4 / 8
      const hostValid = host && transport.valid && transport.bpm > 0;
      if (!host) hostPlayingPrev = false;

      const pressNow = presses.slice(); presses[0] = presses[1] = presses[2] = 0;
      const doReset = edge(3, pv[RESET_IN]) || pressNow[2] > 0;
      const doStart = edge(1, pv[START_IN]) || pressNow[0] > 0;
      const doStep = edge(2, pv[STEP_IN]) || pressNow[1] > 0;
      const extClk = edge(0, pv[CLOCK_IN]);

      // HOST (JCS R5.7): the song step is k = floor(ppq x q), locked to song position: a transport start, loop or jump goes to
      // the step k falls on (locate); the next song step is a tick. Transport stop applies STOP.
      let hostTick = false;
      if (hostValid) {
        const ppq = transport.ppq + hostOffset * transport.bpm / (60 * sr);
        const k = Math.floor(ppq * q + 1e-9), playing = !!transport.playing;
        if (playing && !hostPlayingPrev) { if (!running) start(sampleCount); locate(k) }
        else if (!playing && hostPlayingPrev) { if (running) stop() }
        else if (playing && running && k !== hostStep) { if (k === hostStep + 1) hostTick = true; else locate(k) }   // a loop or a jump
        if (playing) hostStep = k;                                  // also while stopped, so START mid-song plays A1 and then ticks
        hostPlayingPrev = playing;
      }
      ++hostOffset;

      if (doStart) { if (running) stop(); else start(sampleCount) }
      if (doReset) reset();
      else {
        let t = doStep;
        if (running && !external) {                               // TEMPO and TEMPO CV bend the internal clock only; EXT ignores both
          const v = f32(pv[TEMPO_CV]);
          if (Math.abs(v - lastTempoCv) > 1e-6 || tempoRate !== lastTempoRate) {
            lastTempoCv = v; lastTempoRate = tempoRate;
            rate = clamp(tempoRate * Math.pow(2, v), 0.05, 200);
          }
          phase += rate / sr;
          if (phase >= 1) { phase -= 1; t = true }
        }
        if (running && jackClock && extClk && sampleCount > absorbUntil) t = true;
        if (hostTick) t = true;
        if (t) tick();
      }
      sinceTick += 1 / sr; samplesInStep += 1;

      if (mode() === 0) chan = 0;                                 // switched to A mid-row B: carry on in row A
      const jk = outJacks(), settled = samplesInStep >= settle;
      if (pos >= 0 && settled) {
        const knob = p(STEPS + 12 * chan + pos);                  // range, law and portamento belong to the jacks
        if (jk === 0) { const v = f32(knob * rangeA); tgtA = quantA ? f32(pitch.quantize(lawA, v)) : v }
        else { const v = f32(knob * rangeB); tgtB = quantB ? f32(pitch.quantize(lawB, v)) : v }
        cvC = f32(p(STEPS + 24 + pos) * 5);
      }
      if (cIsTime) cvC = 0;                                       // TIME: row C sets gate length only
      cvA = f32z(cvA + f32z(f32z(tgtA - cvA) * kA)); cvB = f32z(cvB + f32z(f32z(tgtB - cvB) * kB));

      const period = host && running ? (hostValid ? 60 / (transport.bpm * q) : 1 / tempoRate)
                   : (running && !external) ? 1 / rate
                   : haveAnyPeriod ? extPeriod : 1 / tempoRate;  // EXT before a period is known: the INT tempo period
      const frac = (cIsTime && pos >= 0) ? 0.05 + 0.9 * p(STEPS + 24 + pos) : 0.5;
      const g = gateOn && pos >= 0 && settled && samplesInStep < settle + frac * period * sr;

      lvl1 += (l1 - lvl1) * kMix; lvl2 += (l2 - lvl2) * kMix;
      if (Math.abs(l1 - lvl1) < 1e-9) lvl1 = l1;                  // land exactly, so the smoother never decays into denormals
      if (Math.abs(l2 - lvl2) < 1e-9) lvl2 = l2;

      pv[CV_A] = cvA; pv[CV_B] = cvB; pv[CV_C] = cvC;
      const gA = g && jk === 0, gB = g && jk === 1;
      pv[GATE_A] = gA ? 5 : 0; pv[GATE_B] = gB ? 5 : 0;
      pv[MIX_OUT] = f32(f32(pv[MIX_IN1]) * lvl1 + f32(pv[MIX_IN2]) * lvl2);
      const trigOn = running && (!pulse || samplesInStep <= pulseLen);   // JCS R5.4: TRIG low while stopped
      for (let s = 0; s < 12; s++) pv[TRIG1 + s] = (trigOn && pos === s) ? 5 : 0;

      const gNow = [gA, gB];
      for (let j = 0; j < 2; j++) if (gNow[j] !== gatePrev[j]) {
        gatePrev[j] = gNow[j];
        const target = j === 0 ? tgtA : tgtB;
        if (events.length < MAX_EVENTS) events.push({ sample: sampleCount, jack: j, on: gNow[j], target, cvC });
        if (gNow[j]) lastNote = { jk: j, volts: target, law: law(j), note: pitch.midiNote(law(j), target), sample: sampleCount };
      }
      ++sampleCount;
    };
    m.state = () => ({ running, pos, chan, jk: outJacks(), cv: [cvA, cvB, cvC], tgt: [tgtA, tgtB], law: [law(0), law(1)],
                       gate: [m.portValue[GATE_A] > 1, m.portValue[GATE_B] > 1], mode: mode(), note: lastNote,
                       extPeriod: haveAnyPeriod ? extPeriod : 0, settle: Math.ceil(settle) });
    m.prepare(sr);
    return m;
  }
  return { create, JACKS, PARAMS, P, stepsPerSecond, stepsPerBeat, bpm, tempoForBpm, pitch, createMidiOut, FORMAT, migrate, splitJack,
           MAX_EVENTS, CLOCK_IN, TEMPO_CV, START_IN, STEP_IN, RESET_IN, MIX_IN1, MIX_IN2, CV_A, CV_B, CV_C, GATE_A, GATE_B, MIX_OUT, TRIG1 };
})();
if (typeof module !== "undefined") module.exports = BUSHIDO_DSP;
