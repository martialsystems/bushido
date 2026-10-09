// BUSHIDO redesign tests (behaviour in docs/REFERENCE.md; JCS R4/R5). No JUCE.
// Build: g++ -std=c++17 -O2 -I. -isystem third_party/jidai-common/include tests/test_redesign.cpp rack/PatchGraph.cpp engine/BushidoModule.cpp
#include "../rack/PatchGraph.h"
#include "../engine/BushidoModule.h"
#include "../engine/MidiOut.h"
#include "../engine/BushidoState.h"
#include <cstdio>
#include <type_traits>
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>
using namespace rack;
static int fails = 0;
#define CHECK(c, msg) do { bool _ok = (c); std::printf("%s %s\n", _ok ? "PASS" : "FAIL", msg); if (!_ok) ++fails; } while (0)
// Exact floating-point compare, spelled out so -Wfloat-equal stays quiet under the rack's flags: same result as a == b
// (usual arithmetic conversions via std::common_type, NaN never equal, -0 == +0). Not an epsilon compare.
template <class A, class B> static constexpr bool exactEq(A a, B b) { using C = std::common_type_t<A, B>; return std::equal_to<C>{}(static_cast<C>(a), static_cast<C>(b)); }

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
    BushidoModule m; CHECK(exactEq(m.getParam(BushidoModule::SETTLE), 0.0f), "new patches use SETTLE = TIGHT");
}

// ------------------------------------------------------------------ TRIG self-patch still works with TIGHT
static void testTrigResetTight()
{
    Rig r; r.sq.setParam(BushidoModule::MODE, 0.0f); for (int i = 0; i < 12; ++i) r.sq.setParam(BushidoModule::STEPS + i, static_cast<float>(i + 1) / 12.0f);
    r.g.setCables({ { r.S, r.jack("5:TRIG"), r.S, r.jack("INPUTS:RESET") } });
    r.press("MODE:START/STOP"); float mx = 0; const int ca = r.jack("OUTPUTS:CV A");
    r.run(48000 * 3, [&](long long, int i) { mx = std::max(mx, r.out(ca, i)); });
    CHECK(mx <= 5.0f * 4 / 12 + 1e-4 && r.sq.currentStep() < 4, "TIGHT: TRIG 5 -> RESET loops 1-4 and step 5's CV never appears");
}

