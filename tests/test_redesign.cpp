// BUSHIDO redesign tests (BUSHIDO_Redesign.md section 5, JCS R4/R5). No JUCE.
// Build: g++ -std=c++17 -O2 -I. tests/test_redesign.cpp rack/PatchGraph.cpp engine/BushidoModule.cpp
#include "../rack/PatchGraph.h"
#include "../engine/BushidoModule.h"
#include "../engine/MidiOut.h"
#include "../engine/BushidoState.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>
using namespace rack;
static int fails = 0;
#define CHECK(c, msg) do { bool _ok = (c); std::printf("%s %s\n", _ok ? "PASS" : "FAIL", msg); if (!_ok) ++fails; } while (0)

// Drives any number of outputs from a function of the absolute sample index.
struct Driver : Module {
    std::vector<JackInfo> j; std::vector<ParamInfo> pr; long long t = 0;
    std::function<float(int, long long)> f = [](int, long long) { return 0.0f; };
    explicit Driver(int outs) { for (int i = 0; i < outs; ++i) j.push_back({ "D:OUT " + std::to_string(i), Dir::Out }); }
    const char* name() const override { return "DRIVER"; }
    const std::vector<JackInfo>& jacks() const override { return j; }
    const std::vector<ParamInfo>& params() const override { return pr; }
    void prepare(double, int) override { t = 0; }
    void process(const float* const*, float* const* out, int n) override { for (int i = 0; i < n; ++i, ++t) for (size_t k = 0; k < j.size(); ++k) out[k][i] = f((int) k, t); }
    void setParam(int, float) override {} float getParam(int) const override { return 0; }
};

struct Rig {
    BushidoModule sq; Driver d { 4 }; PatchGraph g; int S, D; double sr; int block;
    std::vector<std::vector<float>> log;     // per sample: every BUSHIDO output we care about
    explicit Rig(double rate = 48000, int blk = 256) : sr(rate), block(blk) { S = g.addModule(&sq); D = g.addModule(&d); g.prepare(sr, block); }
    int jack(const char* id) const { return findJack(sq, id); }
    void patch(std::vector<std::pair<int, const char*>> dIntoS) { std::vector<Cable> c; for (auto& [o, id] : dIntoS) c.push_back({ D, o, S, jack(id) }); g.setCables(c); }
    void press(const char* id) { int i = findParam(sq, id); sq.setParam(i, 1); sq.setParam(i, 0); }
    // run `n` samples, calling `each(sampleIndex, i, outputs)` per sample
    template <typename F> void run(long long n, F&& each, const Transport* tr = nullptr, std::function<Transport(long long)> trf = nullptr) {
        long long done = 0;
        while (done < n) { const int k = (int) std::min<long long>(block, n - done);
            if (trf) sq.setTransport(trf(d.t));
            g.process(k);
            for (int i = 0; i < k; ++i) each(d.t - k + i, i);
            done += k; }
        (void) tr;
    }
    void run(long long n) { run(n, [](long long, int) {}); }
    float out(int jk, int i) const { return g.output(S, jk)[i]; }
};

// ------------------------------------------------------------------ B1: START and CLOCK on the same sample
static void testStartClockRace()
{
    Rig r; r.sq.setParam(BushidoModule::SOURCE, 1.0f);
    const long long t0 = 1000, P = 4800;                                          // clock period 4800, START on the same sample as an edge
    r.d.f = [&](int k, long long t) { if (k == 0) return (t >= t0 && (t - t0) % P < 240) ? 5.0f : 0.0f; return (k == 1 && t >= t0 && t < t0 + 240) ? 5.0f : 0.0f; };
    r.patch({ { 0, "CLOCK:CLOCK" }, { 1, "INPUTS:START/STOP" } });
    long long a1Start = -1, a2Start = -1; const int t1 = r.jack("1:TRIG"), t2 = r.jack("2:TRIG");
    r.run(t0 + 3 * P, [&](long long t, int i) { if (a1Start < 0 && r.out(t1, i) > 1) a1Start = t; if (a2Start < 0 && r.out(t2, i) > 1) a2Start = t; });
    CHECK(a1Start == t0, "B1: START plays A1 on its own sample");
    CHECK(a2Start - a1Start == P, "B1: a coincident EXT edge is absorbed, A1 lasts one full period (4800 samples)");

    Rig q; q.sq.setParam(BushidoModule::SOURCE, 1.0f);                           // an edge 2 samples after START is absorbed too, 3 samples is a tick
    for (int lag : { 2, 3 }) {
        Rig w; w.sq.setParam(BushidoModule::SOURCE, 1.0f);
        w.d.f = [&](int k, long long t) { if (k == 0) return (t >= t0 + lag && (t - t0 - lag) % P < 240) ? 5.0f : 0.0f; return (k == 1 && t >= t0 && t < t0 + 240) ? 5.0f : 0.0f; };
        w.patch({ { 0, "CLOCK:CLOCK" }, { 1, "INPUTS:START/STOP" } });
        long long a2 = -1; const int j2 = w.jack("2:TRIG");
        w.run(t0 + 2 * P, [&](long long t, int i) { if (a2 < 0 && w.out(j2, i) > 1) a2 = t; });
        if (lag == 2) CHECK(a2 == t0 + lag + P, "B1: an edge 2 samples after START is still step 1's clock");
        else          CHECK(a2 == t0 + lag, "B1: an edge 3 samples after START advances");
    }
}

