#include "Sq10Module.h"
#include <cmath>
#include <algorithm>

using namespace rack;

Sq10Module::Sq10Module() : ind(14)
{
    jackList = { {"CLOCK:CLOCK", Dir::In}, {"CLOCK:TEMPO CV", Dir::In}, {"INPUTS:START/STOP", Dir::In}, {"INPUTS:STEP", Dir::In},
                 {"INPUTS:RESET", Dir::In}, {"MIXER:IN 1", Dir::In}, {"MIXER:IN 2", Dir::In},
                 {"OUTPUTS:CV A", Dir::Out}, {"OUTPUTS:GATE A", Dir::Out}, {"OUTPUTS:CV B", Dir::Out}, {"OUTPUTS:GATE B", Dir::Out},
                 {"OUTPUTS:CV C", Dir::Out}, {"MIXER:OUT", Dir::Out} };
    for (int i = 1; i <= 12; ++i) jackList.push_back({ std::to_string(i) + ":TRIG", Dir::Out });

    for (char row : { 'A', 'B', 'C' }) for (int i = 1; i <= 12; ++i) paramList.push_back({ std::string(1, row) + ":" + std::to_string(i), 0.5f, 0 });
    paramList.insert(paramList.end(), {
        {"CH:PORTA A", 0.0f, 0}, {"CH:PORTA B", 0.0f, 0}, {"CH:RANGE A", 1.0f, 2}, {"CH:RANGE B", 1.0f, 2}, {"CH:C MODE", 0.0f, 2},
        {"CLOCK:TEMPO", 0.5f, 0}, {"CLOCK:SOURCE", 0.0f, 2}, {"MODE:MODE", 0.5f, 3},
        {"MODE:START/STOP", 0.0f, -1}, {"MODE:STEP", 0.0f, -1}, {"MODE:RESET", 0.0f, -1},
        {"MIXER:LEVEL 1", 0.7f, 0}, {"MIXER:LEVEL 2", 0.7f, 0} });
    for (size_t i = 0; i < values.size(); ++i) values[i].store(paramList[i].def);

    indList = { {"CH:A"}, {"CH:B"} };
    for (int i = 1; i <= 12; ++i) indList.push_back({ "STEP:" + std::to_string(i) });
}

void Sq10Module::prepare(double sampleRate, int) { sr = sampleRate; settle = std::max(1.0, sr * 0.0006); }

void Sq10Module::setParam(int i, float v)
{
    if (paramList[(size_t) i].positions == -1) {                 // momentary: count presses on the rising edge
        if (v > 0.5f && values[(size_t) i].load() <= 0.5f) presses[(size_t) (i - BTN_START)].fetch_add(1);
    } else if (paramList[(size_t) i].positions >= 2) {            // switch: snap
        const int n = paramList[(size_t) i].positions - 1;
        v = std::round(std::clamp(v, 0.0f, 1.0f) * (float) n) / (float) n;
    }
    values[(size_t) i].store(std::clamp(v, 0.0f, 1.0f));
}

bool Sq10Module::edge(int which, float v)                         // rising edge with hysteresis: high > 1 V, low < 0.5 V
{
    bool& h = high[(size_t) which];
    if (! h && v > 1.0f) { h = true; return true; }
    if (h && v < 0.5f) h = false;
    return false;
}

void Sq10Module::fire()                                           // start of a step on the current channel
{
    samplesInStep = 0.0; gateOn = true;           // gate (and CV) start after the settle time, see process()
}

void Sq10Module::start()                                          // every start, including after a stop, begins at A step 1
{
    running = true; pos = 0; chan = 0;
    phase = 0.0; fire();
}

void Sq10Module::reset()                                          // A step 1, keeps running (or stays stopped)
{
    pos = 0; chan = 0;
    if (running) { phase = 0.0; fire(); } else gateOn = false;     // A1 gets a full clock period
}

// Every mode loops until Stop. `chan` is the row being read; outJacks() is the jack pair it plays on.
//   A    (0): row A, 12 steps, on the A jacks. The B jacks hold.
//   A+B  (1): one 24-step sequence on the A jacks: steps 1-12 = row A, 13-24 = row B, then A1. The B jacks hold.
//   ALT  (2): one row per pass, each on its own jacks: row A on the A jacks, then row B on the B jacks, and so on.
void Sq10Module::tick()
{
    if (pos >= 0) lastPeriod = std::clamp(sinceTick, 0.005, 4.0);   // gate length for EXT and STEP: time since the last tick
    sinceTick = 0.0;
    if (pos < 0) { pos = 0; chan = 0; fire(); return; }
    if (++pos < 12) { fire(); return; }
    pos = 0;
    chan = mode() == 0 ? 0 : 1 - chan;
    fire();
}

