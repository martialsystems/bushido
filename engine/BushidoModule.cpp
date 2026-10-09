#include "BushidoModule.h"
#include <jidai/jcs/Volts.h>
#include <cmath>
#include <algorithm>
#include <functional>

using namespace rack;

BushidoModule::BushidoModule() : ind(15)
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
        {"MIXER:LEVEL 1", 0.7f, 0}, {"MIXER:LEVEL 2", 0.7f, 0},
        {"CLOCK:DIV", 0.5f, 3} });                                // 1/8, 1/16, 1/32: the BPM readout's unit, and the HOST step size
    paramList.insert(paramList.end(), {                           // tab controls (the front panel is unchanged)
        {"CLOCK:EXT SOURCE", 0.0f, 2},                            // JACK / HOST
        {"CLOCK:SETTLE", 0.0f, 2},                                // TIGHT / VINTAGE
        {"CLOCK:TRIG MODE", 0.0f, 2},                             // STEP / PULSE
        {"STEPS:LAW A", 0.0f, 2}, {"STEPS:LAW B", 0.0f, 2},       // V/OCT / HZ/V LIN
        {"STEPS:QUANT A", 0.0f, 2}, {"STEPS:QUANT B", 0.0f, 2},   // OFF / SEMI
        {"MIDI:CH A", 0.0f, 16}, {"MIDI:CH B", 1.0f / 15.0f, 16}, // channel 1..16 (A on 1, B on 2)
        {"MIDI:VEL A", 0.0f, 2}, {"MIDI:VEL B", 0.0f, 2} });      // 100 / FROM C
    for (size_t i = 0; i < values.size(); ++i) values[i].store(paramList[i].def);

    indList = { {"CH:A"}, {"CH:B"} };
    for (int i = 1; i <= 12; ++i) indList.push_back({ "STEP:" + std::to_string(i) });
    indList.push_back({ "MODE:RUN" });                            // red lamp under START/STOP, lit while running
}

void BushidoModule::prepare(double sampleRate, int)
{
    sr = sampleRate;
    kMix = -std::expm1(-1.0 / (0.010 * sr));                      // mixer LEVEL smoothing, 10 ms
    levelsPrimed = false;
    sampleCount = 0; absorbUntil = -1000; numEvents = 0;
    lastTempoCv = 1e30; lastTempoRate = -1.0;
    slewPortaA = slewPortaB = rateTempo = -1.0f;                  // the coefficients depend on the sample rate
}

// PORTA law, kept bit-exact from v1 (user decision): tau = PORTA^2 x 2 s, k in double, the update in float.
float BushidoModule::slewFor(float porta) const
{
    const double tau = (double) porta * (double) porta * 2.0;
    return tau < 1e-4 ? 1.0f : (float) (1.0 - std::exp(-1.0 / (tau * sr)));
}

// Stopped with nothing to do: no button press, no RESET, START or STEP edge, no transport start, no gate to rise or fall and
// no step still settling. Then every sample of the block is the same except the CV slew and the mixer, and the per-sample loop
// would only count time. Decides before any state changes; on true the edge detectors have consumed the block.
bool BushidoModule::idleBlock(const float* const* in, int n, int pressed, bool hostValid)
{
    if (! idleSkip || running || pressed != 0 || gatePrev[0] || gatePrev[1]) return false;
    if (hostValid && transport.playing && ! hostPlayingPrev) return false;          // the transport starts in this block
    auto d = detect;                                                                 // trial run on copies: commit only if idle
    for (int i = 0; i < n; ++i) if (d[3].rising(in[RESET_IN][i]) | d[1].rising(in[START_IN][i]) | d[2].rising(in[STEP_IN][i])) return false;
    for (int i = 0; i < n; ++i) d[0].process(in[CLOCK_IN][i]);                       // a CLOCK edge does nothing while stopped
    if (pos >= 0) {
        if (samplesInStep + 1.0 < settle) return false;                              // the step's CV is still settling
        if (gateOn) {                                                                // STEP while stopped: its gate may still be open
            const bool cIsTime = p(C_MODE) > 0.5f;
            const double period = haveAnyPeriod ? extPeriod : 1.0 / rateSteps;       // the loop's period while stopped
            const double frac = cIsTime ? 0.05 + 0.9 * (double) p(STEPS + 24 + pos) : 0.5;
            if (samplesInStep + 1.0 < settle + frac * period * sr) return false;    // samplesInStep only grows: closed now, closed all block
        }
    }
    detect = d;
    return true;
}

