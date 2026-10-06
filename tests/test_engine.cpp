// Engine + patch graph tests. Build: g++ -std=c++17 -O2 -I. tests/test_engine.cpp rack/PatchGraph.cpp engine/Sq10Module.cpp
#include "../rack/PatchGraph.h"
#include "../rack/HzPerVolt.h"
#include "../engine/Sq10Module.h"
#include <cstdio>
#include <cmath>
#include <thread>
#include <set>
#include <utility>
using namespace rack;
static int fails = 0;
#define CHECK(c, msg) do { bool _ok = (c); std::printf("%s %s\n", _ok ? "PASS" : "FAIL", msg); if (!_ok) ++fails; } while (0)

// A second "rack" for cross-rack tests: one input it records, one output it drives.
struct Probe : Module {
    std::vector<JackInfo> j { {"P:IN", Dir::In}, {"P:OUT", Dir::Out} }; std::vector<ParamInfo> pr;
    float last = 0, drive = 0;
    const char* name() const override { return "PROBE"; }
    const std::vector<JackInfo>& jacks() const override { return j; }
    const std::vector<ParamInfo>& params() const override { return pr; }
    void prepare(double, int) override {}
    void process(const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { last = in[0][i]; out[1][i] = drive; } }
    void setParam(int, float) override {} float getParam(int) const override { return 0; }
};

// For timing tests: OUT = IN + add, and every sample of IN and OUT is logged.
struct Tap : Module {
    std::vector<JackInfo> j { {"T:IN", Dir::In}, {"T:OUT", Dir::Out} }; std::vector<ParamInfo> pr;
    float add = 1; std::vector<float> ins, outs;
    const char* name() const override { return "TAP"; }
    const std::vector<JackInfo>& jacks() const override { return j; }
    const std::vector<ParamInfo>& params() const override { return pr; }
    void prepare(double, int) override {}
    void process(const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { out[1][i] = in[0][i] + add; ins.push_back(in[0][i]); outs.push_back(out[1][i]); } }
    void setParam(int, float) override {} float getParam(int) const override { return 0; }
};

struct Rig {
    Sq10Module sq; Probe pb; PatchGraph g; int S, P; const int block = 256;
    Rig() { S = g.addModule(&sq); P = g.addModule(&pb); g.prepare(48000, block); }
    void run(double seconds) { int n = (int) std::lround(seconds * 48000); while (n > 0) { int k = std::min(n, block); g.process(k); n -= k; } }
    void press(const char* id) { int i = findParam(sq, id); sq.setParam(i, 1); sq.setParam(i, 0); }
    float out(const char* id) const { return g.output(S, findJack(sq, id))[0]; }
    Cable c(const char* a, const char* b) const { return { S, findJack(sq, a), S, findJack(sq, b) }; }
    // Every (row, step) the sequencer moves to, in order, over `steps` clock periods at the default 4 steps/s.
    std::vector<std::pair<int, int>> play(int steps) {
        std::vector<std::pair<int, int>> seq { { sq.currentChannel(), sq.currentStep() } };
        for (long s = 0; s < steps * 12000L; s += 16) { g.process(16);
            std::pair<int, int> now { sq.currentChannel(), sq.currentStep() }; if (now != seq.back()) seq.push_back(now); }
        return seq;
    }
};
static const float kModeA = 0.0f, kModeAB = 0.5f, kModeAlt = 1.0f;

// Rows A and B set to distinct voltages: A step n = n x 0.01, B step n = 0.5 + n x 0.01 (x 5 V range).
static void distinctRows(Rig& r) { for (int i = 0; i < 12; ++i) { r.sq.setParam(Sq10Module::STEPS + i, (i + 1) * 0.01f); r.sq.setParam(Sq10Module::STEPS + 12 + i, 0.5f + (i + 1) * 0.01f); } }
struct JackLog { int gateA = 0, gateB = 0; std::vector<float> cvAAtGate, cvBAtGate; float cvBMin = 1e9f, cvBMax = -1e9f; };
// Runs `samples` and records every gate rise on each jack pair, with that jack's CV 2 ms into the gate.
static JackLog logJacks(Rig& r, long samples)
{
    JackLog L; bool pa = false, pb = false; long riseA = -1000, riseB = -1000, t = 0;
    for (long s = 0; s < samples; s += 16) { r.g.process(16);
        const float* ga = r.g.output(r.S, Sq10Module::GATE_A); const float* gb = r.g.output(r.S, Sq10Module::GATE_B);
        const float* ca = r.g.output(r.S, Sq10Module::CV_A);   const float* cb = r.g.output(r.S, Sq10Module::CV_B);
        for (int i = 0; i < 16; ++i, ++t) {
            if (ga[i] > 1 && ! pa) { ++L.gateA; riseA = t; }
            if (gb[i] > 1 && ! pb) { ++L.gateB; riseB = t; }
            if (t == riseA + 96) L.cvAAtGate.push_back(ca[i]);
            if (t == riseB + 96) L.cvBAtGate.push_back(cb[i]);
            L.cvBMin = std::min(L.cvBMin, cb[i]); L.cvBMax = std::max(L.cvBMax, cb[i]);
            pa = ga[i] > 1; pb = gb[i] > 1; } }
    return L;
}
static bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

