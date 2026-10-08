// C++ vs JS sample parity for BUSHIDO: builds a tiny harness around engine/BushidoModule.cpp under the system temp dir, runs the
// same scenarios through it and through web/bushido_dsp.js, and compares every output on every sample plus the gate events.
// Run: node web/test_bushido_parity.js   (needs g++; BUSHIDO_CPP_ROOT=<dir> uses another copy of the C++ sources)
const fs = require("fs"), path = require("path"), os = require("os"), cp = require("child_process");
const B = require(path.join(__dirname, "bushido_dsp.js")), P = B.P;
const ROOT = process.env.BUSHIDO_CPP_ROOT || path.join(__dirname, ".."), TMP = path.join(os.tmpdir(), "bushido_parity");
const HARNESS = String.raw`
#include "engine/BushidoModule.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <xmmintrin.h>
// harness <sr> <block> <ftz> <inputs.f32> <script.txt> <out.f32> <events.txt>
int main(int argc, char** argv)
{
    if (argc < 8) return 2;
    const double sr = atof(argv[1]); const int block = atoi(argv[2]);
    if (atoi(argv[3])) _mm_setcsr(_mm_getcsr() | 0x8040);       // FTZ + DAZ, as JUCE ScopedNoDenormals
    FILE* fi = fopen(argv[4], "rb"); std::vector<float> in; float x; while (fread(&x, 4, 1, fi) == 1) in.push_back(x); fclose(fi);
    const int NIN = BushidoModule::NUM_IN; const long long N = (long long) in.size() / NIN;
    struct Act { char k; int idx; double v[3]; };
    std::multimap<long long, Act> acts;
    FILE* fs = fopen(argv[5], "r"); char k; long long at;
    while (fscanf(fs, " %c %lld", &k, &at) == 2) {
        Act a { k, 0, { 0, 0, 0 } };
        if (k == 'p' || k == 'b') { if (fscanf(fs, "%d %lf", &a.idx, &a.v[0]) != 2) return 3; }
        else if (k == 't') { if (fscanf(fs, "%d %lf %lf %lf", &a.idx, &a.v[0], &a.v[1], &a.v[2]) != 4) return 3; }
        acts.insert({ at, a });
    }
    fclose(fs);
    BushidoModule sq; const int nj = (int) sq.jacks().size(); sq.prepare(sr, block);
    std::vector<std::vector<float>> ib((size_t) nj, std::vector<float>((size_t) block)), ob((size_t) nj, std::vector<float>((size_t) block));
    std::vector<const float*> ip((size_t) nj); std::vector<float*> op((size_t) nj);
    for (int j = 0; j < nj; ++j) { ip[(size_t) j] = ib[(size_t) j].data(); op[(size_t) j] = ob[(size_t) j].data(); }
    FILE* fo = fopen(argv[6], "wb"); FILE* fe = fopen(argv[7], "w");
    for (long long s0 = 0; s0 < N; s0 += block) {
        auto r = acts.equal_range(s0);
        for (auto it = r.first; it != r.second; ++it) {
            const Act& a = it->second;
            if (a.k == 'p') sq.setParam(a.idx, (float) a.v[0]);
            else if (a.k == 'b') { sq.setParam(a.idx, 1.0f); sq.setParam(a.idx, 0.0f); }
            else { rack::Transport t; t.valid = a.idx != 0; t.playing = a.v[0] != 0; t.bpm = a.v[1]; t.ppq = a.v[2]; t.samplePos = s0; sq.setTransport(t); }
        }
        const int n = (int) std::min<long long>(block, N - s0);
        for (int j = 0; j < nj; ++j) for (int i = 0; i < n; ++i) ib[(size_t) j][(size_t) i] = j < NIN ? in[(size_t) ((s0 + i) * NIN + j)] : 0.0f;
        sq.process(ip.data(), op.data(), n);
        for (int i = 0; i < n; ++i) for (int j = NIN; j < nj; ++j) fwrite(&ob[(size_t) j][(size_t) i], 4, 1, fo);
        BushidoModule::GateEvent ev[BushidoModule::kMaxEvents]; const int ke = sq.takeGateEvents(ev, BushidoModule::kMaxEvents);
        for (int e = 0; e < ke; ++e) fprintf(fe, "%lld %d %d %.9g %.9g\n", ev[e].sample, ev[e].jack, ev[e].on ? 1 : 0, (double) ev[e].target, (double) ev[e].cvC);
    }
    fclose(fo); fclose(fe); return 0;
}
`;
let fails = 0, passes = 0;
const CHECK = (c, msg) => { console.log((c ? "PASS " : "FAIL ") + msg); if (c) passes++; else fails++ };
try { cp.execSync("g++ --version", { stdio: "ignore" }) } catch (e) { console.log("SKIP: no g++ on this machine, parity not checked"); process.exit(0) }
fs.mkdirSync(TMP, { recursive: true });
const exe = path.join(TMP, "harness");
fs.writeFileSync(path.join(TMP, "harness.cpp"), HARNESS);
cp.execSync(`g++ -std=c++17 -O2 -I"${ROOT}" "${path.join(TMP, "harness.cpp")}" "${path.join(ROOT, "engine/BushidoModule.cpp")}" -o "${exe}"`, { stdio: "inherit" });