// ------------------------------------------------------------------ B2: first EXT gates after a restart
static void testExtRestartGates()
{
    Rig r; r.sq.setParam(BushidoModule::SOURCE, 1.0f);
    const long long P = 5760;                                                     // 120 ms at 48 kHz
    std::vector<long long> startAt { 100, 20 * P + 100 + 480000 };                // run, stop for ~10 s, restart on a clock edge
    const long long stopAt = 20 * P + 50;
    r.d.f = [&](int k, long long t) {
        if (k == 0) return (t >= 100 && (t - 100) % P < 100) ? 5.0f : 0.0f;
        if (k == 1) { for (long long s : startAt) if (t >= s && t < s + 50) return 5.0f; return (t >= stopAt && t < stopAt + 50) ? 5.0f : 0.0f; }
        return 0.0f; };
    r.patch({ { 0, "CLOCK:CLOCK" }, { 1, "INPUTS:START/STOP" } });
    std::vector<long long> lens; long long hi = 0; const int ga = r.jack("OUTPUTS:GATE A");
    r.run(startAt[1] + 6 * P, [&](long long t, int i) { const bool g = r.out(ga, i) > 1; if (g) ++hi; else if (hi) { if (t > startAt[1]) lens.push_back(hi); hi = 0; } });
    bool ok = lens.size() >= 5; for (auto l : lens) ok &= l == P / 2;
    CHECK(ok, "B2: every gate after a 10 s stop is 2880 samples (50 % of a 120 ms clock), the first one too");
}

// ------------------------------------------------------------------ B3 / R5.5: settle
static void testSettle()
{
    for (int vintage = 0; vintage < 2; ++vintage) {
        Rig r; r.sq.setParam(BushidoModule::SETTLE, (float) vintage); r.sq.setParam(BushidoModule::STEPS + 1, 0.9f);
        const int t2 = r.jack("2:TRIG"), ga = r.jack("OUTPUTS:GATE A"), ca = r.jack("OUTPUTS:CV A");
        r.press("MODE:START/STOP");
        long long trig = -1, gate = -1, cv = -1; bool prevG = true;
        r.run(30000, [&](long long t, int i) { if (trig < 0 && r.out(t2, i) > 1) trig = t;
            const bool g = r.out(ga, i) > 1; if (trig >= 0 && gate < 0 && g && ! prevG) gate = t; prevG = g;
            if (trig >= 0 && cv < 0 && r.out(ca, i) > 4.0f) cv = t; });
        if (! vintage) CHECK(gate - trig == 1 && cv - trig == 1 && gate - trig <= 2, "R5.5 TIGHT (default): GATE and CV within 2 samples of TRIG");
        else           CHECK(gate - trig == 28 && cv - trig == 28, "VINTAGE: v1's 0.6 ms settle (28.8 samples at 48 kHz) is kept");
    }
    BushidoModule m; CHECK(m.getParam(BushidoModule::SETTLE) == 0.0f, "new patches use SETTLE = TIGHT");
}