// ------------------------------------------------------------------ B4: TRIG outs
static void testTrigs()
{
    BushidoModule m; CHECK(exactEq(m.getParam(BushidoModule::TRIG_MODE), 0.0f), "testTrigModeDefaultStep: a new patch is STEP");
    { Rig r; r.press("MODE:STEP"); int hi = 0; const int t1 = r.jack("1:TRIG");
      r.run(4800, [&](long long, int i) { hi += r.out(t1, i) > 1; });
      CHECK(r.sq.currentStep() == 0 && hi == 0, "B4: TRIG outs stay low while stopped (STEP button moved to step 1)"); }
    { Rig r; r.press("MODE:START/STOP"); r.run(6000); r.press("MODE:START/STOP"); int hi = 0; const int t1 = r.jack("1:TRIG");
      r.run(1, [&](long long, int i) { hi += r.out(t1, i) > 1; });
      CHECK(hi == 0 && exactEq(r.sq.indicator(2), 1.0f), "R5.4: STOP drops TRIG on that block; the step lamp stays lit"); }
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
        r.run(6000, [&](long long t, int i) { const float v = r.out(ca, i); if (first < 0 && ! exactEq(v, 0.0f)) first = t; if (first >= 0 && (long long) got.size() < 4800) got.push_back(v); });
        const double tau = (double) porta * porta * 2.0;                          // v1 engine module, L90 and L124, copied verbatim
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
    CHECK(exactEq(o.back(), 5.0f), "B8: the level lands exactly (gain law unchanged)");
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
      CHECK(exactEq(m.getParam(BushidoModule::SOURCE), 1.0f) && exactEq(m.getParam(BushidoModule::EXT_SOURCE), 1.0f), "testHostDefaultOnlyNewRackInstancePlaying: new rack instance, transport playing -> HOST"); }
    { BushidoModule m; m.applyNewInstanceDefaults(true, false);
      CHECK(exactEq(m.getParam(BushidoModule::SOURCE), 0.0f) && exactEq(m.getParam(BushidoModule::EXT_SOURCE), 0.0f), "testHostDefaultOnlyNewRackInstancePlaying: stopped -> panel default (INT)"); }
    { BushidoModule m; m.applyNewInstanceDefaults(false, true);
      CHECK(exactEq(m.getParam(BushidoModule::SOURCE), 0.0f) && exactEq(m.getParam(BushidoModule::EXT_SOURCE), 0.0f), "testHostDefaultOnlyNewRackInstancePlaying: outside the rack -> panel (INT)"); }
    for (int src = 0; src < 3; ++src) {                                                          // INT, EXT-JACK, EXT-HOST
        std::map<std::string, float> p { { "CLOCK:SOURCE", src ? 1.0f : 0.0f }, { "CLOCK:EXT SOURCE", src == 2 ? 1.0f : 0.0f } };
        auto before = p; bushido::migrate(bushido::kFormat, p, {});
        CHECK(p == before, src == 0 ? "testLoadKeepsSource: INT loads unchanged" : src == 1 ? "testLoadKeepsSource: EXT-JACK loads unchanged" : "testLoadKeepsSource: EXT-HOST loads unchanged");
    }
    std::map<std::string, float> old { { "CLOCK:SOURCE", 1.0f } }; bushido::migrate(0, old, {});
    CHECK(exactEq(old["CLOCK:SOURCE"], 1.0f) && exactEq(old["CLOCK:EXT SOURCE"], 0.0f), "testLoadKeepsSource: a v1 EXT patch stays EXT-JACK (never flipped to HOST)");
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
      CHECK(exactEq(p["STEPS:LAW A"], 1.0f) && exactEq(p["STEPS:LAW B"], 0.0f), "testMigrationPitchLawFromCables: row A into RONIN VCO:HZ/V -> LIN, row B -> V/OCT"); }
    { auto [p, r] = lawOf({ { "RONIN#1/VCO:V/OCT", "BUSHIDO#1/OUTPUTS:CV A" }, { "BUSHIDO#1/OUTPUTS:CV B", "SHOGUN#1/LEAD:NOTE" } });
      CHECK(exactEq(p["STEPS:LAW A"], 0.0f) && exactEq(p["STEPS:LAW B"], 0.0f), "testMigrationPitchLawFromCables: into RONIN V/OCT or SHOGUN NOTE -> V/OCT"); }
    { auto [p, r] = lawOf({ { "BUSHIDO/OUTPUTS:CV B", "RONIN/VCO:HZ/V" } });
      CHECK(exactEq(p["STEPS:LAW B"], 1.0f) && exactEq(p["STEPS:LAW A"], 0.0f), "testMigrationPitchLawFromCables: file form BUSHIDO/... into RONIN/VCO:HZ/V -> LIN"); }
    { const std::string old = std::string("SQ") + "-10";   // a retired prefix, built from parts so the repo has no literal old name
      auto [p, r] = lawOf({ { old + "#1/OUTPUTS:CV B", "RONIN#1/VCO:HZ/V" } });
      CHECK(exactEq(p["STEPS:LAW B"], 0.0f) && bushido::splitJack(old + "/OUTPUTS:CV B").first == old, "no prefix aliases: a retired-prefix cable is not rewritten and sets no law"); }
    { auto [p, r] = lawOf({ { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V" }, { "BUSHIDO#1/OUTPUTS:CV A", "RONIN#2/VCO:V/OCT" } });
      CHECK(exactEq(p["STEPS:LAW A"], 1.0f) && r.lawMismatch[0], "a row cabled to both gets LIN and flags its V/OCT cable"); }
    { auto [p, r] = lawOf({ { "BUSHIDO#2/OUTPUTS:CV A", "RONIN#1/VCO:HZ/V" } }, "BUSHIDO#1");
      CHECK(exactEq(p["STEPS:LAW A"], 0.0f), "another BUSHIDO's cable does not set this instance's law"); }
    { auto [p, r] = lawOf({});
      CHECK(exactEq(p["CLOCK:SETTLE"], 1.0f) && exactEq(p["CLOCK:TRIG MODE"], 0.0f) && ! r.lines.empty(), "format 0 -> 1: SETTLE = VINTAGE, TRIG MODE = STEP, report lines for SETUP"); }
    { std::map<std::string, float> p { { "A:1", 0.3f } }; auto r = bushido::migrate(7, p, {});
      CHECK(r.readOnly && p.size() == 1, "a newer format loads read-only and untouched"); }
    using SP = std::pair<std::string, std::string>;
    CHECK(bushido::splitJack("INPUTS:START/STOP") == SP("", "INPUTS:START/STOP") && bushido::splitJack("OUTPUTS:CV A") == SP("", "OUTPUTS:CV A"),
          "splitJack: a bare id is never split at a '/' inside its label");
    CHECK(bushido::splitJack("RONIN#2/VCO:HZ/V") == SP("RONIN", "VCO:HZ/V") && bushido::splitJack("BUSHIDO/INPUTS:START/STOP") == SP("BUSHIDO", "INPUTS:START/STOP")
          && bushido::splitJack("bushido#1/outputs:cv a") == SP("bushido", "outputs:cv a"), "splitJack: prefix at the first '/' before ':'; ids the shared parser rejects split the same way");
    { auto [p, r] = lawOf({ { "OUTPUTS:CV A", "INPUTS:START/STOP" }, { "BUSHIDO/OUTPUTS:CV B", "RONIN/VCO:HZ/V" } });
      CHECK(exactEq(p["STEPS:LAW A"], 0.0f) && exactEq(p["STEPS:LAW B"], 1.0f), "verify: cables-to-law with a bare START/STOP cable; RONIN VCO:HZ/V gives LIN"); }
}

// ------------------------------------------------------------------ HOST locked to song position
// Song step k = floor(ppq x q) (q = 2, 4, 8 steps per quarter for DIV 1/8, 1/16, 1/32). On a transport start, a loop or a jump
// BUSHIDO plays step k of the unpatched sequence (12 steps in A, 24 in A+B and ALT); the next song step is a tick.
struct StepHit { long long t; int chan, pos; };
static std::vector<StepHit> hostRun(float div, float mode, long long n, std::function<Transport(long long)> trf, bool swingCable = false,
                                    std::function<void(Rig&, long long)> act = nullptr)
{
    Rig r; r.sq.setParam(BushidoModule::SOURCE, 1.0f); r.sq.setParam(BushidoModule::EXT_SOURCE, 1.0f);
    r.sq.setParam(BushidoModule::DIV, div); r.sq.setParam(BushidoModule::MODE, mode);
    if (swingCable) {                                                   // the swing patch: CV C -> TEMPO CV, row C alternating 0 / 0.71 V
        for (int s = 0; s < 12; ++s) r.sq.setParam(BushidoModule::STEPS + 24 + s, s % 2 ? 0.71f / 5.0f : 0.0f);
        r.g.setCables({ { r.S, r.jack("OUTPUTS:CV C"), r.S, r.jack("CLOCK:TEMPO CV") } });
    }
    std::vector<int> tj; for (int s = 1; s <= 12; ++s) tj.push_back(r.jack((std::to_string(s) + ":TRIG").c_str()));
    std::vector<StepHit> hits; int last = -1;
    r.run(n, [&](long long t, int i) {
        if (act) act(r, t);
        int h = -1; for (int s = 0; s < 12; ++s) if (r.out(tj[(size_t) s], i) > 1) h = s;
        const int key = h < 0 ? -1 : r.sq.currentChannel() * 12 + h;      // a new step: another TRIG, or the same TRIG on the other row
        if (h >= 0 && key != last) hits.push_back({ t, r.sq.currentChannel(), h });
        last = key; }, nullptr, trf);
    return hits;
}
static std::string hitsText(const std::vector<StepHit>& h, size_t n)
{
    std::string s; for (size_t i = 0; i < std::min(n, h.size()); ++i) s += (i ? " " : "") + std::string(h[i].chan ? "B" : "A") + std::to_string(h[i].pos + 1);
    return s;
}

static void testHostSongPosition()
{
    const float divs[] = { 0.0f, 0.5f, 1.0f }; const int qs[] = { 2, 4, 8 }; const char* dn[] = { "1/8", "1/16", "1/32" };
    for (int d = 0; d < 3; ++d) {
        const double stepSamples = 48000.0 * 60.0 / (120.0 * qs[d]);
        // start at the top of the song
        auto h0 = hostRun(divs[d], 0.0f, 4 * (long long) stepSamples, [](long long s) { return hostAt(s, true); });
        CHECK(! h0.empty() && h0[0].t == 0 && h0[0].pos == 0 && h0[0].chan == 0, (std::string("HOST lock ") + dn[d] + ": transport start at ppq 0 plays A1").c_str());
        // mid-bar start at beat 3 (ppq 2.0): song step 2q
        for (float mode : { 0.0f, 0.5f, 1.0f }) {
            const int len = mode < 0.25f ? 12 : 24, k = 2 * qs[d], sstep = k % len;
            auto h = hostRun(divs[d], mode, 3 * (long long) stepSamples, [](long long s) { return hostAt(s, true, 120.0, 48000.0, 2.0); });
            const bool ok = h.size() >= 2 && h[0].t == 0 && h[0].chan == sstep / 12 && h[0].pos == sstep % 12
                            && h[1].pos == (sstep + 1) % 12 && h[1].t == (long long) stepSamples;
            CHECK(ok, (std::string("HOST lock ") + dn[d] + (mode < 0.25f ? " A" : mode < 0.75f ? " A+B" : " ALT") + ": start at beat 3 (song step " + std::to_string(k)
                       + ") plays " + hitsText(h, 3) + ", then ticks on the grid").c_str());
        }
        // a 1-bar DAW loop (ppq 0..4) wraps to song step 0: A1 again, every pass the same
        const long long loopLen = (long long) (4 * 60.0 / 120.0 * 48000.0);
        auto hl = hostRun(divs[d], 0.0f, 3 * loopLen, [loopLen](long long s) { return hostAt(s % loopLen, true); });
        std::vector<StepHit> p1, p2; for (auto& x : hl) { if (x.t < loopLen) p1.push_back(x); else if (x.t < 2 * loopLen) p2.push_back(x); }
        bool same = p1.size() == p2.size() && ! p2.empty() && p2[0].t == loopLen && p2[0].pos == 0;
        for (size_t i = 0; same && i < p1.size(); ++i) same &= p1[i].pos == p2[i].pos && p2[i].t - p1[i].t == loopLen;
        CHECK(same, (std::string("HOST lock ") + dn[d] + ": a 1-bar loop wraps to A1 on the loop start, every pass identical (" + std::to_string(p1.size()) + " steps per pass)").c_str());
        // relocate: playing from ppq 0, the host jumps to ppq 9.5 at sample 48128; A+B: song step 9.5q mod 24
        const long long jumpAt = 48128; const double ppqJ = 9.5;                     // a block start (the host hands the position per block)
        auto hj = hostRun(divs[d], 0.5f, jumpAt + 3 * (long long) stepSamples, [&](long long s) { return s < jumpAt ? hostAt(s, true) : hostAt(s - jumpAt, true, 120.0, 48000.0, ppqJ); });
        const int kj = (int) std::floor(ppqJ * qs[d]) % 24; StepHit after { -1, -1, -1 }; for (auto& x : hj) if (x.t >= jumpAt) { after = x; break; }
        CHECK(after.t == jumpAt && after.chan == kj / 12 && after.pos == kj % 12,
              (std::string("HOST lock ") + dn[d] + ": a jump to ppq 9.5 plays song step " + std::to_string((int) std::floor(ppqJ * qs[d])) + " = " + (kj >= 12 ? "B" : "A") + std::to_string(kj % 12 + 1) + " at once").c_str());
    }
    // swing patch (CV C -> TEMPO CV bends INT only): in HOST the steps stay on the song grid, mid-bar start still locks
    {
        auto h = hostRun(0.5f, 0.0f, 48000, [](long long s) { return hostAt(s, true, 120.0, 48000.0, 1.0); }, true);
        bool grid = h.size() >= 6 && h[0].pos == 4;
        for (size_t i = 1; i < h.size(); ++i) grid &= h[i].t == (long long) i * 6000 && h[i].pos == (int) ((4 + i) % 12);
        CHECK(grid, ("HOST lock with the swing cable: start at ppq 1 plays A5, every step on the 1/16 grid (" + hitsText(h, 6) + ")").c_str());
    }
    // count-in: a start at ppq -1 (song step -4 at 1/16) lands on A9 and reaches A1 at ppq 0
    {
        auto h = hostRun(0.5f, 0.0f, 30000, [](long long s) { return hostAt(s, true, 120.0, 48000.0, -1.0); });
        CHECK(h.size() >= 5 && h[0].pos == 8 && h[4].pos == 0 && h[4].t == 24000, ("HOST lock: a count-in from ppq -1 plays " + hitsText(h, 5) + ", A1 on the downbeat").c_str());
    }
    // START while the song plays (BUSHIDO was stopped) keeps the START rule: A1 now, then A2 on the next song step
    {
        auto h = hostRun(0.5f, 0.0f, 40000, [](long long s) { return hostAt(s, true, 120.0, 48000.0, 0.0); }, false, [](Rig& r, long long t) {
            if (t == 1000) r.press("MODE:START/STOP");                    // stop (the transport started it)
            if (t == 15000) r.press("MODE:START/STOP"); });               // start again mid-song
        StepHit a { -1, -1, -1 }, b { -1, -1, -1 }; for (size_t i = 0; i + 1 < h.size(); ++i) if (h[i].t >= 15000) { a = h[i]; b = h[i + 1]; break; }
        CHECK(a.pos == 0 && b.pos == 1 && b.t == 18000, "HOST: START mid-song plays A1 at once, then A2 on the next song step (the START rule)");
    }
}

// ------------------------------------------------------------------ stopped fast path
// The stopped fast path (BushidoModule::idleBlock) must give exactly the outputs of the full loop. Two engines, one with the
// fast path and one without, get the same inputs, cables, knob moves, presses and transport; every output sample and every
// gate event must match bit for bit, and the fast path must actually have run.
static void testIdleSkipExact()
{
    struct Scene { const char* name; int block; bool selfPatch, extClock, host; };
    const Scene scenes[] = { { "no cables, 256-sample blocks", 256, false, false, false }, { "TRIG 8 -> RESET (sample by sample)", 64, true, false, false },
                             { "ext clock into CLOCK, odd blocks", 37, false, true, false }, { "HOST, transport stopped and started", 128, false, false, true } };
    for (const auto& sc : scenes) {
        std::vector<std::vector<float>> outs[2]; std::vector<BushidoModule::GateEvent> evs[2]; long long idle[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k) {
            BushidoModule sq; Driver d { 4 }; PatchGraph g; const int S = g.addModule(&sq), D = g.addModule(&d); g.prepare(48000, sc.block);
            sq.setIdleSkip(k == 0);
            for (int i = 0; i < 36; ++i) sq.setParam(i, (float) ((i * 5) % 12) / 11.0f);
            sq.setParam(BushidoModule::PORTA_A, 0.4f); sq.setParam(BushidoModule::PORTA_B, 0.2f); sq.setParam(BushidoModule::MODE, 1.0f);
            d.f = [&](int o, long long t) {
                if (o == 0) return sc.extClock && t % 3000 < 100 ? 5.0f : 0.0f;                          // clock pulses, even while stopped
                if (o == 1) return t >= 90000 && t < 90200 ? 5.0f : 0.0f;                                 // STEP edge while stopped
                if (o == 2) return t >= 150000 && t < 150050 ? 5.0f : 0.0f;                               // RESET edge
                return 0.3f * std::sin((float) t * 0.01f);                                                // audio into the mixer
            };
            std::vector<Cable> c = { { D, 1, S, findJack(sq, "INPUTS:STEP") }, { D, 2, S, findJack(sq, "INPUTS:RESET") }, { D, 3, S, findJack(sq, "MIXER:IN 1") } };
            if (sc.extClock) { c.push_back({ D, 0, S, findJack(sq, "CLOCK:CLOCK") }); sq.setParam(BushidoModule::SOURCE, 1.0f); }
            if (sc.host) { sq.setParam(BushidoModule::SOURCE, 1.0f); sq.setParam(BushidoModule::EXT_SOURCE, 1.0f); }
            if (sc.selfPatch) c.push_back({ S, findJack(sq, "8:TRIG"), S, findJack(sq, "INPUTS:RESET") });
            g.setCables(c);
            const int nj = (int) sq.jacks().size(); outs[k].assign((size_t) nj, {});
            auto press = [&](int prm) { sq.setParam(prm, 1); sq.setParam(prm, 0); };
            for (long long t = 0; t < 240000; t += sc.block) {
                if (t == 20000) press(BushidoModule::BTN_STEP);                         // STEP while stopped: a gate, then CV slews at rest
                if (t == 30000) sq.setParam(BushidoModule::STEPS + 12, 0.9f);           // a knob turned while stopped is heard
                if (t == 40000) sq.setParam(BushidoModule::C_MODE, 1.0f);
                if (t == 50000) sq.setParam(BushidoModule::LEVEL1, 0.2f);
                if (t == 60000) { sq.setParam(BushidoModule::QUANT_A, 1.0f); sq.setParam(BushidoModule::SETTLE, 1.0f); }
                if (t == 110000 && ! sc.host) press(BushidoModule::BTN_START);
                if (t == 140000 && ! sc.host) press(BushidoModule::BTN_START);         // stop again
                if (t == 170000) press(BushidoModule::BTN_RESET);
                if (t == 200000) sq.setParam(BushidoModule::TEMPO, 0.8f);
                if (sc.host && (t == 120064 || t == 130048)) press(BushidoModule::BTN_START);   // STOP and START mid-song
                if (sc.host) { Transport tr; tr.valid = true; tr.bpm = 120; tr.playing = t >= 100000 && t < 160000; tr.ppq = (double) t * 120.0 / (60.0 * 48000.0); tr.samplePos = t; sq.setTransport(tr); }
                g.process(sc.block);
                for (int j = 0; j < nj; ++j) { const float* o = g.output(S, j); outs[k][(size_t) j].insert(outs[k][(size_t) j].end(), o, o + sc.block); }
                BushidoModule::GateEvent e[BushidoModule::kMaxEvents]; const int ne = sq.takeGateEvents(e, BushidoModule::kMaxEvents); evs[k].insert(evs[k].end(), e, e + ne);
            }
            idle[k] = sq.idleBlocks();
        }
        bool same = evs[0].size() == evs[1].size();
        for (size_t j = 0; j < outs[0].size(); ++j) same &= outs[0][j].size() == outs[1][j].size() && std::memcmp(outs[0][j].data(), outs[1][j].data(), outs[0][j].size() * sizeof(float)) == 0;
        for (size_t e = 0; same && e < evs[0].size(); ++e) same &= evs[0][e].sample == evs[1][e].sample && evs[0][e].jack == evs[1][e].jack && evs[0][e].on == evs[1][e].on
                                                              && std::memcmp(&evs[0][e].target, &evs[1][e].target, sizeof(float)) == 0 && std::memcmp(&evs[0][e].cvC, &evs[1][e].cvC, sizeof(float)) == 0;
        CHECK(same && idle[0] > 0 && idle[1] == 0, (std::string("stopped fast path: bit-identical outputs and gate events, ") + sc.name + " (" + std::to_string(idle[0]) + " blocks skipped)").c_str());
    }
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
    testHostSongPosition();
    testIdleSkipExact();
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails); return fails ? 1 : 0;
}
