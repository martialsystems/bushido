// Engine + patch graph tests. Build: g++ -std=c++17 -O2 -I. tests/test_engine.cpp rack/PatchGraph.cpp engine/Sq10Module.cpp
#include "../rack/PatchGraph.h"
#include "../engine/Sq10Module.h"
#include <cstdio>
#include <cmath>
#include <thread>
#include <set>
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

struct Rig {
    Sq10Module sq; Probe pb; PatchGraph g; int S, P; const int block = 256;
    Rig() { S = g.addModule(&sq); P = g.addModule(&pb); g.prepare(48000, block); }
    void run(double seconds) { int n = (int) std::lround(seconds * 48000); while (n > 0) { int k = std::min(n, block); g.process(k); n -= k; } }
    void press(const char* id) { int i = findParam(sq, id); sq.setParam(i, 1); sq.setParam(i, 0); }
    float out(const char* id) const { return g.output(S, findJack(sq, id))[0]; }
    Cable c(const char* a, const char* b) const { return { S, findJack(sq, a), S, findJack(sq, b) }; }
};

int main() {
    { Rig r; r.run(0.1); CHECK(! r.sq.isRunning() && r.sq.currentStep() == -1, "starts stopped with no step");
      r.press("MODE:START/STOP"); r.run(0.01);
      CHECK(r.sq.isRunning() && r.sq.currentStep() == 0, "START plays step 1");
      CHECK(r.out("OUTPUTS:GATE A") == 5.0f && r.out("1:TRIG") == 5.0f, "gate A and TRIG 1 high on step 1");
      // tempo 0.5 -> 0.5 * 2^3 = 4 steps/s -> a new step every 12000 samples
      int changes = 0, prev = r.sq.currentStep(); long firstAt = -1;
      for (int s = 0; s < 48000; s += 16) { r.g.process(16); if (r.sq.currentStep() != prev) { prev = r.sq.currentStep(); if (firstAt < 0) firstAt = s; ++changes; } }
      CHECK(changes == 4, "internal clock: 4 steps per second at default tempo");
      CHECK(std::abs(firstAt + 480 - 12000) <= 16, "step period is 12000 samples (+-1 sub-block)"); }

    { Rig r; r.sq.setParam(Sq10Module::MODE, 0.0f); r.press("MODE:START/STOP"); r.run(12 * 0.25 + 0.1);
      CHECK(! r.sq.isRunning() && r.sq.currentStep() == -1 && r.out("OUTPUTS:GATE A") == 0, "mode A: plays 12 steps then stops"); }

    { Rig r; r.sq.setParam(Sq10Module::MODE, 0.5f); r.press("MODE:START/STOP"); r.run(13 * 0.25 - 0.1);
      CHECK(r.sq.currentChannel() == 1 && r.sq.currentStep() == 0, "mode A->B: step 13 is B1");
      r.run(12 * 0.25); CHECK(! r.sq.isRunning(), "mode A->B: stops after B12"); }

    { Rig r; r.sq.setParam(Sq10Module::MODE, 1.0f); r.press("MODE:START/STOP"); r.run(30 * 0.25);
      CHECK(r.sq.isRunning(), "mode A<->B: keeps alternating past 24 steps"); }

    { Rig r; for (int i = 0; i < 12; ++i) r.sq.setParam(Sq10Module::STEPS + i, (i + 1) / 12.0f);
      r.g.setCables({ r.c("5:TRIG", "INPUTS:RESET") }); r.press("MODE:START/STOP");
      std::set<int> seen; float maxCv = 0; int gateRises = 0; bool prevG = false;
      for (int s = 0; s < 48000 * 3; s += 16) { r.g.process(16); seen.insert(r.sq.currentStep());
          const float* cv = r.g.output(r.S, findJack(r.sq, "OUTPUTS:CV A")); const float* ga = r.g.output(r.S, findJack(r.sq, "OUTPUTS:GATE A"));
          for (int i = 0; i < 16; ++i) { maxCv = std::max(maxCv, cv[i]); bool gg = ga[i] > 1; if (gg && ! prevG) ++gateRises; prevG = gg; } }
      CHECK(seen.count(5) == 0 && seen.count(3) == 1, "TRIG 5 -> RESET loops steps 1-4");
      CHECK(maxCv <= 5.0f * 4 / 12 + 1e-4, "the skipped step 5 never reaches CV A");
      CHECK(gateRises == 12, "one clean gate per played step (12 in 3 s)"); }

    { Rig r; r.press("MODE:STEP"); r.run(0.01); CHECK(! r.sq.isRunning() && r.sq.currentStep() == 0, "STEP while stopped moves to step 1");
      r.press("MODE:STEP"); r.run(0.01); CHECK(r.sq.currentStep() == 1, "STEP again moves to step 2"); }

    { Rig r; r.sq.setParam(Sq10Module::SOURCE, 1.0f); r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "CLOCK:CLOCK") } });
      r.press("MODE:START/STOP"); r.run(0.5); CHECK(r.sq.currentStep() == 0, "external clock: no internal ticks");
      for (int k = 0; k < 3; ++k) { r.pb.drive = 5; r.run(0.01); r.pb.drive = 0; r.run(0.01); }
      CHECK(r.sq.currentStep() == 3, "external clock from another rack advances 3 steps"); }

    { Rig r; r.sq.setParam(Sq10Module::STEPS, 0.8f); r.sq.setParam(Sq10Module::RANGE_A, 1.0f);
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.P, 0 } }); r.press("MODE:START/STOP"); r.run(0.02);
      CHECK(std::abs(r.pb.last - 4.0f) < 1e-4, "CV A (0.8 x 5 V) reaches a jack on another rack");
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.P, 0 }, { r.S, findJack(r.sq, "OUTPUTS:CV C"), r.P, 0 } }); r.run(0.01);
      CHECK(std::abs(r.pb.last - (4.0f + r.out("OUTPUTS:CV C"))) < 1e-4, "two cables into one input are summed");
      r.g.setCables({ { r.S, findJack(r.sq, "OUTPUTS:CV A"), r.S, findJack(r.sq, "OUTPUTS:CV B") } }); r.run(0.01);
      CHECK(r.pb.last == 0.0f && ! r.g.isConnected(r.P, 0), "output-to-output cable does nothing"); }

    { Rig r; std::vector<float> host(256, 2.0f); r.g.setNormal(r.S, findJack(r.sq, "MIXER:IN 1"), host.data());
      r.sq.setParam(Sq10Module::LEVEL1, 0.5f); r.run(0.01); CHECK(std::abs(r.out("MIXER:OUT") - 1.0f) < 1e-5, "unpatched MIXER IN 1 uses the host signal");
      r.pb.drive = 4; r.g.setCables({ { r.P, 1, r.S, findJack(r.sq, "MIXER:IN 1") } }); r.run(0.01);
      CHECK(std::abs(r.out("MIXER:OUT") - 2.0f) < 1e-5, "patching MIXER IN 1 replaces the host signal"); }

    { Rig r; r.sq.setParam(Sq10Module::PORTA_A, 0.6f); r.sq.setParam(Sq10Module::STEPS, 1.0f); r.press("MODE:START/STOP"); r.run(0.05);
      float v = r.out("OUTPUTS:CV A"); CHECK(v > 0.05f && v < 4.5f, "portamento slews CV A instead of jumping"); }

    { auto gateLen = [](float c1) { Rig r; r.sq.setParam(Sq10Module::C_MODE, 1.0f); r.sq.setParam(Sq10Module::STEPS + 24, c1); r.press("MODE:START/STOP");
          int hi = 0; for (int s = 0; s < 11000; s += 16) { r.g.process(16); const float* g = r.g.output(r.S, findJack(r.sq, "OUTPUTS:GATE A")); for (int i = 0; i < 16; ++i) hi += g[i] > 1; } return hi; };
      int shortG = gateLen(0.1f), longG = gateLen(0.9f);
      CHECK(longG > 3 * shortG, "C MODE = TIME: knob C sets gate length"); }

    { Rig r; r.press("MODE:START/STOP"); bool stop = false;
      std::thread t([&] { int k = 0; while (! stop) { r.g.setCables(k++ % 2 ? std::vector<Cable>{ r.c("3:TRIG", "INPUTS:RESET") } : std::vector<Cable>{}); } });
      r.run(1.0); stop = true; t.join(); CHECK(true, "re-patching from another thread while audio runs (no crash)"); }

    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails); return fails ? 1 : 0;
}