// ------------------------------------------------------------------ TRIG self-patch still works with TIGHT
static void testTrigResetTight()
{
    Rig r; r.sq.setParam(BushidoModule::MODE, 0.0f); for (int i = 0; i < 12; ++i) r.sq.setParam(BushidoModule::STEPS + i, (i + 1) / 12.0f);
    r.g.setCables({ { r.S, r.jack("5:TRIG"), r.S, r.jack("INPUTS:RESET") } });
    r.press("MODE:START/STOP"); float mx = 0; const int ca = r.jack("OUTPUTS:CV A");
    r.run(48000 * 3, [&](long long, int i) { mx = std::max(mx, r.out(ca, i)); });
    CHECK(mx <= 5.0f * 4 / 12 + 1e-4 && r.sq.currentStep() < 4, "TIGHT: TRIG 5 -> RESET loops 1-4 and step 5's CV never appears");
}

// ------------------------------------------------------------------ B4: TRIG outs
static void testTrigs()
{
    BushidoModule m; CHECK(m.getParam(BushidoModule::TRIG_MODE) == 0.0f, "testTrigModeDefaultStep: a new patch is STEP");
    { Rig r; r.press("MODE:STEP"); int hi = 0; const int t1 = r.jack("1:TRIG");
      r.run(4800, [&](long long, int i) { hi += r.out(t1, i) > 1; });
      CHECK(r.sq.currentStep() == 0 && hi == 0, "B4: TRIG outs stay low while stopped (STEP button moved to step 1)"); }
    { Rig r; r.press("MODE:START/STOP"); r.run(6000); r.press("MODE:START/STOP"); int hi = 0; const int t1 = r.jack("1:TRIG");
      r.run(1, [&](long long, int i) { hi += r.out(t1, i) > 1; });
      CHECK(hi == 0 && r.sq.indicator(2) == 1.0f, "R5.4: STOP drops TRIG on that block; the step lamp stays lit"); }
    { Rig r; int hi = 0, run = 0, maxRun = 0; const int t1 = r.jack("1:TRIG"); r.press("MODE:START/STOP");
      r.run(11000, [&](long long, int i) { const bool h = r.out(t1, i) > 1; hi += h; run = h ? run + 1 : 0; maxRun = std::max(maxRun, run); });
      CHECK(hi == 11000, "TRIG MODE STEP: high for the whole step");
      Rig p; p.sq.setParam(BushidoModule::TRIG_MODE, 1.0f); int hp = 0; const int pt = p.jack("1:TRIG"); p.press("MODE:START/STOP");
      p.run(11000, [&](long long, int i) { hp += p.out(pt, i) > 1; });
      CHECK(hp == 240, "TRIG MODE PULSE: 5 ms (240 samples at 48 kHz)");
      Rig w(44100); w.sq.setParam(BushidoModule::TRIG_MODE, 1.0f); int hw = 0; const int wt = w.jack("1:TRIG"); w.press("MODE:START/STOP");
      w.run(9000, [&](long long, int i) { hw += w.out(wt, i) > 1; });
      CHECK(hw == 221, "TRIG MODE PULSE: round(0.005 x 44100) = 221 samples at 44.1 kHz"); }
}

// ------------------------------------------------------------------ portamento law, bit-exact against v1
static void testPortaLawUnchanged()
{
    bool all = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 }) for (float porta : { 0.0f, 0.1f, 0.5f, 1.0f }) {
        Rig r(sr); r.sq.setParam(BushidoModule::SETTLE, 1.0f); r.sq.setParam(BushidoModule::PORTA_A, porta); r.sq.setParam(BushidoModule::STEPS, 0.8f);
        const int ca = r.jack("OUTPUTS:CV A"); std::vector<float> got; long long first = -1;
        r.press("MODE:START/STOP");
        r.run(6000, [&](long long t, int i) { const float v = r.out(ca, i); if (first < 0 && v != 0.0f) first = t; if (first >= 0 && (long long) got.size() < 4800) got.push_back(v); });
        const double tau = (double) porta * porta * 2.0;                          // v1, engine/Sq10Module.cpp L90 and L124, copied verbatim
        const float k = tau < 1e-4 ? 1.0f : (float) (1.0 - std::exp(-1.0 / (tau * sr)));
        float cv = 0.0f; const float tgt = 0.8f * 5.0f; bool same = got.size() == 4800;
        for (size_t s = 0; s < got.size() && same; ++s) { cv += (tgt - cv) * k; same = std::memcmp(&cv, &got[s], sizeof cv) == 0; }
        const long long v1First = (long long) std::ceil(std::max(1.0, sr * 0.0006)) - 1;
        all &= same && first == v1First;
    }
    CHECK(all, "testPortaLawUnchanged: coefficient and 4800-sample trajectory bit-exact vs v1 (44.1/48/96 k, PORTA 0/0.1/0.5/1)");
}