static void testModeALoops()
{
    Rig r; r.sq.setParam(Sq10Module::MODE, kModeA); r.press("MODE:START/STOP"); r.run(0.01);
    auto seq = r.play(30);
    bool ok = seq.size() >= 30;
    for (size_t k = 0; k < seq.size() && ok; ++k) ok = seq[k].first == 0 && seq[k].second == (int) (k % 12);
    CHECK(ok, "mode A: A1..A12, A1..A12, A1.. (row A only, 12-step loop)");
    CHECK(r.sq.isRunning(), "mode A: still running after 2.5 passes");
    r.press("MODE:START/STOP"); r.run(1.0);
    CHECK(! r.sq.isRunning() && r.out("OUTPUTS:GATE A") == 0, "mode A: only Stop stops it");

    Rig q; distinctRows(q); q.sq.setParam(Sq10Module::MODE, kModeA); q.press("MODE:START/STOP");
    auto L = logJacks(q, 24 * 12000L - 2000);
    bool cv = L.cvAAtGate.size() == 24; for (size_t k = 0; k < L.cvAAtGate.size() && cv; ++k) cv = near(L.cvAAtGate[k], (k % 12 + 1) * 0.05f);
    CHECK(L.gateA == 24 && cv, "mode A: CV A and GATE A carry row A, 24 gates in two passes");
    CHECK(L.gateB == 0 && L.cvBMin == L.cvBMax, "mode A: row B holds (no GATE B, CV B unchanged)");
}

static void testModeABLoops24()
{
    Rig r; r.sq.setParam(Sq10Module::MODE, kModeAB); r.press("MODE:START/STOP"); r.run(0.01);
    auto seq = r.play(50);
    bool ok = seq.size() >= 50;
    for (size_t k = 0; k < seq.size() && ok; ++k) { const int n = (int) (k % 24); ok = seq[k].first == (n < 12 ? 0 : 1) && seq[k].second == n % 12; }
    CHECK(ok, "mode A+B: A1..A12 then B1..B12, then back to A1 (24-step loop)");
    CHECK(r.sq.isRunning(), "mode A+B: still running after two passes of 24");

    Rig q; distinctRows(q); q.sq.setParam(Sq10Module::MODE, kModeAB); q.press("MODE:START/STOP");
    auto L = logJacks(q, 48 * 12000L - 2000);
    bool cv = L.cvAAtGate.size() == 48;
    for (size_t k = 0; k < L.cvAAtGate.size() && cv; ++k) { const int n = (int) (k % 24); cv = near(L.cvAAtGate[k], n < 12 ? (n + 1) * 0.05f : 2.5f + (n - 11) * 0.05f); }
    CHECK(L.gateA == 48 && cv, "mode A+B: one 24-step sequence on the A jacks, steps 13-24 are row B");
    CHECK(L.gateB == 0 && L.cvBMin == L.cvBMax, "mode A+B: CV B and GATE B hold");
}