void Sq10Module::process(const float* const* in, float* const* out, int n)
{
    int pressNow[3];
    for (int b = 0; b < 3; ++b) { const int c = presses[(size_t) b].load(); pressNow[b] = c - pressesSeen[(size_t) b]; pressesSeen[(size_t) b] = c; }

    const float rangeA = p(RANGE_A) > 0.5f ? 5.0f : 1.0f, rangeB = p(RANGE_B) > 0.5f ? 5.0f : 1.0f;
    const bool cIsTime = p(C_MODE) > 0.5f, external = p(SOURCE) > 0.5f;
    const double tempoRate = 0.5 * std::pow(2.0, p(TEMPO) * 6.0);          // 0.5..32 steps/s, INT only
    auto slew = [this](float porta) { const double tau = (double) porta * porta * 2.0; return tau < 1e-4 ? 1.0f : (float) (1.0 - std::exp(-1.0 / (tau * sr))); };
    const float kA = slew(p(PORTA_A)), kB = slew(p(PORTA_B));

    for (int i = 0; i < n; ++i) {
        const bool doReset = edge(3, in[RESET_IN][i]) || (i == 0 && pressNow[2] > 0);
        const bool doStart = edge(1, in[START_IN][i]) || (i == 0 && pressNow[0] > 0);
        const bool doStep  = edge(2, in[STEP_IN][i])  || (i == 0 && pressNow[1] > 0);
        const bool extClk  = edge(0, in[CLOCK_IN][i]);

        if (doStart) { if (running) { running = false; gateOn = false; } else start(); }
        if (doReset) reset();
        else {
            bool t = doStep;
            if (running && ! external) {                         // TEMPO and TEMPO CV bend the internal clock only; EXT ignores both
                rate = std::clamp(tempoRate * std::pow(2.0, (double) in[TEMPO_CV][i]), 0.05, 200.0);
                phase += rate / sr;
                if (phase >= 1.0) { phase -= 1.0; t = true; }
            }
            if (running && external && extClk) t = true;
            if (t) tick();
        }
        sinceTick += 1.0 / sr; samplesInStep += 1.0;

        if (mode() == 0) chan = 0;                               // switched to A mid-row B: carry on in row A
        const int jk = outJacks();
        // CV: the jacks playing follow the knob at the current step (live edits are heard); the other jacks hold.
        // A new step waits `settle` (0.6 ms) before CV and gate change, so a reset patched from a TRIG jack lands first.
        const bool settled = samplesInStep >= settle;
        if (pos >= 0 && settled) {
            const float knob = p(STEPS + 12 * chan + pos);       // row A or row B
            if (jk == 0) tgtA = knob * rangeA; else tgtB = knob * rangeB;   // range and portamento belong to the jacks
            cvC = p(STEPS + 24 + pos) * 5.0f;
        }
        if (cIsTime) cvC = 0.0f;                                 // TIME: row C sets gate length only and is never emitted as CV
        cvA += (tgtA - cvA) * kA; cvB += (tgtB - cvB) * kB;

        const double period = (running && ! external) ? 1.0 / rate : lastPeriod;
        const double frac = (cIsTime && pos >= 0) ? 0.05 + 0.9 * p(STEPS + 24 + pos) : 0.5;
        const bool g = gateOn && pos >= 0 && settled && samplesInStep < settle + frac * period * sr;

        out[CV_A][i] = cvA; out[CV_B][i] = cvB; out[CV_C][i] = cvC;
        out[GATE_A][i] = (g && jk == 0) ? 5.0f : 0.0f;
        out[GATE_B][i] = (g && jk == 1) ? 5.0f : 0.0f;
        out[MIX_OUT][i] = in[MIX_IN1][i] * p(LEVEL1) + in[MIX_IN2][i] * p(LEVEL2);
        for (int s = 0; s < 12; ++s) out[TRIG1 + s][i] = (pos == s) ? 5.0f : 0.0f;
    }
    ind[0].store(pos >= 0 && chan == 0 ? 1.0f : 0.0f); ind[1].store(pos >= 0 && chan == 1 ? 1.0f : 0.0f);   // which row is being read
    for (int s = 0; s < 12; ++s) ind[(size_t) s + 2].store(pos == s ? 1.0f : 0.0f);   // one lamp per step, shared by rows A, B and C
}