// ------------------------------------------------------------------ B5/B6: MIDI from the target under the row's law
static void testRowPitchLawMidi()
{
    auto collect = [](float lawA, std::vector<float> volts, float porta, std::vector<float>* cvs) {
        Rig r; r.sq.setParam(BushidoModule::LAW_A, lawA); r.sq.setParam(BushidoModule::PORTA_A, porta); r.sq.setParam(BushidoModule::MODE, 0.0f);
        for (int i = 0; i < 12; ++i) r.sq.setParam(BushidoModule::STEPS + i, volts[(size_t) i % volts.size()] / 5.0f);
        BushidoMidiOut midi; std::vector<int> notes; std::vector<int> vel; const int ca = r.jack("OUTPUTS:CV A");
        r.press("MODE:START/STOP");
        for (long long done = 0; done < 12000LL * (long long) volts.size() - 100; done += r.block) {
            r.g.process(r.block); r.d.t += 0;
            if (cvs) for (int i = 0; i < r.block; ++i) cvs->push_back(r.out(ca, i));
            BushidoModule::GateEvent ev[BushidoModule::kMaxEvents]; const int k = r.sq.takeGateEvents(ev, BushidoModule::kMaxEvents);
            midi.handle(r.sq, ev, k, [&](const BushidoMidiOut::Msg& m) { if (m.on) { notes.push_back(m.note); vel.push_back(m.velocity); } });
        }
        return notes;
    };
    CHECK(collect(1.0f, { 0.5f, 1, 2, 4, 5 }, 0.0f, nullptr) == std::vector<int>({ 36, 48, 60, 72, 76 }), "testRowPitchLawMidi: LIN 0.5/1/2/4/5 V -> 36/48/60/72/76");
    CHECK(collect(0.0f, { 0, 1, 2, 4, 5 }, 0.0f, nullptr) == std::vector<int>({ 48, 60, 72, 96, 108 }), "testRowPitchLawMidi: V/OCT 0/1/2/4/5 V -> 48/60/72/96/108");
    std::vector<float> a, b; collect(0.0f, { 0.5f, 1, 2, 4, 5 }, 0.3f, &a); collect(1.0f, { 0.5f, 1, 2, 4, 5 }, 0.3f, &b);
    CHECK(a == b && ! a.empty(), "testRowPitchLawMidi: the CV out is bit-identical under both laws");
    CHECK(collect(1.0f, { 1.0f, 1.5874f, 2.0f, 2.3784f }, 0.5f, nullptr) == std::vector<int>({ 48, 56, 60, 63, 48 }), "B5: PORTA 0.5 no longer changes the MIDI note (target, not the glide)");
    { Rig r; r.sq.setParam(BushidoModule::LAW_A, 1.0f); r.sq.setParam(BushidoModule::STEPS, 0.0f); BushidoMidiOut midi; int on = 0;
      r.press("MODE:START/STOP"); r.g.process(256); BushidoModule::GateEvent ev[8]; const int k = r.sq.takeGateEvents(ev, 8);
      midi.handle(r.sq, ev, k, [&](const BushidoMidiOut::Msg& m) { on += m.on; });
      CHECK(k == 1 && on == 0, "HZ/V LIN: 0 V sends no note"); }
    { BushidoModule m; m.setParam(BushidoModule::VEL_A, 1.0f);
      CHECK(BushidoMidiOut::velocity(m, 0, 5.0f) == 127 && BushidoMidiOut::velocity(m, 0, 0.0f) == 1 && BushidoMidiOut::velocity(m, 0, 2.5f) == 64 && BushidoMidiOut::velocity(m, 1, 5.0f) == 100,
            "VEL FROM C: round(1 + 126 C / 5) on row A only; row B stays 100");
      m.setParam(BushidoModule::C_MODE, 1.0f); CHECK(BushidoMidiOut::velocity(m, 0, 5.0f) == 100, "VEL FROM C needs C MODE = CV");
      CHECK(BushidoMidiOut::channel(m, 0) == 1 && BushidoMidiOut::channel(m, 1) == 2, "MIDI channels default to 1 (A) and 2 (B)"); }
}