static void testAltSwapsEachPass()
{
    Rig r; r.sq.setParam(Sq10Module::MODE, kModeAlt); r.press("MODE:START/STOP"); r.run(0.01);
    auto seq = r.play(60);
    bool ok = seq.size() >= 60;
    for (size_t k = 0; k < seq.size() && ok; ++k) ok = seq[k].first == (int) ((k / 12) % 2) && seq[k].second == (int) (k % 12);
    CHECK(ok, "mode ALT: one row per pass, A, B, A, B, A");
    CHECK(r.sq.isRunning(), "mode ALT: still running after five passes");
    Rig q; q.sq.setParam(Sq10Module::MODE, kModeAlt); q.press("MODE:START/STOP");
    int gateA = 0, gateB = 0; bool pa = false, pb = false;            // each row's gate only fires on its own pass
    for (long s = 0; s < 24 * 12000L - 2000; s += 16) { q.g.process(16);
        const float* ga = q.g.output(q.S, Sq10Module::GATE_A); const float* gb = q.g.output(q.S, Sq10Module::GATE_B);
        for (int i = 0; i < 16; ++i) { gateA += (ga[i] > 1) && ! pa; gateB += (gb[i] > 1) && ! pb; pa = ga[i] > 1; pb = gb[i] > 1; } }
    CHECK(gateA == 12 && gateB == 12, "mode ALT: 12 gates on A and 12 on B over two passes");

    Rig w; distinctRows(w); w.sq.setParam(Sq10Module::MODE, kModeAlt); w.press("MODE:START/STOP");
    auto L = logJacks(w, 48 * 12000L - 2000);
    bool cvA = L.cvAAtGate.size() == 24, cvB = L.cvBAtGate.size() == 24;
    for (size_t k = 0; k < L.cvAAtGate.size() && cvA; ++k) cvA = near(L.cvAAtGate[k], (k % 12 + 1) * 0.05f);
    for (size_t k = 0; k < L.cvBAtGate.size() && cvB; ++k) cvB = near(L.cvBAtGate[k], 2.5f + (k % 12 + 1) * 0.05f);
    CHECK(cvA && cvB, "mode ALT: row A plays on the A jacks, row B on the B jacks");

    Rig t; t.sq.setParam(Sq10Module::MODE, kModeAlt); t.g.setCables({ t.c("5:TRIG", "INPUTS:RESET") }); t.press("MODE:START/STOP"); t.run(0.01);
    auto loop = t.play(12); bool a14 = loop.size() >= 12;
    for (size_t k = 0; k < loop.size() && a14; ++k) a14 = loop[k].first == 0 && loop[k].second == (int) (k % 4);
    CHECK(a14, "mode ALT + TRIG 5 -> RESET: RESET means A1, so the loop is A1-4");
}

static void testTrigIntoResetSkipsStep()
{
    Rig r; for (int i = 0; i < 12; ++i) r.sq.setParam(Sq10Module::STEPS + i, (i + 1) / 12.0f);
    r.sq.setParam(Sq10Module::MODE, kModeA);
    r.g.setCables({ r.c("5:TRIG", "INPUTS:RESET") }); r.press("MODE:START/STOP");
    std::set<int> played; float maxCv = 0; int gateRises = 0, trig5Len = 0, trig5Max = 0; bool prevG = false;
    for (int s = 0; s < 48000 * 3; s += 16) { r.g.process(16);
        const float* cv = r.g.output(r.S, Sq10Module::CV_A); const float* ga = r.g.output(r.S, Sq10Module::GATE_A);
        for (int i = 0; i < 16; ++i) {
            for (int t = 0; t < 12; ++t) if (t != 4 && r.g.output(r.S, Sq10Module::TRIG1 + t)[i] > 1) played.insert(t + 1);
            const bool t5 = r.g.output(r.S, Sq10Module::TRIG1 + 4)[i] > 1; trig5Len = t5 ? trig5Len + 1 : 0; trig5Max = std::max(trig5Max, trig5Len);
            maxCv = std::max(maxCv, cv[i]); const bool gg = ga[i] > 1; if (gg && ! prevG) ++gateRises; prevG = gg; } }
    CHECK(played == std::set<int>({ 1, 2, 3, 4 }), "TRIG 5 -> RESET loops steps 1-4");
    CHECK(maxCv <= 5.0f * 4 / 12 + 1e-4, "the skipped step 5 never reaches CV A");
    CHECK(gateRises == 12, "one clean gate per played step (12 in 3 s), none for step 5");
    CHECK(trig5Max == PatchGraph::kFeedbackDelay, "TRIG 5 is cut off after the 1-sample cable delay");
    CHECK(r.sq.isRunning(), "RESET from the cable does not stop the sequencer");
}

