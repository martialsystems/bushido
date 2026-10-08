#pragma once
// BUSHIDO, the 3 x 12 step sequencer, as a rack::Module. Behaviour is documented in docs/REFERENCE.md.
// Clock, transport and pitch rules follow the Jidai Cable Standard v1.1 (JCS R4, R5); see BUSHIDO_Redesign.md.
#include "../rack/Module.h"
#include "../rack/PitchLaw.h"
#include <jidai/jcs/Detect.h>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

class BushidoModule : public rack::Module {
public:
    BushidoModule();
    const char* name() const override { return "BUSHIDO"; }
    const std::vector<rack::JackInfo>& jacks() const override { return jackList; }
    const std::vector<rack::ParamInfo>& params() const override { return paramList; }
    const std::vector<rack::IndicatorInfo>& indicators() const override { return indList; }
    void prepare(double sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int numSamples) override;
    void setParam(int index, float value) override;
    float getParam(int index) const override { return values[(size_t) index].load(std::memory_order_relaxed); }
    float indicator(int index) const override { return ind[(size_t) index].load(std::memory_order_relaxed); }
    void setTransport(const rack::Transport& t) override { transport = t; hostOffset = 0; hostBpmShown.store(t.valid ? t.bpm : 0.0, std::memory_order_relaxed); }

    // HOST is the default clock only for a NEW instance created in the rack while the DAW transport plays.
    // Saved patches and plugin instances outside the rack keep their stored or panel SOURCE.
    void applyNewInstanceDefaults(bool inRack, bool transportPlaying)
    { if (inRack && transportPlaying) { setParam(SOURCE, 1.0f); setParam(EXT_SOURCE, 1.0f); } }

    // Gate rises and falls on the CV/GATE jack pairs, for the MIDI convenience out. `sample` counts from prepare();
    // the note comes from the jack's target volts (never the slewed CV) under its PITCH LAW (JCS R4.5).
    struct GateEvent { long long sample; int jack; bool on; float target; float cvC; };
    static constexpr int kMaxEvents = 128;
    int takeGateEvents(GateEvent* dst, int max);                     // audio thread; drains the queue
    long long samplesProcessed() const { return sampleCount; }

    rack::pitch::Law law(int jack) const { return p(jack == 0 ? LAW_A : LAW_B) > 0.5f ? rack::pitch::Law::HzvLin : rack::pitch::Law::VOct; }
    double measuredExtPeriod() const { return extPeriodShown.load(std::memory_order_relaxed); }   // seconds, 0 = none yet
    double hostBpm() const { return hostBpmShown.load(std::memory_order_relaxed); }                // the DAW tempo last passed to setTransport, 0 = none (UI readouts)
    int settleSamples() const { return (int) std::ceil(settle); }

    // Tempo in two units. TEMPO is the internal clock in steps per second; the BPM readout shows the same parameter as
    // BPM = steps per second x 60 / steps per beat, where DIV (1/8, 1/16, 1/32) sets 2, 4 or 8 steps per beat.
    // DIV only changes the unit shown: it never changes the clock. TEMPO CV bends around TEMPO; EXT ignores both.
    static double stepsPerSecond(float tempo) { return 0.5 * std::pow(2.0, (double) tempo * 6.0); }     // 0.5..32 steps/s
    static int    stepsPerBeat(float div)     { return 2 << (int) std::lround(std::clamp(div, 0.0f, 1.0f) * 2.0f); }
    static double bpm(float tempo, float div) { return stepsPerSecond(tempo) * 60.0 / stepsPerBeat(div); }
    static float  tempoForBpm(double bpm, float div)
    { return (float) std::clamp(std::log2(std::max(1e-6, bpm * stepsPerBeat(div) / 60.0 / 0.5)) / 6.0, 0.0, 1.0); }

    // test helpers
    int  currentStep() const    { return pos; }      // 0..11, -1 = none
    int  currentChannel() const { return chan; }     // row being read: 0 = A, 1 = B
    int  outJacks() const { return mode() == 2 ? chan : 0; }   // jacks it plays on: 0 = CV/GATE A, 1 = CV/GATE B (only ALT uses B)
    bool isRunning() const      { return running; }

    // jack indices
    enum In  { CLOCK_IN, TEMPO_CV, START_IN, STEP_IN, RESET_IN, MIX_IN1, MIX_IN2, NUM_IN };
    enum Out { CV_A = NUM_IN, GATE_A, CV_B, GATE_B, CV_C, MIX_OUT, TRIG1 };   // TRIG1..TRIG1+11
    // param indices
    // Indices 0..DIV are the v1 front panel and never move. Everything after DIV lives on a tab (CLOCK, STEPS, MIDI).
    enum P { STEPS = 0 /* A1..A12, B1..B12, C1..C12 */, PORTA_A = 36, PORTA_B, RANGE_A, RANGE_B, C_MODE,
             TEMPO, SOURCE, MODE, BTN_START, BTN_STEP, BTN_RESET, LEVEL1, LEVEL2, DIV,
             EXT_SOURCE,             // CLOCK tab: JACK / HOST, used when the front SOURCE switch is at EXT
             SETTLE,                 // CLOCK tab: TIGHT (2 samples, new patches) / VINTAGE (0.6 ms, migrated patches)
             TRIG_MODE,              // CLOCK tab: STEP (whole step, default) / PULSE (5 ms)
             LAW_A, LAW_B,           // STEPS tab: V/OCT (default) / HZ/V LIN, per CV jack
             QUANT_A, QUANT_B,       // STEPS tab: OFF / SEMI
             MIDI_CH_A, MIDI_CH_B,   // MIDI tab: channel 1..16
             VEL_A, VEL_B,           // MIDI tab: 100 / FROM C
             NUM_PARAMS };
    static constexpr int kNumV1Params = DIV + 1;

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
    double sinceTick = 0.0, settle = 2.0;
    float cvA = 0, cvB = 0, cvC = 0, tgtA = 0, tgtB = 0;
    bool gateOn = false;
    // EXT period (JCS R5.2): measured between ticks, restarted at START
    bool havePeriod = false, haveAnyPeriod = false;
    double extPeriod = 0.25;
    std::atomic<double> extPeriodShown { 0.0 };
    std::atomic<double> hostBpmShown { 0.0 };
    long long sampleCount = 0, absorbUntil = -1000;   // START absorbs an EXT edge on its sample and the next 2
    // INT clock: exp2 only when TEMPO CV moves
    double lastTempoCv = 1e30, lastTempoRate = -1.0, tempoFactor = 1.0;
    // HOST clock
    rack::Transport transport;
    int hostOffset = 0;
    bool hostPlayingPrev = false;
    long long hostStep = 0;
    // mixer level smoothing, 10 ms
    double lvl1 = 0.0, lvl2 = 0.0, kMix = 1.0; bool levelsPrimed = false;
    // gate events for MIDI
    std::array<GateEvent, kMaxEvents> events {};
    int numEvents = 0;
    bool gatePrev[2] = { false, false };
    std::array<jidai::jcs::Schmitt, 4> detect {};    // JCS R3 edge detectors (shared jidai-common): clock, start, step, reset

    float p(int i) const { return values[(size_t) i].load(std::memory_order_relaxed); }
    int mode() const { return (int) std::lround(p(MODE) * 2.0f); }     // 0 = A, 1 = A+B, 2 = ALT
    bool edge(int which, float v);
    void start(long long n);
    void stop();
    void tick();
    void reset();
    void fire();
};