// ------------------------------------------------------------------ QUANT on the jack
static void testQuant()
{
    Rig r; r.sq.setParam(BushidoModule::RANGE_A, 0.0f); r.sq.setParam(BushidoModule::STEPS, 0.53f); r.sq.setParam(BushidoModule::QUANT_A, 1.0f);
    r.press("MODE:START/STOP"); r.run(256); const int ca = r.jack("OUTPUTS:CV A");
    CHECK(std::abs(r.out(ca, 255) - 0.5f) < 1e-6f, "QUANT SEMI on a V/OCT row: 0.53 V -> 0.5 V (F#3)");
    r.sq.setParam(BushidoModule::QUANT_A, 0.0f); r.run(256);
    CHECK(std::abs(r.out(ca, 255) - 0.53f) < 1e-6f, "QUANT OFF: knob volts are unchanged");
}

// ------------------------------------------------------------------ B8: mixer smoothing
static void testMixerSmoothing()
{
    Rig r; r.d.f = [](int k, long long) { return k == 0 ? 5.0f : 0.0f; }; r.patch({ { 0, "MIXER:IN 1" } });
    r.sq.setParam(BushidoModule::LEVEL1, 0.0f); r.run(256);
    const int mo = r.jack("MIXER:OUT"); r.sq.setParam(BushidoModule::LEVEL1, 1.0f);
    std::vector<float> o; r.run(48000, [&](long long, int i) { o.push_back(r.out(mo, i)); });
    float maxStep = 0; for (size_t i = 1; i < o.size(); ++i) maxStep = std::max(maxStep, std::abs(o[i] - o[i - 1]));
    CHECK(o[0] < 0.02f && maxStep < 0.02f, "B8: a LEVEL jump ramps instead of stepping (no zipper)");
    CHECK(std::abs(o[479] - 5.0f * (1.0f - std::exp(-1.0f))) < 0.01f, "B8: 10 ms one-pole (63 % after 480 samples)");
    CHECK(o.back() == 5.0f, "B8: the level lands exactly (gain law unchanged)");
}

// ------------------------------------------------------------------ HOST sync
static Transport hostAt(long long s, bool playing, double bpm = 120.0, double sr = 48000.0, double ppq0 = 0.0)
{ Transport t; t.valid = true; t.playing = playing; t.bpm = bpm; t.ppq = ppq0 + (double) s * bpm / (60.0 * sr); t.samplePos = s; return t; }

static void testHostSyncTicksOnSixteenths()
{
    std::vector<long long> ticks;                                            // per-sample tick times, from the TRIG outs
    Rig q; q.sq.setParam(BushidoModule::SOURCE, 1.0f); q.sq.setParam(BushidoModule::EXT_SOURCE, 1.0f); q.sq.setParam(BushidoModule::DIV, 0.5f);
    std::vector<int> tj; for (int s = 1; s <= 12; ++s) tj.push_back(q.jack((std::to_string(s) + ":TRIG").c_str()));
    int lastHigh = -1; int trigWhileStopped = 0;
    // the transport plays from ppq 0 at sample 0 and stops at 180000; play state reaches the module once per 256-sample block
    q.run(6000LL * 40, [&](long long t, int i) {
        int h = -1; for (int s = 0; s < 12; ++s) if (q.out(tj[(size_t) s], i) > 1) h = s;
        if (h >= 0 && h != lastHigh) ticks.push_back(t);
        if (t >= 6000LL * 30 + 256 && h >= 0) ++trigWhileStopped;           // the stop arrives with the first block at or after sample 180000
        lastHigh = h; }, nullptr,
        [&](long long s) { return hostAt(s, s < 6000LL * 30); });
    bool even = ticks.size() >= 29 && ticks[0] == 0;
    for (size_t k = 1; k < ticks.size(); ++k) even &= ticks[k] - ticks[k - 1] == 6000;
    CHECK(even, "testHostSyncTicksOnSixteenths: 120 BPM at 1/16 ticks every 6000 samples +-0 at 48 kHz, step 1 on the transport start");
    CHECK(trigWhileStopped == 0 && ! q.sq.isRunning(), "HOST: transport stop applies STOP (TRIGs low)");

    Rig e; e.sq.setParam(BushidoModule::SOURCE, 1.0f); e.sq.setParam(BushidoModule::EXT_SOURCE, 1.0f); e.sq.setParam(BushidoModule::DIV, 0.0f);   // 1/8
    std::vector<long long> t8; int lh = -1;
    e.run(64000, [&](long long t, int i) { int h = -1; for (int s = 0; s < 12; ++s) if (e.out(e.jack((std::to_string(s + 1) + ":TRIG").c_str()), i) > 1) h = s;
        if (h >= 0 && h != lh) t8.push_back(t); lh = h; }, nullptr, [&](long long s) { return hostAt(s, true, 90.0, 48000.0, 0.3); });
    bool ok8 = t8.size() >= 5 && t8[0] == 0;
    for (size_t k = 2; k < t8.size(); ++k) ok8 &= t8[k] - t8[k - 1] == 16000;
    CHECK(ok8 && t8[1] == 6400, "HOST 1/8 at 90 BPM: starting mid-step at ppq 0.3, the next tick lands on the host's next eighth (ppq 0.5, sample 6400), then every 16000");
}