static void testMidiUsesHzPerVolt()
{
    CHECK(hzv::midiNote(1.0f) == 33 && std::abs(hzv::hz(1.0f) - 55.0f) < 1e-4f, "Hz/V: 1 V = 55 Hz = A1 (MIDI 33)");
    CHECK(hzv::midiNote(2.0f) == 45 && hzv::midiNote(4.0f) == 57 && hzv::midiNote(0.5f) == 21, "Hz/V: doubling the volts is one octave up");
    CHECK(hzv::midiNote(1.5f) == 40, "Hz/V: 1.5 V is a fifth above 1 V, not 1/2 octave");
    CHECK(hzv::midiNote(1.0f) != 36 + 12 && hzv::midiNote(5.0f) == 61, "not 36 + CV x 12 (5 V would be note 96)");
    CHECK(hzv::midiNote(0.0f) == -1 && hzv::midiNote(-1.0f) == -1, "0 V and below: no note (a Hz/V VCO is silent)");
    CHECK(hzv::midiNote(0.001f) == 0 && hzv::midiNote(1000.0f) == 127, "notes clamp to 0..127");
    Rig r; r.sq.setParam(Sq10Module::STEPS, 0.4f); r.sq.setParam(Sq10Module::RANGE_A, 1.0f); r.press("MODE:START/STOP"); r.run(0.01);
    CHECK(std::abs(r.out("OUTPUTS:CV A") - 2.0f) < 1e-4f, "CV A stays in volts on the jack (0.4 x 5 V = 2 V)");
}

static void testFeedbackDelayIsOneSample()
{
    { Tap t; PatchGraph g; const int T = g.addModule(&t); g.prepare(48000, 256);
      g.setCables({ { T, 1, T, 0 } }); g.process(256); g.process(100);
      bool ok = true; for (size_t i = 1; i < t.ins.size(); ++i) ok &= t.ins[i] == t.outs[i - 1];
      CHECK(ok && t.outs.back() == 356.0f, "self-patch: input is the output one sample earlier (356 samples -> 356)"); }

    { Tap a, b; PatchGraph g; const int B = g.addModule(&b), A = g.addModule(&a); g.prepare(48000, 256);   // B runs first by index
      g.setCables({ { A, 1, B, 0 } }); g.process(64);
      bool ok = true; for (size_t i = 0; i < b.ins.size(); ++i) ok &= b.ins[i] == a.outs[i];
      CHECK(ok, "forward cable is sample-accurate, whatever order the modules were added in"); }

    { Tap a, b; PatchGraph g; const int A = g.addModule(&a), B = g.addModule(&b); g.prepare(48000, 256);
      g.setCables({ { A, 1, B, 0 }, { B, 1, A, 0 } }); g.process(200);           // A->B older, B->A newest
      bool fwd = true, back = b.ins.size() == 200 && a.ins[0] == 0.0f;
      for (size_t i = 0; i < 200; ++i) fwd &= b.ins[i] == a.outs[i];
      for (size_t i = 1; i < 200; ++i) back &= a.ins[i] == b.outs[i - 1];
      CHECK(fwd && back, "loop A->B->A: only the newest cable (B->A) is delayed, by exactly 1 sample"); }

    { Tap a, b; PatchGraph g; const int A = g.addModule(&a), B = g.addModule(&b); g.prepare(48000, 256);
      g.setCables({ { B, 1, A, 0 }, { A, 1, B, 0 } }); g.process(200);           // now A->B is the newest
      bool fwd = true, back = true;
      for (size_t i = 0; i < 200; ++i) fwd &= a.ins[i] == b.outs[i];
      for (size_t i = 1; i < 200; ++i) back &= b.ins[i] == a.outs[i - 1];
      CHECK(fwd && back, "same loop patched in the other order: the newest cable (A->B) takes the delay"); }

    CHECK(PatchGraph::kFeedbackDelay == 1, "feedback delay is 1 sample (was 16)");
}

