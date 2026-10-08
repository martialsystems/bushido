// RONIN modules for the web page: a line-for-line port of RONIN Source/Modular/*.cpp plus the
// host glue of Source/PluginProcessor.cpp, run one sample at a time. Plain script: defines the global RONIN_DSP.
// Float32 math is reproduced with Math.fround wherever the C++ uses float (so results match the plugin to the
// last bit or close to it); double math stays double. portValue is a Float32Array so a graph that sums cables
// into it rounds exactly like the C++ `float portValue[8]`.
//
// Graph contract (PatchGraph.cpp is not ported): each sample the graph sets every input of every module like
// clearModuleInputs (inputConnected = patched; unpatched Audio/CV input = RONIN_DSP.restFor(desc), unpatched Gate
// input left untouched; patched input = 0), then per module in dependency order adds every cable landing on it
// (through RONIN_DSP.graphRules.promote) into portValue and calls processSample(). The newest cable that closes a
// cycle reads its source value from the previous sample.
var RONIN_DSP = (function () {
  "use strict";
  const F = Math.fround;
  const PI = 3.14159265358979323846;
  const PI_F = F(PI);
  const fin = Number.isFinite;

  // ---------- shared helpers ----------
  function clamp01(v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }              // float in, float out
  function clampf(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
  const FLUSH_F = F(1.0e-15);
  function flushF(s) { return (!fin(s) || (s < FLUSH_F && s > -FLUSH_F)) ? 0 : s; }
  function flushD(s) { return (!fin(s) || (s < 1.0e-15 && s > -1.0e-15)) ? 0 : s; }
  const powf = (a, b) => F(Math.pow(a, b));
  const expf = (x) => F(Math.exp(x));
  const logf = (x) => F(Math.log(x));
  function rateF(sr) { return F(sr > 1.0 ? sr : 48000.0); }
  function rateD(sr) { return sr > 1.0 ? sr : 48000.0; }
  // 1 - exp(-1 / (tau * rate)) in float
  function coeffF(tau, rate) { return F(1 - expf(F(-1 / F(tau * rate)))); }

  function P(name, type, dir, rest, strigVolts) {
    return { name: name, type: type, dir: dir, rest: rest === undefined ? 0 : F(rest), strigVolts: !!strigVolts };
  }

  // Base object. Every module has portValue[8] (float32) and inputConnected[8].
  function base(name) {
    return {
      name: name,
      portValue: new Float32Array(8),
      inputConnected: [false, false, false, false, false, false, false, false],
      sampleRate: 48000.0,
      presetKnobCount() { return 0; },
      presetKnob() { return 0; },
      applyFactoryPreset() {},
      presetScaleIndex() { return -1; },
      numKnobs() { return 0; },
      setKnob() {},
    };
  }

  // ---------- Ext In (plugin boundary, S-01/S-22) ----------
  function knobForRatio(ratio, span) {
    if (ratio < 1) ratio = 1;
    return F(logf(ratio) / logf(span));
  }
  function ExtIn() {
    const m = base("Ext In");
    const PORTS = [P("L", "Audio", "Out"), P("R", "Audio", "Out"), P("Mono", "Audio", "Out"), P("Gate", "Gate", "Out", 5, true)];
    const kAttack = F(0.005);
    let inL = 0, inR = 0, env = 0, held = false;
    let threshold01 = knobForRatio(F(F(0.2) / F(0.05)), 40);
    let release01 = knobForRatio(F(F(0.080) / F(0.010)), 50);
    const thresholdVolts = () => F(F(0.05) * powf(40, clamp01(threshold01)));
    const releaseSeconds = () => F(F(0.010) * powf(50, clamp01(release01)));
    m.numPorts = () => 4;
    m.port = (i) => PORTS[i < 3 ? i : 3];
    m.numKnobs = () => 2;
    m.setKnob = (k, v) => { v = clamp01(F(v)); if (k === 0) threshold01 = v; else if (k === 1) release01 = v; };
    m.presetKnobCount = () => 2;
    m.presetKnob = (k) => (k === 0 ? threshold01 : k === 1 ? release01 : 0);
    m.prepare = (sr) => { m.sampleRate = sr; env = 0; };
    m.setHostSample = (l, r) => { inL = F(F(l) * 5); inR = F(F(r) * 5); };
    m.setButtonHeld = (h) => { held = !!h; };
    m.buttonHeld = () => held;
    m.processSample = () => {
      const pv = m.portValue;
      pv[0] = inL; pv[1] = inR;
      const mono = F(0.5 * F(inL + inR));
      pv[2] = mono;
      const level = Math.abs(mono);
      const rate = rateF(m.sampleRate);
      const tau = level > env ? kAttack : releaseSeconds();
      env = F(env + F(F(level - env) * coeffF(tau, rate)));
      env = flushF(env);
      const open = held || env > thresholdVolts();
      pv[3] = open ? 0 : 5;
    };
    return m;
  }

  // ---------- Output (plugin boundary) ----------
  const kUnity = F(0.7);
  function outputLevelGain(knob) {
    const travel = knob < 0 ? 0 : (knob > 1 ? 1 : knob);
    if (travel <= kUnity) return F(travel / kUnity);
    return F(1 + F(F(travel - kUnity) / F(1 - kUnity)));
  }
  function outputMixAfterSwitch(effectOn, mixKnob) {
    if (!effectOn) return 0;
    if (mixKnob < 0) return 0;
    if (mixKnob > 1) return 1;
    return mixKnob;
  }
  function OutputModule() {
    const m = base("Output");
    const PORTS = [P("L", "Audio", "In"), P("R", "Audio", "In"), P("Wet", "Audio", "In")];
    let mix = 0, level = 1, outLevel = kUnity, ampGain = outputLevelGain(kUnity), hostL = 0, hostR = 0;
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.numKnobs = () => 2;
    m.setMix = (v) => { mix = clamp01(F(v)); };
    m.setLevel = (v) => { level = clamp01(F(v)); };
    m.setOutputLevel = (v) => { outLevel = clamp01(F(v)); ampGain = outputLevelGain(outLevel); };
    m.outputLevel = () => outLevel;
    m.mix = () => mix;
    m.setKnob = (k, v) => { if (k === 0) m.setMix(v); else if (k === 1) m.setLevel(v); };
    m.presetKnobCount = () => 2;
    m.presetKnob = (k) => (k === 0 ? mix : k === 1 ? level : 0);
    m.prepare = (sr) => { m.sampleRate = sr; };
    m.hostLeft = () => hostL;
    m.hostRight = () => hostR;
    m.processSample = () => {
      const pv = m.portValue;
      const wet = pv[2];
      const dry = F(1 - mix);
      const gain = F(F(level * ampGain) * F(0.2));
      const wm = F(wet * mix);
      hostL = F(F(F(pv[0] * dry) + wm) * gain);
      hostR = F(F(F(pv[1] * dry) + wm) * gain);
    };
    return m;
  }

  // ---------- Noise (S-17) ----------
  const kSeed = 0xA341316C >>> 0;
  function Noise() {
    const m = base("Noise");
    const PORTS = [P("White", "Audio", "Out"), P("Pink", "Audio", "Out")];
    const kWhiteScale = F(F(2.5) / F(3.0)), kPinkTrim = F(0.2), kUnit = F(1 / 8388608);
    const c = [0.99886, 0.0555179, 0.99332, 0.0750759, 0.96900, 0.1538520, 0.86650, 0.3104856, 0.55000, 0.5329522,
      -0.7616, 0.0168980, 0.5362, 0.115926].map(F);
    let rng = kSeed, b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    function nextUniform() {
      let x = rng;
      x = (x ^ (x << 13)) >>> 0;
      x = (x ^ (x >>> 17)) >>> 0;
      x = (x ^ (x << 5)) >>> 0;
      rng = x;
      const top = x >>> 8;
      return F((top - 8388608) * kUnit);
    }
    m.numPorts = () => 2;
    m.port = (i) => PORTS[i === 0 ? 0 : 1];
    m.prepare = (sr) => { m.sampleRate = sr; rng = kSeed; b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0; };
    m.processSample = () => {
      const u1 = nextUniform(), u2 = nextUniform(), u3 = nextUniform();
      const sum = F(F(u1 + u2) + u3);
      const white = F(sum * kWhiteScale);
      b0 = F(F(c[0] * b0) + F(white * c[1]));
      b1 = F(F(c[2] * b1) + F(white * c[3]));
      b2 = F(F(c[4] * b2) + F(white * c[5]));
      b3 = F(F(c[6] * b3) + F(white * c[7]));
      b4 = F(F(c[8] * b4) + F(white * c[9]));
      b5 = F(F(c[10] * b5) - F(white * c[11]));
      let s = F(b0 + b1); s = F(s + b2); s = F(s + b3); s = F(s + b4); s = F(s + b5); s = F(s + b6);
      s = F(s + F(white * c[12]));
      const pink = F(s * kPinkTrim);
      b6 = F(white * c[13]);
      m.portValue[0] = white;
      m.portValue[1] = pink;
    };
    return m;
  }

  // ---------- VCF (step 20, S-07..S-10) ----------
  const kIs = 2.52e-9, kN = 1.752, kVt = 0.02585, kBridgeC = 22.0e-9, kInputPull = 0.012, kDiodeVolts = 5.0;
  function biasForHz(hz) {
    const nVt = kN * kVt;
    const ib = hz * (2.0 * PI * nVt * kBridgeC);
    if (ib <= 0.0) return 0.0;
    return nVt * Math.log(ib / kIs + 1.0);
  }
  function hzFromBias(vBias) {
    const nVt = kN * kVt;
    if (vBias < 1.0e-4) vBias = 1.0e-4;
    const ib = kIs * (Math.exp(vBias / nVt) - 1.0);
    if (ib <= 0.0) return 15.0;
    const rd = nVt / ib;
    const r1 = rd, r2 = rd;
    return 1.0 / (2.0 * PI * Math.sqrt(r1 * r2) * kBridgeC);
  }
  function Vcf() {
    const m = base("VCF");
    const PORTS = [P("SigIn", "Audio", "In"), P("Cutoff", "CV", "In"), P("SigOut", "Audio", "Out")];
    let cutoff01 = F(0.55), peak01 = F(0.15), amount01 = F(0.4);
    let z1 = 0, z2 = 0, env = 0, hpX = 0, hpY = 0;
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.numKnobs = () => 3;
    m.setKnob = (k, v) => { v = clamp01(F(v)); if (k === 0) cutoff01 = v; else if (k === 1) peak01 = v; else if (k === 2) amount01 = v; };
    m.presetKnobCount = () => 3;
    m.presetKnob = (k) => (k === 0 ? cutoff01 : k === 1 ? peak01 : k === 2 ? amount01 : 0);
    m.prepare = (sr) => { m.sampleRate = sr; z1 = z2 = env = hpX = hpY = 0; };
    const knobHz = () => F(20 * powf(900, cutoff01));
    function cutoffHz() {
      const cv = F(m.portValue[1] * amount01);
      const hz = F(knobHz() * powf(2, F(F(cv / 5) * 4)));
      if (!fin(hz)) return 15;
      if (hz < 15) return 15;
      if (hz > 20000) return 20000;
      return hz;
    }
    function resonance() {
      const q = F(0.5 + F(peak01 * F(7.5)));
      if (q < 0.5) return 0.5;
      if (q > 8) return 8;
      return q;
    }
    m.processSample = () => {
      let input = m.portValue[0];
      if (!fin(input)) input = 0;
      const q = resonance();
      const rate = rateD(m.sampleRate);
      const envA = 1.0 - Math.exp(-1.0 / (0.03 * rate));
      env += (Math.abs(input) - env) * envA;
      env = flushD(env);
      const target = cutoffHz();
      const vBias = biasForHz(target) - kInputPull * env;
      let hz = hzFromBias(vBias);
      if (!fin(hz) || hz < 15.0) hz = 15.0;
      if (hz > 20000.0) hz = 20000.0;
      if (hz > rate * 0.45) hz = rate * 0.45;
      const g = Math.tan(PI * hz / rate);
      const k = 1.0 / q;
      const a1 = 1.0 / (1.0 + g * (g + k));
      const a2 = g * a1;
      const a3 = g * a2;
      const feedback = kDiodeVolts * Math.tanh(z2 / kDiodeVolts);
      const v3 = input - feedback;
      const v1 = a1 * z1 + a2 * v3;
      const v2 = feedback + a2 * z1 + a3 * v3;
      z1 = 2.0 * v1 - z1;
      z2 = 2.0 * v2 - feedback;
      z1 = flushD(z1); z2 = flushD(z2);
      const hpA = Math.exp(-2.0 * PI * 5.0 / rate);
      const hp = hpA * (hpY + v2 - hpX);
      hpX = v2; hpY = hp;
      hpX = flushD(hpX); hpY = flushD(hpY);
      let out = F(hp);
      if (!fin(out)) out = 0;
      m.portValue[2] = out;
    };
    return m;
  }

  // ---------- VCA 1 (S-11) ----------
  // The factory bank is INIT only (FactoryPresets.h): one program, index 0, and it leaves VCA 1 Initial at 0.
  const kDefaultFactoryPreset = 0;
  function factoryVca1Initial(index) { return 0; }
  function Vca1() {
    const m = base("VCA 1");
    const PORTS = [P("SigIn", "Audio", "In"), P("Env", "CV", "In"), P("Out", "Audio", "Out")];
    let lowCut01 = 0, intensity = F(0.85), initial01 = 0, low = 0;
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.numKnobs = () => 2;
    // knob 2 is Initial: not a panel knob, set by a factory program load (applyFactoryPreset); INIT sets 0
    m.setKnob = (k, v) => { v = clamp01(F(v)); if (k === 0) lowCut01 = v; else if (k === 1) intensity = v; else if (k === 2) initial01 = v; };
    m.presetKnobCount = () => 2;
    m.presetKnob = (k) => (k === 0 ? lowCut01 : k === 1 ? intensity : 0);
    m.applyFactoryPreset = (i) => m.setKnob(2, factoryVca1Initial(i));
    m.initial = () => initial01;
    m.prepare = (sr) => { m.sampleRate = sr; low = 0; };
    m.processSample = () => {
      const pv = m.portValue;
      const input = pv[0];
      const hz = F(10 * powf(200, lowCut01));
      const rate = rateF(m.sampleRate);
      const tau = F(1 / F(F(2 * PI_F) * hz));
      const coeff = coeffF(tau, rate);
      low = F(low + F(F(input - low) * coeff));
      low = flushF(low);
      let envGain = F(F(pv[1] / 5) + initial01);
      envGain = clamp01(envGain);
      let out = F(F(F(input - low) * envGain) * intensity);
      if (!fin(out)) out = 0;
      pv[2] = out;
    };
    return m;
  }

  // ---------- VCA 2 (S-12) ----------
  function Vca2() {
    const m = base("VCA 2");
    const PORTS = [P("In", "CV", "In"), P("Control", "CV", "In"), P("Out", "CV", "Out")];
    let gain = 0;
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.prepare = (sr) => { m.sampleRate = sr; gain = 0; };
    m.processSample = () => {
      const pv = m.portValue;
      const target = clamp01(F(pv[1] / 5));
      const rate = rateF(m.sampleRate);
      const coeff = coeffF(F(0.020), rate);
      gain = F(gain + F(F(target - gain) * coeff));
      gain = flushF(gain);
      let out = F(pv[0] * gain);
      if (!fin(out)) out = 0;
      pv[2] = out;
    };
    return m;
  }

  // ---------- EG 1 (S-13..S-15) ----------
  const kHeldBelow = F(1.5), kAttackDone = F(4.99), kSettle = F(0.01), kSpan = 5;
  const knobForSeconds = (s) => F(logf(F(s / F(0.001))) / logf(10000));
  const secondsFor = (k) => F(F(0.001) * powf(10000, clamp01(k)));
  const IDLE = 0, ATTACK = 1, DECAY = 2, SUSTAIN = 3, RELEASE = 4, WAIT = 5;
  function Eg1() {
    const m = base("EG 1");
    const PORTS = [P("Trig", "CV", "In", 5), P("OutA", "CV", "Out"), P("OutB", "CV", "Out"), P("OutC", "CV", "Out")];
    const kn = [knobForSeconds(F(0.01)), knobForSeconds(F(0.25)), F(0.6), knobForSeconds(F(0.30))];
    let outA = 0, wasHeld = false, stage = IDLE;
    m.numPorts = () => 4;
    m.port = (i) => PORTS[i < 3 ? i : 3];
    m.numKnobs = () => 4;
    m.setKnob = (k, v) => { if (k >= 0 && k < 4) kn[k] = clamp01(F(v)); };
    m.presetKnobCount = () => 4;
    m.presetKnob = (k) => (k >= 0 && k < 4 ? kn[k] : 0);
    m.prepare = (sr) => { m.sampleRate = sr; outA = 0; wasHeld = false; stage = IDLE; };
    function follow(target, seconds) {
      const rate = rateF(m.sampleRate);
      const tau = seconds > F(1.0e-6) ? seconds : F(1.0e-6);
      outA = F(outA + F(F(target - outA) * coeffF(tau, rate)));
      outA = flushF(outA);
    }
    m.processSample = () => {
      const pv = m.portValue;
      const held = pv[0] < kHeldBelow;
      const rising = held && !wasHeld;
      wasHeld = held;
      if (rising) stage = ATTACK;
      else if (!held && stage !== IDLE && stage !== RELEASE) stage = RELEASE;
      const sus = F(kn[2] * kSpan);
      if (stage === ATTACK) {
        follow(kSpan, secondsFor(kn[0]));
        if (outA >= kAttackDone) stage = DECAY;
      } else if (stage === DECAY) {
        follow(sus, secondsFor(kn[1]));
        if (Math.abs(F(outA - sus)) <= kSettle) { outA = sus; stage = SUSTAIN; }
      } else if (stage === SUSTAIN) {
        outA = sus;
      } else if (stage === RELEASE) {
        follow(0, secondsFor(kn[3]));
        if (outA <= kSettle) { outA = 0; stage = IDLE; }
      }
      if (!fin(outA)) { outA = 0; stage = IDLE; }
      pv[1] = outA;
      pv[2] = -outA;
      pv[3] = F(outA - sus);
    };
    return m;
  }

  // ---------- MG (S-16) ----------
  function Mg() {
    const m = base("MG");
    const PORTS = [P("FreqMod", "CV", "In"), P("PWM", "CV", "In"), P("Tri", "Audio", "Out"), P("SawUp", "Audio", "Out"),
      P("SawDown", "Audio", "Out"), P("Pulse", "Audio", "Out")];
    const kMinHz = F(0.01), kMaxHz = 200, SYM_LO = F(1.0e-6), SYM_HI = F(1 - F(1.0e-6));
    let freq01 = F(logf(500) / logf(20000)), pw01 = F(0.5), phase = 0;
    function morph(ph, sym) {
      if (sym <= SYM_LO) return F(2.5 - F(5 * ph));
      if (sym >= SYM_HI) return F(-2.5 + F(5 * ph));
      if (ph < sym) return F(-2.5 + F(5 * F(ph / sym)));
      return F(2.5 - F(5 * F(F(ph - sym) / F(1 - sym))));
    }
    m.numPorts = () => 6;
    m.port = (i) => PORTS[i < 5 ? i : 5];
    m.numKnobs = () => 2;
    m.setKnob = (k, v) => { v = clamp01(F(v)); if (k === 0) freq01 = v; else if (k === 1) pw01 = v; };
    m.presetKnobCount = () => 2;
    m.presetKnob = (k) => (k === 0 ? freq01 : k === 1 ? pw01 : 0);
    m.prepare = (sr) => { m.sampleRate = sr; phase = 0; };
    m.processSample = () => {
      const pv = m.portValue;
      const b = F(kMinHz * powf(20000, clamp01(freq01)));
      const fm = pv[0];
      const hz = clampf(F(b + F(F(fm / 5) * b)), kMinHz, kMaxHz);
      const rate = rateF(m.sampleRate);
      phase += hz / rate;
      if (phase >= 1.0) phase -= Math.floor(phase);
      const ph = F(phase);
      const sym = clampf(F(pw01 + F(pv[1] / 5)), 0, 1);
      const duty = F(F(0.05) + F(sym * F(0.90)));
      const sawUp = F(-2.5 + F(5 * ph));
      pv[2] = morph(ph, sym);
      pv[3] = sawUp;
      pv[4] = -sawUp;
      pv[5] = ph < duty ? 5 : 0;
    };
    return m;
  }

  // ---------- VCO (S-02..S-06) ----------
  const kFootage = [32.703, 65.406, 130.813, 261.626].map(F);
  const DT_MIN = F(1.0e-8);
  function polyBlep(t, dt) {
    if (dt <= DT_MIN) return 0;
    if (t < dt) {
      const x = F(t / dt);
      return F(F(F(x + x) - F(x * x)) - 1);
    }
    if (t > F(1 - dt)) {
      const x = F(F(t - 1) / dt);
      return F(F(F(F(x * x) + x) + x) + 1);
    }
    return 0;
  }
  function Vco() {
    const m = base("VCO");
    const PORTS = [P("Hz/V", "CV", "In"), P("Oct/V", "CV", "In"), P("FreqA", "CV", "In"), P("FreqB", "CV", "In"), P("PWM", "CV", "In"),
      P("Saw", "Audio", "Out"), P("Tri", "Audio", "Out"), P("Pulse", "Audio", "Out")];
    const kn = [F(0.5), 0, 0, F(0.5), F(0.5)]; // scale, amountA, amountB, fine, pw
    let phase = 0, triState = 0;
    function scaleIndex() {
      const i = Math.round(F(clamp01(kn[0]) * 3)); // value >= 0, so round-half-up == std::round
      return i < 0 ? 0 : (i > 3 ? 3 : i);
    }
    function fineRatio() {
      const s = F(F(clamp01(kn[3]) * 2) - 1);
      return powf(2, F(F(s * 2) / 12));
    }
    m.numPorts = () => 8;
    m.port = (i) => PORTS[i < 7 ? i : 7];
    m.numKnobs = () => 5;
    m.setKnob = (k, v) => { if (k >= 0 && k < 5) kn[k] = clamp01(F(v)); };
    m.presetKnobCount = () => 5;
    m.presetKnob = (k) => (k >= 0 && k < 5 ? kn[k] : 0);
    m.presetScaleIndex = () => scaleIndex();
    m.prepare = (sr) => { m.sampleRate = sr; phase = 0; triState = 0; };
    m.processSample = () => {
      const pv = m.portValue;
      const footage = kFootage[scaleIndex()];
      const oct = F(F(pv[1] + F(pv[2] * kn[1])) + F(pv[3] * kn[2]));
      const octExpo = powf(2, oct);
      let linear = footage;
      if (m.inputConnected[0]) {
        let volts = pv[0];
        if (volts < F(0.05)) volts = F(0.05);
        linear = F(footage * volts);
      }
      let hz = F(F(linear * fineRatio()) * octExpo);
      if (!fin(hz) || hz < 0) hz = footage;
      const rate = rateF(m.sampleRate);
      let dt = F(hz / rate);
      if (dt > F(0.45)) dt = F(0.45);
      if (dt < 0) dt = 0;
      phase += dt;
      if (phase >= 1.0) phase -= Math.floor(phase);
      const ph = F(phase);
      let saw = F(F(2 * ph) - 1);
      saw = F(saw - polyBlep(ph, dt));
      let duty = F(F(F(0.05) + F(clamp01(kn[4]) * F(0.90))) + F(F(pv[4] / 5) * 0.5));
      duty = clampf(duty, F(0.05), F(0.95));
      let pulse = ph < duty ? 1 : -1;
      pulse = F(pulse + polyBlep(ph, dt));
      let fall = F(ph - duty);
      if (fall < 0) fall = F(fall + 1);
      pulse = F(pulse - polyBlep(fall, dt));
      triState += saw * dt;
      triState -= triState * 1.0e-4;
      if (!fin(triState)) triState = 0;
      let tri = F(triState * 40.0);
      if (!fin(tri)) tri = 0;
      pv[5] = F(saw * 5);
      pv[6] = tri;
      pv[7] = F(pulse * 5);
    };
    return m;
  }

  // ---------- EG 2 (S-26) ----------
  function Eg2() {
    const m = base("EG 2");
    const PORTS = [P("Trig", "CV", "In", 5), P("OutPos", "CV", "Out"), P("OutNeg", "CV", "Out"), P("DelayTrig", "Gate", "Out")];
    const kn = [knobForSeconds(F(0.001)), knobForSeconds(F(0.001)), knobForSeconds(F(0.02)), knobForSeconds(F(0.20))]; // hold, delay, attack, release
    let out = 0, elapsed = 0, delayLeft = 0, delayArmed = false, wasHeld = false, stage = IDLE;
    m.numPorts = () => 4;
    m.port = (i) => PORTS[i < 3 ? i : 3];
    m.numKnobs = () => 4;
    m.setKnob = (k, v) => { if (k >= 0 && k < 4) kn[k] = clamp01(F(v)); };
    m.presetKnobCount = () => 4;
    m.presetKnob = (k) => (k >= 0 && k < 4 ? kn[k] : 0);
    m.prepare = (sr) => { m.sampleRate = sr; out = 0; elapsed = 0; delayLeft = 0; delayArmed = false; wasHeld = false; stage = IDLE; };
    function follow(target, seconds) {
      const rate = rateF(m.sampleRate);
      const tau = seconds > F(1.0e-6) ? seconds : F(1.0e-6);
      out = F(out + F(F(target - out) * coeffF(tau, rate)));
      out = flushF(out);
    }
    m.processSample = () => {
      const pv = m.portValue;
      const held = pv[0] < kHeldBelow;
      const rising = held && !wasHeld;
      wasHeld = held;
      if (rising) { stage = WAIT; out = 0; elapsed = 0; delayLeft = 0; delayArmed = false; }
      const rate = rateF(m.sampleRate);
      if (stage === WAIT) {
        out = 0;
        elapsed += 1.0 / rate;
        const hold = secondsFor(kn[0]);
        const delay = secondsFor(kn[1]);
        if (!delayArmed && elapsed >= hold) {
          delayArmed = true;
          const x = 0.001 * rate;
          const width = x < 0 ? -Math.round(-x) : Math.floor(x + 0.5); // lround (half away from zero)
          delayLeft = width > 1 ? width : 1;
        }
        if (elapsed >= hold + delay) stage = ATTACK;
      } else if (stage === ATTACK) {
        follow(kSpan, secondsFor(kn[2]));
        if (out >= kAttackDone) stage = RELEASE;
      } else if (stage === RELEASE) {
        follow(0, secondsFor(kn[3]));
        if (out <= kSettle) { out = 0; stage = IDLE; }
      }
      pv[1] = out;
      pv[2] = -out;
      if (delayLeft > 0) { pv[3] = 1; --delayLeft; } else pv[3] = 0;
    };
    return m;
  }

  // ---------- Ring (S-21) ----------
  function Ring() {
    const m = base("Ring");
    const PORTS = [P("A", "CV", "In"), P("B", "CV", "In"), P("Out", "CV", "Out")];
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.prepare = (sr) => { m.sampleRate = sr; };
    m.processSample = () => {
      const pv = m.portValue;
      const p = F(F(pv[0] * pv[1]) / 5);
      pv[2] = fin(p) ? p : 0;
    };
    return m;
  }

  // ---------- Divider (S-18) ----------
  function Divider() {
    const m = base("Divider");
    const PORTS = [P("In", "CV", "In"), P("Div2", "CV", "Out"), P("Div4", "CV", "Out")];
    const hi = F(0.5), lo = F(0.3);
    let sh = false, d2 = false, d4 = false;
    m.numPorts = () => 3;
    m.port = (i) => PORTS[i < 2 ? i : 2];
    m.prepare = (sr) => { m.sampleRate = sr; sh = d2 = d4 = false; };
    m.processSample = () => {
      const pv = m.portValue;
      const input = pv[0];
      const wasHigh = sh;
      if (fin(input)) {
        if (!sh && input >= hi) sh = true;
        else if (sh && input <= lo) sh = false;
      }
      if (sh && !wasHigh) {
        const wasD2 = d2;
        d2 = !d2;
        if (d2 && !wasD2) d4 = !d4;
      }
      pv[1] = d2 ? 5 : 0;
      pv[2] = d4 ? 5 : 0;
    };
    return m;
  }

  // ---------- Inverter (S-19) ----------
  function Inverter() {
    const m = base("Inverter");
    const PORTS = [P("In", "CV", "In"), P("Out", "CV", "Out")];
    m.numPorts = () => 2;
    m.port = (i) => PORTS[i === 0 ? 0 : 1];
    m.prepare = (sr) => { m.sampleRate = sr; };
    m.processSample = () => { const n = -m.portValue[0]; m.portValue[1] = fin(n) ? n : 0; };
    return m;
  }

  // ---------- Integrator (S-20) ----------
  function Integrator() {
    const m = base("Integrator");
    const PORTS = [P("In", "CV", "In"), P("Out", "CV", "Out")];
    let time01 = F(Math.log(F(0.050) / 0.001) / Math.log(2000.0)), state = 0;
    m.numPorts = () => 2;
    m.port = (i) => PORTS[i === 0 ? 0 : 1];
    m.numKnobs = () => 1;
    m.setKnob = (k, v) => { if (k === 0) time01 = clamp01(F(v)); };
    m.presetKnobCount = () => 1;
    m.presetKnob = (k) => (k === 0 ? time01 : 0);
    m.prepare = (sr) => { m.sampleRate = sr; state = 0; };
    m.processSample = () => {
      const rate = rateD(m.sampleRate);
      const tau = 0.001 * Math.pow(2000.0, time01);
      const coeff = F(1.0 - Math.exp(-1.0 / (tau * rate)));
      state = F(state + F(F(m.portValue[0] - state) * coeff));
      if (!fin(state)) state = 0;
      m.portValue[1] = state;
    };
    return m;
  }

  // ---------- Mixer (adding amplifier) ----------
  function Mixer() {
    const m = base("Mixer");
    const PORTS = [P("In 1", "Audio", "In"), P("In 2", "Audio", "In"), P("In 3", "Audio", "In"), P("Out", "Audio", "Out")];
    const level = [F(0.8), F(0.8), F(0.8)];
    m.numPorts = () => 4;
    m.port = (i) => PORTS[i < 3 ? i : 3];
    m.numKnobs = () => 3;
    m.setKnob = (k, v) => { if (k >= 0 && k <= 2) level[k] = clamp01(F(v)); };
    m.presetKnobCount = () => 3;
    m.presetKnob = (k) => (k >= 0 && k <= 2 ? level[k] : 0);
    m.prepare = (sr) => { m.sampleRate = sr; };
    m.processSample = () => {
      const pv = m.portValue;
      let sum = 0;
      for (let i = 0; i < 3; ++i) {
        if (level[i] === 0) continue;
        const s = fin(pv[i]) ? pv[i] : 0;
        sum = F(sum + F(s * level[i]));
      }
      const inv = -sum;
      pv[3] = fin(inv) ? inv : 0;
    };
    return m;
  }

  // ---------- Sample and hold ----------
  function SampleHold() {
    const m = base("Sample and hold");
    // port order is In, Out, Ext Clock, Clock Out
    const PORTS = [P("In", "Audio", "In"), P("Out", "CV", "Out"), P("Ext Clock", "CV", "In"), P("Clock Out", "CV", "Out")];
    let rate01 = F(0.5), held = 0, lastExt = 0, phase = 0.5, clockHigh = false;
    m.numPorts = () => 4;
    m.port = (i) => PORTS[i < 3 ? i : 3];
    m.numKnobs = () => 1;
    m.setKnob = (k, v) => { if (k === 0) rate01 = clamp01(F(v)); };
    m.presetKnobCount = () => 1;
    m.presetKnob = (k) => (k === 0 ? rate01 : 0);
    m.prepare = (sr) => { m.sampleRate = sr; held = 0; lastExt = 0; phase = 0.5; clockHigh = false; };
    m.processSample = () => {
      const pv = m.portValue;
      let high = false, rising = false;
      if (m.inputConnected[2]) {
        const ext = fin(pv[2]) ? pv[2] : 0;
        high = ext >= 1;
        rising = high && lastExt < 1;
        lastExt = ext;
      } else {
        const rate = rateD(m.sampleRate);
        phase += 0.1 * Math.pow(1000.0, clamp01(rate01)) / rate;
        if (phase >= 1.0) phase -= Math.floor(phase);
        high = phase < 0.5;
        rising = high && !clockHigh;
        lastExt = 0;
      }
      clockHigh = high;
      if (rising) held = fin(pv[0]) ? pv[0] : 0;
      pv[1] = held;
      pv[3] = high ? 5 : 0;
    };
    return m;
  }

  // ---------- graph rules (PatchGraph.cpp) ----------
  const TYPE_INDEX = { Audio: 0, CV: 1, Gate: 2 };
  const ALLOWED = [[true, true, false], [true, true, false], [true, true, true]];
  const graphRules = {
    // kAllowed[source][dest]. Directions must also be Out -> In (indicesLegal), and a port may not feed itself.
    canConnect(srcType, dstType) { return ALLOWED[TYPE_INDEX[srcType]][TYPE_INDEX[dstType]]; },
    canConnectPorts(srcDesc, dstDesc) {
      return srcDesc.dir === "Out" && dstDesc.dir === "In" && graphRules.canConnect(srcDesc.type, dstDesc.type);
    },
    // S-15 promotion in contributeCables: a logic Gate source into a CV/Audio input becomes S-trig volts.
    promote(srcDesc, dstDesc, volts) {
      if (srcDesc.type === "Gate" && dstDesc.type !== "Gate" && !srcDesc.strigVolts) return volts >= 0.5 ? 0 : 5;
      return volts;
    },
  };
  // clearModuleInputs: unpatched Audio/CV inputs get desc.rest; unpatched Gate inputs are left untouched
  // (returns undefined); patched inputs start at 0 before the cables are summed.
  function restFor(desc) { return desc.type === "Gate" ? undefined : desc.rest; }

  // ---------- panel maps ----------
  // addModule order of RoninAudioProcessor
  const MOD = { Ext: 0, Output: 1, Noise: 2, Vcf: 3, Vca1: 4, Vca2: 5, Eg1: 6, Mg: 7, Vco: 8, Eg2: 9, Ring: 10, Divider: 11,
    Inverter: 12, Integrator: 13, Mixer: 14, SampleHold: 15 };
  // FaceKnobs.h bindings: [id, module, module knob, default travel]. host: knobs the processor applies itself.
  const KNOBS = [
    ["VCF:CUTOFF", MOD.Vcf, 0, 0.50], ["VCF:PEAK", MOD.Vcf, 1, 0.30], ["VCF:MOD", MOD.Vcf, 2, 0.68],
    ["VCA 1:LOW CUT", MOD.Vca1, 0, 0.68],
    ["EG 1:ATTACK", MOD.Eg1, 0, 0.50], ["EG 1:DECAY", MOD.Eg1, 1, 0.30], ["EG 1:SUSTAIN", MOD.Eg1, 2, 0.68], ["EG 1:RELEASE", MOD.Eg1, 3, 0.42],
    ["MG:RATE", MOD.Mg, 0, 0.50], ["MG:PW", MOD.Mg, 1, 0.30],
    ["VCO:RANGE", MOD.Vco, 0, 0.50], ["VCO:FINE", MOD.Vco, 3, 0.30], ["VCO:PW", MOD.Vco, 4, 0.68], ["VCO:FM 1", MOD.Vco, 1, 0.42], ["VCO:FM 2", MOD.Vco, 2, 0.78],
    ["EG 2:HOLD", MOD.Eg2, 0, 0.50], ["EG 2:DELAY", MOD.Eg2, 1, 0.30], ["EG 2:ATTACK", MOD.Eg2, 2, 0.68], ["EG 2:RELEASE", MOD.Eg2, 3, 0.42],
    ["INT:TIME", MOD.Integrator, 0, 0.50],
    ["MIX:LEVEL 1", MOD.Mixer, 0, 0.80], ["MIX:LEVEL 2", MOD.Mixer, 1, 0.80], ["MIX:LEVEL 3", MOD.Mixer, 2, 0.80],
    ["S&H:RATE", MOD.SampleHold, 0, 0.50],
  ];
  const HOST_KNOBS = [["OUTPUT:MIX", "mix", 1.0], ["OUTPUT:LEVEL", "level", 0.70]];
  // Layout knobs that are still pictures in the plugin (no FaceKnob binding).
  const UNMAPPED_KNOBS = ["VCA 1:INITIAL", "VCA 1:MOD", "VCA 2:INITIAL", "VCA 2:MOD", "DIV:RATIO SWITCH"];
  // PanelGeometry.inc kPanelJacks (module there is 1-based; here it is the addModule index)
  const JACKS = [
    ["VCO:HZ/V", MOD.Vco, 0], ["VCO:V/OCT", MOD.Vco, 1], ["VCO:FM 1", MOD.Vco, 2], ["VCO:FM 2", MOD.Vco, 3], ["VCO:PWM", MOD.Vco, 4],
    ["VCO:TRI", MOD.Vco, 6], ["VCO:SAW", MOD.Vco, 5], ["VCO:PULSE", MOD.Vco, 7],
    ["VCF:IN", MOD.Vcf, 0], ["VCF:CUTOFF", MOD.Vcf, 1], ["VCF:OUT", MOD.Vcf, 2],
    ["VCA 1:IN", MOD.Vca1, 0], ["VCA 1:ENV", MOD.Vca1, 1], ["VCA 1:OUT", MOD.Vca1, 2],
    ["VCA 2:IN", MOD.Vca2, 0], ["VCA 2:CV", MOD.Vca2, 1], ["VCA 2:OUT", MOD.Vca2, 2],
    ["MG:FM", MOD.Mg, 0], ["MG:PWM", MOD.Mg, 1], ["MG:TRI", MOD.Mg, 2], ["MG:SAW", MOD.Mg, 3], ["MG:INV SAW", MOD.Mg, 4], ["MG:PULSE", MOD.Mg, 5],
    ["EG 1:TRIG", MOD.Eg1, 0], ["EG 1:OUT A", MOD.Eg1, 1], ["EG 1:OUT B", MOD.Eg1, 2], ["EG 1:OUT C", MOD.Eg1, 3],
    ["EG 2:TRIG", MOD.Eg2, 0], ["EG 2:OUT +", MOD.Eg2, 1], ["EG 2:OUT −", MOD.Eg2, 2], ["EG 2:DELAY", MOD.Eg2, 3],
    ["NOISE:WHITE", MOD.Noise, 0], ["NOISE:PINK", MOD.Noise, 1],
    ["S&H:IN", MOD.SampleHold, 0], ["S&H:OUT", MOD.SampleHold, 1], ["S&H:CLOCK", MOD.SampleHold, 2],
    ["RING:A", MOD.Ring, 0], ["RING:B", MOD.Ring, 1], ["RING:OUT", MOD.Ring, 2],
    ["DIV:IN", MOD.Divider, 0], ["DIV:/2", MOD.Divider, 1], ["DIV:/4", MOD.Divider, 2], ["DIV:/16", -1, -1],
    ["INV:IN", MOD.Inverter, 0], ["INV:OUT", MOD.Inverter, 1],
    ["INT:IN", MOD.Integrator, 0], ["INT:OUT", MOD.Integrator, 1],
    ["MIX:IN 1", MOD.Mixer, 0], ["MIX:IN 2", MOD.Mixer, 1], ["MIX:IN 3", MOD.Mixer, 2], ["MIX:OUT", MOD.Mixer, 3],
    ["EXT IN:L", MOD.Ext, 0], ["EXT IN:R", MOD.Ext, 1], ["EXT IN:MONO", MOD.Ext, 2], ["EXT IN:GATE", MOD.Ext, 3],
    ["OUTPUT:L", MOD.Output, 0], ["OUTPUT:R", MOD.Output, 1], ["OUTPUT:WET", MOD.Output, 2],
  ];
  function jackFor(module, port) {
    for (const j of JACKS) if (j[1] === module && j[2] === port) return j[0];
    return null;
  }

  // ---------- factory presets (FactoryPresets.h) ----------
  // The factory bank is cleared for now: INIT only. INIT is the fresh-instance patch (EXT IN through VCF and VCA 1,
  // EG 1 on the Ext gate, dry L/R to the outputs), effect on, every knob on the default row.
  const PRESET_CABLES = [
    [[MOD.Ext, 2, MOD.Vcf, 0], [MOD.Vcf, 2, MOD.Vca1, 0], [MOD.Vca1, 2, MOD.Output, 2], [MOD.Ext, 0, MOD.Output, 0],
      [MOD.Ext, 1, MOD.Output, 1], [MOD.Ext, 3, MOD.Eg1, 0], [MOD.Eg1, 1, MOD.Vca1, 1], [MOD.Eg1, 1, MOD.Vcf, 1]],   // INIT
  ];
  const PRESET_META = [["INIT", "INIT", true]];
  // applyProgramParameters + factoryProgramKnobs: every host parameter a program load restores.
  function programKnobs(index) {
    return {
      "VCF:CUTOFF": 0.50, "VCF:PEAK": 0.30, "VCF:MOD": 0.68, "VCA 1:LOW CUT": 0.68,
      "EG 1:ATTACK": 0.50, "EG 1:DECAY": 0.30, "EG 1:SUSTAIN": 0.68, "EG 1:RELEASE": 0.42,
      "MG:RATE": 0.50, "MG:PW": 0.30,
      "VCO:RANGE": 0.50, "VCO:FINE": 0.30, "VCO:PW": 0.68, "VCO:FM 1": 0.42, "VCO:FM 2": 0.78,
      "EG 2:HOLD": 0.50, "EG 2:DELAY": 0.30, "EG 2:ATTACK": 0.68, "EG 2:RELEASE": 0.42, "INT:TIME": 0.50,
      "MIX:LEVEL 1": 0.80, "MIX:LEVEL 2": 0.80, "MIX:LEVEL 3": 0.80, "S&H:RATE": 0.50,
      "OUTPUT:LEVEL": 0.70, "OUTPUT:MIX": 1.0,
    };
  }
  const presets = PRESET_CABLES.map((cables, i) => ({
    name: PRESET_META[i][0],
    screen: (String(i + 1).padStart(2, "0") + " " + PRESET_META[i][1]).substring(0, 16), // presetScreenLine
    power: PRESET_META[i][2],                    // effectOn (the panel rocker) after load
    knobs: programKnobs(i),
    cables: cables.map(c => [jackFor(c[0], c[1]), jackFor(c[2], c[3])]),
    rawCables: cables.map(c => c.slice()),       // [srcModule, srcPort, dstModule, dstPort]
    vca1Initial: factoryVca1Initial(i),          // hidden VCA 1 Initial knob (0 for INIT)
  }));

  // ---------- rack: modules + PluginProcessor glue ----------
  function createRack(sampleRate) {
    const sr = sampleRate > 0 ? sampleRate : 48000;
    const ext = ExtIn(), out = OutputModule();
    const modules = [ext, out, Noise(), Vcf(), Vca1(), Vca2(), Eg1(), Mg(), Vco(), Eg2(), Ring(), Divider(), Inverter(), Integrator(),
      Mixer(), SampleHold()];
    const knobs = {}, values = {};
    for (const k of KNOBS) { knobs[k[0]] = { module: k[1], knob: k[2], def: k[3] }; values[k[0]] = k[3]; }
    for (const k of HOST_KNOBS) { knobs[k[0]] = { module: MOD.Output, knob: -1, def: k[2], host: k[1] }; values[k[0]] = k[2]; }
    for (const id of UNMAPPED_KNOBS) knobs[id] = { module: -1, knob: -1, def: null };
    const jacks = {};
    for (const j of JACKS) jacks[j[0]] = j[1] < 0 ? null : { module: j[1], port: j[2] };

    let effectOn = false;          // juce::AudioParameterBool "effectOn", default false
    let meterSource = null;        // {module, port} or null (null reads Output Wet)

    // applyHostControls(): pushes every parameter to its module, then the switch-gated mix and the level.
    function applyHostControls() {
      for (const k of KNOBS) modules[k[1]].setKnob(k[2], values[k[0]]);
      out.setMix(outputMixAfterSwitch(effectOn, F(values["OUTPUT:MIX"])));
      out.setOutputLevel(values["OUTPUT:LEVEL"]);
    }

    const rack = {
      modules, knobs, jacks, presets, defaultPreset: kDefaultFactoryPreset, sampleRate: sr, MOD,
      // Cables the processor constructor patches (connectFactoryCables = the INIT cables), with power off.
      initialCables: presets[kDefaultFactoryPreset].cables.map(c => c.slice()),
      knobValue(id) { return values[id]; },
      // Set a layout knob (0..1 travel). Same as moving the host parameter: the processor re-applies it.
      setKnob(id, v) {
        const b = knobs[id];
        if (!b || (b.module < 0 && !b.host)) return false;
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        values[id] = v;
        if (b.host) {
          out.setMix(outputMixAfterSwitch(effectOn, F(values["OUTPUT:MIX"])));
          out.setOutputLevel(values["OUTPUT:LEVEL"]);
        } else modules[b.module].setKnob(b.knob, v);
        return true;
      },
      // EXT IN HOLD key (momentary): ORs the Ext In gate while held. ExtIn reads it inside processSample.
      setHold(h) { ext.setButtonHeld(h); },
      hold() { return ext.buttonHeld(); },
      // Power rocker = the plugin's "Effect" switch. Off: Output ignores MIX and passes dry L/R (mix 0).
      // On: dry*(1-mix) + wet*mix. Output LEVEL applies either way. It does not stop the graph.
      setPower(on) { effectOn = !!on; out.setMix(outputMixAfterSwitch(effectOn, F(values["OUTPUT:MIX"]))); },
      power() { return effectOn; },
      effectiveOutputMix() { return outputMixAfterSwitch(effectOn, F(values["OUTPUT:MIX"])); },
      // VU meter: a click on a mapped jack selects it; with no selection it reads Output Wet.
      setMeterSource(jackId) { const j = jacks[jackId]; if (j) meterSource = { module: j.module, port: j.port }; return !!j; },
      meterVolts() {
        const s = meterSource || { module: MOD.Output, port: 2 };
        const v = modules[s.module].portValue[s.port];
        return fin(v) ? v : 0;
      },
      meterNeedle() { return meterNeedle(rack.meterVolts()); },
      prepare(rate) {
        rack.sampleRate = rate;
        for (const m of modules) { m.sampleRate = rate; m.prepare(rate); }
        applyHostControls();
      },
      // per sample, before graph.process(): extIn.setHostSample(inLeft, inRight)
      beforeGraph(inL, inR) { ext.setHostSample(inL, inR); },
      // per sample, after graph.process(): left = output.hostLeft(), right = output.hostRight()
      afterGraph() { return [out.hostLeft(), out.hostRight()]; },
      // RoninAudioProcessor::setCurrentProgram. Resets every module (graph.prepare). The caller replaces
      // its cable list with the returned cables (and should clear any delayed-cable memory).
      applyPreset(i) {
        if (i < 0 || i >= presets.length) return null;
        const p = presets[i];
        // loadFactoryPreset: mixer levels, S&H rate, applyFactoryPreset (VCA 1 Initial)
        modules[MOD.Mixer].setKnob(0, 0.8); modules[MOD.Mixer].setKnob(1, 0.8); modules[MOD.Mixer].setKnob(2, 0.8);
        modules[MOD.SampleHold].setKnob(0, 0.5);
        for (const m of modules) m.applyFactoryPreset(i);
        // applyProgramParameters
        effectOn = p.power;
        for (const id in p.knobs) values[id] = p.knobs[id];
        modules[MOD.Vca1].setKnob(1, 0.85);
        out.setLevel(1.0);
        applyHostControls();
        rack.currentPreset = i;
        for (const m of modules) { m.sampleRate = rack.sampleRate; m.prepare(rack.sampleRate); }
        return { knobs: Object.assign({}, p.knobs), cables: p.cables.map(c => c.slice()), power: p.power };
      },
      currentPreset: kDefaultFactoryPreset,
    };
    rack.prepare(sr);
    return rack;
  }

  function meterNeedle(volts) {
    if (!fin(volts)) return 0;
    const u = F(volts / 5);
    return u < -1 ? -1 : (u > 1 ? 1 : u);
  }

  return {
    createRack, graphRules, restFor, meterNeedle, outputLevelGain, outputMixAfterSwitch,
    factories: { ExtIn, OutputModule, Noise, Vcf, Vca1, Vca2, Eg1, Mg, Vco, Eg2, Ring, Divider, Inverter, Integrator, Mixer, SampleHold },
    MOD, presets, defaultPreset: kDefaultFactoryPreset,
  };
})();
if (typeof module !== "undefined") module.exports = RONIN_DSP;