void BushidoModule::setParam(int i, float v)
{
    if (paramList[(size_t) i].positions == -1) {                 // momentary: count presses on the rising edge
        if (v > 0.5f && values[(size_t) i].load() <= 0.5f) presses[(size_t) (i - BTN_START)].fetch_add(1);
    } else if (paramList[(size_t) i].positions >= 2) {            // switch: snap
        const int n = paramList[(size_t) i].positions - 1;
        v = std::round(std::clamp(v, 0.0f, 1.0f) * (float) n) / (float) n;
    }
    values[(size_t) i].store(std::clamp(v, 0.0f, 1.0f));
}

bool BushidoModule::edge(int which, float v)                         // JCS R3: rising edge with hysteresis, high > 1 V, low < 0.5 V
{
    return detect[(size_t) which].rising(v);                          // the shared jidai::jcs::Schmitt
}

void BushidoModule::fire()                                           // start of a step on the current channel
{
    samplesInStep = 0.0; gateOn = true;           // gate (and CV) start after the settle time, see process()
}

void BushidoModule::start(long long n)                               // JCS R5.2: every start, including after a stop, plays A step 1 now
{
    running = true; pos = 0; chan = 0;
    phase = 0.0; fire();
    sinceTick = 0.0; havePeriod = false;          // the stopped time is not a clock period
    absorbUntil = n + 2;                          // an EXT edge on this sample or the next 2 is step 1's own clock
}

void BushidoModule::stop()                                           // JCS R5.4: gates and TRIGs go low now; lamps and CV hold
{
    running = false; gateOn = false;
}

void BushidoModule::reset()                                          // A step 1, keeps running (or stays stopped)
{
    pos = 0; chan = 0;
    if (running) { phase = 0.0; fire(); } else gateOn = false;     // A1 gets a full clock period
}

// Every mode loops until Stop. `chan` is the row being read; outJacks() is the jack pair it plays on.
//   A    (0): row A, 12 steps, on the A jacks. The B jacks hold.
//   A+B  (1): one 24-step sequence on the A jacks: steps 1-12 = row A, 13-24 = row B, then A1. The B jacks hold.
//   ALT  (2): one row per pass, each on its own jacks: row A on the A jacks, then row B on the B jacks, and so on.
void BushidoModule::tick()
{
    if (havePeriod) { extPeriod = std::clamp(sinceTick, 0.005, 4.0); haveAnyPeriod = true; }   // gate length for EXT and STEP
    havePeriod = true; sinceTick = 0.0;
    if (pos < 0) { pos = 0; chan = 0; fire(); return; }
    if (++pos < 12) { fire(); return; }
    pos = 0;
    chan = mode() == 0 ? 0 : 1 - chan;
    fire();
}

int BushidoModule::takeGateEvents(GateEvent* dst, int max)
{
    const int k = std::min(max, numEvents);
    std::copy(events.begin(), events.begin() + k, dst);
    numEvents = 0;
    return k;
}