int main() {
    { Rig r; r.run(0.1); CHECK(! r.sq.isRunning() && r.sq.currentStep() == -1, "starts stopped with no step");
      r.press("MODE:START/STOP"); r.run(0.01);
      CHECK(r.sq.isRunning() && r.sq.currentStep() == 0, "START plays step 1");
      CHECK(r.out("OUTPUTS:GATE A") == 5.0f && r.out("1:TRIG") == 5.0f, "gate A and TRIG 1 high on step 1");
      // tempo 0.5 -> 0.5 * 2^3 = 4 steps/s -> a new step every 12000 samples
      int changes = 0, prev = r.sq.currentStep(); long firstAt = -1;
      for (int s = 0; s < 48000; s += 16) { r.g.process(16); if (r.sq.currentStep() != prev) { prev = r.sq.currentStep(); if (firstAt < 0) firstAt = s; ++changes; } }
      CHECK(changes == 4, "internal clock: 4 steps per second at default tempo");
      CHECK(std::abs(firstAt + 480 - 12000) <= 16, "step period is 12000 samples"); }

    testModeALoops();
    testModeABLoops24();
    testAltSwapsEachPass();
    testTrigIntoResetSkipsStep();
    testMidiUsesHzPerVolt();
    testFeedbackDelayIsOneSample();

    { Rig r; r.sq.setParam(Sq10Module::MODE, kModeAB); r.press("MODE:START/STOP"); r.run(14 * 0.25);
      CHECK(r.sq.currentChannel() == 1, "(setup) playing row B");
      r.press("MODE:START/STOP"); r.run(0.1); CHECK(! r.sq.isRunning() && r.out("OUTPUTS:GATE B") == 0, "START while running stops");
      r.press("MODE:START/STOP"); r.run(0.01); CHECK(r.sq.isRunning() && r.sq.currentChannel() == 0 && r.sq.currentStep() == 0, "starting after a stop begins at A step 1"); }

    { Rig r; r.sq.setParam(Sq10Module::MODE, kModeAB); r.press("MODE:START/STOP"); r.run(14 * 0.25);
      r.press("MODE:RESET"); r.run(0.01);
      CHECK(r.sq.isRunning() && r.sq.currentChannel() == 0 && r.sq.currentStep() == 0, "RESET goes to A step 1 and keeps running"); }

    { Rig r; r.press("MODE:STEP"); r.run(0.01); CHECK(! r.sq.isRunning() && r.sq.currentStep() == 0, "STEP while stopped moves to step 1");
      CHECK(r.out("OUTPUTS:GATE A") == 5.0f, "STEP while stopped plays that step's gate");
      r.run(1.0); CHECK(r.out("OUTPUTS:GATE A") == 0.0f && r.sq.currentStep() == 0, "...and the gate closes again; stopped, no clock");
      r.press("MODE:STEP"); r.run(0.01); CHECK(r.sq.currentStep() == 1, "STEP again moves to step 2"); }

    { Rig r; r.sq.setParam(Sq10Module::SOURCE, 1.0f); r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "CLOCK:CLOCK") } });
      r.press("MODE:START/STOP"); r.run(0.5); CHECK(r.sq.currentStep() == 0, "external clock: TEMPO does not run");
      for (int k = 0; k < 3; ++k) { r.pb.drive = 5; r.run(0.01); r.pb.drive = 0; r.run(0.01); }
      CHECK(r.sq.currentStep() == 3, "external clock from another rack advances 3 steps");
      r.pb.drive = 0; r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "CLOCK:TEMPO CV") } }); r.pb.drive = 5; r.run(1.0);
      CHECK(r.sq.currentStep() == 3, "TEMPO CV does not clock EXT mode"); }

    { Rig r; r.press("MODE:START/STOP"); r.pb.drive = 1.0f; r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "CLOCK:TEMPO CV") } });
      int changes = 0, prev = r.sq.currentStep(); for (int s = 0; s < 48000; s += 16) { r.g.process(16); if (r.sq.currentStep() != prev) { prev = r.sq.currentStep(); ++changes; } }
      CHECK(changes == 8, "TEMPO CV +1 V doubles the internal clock (8 steps per second)"); }

    { Rig r; r.sq.setParam(Sq10Module::STEPS, 0.8f); r.sq.setParam(Sq10Module::RANGE_A, 1.0f);
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.P, 0 } }); r.press("MODE:START/STOP"); r.run(0.02);
      CHECK(std::abs(r.pb.last - 4.0f) < 1e-4, "CV A (0.8 x 5 V) reaches a jack on another rack");
      r.sq.setParam(Sq10Module::RANGE_A, 0.0f); r.run(0.01);
      CHECK(std::abs(r.pb.last - 0.8f) < 1e-4, "RANGE 1 V: same knob gives 0.8 V");
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.P, 0 }, { r.S, findJack(r.sq, "OUTPUTS:CV C"), r.P, 0 } }); r.run(0.01);
      CHECK(std::abs(r.pb.last - (0.8f + r.out("OUTPUTS:CV C"))) < 1e-4, "two cables into one input are summed");
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.S, findJack(r.sq, "OUTPUTS:CV B") } }); r.run(0.01);
      CHECK(r.pb.last == 0.0f && ! r.g.isConnected(r.P, 0), "output-to-output cable does nothing"); }

    { Rig r; std::vector<float> host(256, 2.0f); r.g.setNormal(r.S, findJack(r.sq, "MIXER:IN 1"), host.data());
      r.sq.setParam(Sq10Module::LEVEL1, 0.5f); r.run(0.01); CHECK(std::abs(r.out("MIXER:OUT") - 1.0f) < 1e-5, "unpatched MIXER IN 1 uses the host signal");
      r.pb.drive = 4; r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "MIXER:IN 1") } }); r.run(0.01);
      CHECK(std::abs(r.out("MIXER:OUT") - 2.0f) < 1e-5, "patching MIXER IN 1 replaces the host signal"); }

    { Rig r; r.sq.setParam(Sq10Module::STEPS, 1.0f); r.sq.setParam(Sq10Module::STEPS + 12, 1.0f); r.press("MODE:START/STOP"); r.run(0.05);
      CHECK(r.out("MIXER:OUT") == 0.0f, "the mixer has no CV normals: CV A and CV B never reach MIXER OUT"); }

    { Rig r; r.sq.setParam(Sq10Module::PORTA_A, 0.6f); r.sq.setParam(Sq10Module::STEPS, 1.0f); r.press("MODE:START/STOP"); r.run(0.05);
      float v = r.out("OUTPUTS:CV A"); CHECK(v > 0.05f && v < 4.5f, "portamento slews CV A instead of jumping"); }

    { auto gateLen = [](float cMode, float c1) { Rig r; r.sq.setParam(Sq10Module::C_MODE, cMode); r.sq.setParam(Sq10Module::STEPS + 24, c1); r.press("MODE:START/STOP");
          int hi = 0; for (int s = 0; s < 11000; s += 16) { r.g.process(16); const float* g = r.g.output(r.S, findJack(r.sq, "OUTPUTS:GATE A")); for (int i = 0; i < 16; ++i) hi += g[i] > 1; } return hi; };
      int shortG = gateLen(1, 0.1f), longG = gateLen(1, 0.9f);
      CHECK(longG > 3 * shortG, "C MODE = TIME: knob C sets gate length");
      CHECK(std::abs(shortG - (int) (0.14 * 12000)) < 40 && std::abs(longG - (int) (0.86 * 12000)) < 40, "C MODE = TIME: 5%..95% of the step");
      CHECK(gateLen(0, 0.1f) == gateLen(0, 0.9f), "C MODE = CV: knob C does not set gate length"); }

    { Rig r; r.sq.setParam(Sq10Module::C_MODE, 1.0f); for (int i = 0; i < 12; ++i) r.sq.setParam(Sq10Module::STEPS + 24 + i, 0.9f);
      r.press("MODE:START/STOP"); float mx = 0; for (int s = 0; s < 48000; s += 16) { r.g.process(16); for (int i = 0; i < 16; ++i) mx = std::max(mx, std::abs(r.g.output(r.S, Sq10Module::CV_C)[i])); }
      CHECK(mx == 0.0f, "C MODE = TIME: no CV on CV C");
      r.sq.setParam(Sq10Module::C_MODE, 0.0f); r.run(0.3); CHECK(std::abs(r.out("OUTPUTS:CV C") - 4.5f) < 1e-4, "C MODE = CV: row C is a third CV (0..5 V)"); }

    { Rig r; r.sq.setParam(Sq10Module::MODE, kModeAB); for (int i = 0; i < 12; ++i) r.sq.setParam(Sq10Module::STEPS + 24 + i, i / 11.0f);
      r.press("MODE:START/STOP"); r.run(14 * 0.25 + 0.1);
      CHECK(r.sq.currentChannel() == 1 && std::abs(r.out("OUTPUTS:CV C") - 5.0f * r.sq.currentStep() / 11.0f) < 1e-4, "C follows the step of whichever row is playing (row B)");
      CHECK(r.sq.indicator(2 + r.sq.currentStep()) == 1.0f && r.sq.indicator(2 + (r.sq.currentStep() + 1) % 12) == 0.0f, "one step lamp lights for the playing step"); }

    { Rig r; r.press("MODE:START/STOP"); bool stop = false;
      std::thread t([&] { int k = 0; while (! stop) { r.g.setCables(k++ % 2 ? std::vector<Cable>{ r.c("3:TRIG", "INPUTS:RESET") } : std::vector<Cable>{}); } });
      r.run(1.0); stop = true; t.join(); CHECK(true, "re-patching from another thread while audio runs (no crash)"); }

    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails); return fails ? 1 : 0;
}