// deterministic noise
let seed = 12345; const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff };
const NIN = 7, NOUT = B.JACKS.length - NIN;

// scenario: { name, sr, block, ftz, N, input(j, t) -> volts, acts: [[at, 'p'|'b'|'t', ...]] } (acts at block starts)
function runBoth(sc) {
  const inp = new Float32Array(sc.N * NIN);
  for (let t = 0; t < sc.N; t++) for (let j = 0; j < NIN; j++) inp[t * NIN + j] = sc.input(j, t);
  const fIn = path.join(TMP, sc.tag + ".in.f32"), fSc = path.join(TMP, sc.tag + ".txt"), fOut = path.join(TMP, sc.tag + ".out.f32"), fEv = path.join(TMP, sc.tag + ".ev.txt");
  fs.writeFileSync(fIn, Buffer.from(inp.buffer));
  fs.writeFileSync(fSc, sc.acts.map(a => [a[1], a[0], ...a.slice(2)].join(" ")).join("\n") + "\n");   // "<kind> <at> ..."
  cp.execFileSync(exe, [String(sc.sr), String(sc.block), sc.ftz ? "1" : "0", fIn, fSc, fOut, fEv]);
  const cOut = new Float32Array(fs.readFileSync(fOut).buffer.slice(0));
  const cEv = fs.readFileSync(fEv, "utf8").trim().split("\n").filter(Boolean).map(l => l.split(" ").map(Number));
  // JS: the same actions at the same samples, one sample at a time
  const m = B.create(sc.sr); m.prepare(sc.sr); const pv = m.portValue, byAt = {};
  for (const a of sc.acts) (byAt[a[0]] = byAt[a[0]] || []).push(a);
  let maxCv = 0, maxMix = 0, logicBad = 0, samplesOff = 0, exact = 0; const jEv = [];
  for (let t = 0; t < sc.N; t++) {
    for (const a of byAt[t] || []) {
      if (a[1] === "p") m.setParam(a[2], a[3]); else if (a[1] === "b") m.press(a[2]);
      else m.setTransport({ valid: a[2] !== 0, playing: a[3] !== 0, bpm: a[4], ppq: a[5], samplePos: t });
    }
    for (let j = 0; j < NIN; j++) pv[j] = inp[t * NIN + j];
    m.processSample();
    let same = true;
    for (let k = 0; k < NOUT; k++) {
      const c = cOut[t * NOUT + k], v = pv[NIN + k], d = Math.abs(c - v), jack = NIN + k;
      if (!Object.is(c, Math.fround(v))) same = false;
      if (jack === B.CV_A || jack === B.CV_B || jack === B.CV_C) maxCv = Math.max(maxCv, d);
      else if (jack === B.MIX_OUT) maxMix = Math.max(maxMix, d);
      else if (c !== v) logicBad++;
    }
    if (same) exact++; else samplesOff++;
    if ((t + 1) % sc.block === 0 || t === sc.N - 1) for (const e of m.takeGateEvents()) jEv.push([e.sample, e.jack, e.on ? 1 : 0, e.target, e.cvC]);
  }
  let evOk = cEv.length === jEv.length && cEv.length > 0;
  for (let i = 0; evOk && i < cEv.length; i++) { const a = cEv[i], b = jEv[i]; evOk = a[0] === b[0] && a[1] === b[1] && a[2] === b[2] && Math.abs(a[3] - b[3]) < 1e-6 && Math.abs(a[4] - b[4]) < 1e-6 }
  return { maxCv, maxMix, logicBad, exact, samplesOff, events: cEv.length, evOk };
}
const report = (sc, r, bitExact) => {
  const tol = 1e-6;
  CHECK(r.logicBad === 0, `${sc.name}: GATE A/B and TRIG 1-12 identical on all ${sc.N} samples`);
  CHECK(r.maxCv <= tol && r.maxMix <= tol, `${sc.name}: CV A/B/C and MIX OUT within ${tol} V (max diff CV ${r.maxCv.toExponential(1)}, MIX ${r.maxMix.toExponential(1)}; ${r.exact}/${sc.N} samples bit-identical)`);
  if (bitExact) CHECK(r.samplesOff === 0, `${sc.name}: every output bit-identical`);
  CHECK(r.evOk, `${sc.name}: the ${r.events} gate events (MIDI source) match: sample, jack, on/off, target volts, C`);
};

