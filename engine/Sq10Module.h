#pragma once
// SQ-10 style 3 x 12 step sequencer as a rack::Module. Behaviour is documented in ENGINE_NOTES.md.
#include "../rack/Module.h"
#include <array>
#include <atomic>
#include <cmath>

class Sq10Module : public rack::Module {
public:
    Sq10Module();
    const char* name() const override { return "SQ-10"; }
    const std::vector<rack::JackInfo>& jacks() const override { return jackList; }
    const std::vector<rack::ParamInfo>& params() const override { return paramList; }
    const std::vector<rack::IndicatorInfo>& indicators() const override { return indList; }
    void prepare(double sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int numSamples) override;
    void setParam(int index, float value) override;
    float getParam(int index) const override { return values[(size_t) index].load(std::memory_order_relaxed); }
    float indicator(int index) const override { return ind[(size_t) index].load(std::memory_order_relaxed); }

    // test helpers
    int  currentStep() const    { return pos; }      // 0..11, -1 = none
    int  currentChannel() const { return chan; }     // 0 = A, 1 = B
    bool isRunning() const      { return running; }

    // jack indices
    enum In  { CLOCK_IN, TEMPO_CV, START_IN, STEP_IN, RESET_IN, MIX_IN1, MIX_IN2, NUM_IN };
    enum Out { CV_A = NUM_IN, GATE_A, CV_B, GATE_B, CV_C, MIX_OUT, TRIG1 };   // TRIG1..TRIG1+11
    // param indices
    enum P { STEPS = 0 /* A1..A12, B1..B12, C1..C12 */, PORTA_A = 36, PORTA_B, RANGE_A, RANGE_B, C_MODE,
             TEMPO, SOURCE, MODE, BTN_START, BTN_STEP, BTN_RESET, LEVEL1, LEVEL2, NUM_PARAMS };

private:
    std::vector<rack::JackInfo> jackList;
    std::vector<rack::ParamInfo> paramList;
    std::vector<rack::IndicatorInfo> indList;
    std::array<std::atomic<float>, NUM_PARAMS> values;
    std::array<std::atomic<int>, 3> presses {};     // momentary buttons, counted so a quick tap is never lost
    std::array<int, 3> pressesSeen {};
    std::vector<std::atomic<float>> ind;

    double sr = 48000.0;
    bool running = false;
    int pos = -1, chan = 0;
    double phase = 0.0, rate = 4.0;                  // internal clock, steps per second
    double samplesInStep = 0.0, lastPeriod = 0.25;   // seconds
    double sinceTick = 0.0, settle = 29.0;
    float cvA = 0, cvB = 0, cvC = 0, tgtA = 0, tgtB = 0;
    bool gateOn = false;
    std::array<bool, 5> high {};                     // edge detectors: clock, start, step, reset, (unused)

    float p(int i) const { return values[(size_t) i].load(std::memory_order_relaxed); }
    int mode() const { return (int) std::lround(p(MODE) * 2.0f); }     // 0 = A, 1 = A+B, 2 = ALT
    bool edge(int which, float v);
    void start();
    void tick();
    void reset();
    void fire();
};