static void testHostDefaults()
{
    { BushidoModule m; m.applyNewInstanceDefaults(true, true);
      CHECK(m.getParam(BushidoModule::SOURCE) == 1.0f && m.getParam(BushidoModule::EXT_SOURCE) == 1.0f, "testHostDefaultOnlyNewRackInstancePlaying: new rack instance, transport playing -> HOST"); }
    { BushidoModule m; m.applyNewInstanceDefaults(true, false);
      CHECK(m.getParam(BushidoModule::SOURCE) == 0.0f && m.getParam(BushidoModule::EXT_SOURCE) == 0.0f, "testHostDefaultOnlyNewRackInstancePlaying: stopped -> panel default (INT)"); }
    { BushidoModule m; m.applyNewInstanceDefaults(false, true);
      CHECK(m.getParam(BushidoModule::SOURCE) == 0.0f && m.getParam(BushidoModule::EXT_SOURCE) == 0.0f, "testHostDefaultOnlyNewRackInstancePlaying: outside the rack -> panel (INT)"); }
    for (int src = 0; src < 3; ++src) {                                                          // INT, EXT-JACK, EXT-HOST
        std::map<std::string, float> p { { "CLOCK:SOURCE", src ? 1.0f : 0.0f }, { "CLOCK:EXT SOURCE", src == 2 ? 1.0f : 0.0f } };
        auto before = p; bushido::migrate(bushido::kFormat, p, {});
        CHECK(p == before, src == 0 ? "testLoadKeepsSource: INT loads unchanged" : src == 1 ? "testLoadKeepsSource: EXT-JACK loads unchanged" : "testLoadKeepsSource: EXT-HOST loads unchanged");
    }
    std::map<std::string, float> old { { "CLOCK:SOURCE", 1.0f } }; bushido::migrate(0, old, {});
    CHECK(old["CLOCK:SOURCE"] == 1.0f && old["CLOCK:EXT SOURCE"] == 0.0f, "testLoadKeepsSource: a v1 EXT patch stays EXT-JACK (never flipped to HOST)");
}

// ------------------------------------------------------------------ EXT pin and the v1 contract
static void testExtPinUnchanged()
{
    BushidoModule m; const auto& j = m.jacks();
    const char* v1[] = { "CLOCK:CLOCK", "CLOCK:TEMPO CV", "INPUTS:START/STOP", "INPUTS:STEP", "INPUTS:RESET", "MIXER:IN 1", "MIXER:IN 2",
                         "OUTPUTS:CV A", "OUTPUTS:GATE A", "OUTPUTS:CV B", "OUTPUTS:GATE B", "OUTPUTS:CV C", "MIXER:OUT" };
    bool same = j.size() == 25; for (size_t i = 0; i < 13 && same; ++i) same = j[i].id == v1[i];
    for (int s = 0; s < 12 && same; ++s) same = j[(size_t) 13 + (size_t) s].id == std::to_string(s + 1) + ":TRIG";
    CHECK(same, "the 25 v1 jacks keep their ids and order (no jack added, renamed or moved)");
    const auto& p = m.params(); CHECK(p[(size_t) BushidoModule::DIV].id == "CLOCK:DIV" && p[(size_t) BushidoModule::SOURCE].id == "CLOCK:SOURCE" && p[(size_t) BushidoModule::SOURCE].positions == 2,
                                      "v1 params keep their indices; SOURCE stays a two-position INT/EXT switch");
    auto edges = [](std::vector<float> v) { Rig r; r.sq.setParam(BushidoModule::SOURCE, 1.0f); r.press("MODE:START/STOP"); r.run(10);
        r.d.f = [&](int k, long long t) { return k == 0 ? v[(size_t) (t / 100) % v.size()] : 0.0f; }; r.d.t = 0; r.patch({ { 0, "CLOCK:CLOCK" } });
        r.run(100 * (long long) v.size()); return r.sq.currentStep(); };
    CHECK(edges({ 0, 1.0f, 0 }) == 0, "testExtPinUnchanged: exactly 1.0 V is not an edge");
    CHECK(edges({ 0, 1.01f, 0, 1.01f }) == 2, "testExtPinUnchanged: above 1.0 V is an edge, re-armed below 0.5 V");
    CHECK(edges({ 0, 1.01f, 0.6f, 1.01f }) == 1, "testExtPinUnchanged: 0.6 V does not re-arm (hysteresis)");
}