const knobActs = at => { const a = []; for (let i = 0; i < 36; i++) a.push([at, "p", P.STEPS + i, +rnd().toFixed(6)]); return a };
// A: INT clock, A+B then ALT, TEMPO CV moving, PORTA, RANGE, LAW/QUANT, PULSE, VINTAGE, mixer jumps, stop/start/reset, 44.1 kHz
{
  const sc = { name: "A INT 44.1k", tag: "a", sr: 44100, block: 256, ftz: false, N: 256 * 560,
    input: (j, t) => j === 1 ? (t < 60000 ? Math.round(Math.sin(t / 7000) * 8) / 8 : Math.sin(t / 20000) * 0.7) : j === 5 ? 3 * Math.sin(t * 0.0313) : j === 6 ? 2 * (rnd() - 0.5) : 0,
    acts: [...knobActs(0), [0, "p", P.PORTA_A, 0.3], [0, "p", P.PORTA_B, 0.6], [0, "p", P.RANGE_B, 0], [0, "p", P.LAW_B, 1], [0, "p", P.QUANT_A, 1], [0, "p", P.QUANT_B, 1],
      [0, "p", P.TRIG_MODE, 1], [0, "p", P.SETTLE, 1], [0, "p", P.TEMPO, 0.7], [0, "p", P.LEVEL1, 0.3], [512, "b", P.BTN_START, 1], [20480, "p", P.LEVEL1, 0.9],
      [25600, "p", P.LEVEL2, 0.1], [40960, "p", P.C_MODE, 1], [61440, "p", P.MODE, 1], [71680, "p", P.SETTLE, 0], [81920, "b", P.BTN_START, 1], [90112, "b", P.BTN_START, 1],
      [100096, "b", P.BTN_RESET, 1], [110080, "p", P.PORTA_A, 0], [112640, "p", P.C_MODE, 0], [115200, "p", P.VEL_A, 1], [120064, "b", P.BTN_STEP, 1]] };
  report(sc, runBoth(sc), false);
}
// B: EXT jack: jittery clock, START on, 1, 2 and 3 samples after an edge, RESET/STEP pulses, edges near 0.5/1.0 V, a long stop, 48 kHz
{
  const edgesC = []; let t = 300; while (t < 220000) { edgesC.push(t); t += 3000 + Math.floor(rnd() * 2000) }
  const clk = new Uint8Array(220000); for (const e of edgesC) for (let k = 0; k < 120; k++) clk[e + k] = 1;
  const startAt = [edgesC[0], edgesC[12] + 1, edgesC[13] + 400, edgesC[30] + 2, edgesC[31] + 900, edgesC[50] + 3];   // toggles: start, stop, start, stop...
  const resetAt = [edgesC[20] + 50, edgesC[40]], stepAt = [edgesC[22] + 1500, edgesC[23] + 1500];
  const pulse = (list, t, w) => list.some(s => t >= s && t < s + w);
  const sc = { name: "B EXT jack 48k", tag: "b", sr: 48000, block: 128, ftz: false, N: 220000,
    input: (j, t) => j === 0 ? (clk[t] ? (t % 7 === 0 ? 1.01 : 4.8) : (t % 11 === 0 ? 0.6 : 0)) : j === 2 ? (pulse(startAt, t, 60) ? 5 : 0) : j === 3 ? (pulse(stepAt, t, 30) ? 5 : 0)
      : j === 4 ? (pulse(resetAt, t, 30) ? 5 : 0) : j === 5 ? 1.5 : 0,
    acts: [...knobActs(0), [0, "p", P.SOURCE, 1], [0, "p", P.MODE, 1], [0, "p", P.LAW_A, 1], [0, "p", P.QUANT_A, 1], [0, "p", P.PORTA_A, 0.2], [0, "p", P.RANGE_A, 1],
      [64000, "p", P.MODE, 0], [96000, "p", P.C_MODE, 1], [128000, "p", P.TRIG_MODE, 1]] };
  report(sc, runBoth(sc), false);
}
// C: HOST: 1/32 at 133.7 BPM from ppq 1.37, stop, restart at a jumped position, an invalid stretch, 96 kHz
{
  const blk = 512, acts = [...knobActs(0), [0, "p", P.SOURCE, 1], [0, "p", P.EXT_SOURCE, 1], [0, "p", P.DIV, 1], [0, "p", P.MODE, 1]];
  let ppq = 1.37; const bpm = 133.7, sr = 96000;
  for (let b = 0; b < 500; b++) {
    const s0 = b * blk, playing = (b >= 10 && b < 300) || b >= 340, valid = !(b >= 420 && b < 440);
    if (b === 340) ppq = 17.003;
    acts.push([s0, "t", valid ? 1 : 0, playing ? 1 : 0, bpm, +ppq.toFixed(12)]);
    if (playing) ppq += blk * bpm / (60 * sr);
  }
  const sc = { name: "C HOST 96k", tag: "c", sr, block: blk, ftz: false, N: 500 * blk, input: () => 0, acts };
  report(sc, runBoth(sc), false);
}
// D: B11 denormals: glides to 0 V under FTZ/DAZ in C++ (as ScopedNoDenormals) vs the JS flush, bit-exact over 400 000 samples
{
  const acts = [[0, "p", P.PORTA_A, 0.1], [0, "p", P.PORTA_B, 0.25], [0, "p", P.MODE, 0.5], [0, "p", P.TEMPO, 0], [0, "p", P.STEPS, 0.8], [0, "p", P.STEPS + 12, 0.6]];
  for (let i = 1; i < 12; i++) acts.push([0, "p", P.STEPS + i, 0], [0, "p", P.STEPS + 12 + i, 0]);
  acts.push([256, "b", P.BTN_START, 1]);
  const sc = { name: "D FTZ 48k", tag: "d", sr: 48000, block: 256, ftz: true, N: 400128, input: () => 0, acts };
  report(sc, runBoth(sc), true);
}
console.log(fails ? fails + " FAILED (" + passes + " passed)" : "ALL PASSED (" + passes + ")"); process.exit(fails ? 1 : 0);