void BushidoModule::process(const float* const* in, float* const* out, int n)
{
    int pressNow[3];
    for (int b = 0; b < 3; ++b) { const int c = presses[(size_t) b].load(); pressNow[b] = c - pressesSeen[(size_t) b]; pressesSeen[(size_t) b] = c; }

    const float rangeA = p(RANGE_A) > 0.5f ? jidai::jcs::kNominal : 1.0f, rangeB = p(RANGE_B) > 0.5f ? jidai::jcs::kNominal : 1.0f;   // 5 V or 1 V (JCS R1)
    const bool cIsTime = p(C_MODE) > 0.5f, external = p(SOURCE) > 0.5f;
    const bool host = external && p(EXT_SOURCE) > 0.5f, jackClock = external && ! host;
    if (const float t = p(TEMPO); std::not_equal_to<float>{}(t, rateTempo)) { rateTempo = t; rateSteps = stepsPerSecond(t); }   // exact change test
    const double tempoRate = rateSteps;                                     // 0.5..32 steps/s, INT only; DIV does not change it
    if (const float a = p(PORTA_A); std::not_equal_to<float>{}(a, slewPortaA)) { slewPortaA = a; slewKA = slewFor(a); }
    if (const float b = p(PORTA_B); std::not_equal_to<float>{}(b, slewPortaB)) { slewPortaB = b; slewKB = slewFor(b); }
    const float kA = slewKA, kB = slewKB;
    settle = p(SETTLE) > 0.5f ? std::max(1.0, sr * 0.0006) : 2.0;           // VINTAGE keeps v1's exact 0.6 ms; TIGHT is 2 samples (JCS R5.5)
    const bool pulse = p(TRIG_MODE) > 0.5f;
    const double pulseLen = std::max(1.0, std::round(0.005 * sr));
    const pitch::Law lawA = law(0), lawB = law(1);
    const bool quantA = p(QUANT_A) > 0.5f, quantB = p(QUANT_B) > 0.5f;
    const double l1 = (double) p(LEVEL1), l2 = (double) p(LEVEL2);
    if (! levelsPrimed) { lvl1 = l1; lvl2 = l2; levelsPrimed = true; }
    const int q = stepsPerBeat(p(DIV));                                     // HOST: steps per quarter, 2 / 4 / 8
    const bool hostValid = host && transport.valid && transport.bpm > 0.0;
    if (! host) hostPlayingPrev = false;

    if (idleBlock(in, n, pressNow[0] | pressNow[1] | pressNow[2], hostValid)) {
        // The same results as the loop below, without its per-sample work: nothing ticks, no gate or TRIG is high.
        if (hostValid) hostPlayingPrev = transport.playing;
        hostOffset += n;
        if (mode() == 0) chan = 0;
        const int jk = outJacks();
        if (pos >= 0) {                                                       // settled for the whole block (idleBlock)
            const float knob = p(STEPS + 12 * chan + pos);
            if (jk == 0) { const float v = knob * rangeA; tgtA = quantA ? (float) pitch::quantize(lawA, (double) v) : v; }
            else         { const float v = knob * rangeB; tgtB = quantB ? (float) pitch::quantize(lawB, (double) v) : v; }
            cvC = p(STEPS + 24 + pos) * jidai::jcs::kNominal;
        }
        if (cIsTime) cvC = 0.0f;
        const float low = jidai::jcs::gateVolts(false);
        for (int i = 0; i < n; ++i) {                                          // per sample only what still moves: time, CV slew, mixer
            sinceTick += 1.0 / sr; samplesInStep += 1.0;
            cvA += (tgtA - cvA) * kA; cvB += (tgtB - cvB) * kB;
            lvl1 += (l1 - lvl1) * kMix; lvl2 += (l2 - lvl2) * kMix;
            if (std::abs(l1 - lvl1) < 1e-9) lvl1 = l1;
            if (std::abs(l2 - lvl2) < 1e-9) lvl2 = l2;
            out[CV_A][i] = cvA; out[CV_B][i] = cvB;
            out[MIX_OUT][i] = (float) ((double) in[MIX_IN1][i] * lvl1 + (double) in[MIX_IN2][i] * lvl2);
        }
        if (n == 1) {                                                          // a feedback cable runs the graph one sample at a time
            out[CV_C][0] = cvC; out[GATE_A][0] = low; out[GATE_B][0] = low;
            for (int s = 0; s < 12; ++s) out[TRIG1 + s][0] = low;
        } else {                                                               // the rest is constant all block
            std::fill(out[CV_C], out[CV_C] + n, cvC);
            for (int j : { (int) GATE_A, (int) GATE_B }) std::fill(out[j], out[j] + n, low);
            for (int s = 0; s < 12; ++s) std::fill(out[TRIG1 + s], out[TRIG1 + s] + n, low);
        }
        sampleCount += n; ++idleCount;
    } else
    for (int i = 0; i < n; ++i, ++sampleCount) {
        const bool doReset = edge(3, in[RESET_IN][i]) || (i == 0 && pressNow[2] > 0);
        const bool doStart = edge(1, in[START_IN][i]) || (i == 0 && pressNow[0] > 0);
        const bool doStep  = edge(2, in[STEP_IN][i])  || (i == 0 && pressNow[1] > 0);
        const bool extClk  = edge(0, in[CLOCK_IN][i]);

        // HOST (JCS R5.7): the step index is floor(ppq x q); a change of index is a tick. Transport start and stop
        // apply the START and STOP rules, so the step position follows the host and cannot drift.
        bool hostTick = false;
        if (hostValid) {
            const double ppq = transport.ppq + (double) hostOffset * transport.bpm / (60.0 * sr);
            const long long k = (long long) std::floor(ppq * q + 1e-9);
            const bool playing = transport.playing;
            if (playing && ! hostPlayingPrev) { if (! running) start(sampleCount); hostStep = k; }
            else if (! playing && hostPlayingPrev) { if (running) stop(); }
            else if (playing && running && k != hostStep) { hostStep = k; hostTick = true; }
            hostPlayingPrev = playing;
        }
        ++hostOffset;

        if (doStart) { if (running) stop(); else start(sampleCount); }
        if (doReset) reset();
        else {
            bool t = doStep;
            if (running && ! external) {                         // TEMPO and TEMPO CV bend the internal clock only; EXT ignores both
                const double v = (double) in[TEMPO_CV][i];
                if (std::abs(v - lastTempoCv) > 1e-6 || std::not_equal_to<double>{}(tempoRate, lastTempoRate)) {   // exact: any TEMPO change (same as !=)
                    lastTempoCv = v; lastTempoRate = tempoRate;
                    rate = std::clamp(tempoRate * std::exp2(v), 0.05, 200.0);
                }
                phase += rate / sr;
                if (phase >= 1.0) { phase -= 1.0; t = true; }
            }
            if (running && jackClock && extClk && sampleCount > absorbUntil) t = true;
            if (hostTick) t = true;
            if (t) tick();
        }
        sinceTick += 1.0 / sr; samplesInStep += 1.0;

        if (mode() == 0) chan = 0;                               // switched to A mid-row B: carry on in row A
        const int jk = outJacks();
        // CV: the jacks playing follow the knob at the current step (live edits are heard); the other jacks hold.
        // A new step waits `settle` before CV and gate change, so a reset patched from a TRIG jack lands first.
        const bool settled = samplesInStep >= settle;
        if (pos >= 0 && settled) {
            const float knob = p(STEPS + 12 * chan + pos);       // row A or row B
            if (jk == 0) { const float v = knob * rangeA; tgtA = quantA ? (float) pitch::quantize(lawA, (double) v) : v; }   // range, law and portamento belong to the jacks
            else         { const float v = knob * rangeB; tgtB = quantB ? (float) pitch::quantize(lawB, (double) v) : v; }
            cvC = p(STEPS + 24 + pos) * jidai::jcs::kNominal;     // unipolar CV 0..+5 V (JCS R1)
        }
        if (cIsTime) cvC = 0.0f;                                 // TIME: row C sets gate length only and is never emitted as CV
        cvA += (tgtA - cvA) * kA; cvB += (tgtB - cvB) * kB;

        const double period = host && running ? (hostValid ? 60.0 / (transport.bpm * q) : 1.0 / tempoRate)
                            : (running && ! external) ? 1.0 / rate
                            : haveAnyPeriod ? extPeriod : 1.0 / tempoRate;   // EXT before a period is known: the INT tempo period
        const double frac = (cIsTime && pos >= 0) ? 0.05 + 0.9 * (double) p(STEPS + 24 + pos) : 0.5;
        const bool g = gateOn && pos >= 0 && settled && samplesInStep < settle + frac * period * sr;

        lvl1 += (l1 - lvl1) * kMix; lvl2 += (l2 - lvl2) * kMix;
        if (std::abs(l1 - lvl1) < 1e-9) lvl1 = l1;                // land exactly, so the smoother never decays into denormals
        if (std::abs(l2 - lvl2) < 1e-9) lvl2 = l2;

        out[CV_A][i] = cvA; out[CV_B][i] = cvB; out[CV_C][i] = cvC;
        const bool gA = g && jk == 0, gB = g && jk == 1;
        out[GATE_A][i] = jidai::jcs::gateVolts(gA);              // 0 / +5 V (JCS R2)
        out[GATE_B][i] = jidai::jcs::gateVolts(gB);
        out[MIX_OUT][i] = (float) ((double) in[MIX_IN1][i] * lvl1 + (double) in[MIX_IN2][i] * lvl2);
        const bool trigOn = running && (! pulse || samplesInStep <= pulseLen);   // JCS R5.4: TRIG low while stopped
        for (int s = 0; s < 12; ++s) out[TRIG1 + s][i] = jidai::jcs::gateVolts(trigOn && pos == s);

        const bool gNow[2] = { gA, gB };
        for (int j = 0; j < 2; ++j) if (gNow[j] != gatePrev[j]) {
            gatePrev[j] = gNow[j];
            if (numEvents < kMaxEvents) events[(size_t) numEvents++] = { sampleCount, j, gNow[j], j == 0 ? tgtA : tgtB, cvC };
        }
    }
    if (haveAnyPeriod) extPeriodShown.store(extPeriod, std::memory_order_relaxed);
    ind[0].store(pos >= 0 && chan == 0 ? 1.0f : 0.0f); ind[1].store(pos >= 0 && chan == 1 ? 1.0f : 0.0f);   // which row is being read
    for (int s = 0; s < 12; ++s) ind[(size_t) s + 2].store(pos == s ? 1.0f : 0.0f);   // one lamp per step, shared by rows A, B and C
    ind[14].store(running ? 1.0f : 0.0f);
}