// ------------------------------------------------------------------ format 0 -> 1 migration
static void testMigration()
{
    auto lawOf = [](std::vector<std::pair<std::string, std::string>> cables, const std::string& self = "") {
        std::map<std::string, float> p; auto rep = bushido::migrate(0, p, cables, self); return std::make_pair(p, rep); };
    { auto [p, r] = lawOf({ { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:HZ/V" } });
      CHECK(p["STEPS:LAW A"] == 1.0f && p["STEPS:LAW B"] == 0.0f, "testMigrationPitchLawFromCables: row A into RONIN VCO:HZ/V -> LIN, row B -> V/OCT"); }
    { auto [p, r] = lawOf({ { "RONIN#1/VCO:V/OCT", "BUSHIDO#1/OUTPUTS:CV A" }, { "BUSHIDO#1/OUTPUTS:CV B", "SHOGUN#1/LEAD:NOTE" } });
      CHECK(p["STEPS:LAW A"] == 0.0f && p["STEPS:LAW B"] == 0.0f, "testMigrationPitchLawFromCables: into RONIN V/OCT or SHOGUN NOTE -> V/OCT"); }
    { auto [p, r] = lawOf({ { "SQ-10#1/OUTPUTS:CV B", "MS-50#1/VCO:HZ/V" } });
      CHECK(p["STEPS:LAW B"] == 1.0f, "legacy SQ-10/MS-50 prefixes alias to BUSHIDO/RONIN"); }
    { auto [p, r] = lawOf({ { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V" }, { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:V/OCT" } });
      CHECK(p["STEPS:LAW A"] == 1.0f && r.lawMismatch[0], "a row cabled to both gets LIN and flags its V/OCT cable"); }
    { auto [p, r] = lawOf({ { "BUSHIDO#2/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V" } }, "BUSHIDO#1");
      CHECK(p["STEPS:LAW A"] == 0.0f, "another BUSHIDO's cable does not set this instance's law"); }
    { auto [p, r] = lawOf({});
      CHECK(p["CLOCK:SETTLE"] == 1.0f && p["CLOCK:TRIG MODE"] == 0.0f && ! r.lines.empty(), "format 0 -> 1: SETTLE = VINTAGE, TRIG MODE = STEP, report lines for SETUP"); }
    { std::map<std::string, float> p { { "A:1", 0.3f } }; auto r = bushido::migrate(7, p, {});
      CHECK(r.readOnly && p.size() == 1, "a newer format loads read-only and untouched"); }
    CHECK(bushido::canonicalJackId("SQ-10/CLOCK:CLOCK") == "BUSHIDO/CLOCK:CLOCK" && bushido::canonicalJackId("OUTPUTS:CV A") == "OUTPUTS:CV A", "alias table: SQ-10/ -> BUSHIDO/, bare ids unchanged");
}

int main()
{
    testStartClockRace();
    testExtRestartGates();
    testSettle();
    testTrigResetTight();
    testTrigs();
    testPortaLawUnchanged();
    testRowPitchLawMidi();
    testQuant();
    testMixerSmoothing();
    testHostSyncTicksOnSixteenths();
    testHostDefaults();
    testExtPinUnchanged();
    testMigration();
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails); return fails ? 1 : 0;
}
