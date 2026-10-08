// Headless BushidoProcessor test (JUCE, no editor): state format and migration, MIDI out, sub-blocks, bypass, HOST sync, factory bank.
// The user pattern file is redirected to a temp dir (BushidoProcessor::setUserPatternFile), so the real one is never touched on any OS.
#include "../plugin/PluginProcessor.h"
#include "BinaryData.h"
#include <jidai/jcs/Volts.h>
#if defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // exact compares are intended: parameter values and saved state round-trip bit-exactly
#endif
#include <cstdio>
#include <vector>
#include <functional>
#include <set>
#include <map>
#include <cmath>

static int fails = 0, passes = 0;
static void CHECK(bool c, const juce::String& msg) { std::printf("%s %s\n", c ? "PASS" : "FAIL", msg.toRawUTF8()); if (c) ++passes; else ++fails; }

struct FakePlayHead : juce::AudioPlayHead {
    bool valid = true, playing = false; double bpm = 120.0, ppq0 = 0.0, sr = 48000.0; long long sample = 0, frozenAt = -1;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; if (! valid) return p;
        const long long s = frozenAt >= 0 ? frozenAt : sample;                 // stopped: the position stays where it stopped
        p.setBpm(bpm); p.setPpqPosition(ppq0 + (double) s * bpm / (60.0 * sr)); p.setIsPlaying(playing); p.setTimeInSamples((juce::int64) sample);
        return p;
    }
};

struct Ev { long long at; int ch, note, vel; bool on; bool operator==(const Ev& o) const { return at == o.at && ch == o.ch && note == o.note && vel == o.vel && on == o.on; } };

struct Host {
    std::unique_ptr<BushidoProcessor> p = std::make_unique<BushidoProcessor>();
    FakePlayHead ph; int hostBlock; long long t = 0;
    std::vector<Ev> ev; std::vector<long long> riseA, fallA, riseB, fallB; bool gA = false, gB = false; bool trackGates;
    float peak = 0.0f; bool outNaN = false;                                     // the plugin's audio out over every block
    Host(int hb = 512, int maxBlock = 512) : hostBlock(hb), trackGates(maxBlock >= hb)
    { p->setRateAndBufferSizeDetails(48000.0, maxBlock); p->prepareToPlay(48000.0, maxBlock); p->setPlayHead(&ph); }
    void set(const char* id, float v) { auto* a = p->parameter(id); a->setValueNotifyingHost(a->convertTo0to1(v)); }
    float get(const char* id) const { auto* a = p->parameter(id); return a->convertFrom0to1(a->getValue()); }
    void press(const char* id) { p->pressButton(id, true); p->pressButton(id, false); }
    int jack(const char* id) const { return rack::findJack(p->sq, id); }
    // one host block; per-sample callback sees the graph outputs (valid only when one sub-block covers the host block)
    void block(std::function<void(long long, int)> each = nullptr)
    {
        juce::AudioBuffer<float> buf(2, hostBlock); buf.clear(); juce::MidiBuffer midi;
        ph.sample = t; p->processBlock(buf, midi);
        for (int c = 0; c < buf.getNumChannels(); ++c) for (int i = 0; i < hostBlock; ++i) { const float x = buf.getSample(c, i); outNaN |= ! std::isfinite(x); peak = std::max(peak, std::abs(x)); }
        for (const auto m : midi) { const auto msg = m.getMessage();
            if (msg.isNoteOn()) ev.push_back({ t + m.samplePosition, msg.getChannel(), msg.getNoteNumber(), msg.getVelocity(), true });
            else if (msg.isNoteOff()) ev.push_back({ t + m.samplePosition, msg.getChannel(), msg.getNoteNumber(), 0, false }); }
        if (trackGates) {
            const float* ga = p->graph.output(0, BushidoModule::GATE_A); const float* gb = p->graph.output(0, BushidoModule::GATE_B);
            for (int i = 0; i < hostBlock; ++i) {
                const bool a = ga[i] > 1, b = gb[i] > 1;
                if (a != gA) (a ? riseA : fallA).push_back(t + i);
                if (b != gB) (b ? riseB : fallB).push_back(t + i);
                gA = a; gB = b;
                if (each) each(t + i, i);
            }
        }
        t += hostBlock;
    }
    void run(long long n) { while (t < n) block(); }
    std::vector<int> notesOn(size_t first = 1000) const { std::vector<int> v; for (auto& e : ev) if (e.on && v.size() < first) v.push_back(e.note); return v; }
};

static juce::MemoryBlock xmlState(const juce::String& xmlText)
{
    juce::MemoryBlock mb; auto xml = juce::parseXML(xmlText); juce::AudioProcessor::copyXmlToBinary(*xml, mb); return mb;
}

// ------------------------------------------------------------------ state: v1 load, migration, round-trip, read-only
static void testState()
{
    const auto v1 = xmlState("<BUSHIDO><PARAM id=\"CLOCK_SOURCE\" value=\"1\"/><PARAM id=\"A_1\" value=\"0.25\"/><PARAM id=\"CH_PORTA_A\" value=\"0.3\"/>"
                             "<CABLES><CABLE a=\"BUSHIDO/5:TRIG\" b=\"BUSHIDO/INPUTS:RESET\" color=\"1\"/>"
                             "<CABLE a=\"BUSHIDO/OUTPUTS:CV A\" b=\"RONIN/VCO:HZ/V\" color=\"2\"/></CABLES></BUSHIDO>");
    Host h; h.p->setStateInformation(v1.getData(), (int) v1.getSize());
    CHECK(h.get("CLOCK:SETTLE") == 1.0f && h.get("CLOCK:TRIG MODE") == 0.0f && h.get("CLOCK:EXT SOURCE") == 0.0f,
          "v1 state (no format): SETTLE = VINTAGE, TRIG MODE = STEP, EXT SOURCE = JACK");
    CHECK(h.get("CLOCK:SOURCE") == 1.0f && std::abs(h.get("A:1") - 0.25f) < 1e-6f && std::abs(h.get("CH:PORTA A") - 0.3f) < 1e-6f,
          "v1 state keeps its stored SOURCE (EXT), knobs and PORTA");
    const auto cs = h.p->getCables();
    CHECK(cs.size() == 2 && cs[0].a == "BUSHIDO/5:TRIG" && cs[0].b == "BUSHIDO/INPUTS:RESET" && cs[1].a == "BUSHIDO/OUTPUTS:CV A" && cs[1].b == "RONIN/VCO:HZ/V" && cs[0].age == 0 && cs[1].age == 1,
          "v1 cables load as stored, list order kept as age");
    const auto lines = h.p->migrationLines();
    CHECK(lines.size() == 4 && lines[0].contains("VINTAGE") && lines[2].contains("HZ/V LIN") && h.get("STEPS:LAW A") == 1.0f && h.get("STEPS:LAW B") == 0.0f && ! h.p->isReadOnly(),
          "migration lines filled (" + juce::String((int) lines.size()) + "); row A cabled to RONIN HZ/V -> LIN, row B -> V/OCT");

    juce::MemoryBlock saved; h.p->getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
    CHECK(xml != nullptr && xml->getIntAttribute("format", -1) == 1, "saving writes format = 1");
    Host r; r.p->setStateInformation(saved.getData(), (int) saved.getSize());
    bool same = true;
    for (auto& prm : r.p->sq.params()) if (prm.positions != -1) same &= r.p->parameter(prm.id)->getValue() == h.p->parameter(prm.id)->getValue();
    const auto rc = r.p->getCables();
    same &= rc.size() == cs.size(); for (size_t i = 0; same && i < rc.size(); ++i) same &= rc[i].a == cs[i].a && rc[i].b == cs[i].b && rc[i].color == cs[i].color && rc[i].age == cs[i].age;
    juce::MemoryBlock again; r.p->getStateInformation(again);
    CHECK(same && r.p->migrationLines().empty() && again == saved, "format 1 round-trips: every param and cable, no migration, identical bytes on re-save");

    const auto v2 = xmlState("<BUSHIDO format=\"2\" futureThing=\"x\"><PARAM id=\"CLOCK_SETTLE\" value=\"0\"/><PARAM id=\"A_1\" value=\"0.75\"/>"
                             "<CABLES><CABLE a=\"BUSHIDO/1:TRIG\" b=\"BUSHIDO/INPUTS:STEP\" color=\"0\" age=\"0\"/></CABLES><NEWSTUFF/></BUSHIDO>");
    Host f; f.p->setStateInformation(v2.getData(), (int) v2.getSize());
    juce::MemoryBlock back; f.p->getStateInformation(back);
    CHECK(f.p->isReadOnly() && back == v2 && f.p->migrationLines().size() == 1 && f.get("CLOCK:SETTLE") == 0.0f,
          "format 2: loaded read-only (SETTLE untouched), getStateInformation returns it byte-identical");
    f.p->loadPattern(0, 0); juce::MemoryBlock after; f.p->getStateInformation(after);
    auto ax = juce::AudioProcessor::getXmlFromBinary(after.getData(), (int) after.getSize());
    CHECK(! f.p->isReadOnly() && ax && ax->getIntAttribute("format") == 1, "loading a pattern ends read-only; the next save is format 1");
}

// ------------------------------------------------------------------ no prefix aliases: a retired prefix stays as stored and does not bind
static void testNoAliases()
{
    const juce::String old = juce::String("SQ") + "-10", oldRonin = juce::String("MS") + "-50";   // retired prefixes, built from parts
    const auto st = xmlState("<BUSHIDO><PARAM id=\"CLOCK_SOURCE\" value=\"0\"/>"
                             "<CABLES><CABLE a=\"" + old + "/5:TRIG\" b=\"" + old + "/INPUTS:RESET\" color=\"1\"/>"
                             "<CABLE a=\"" + old + "/OUTPUTS:CV A\" b=\"" + oldRonin + "/VCO:HZ/V\" color=\"2\"/></CABLES></BUSHIDO>");
    Host h; h.p->setStateInformation(st.getData(), (int) st.getSize());
    const auto cs = h.p->getCables();
    CHECK(cs.size() == 2 && cs[0].a == old + "/5:TRIG" && cs[0].b == old + "/INPUTS:RESET" && cs[1].b == oldRonin + "/VCO:HZ/V",
          "retired-prefix cables are kept exactly as stored (not rewritten to BUSHIDO/RONIN)");
    CHECK(h.get("STEPS:LAW A") == 0.0f, "a retired-prefix cable into HZ/V sets no law (row A stays V/OCT)");
    h.press("MODE:START/STOP"); int maxPos = -1;
    for (int b = 0; b < 600; ++b) { h.block(); maxPos = std::max(maxPos, h.p->sq.currentStep()); }
    CHECK(maxPos >= 5, "a retired-prefix TRIG 5 -> RESET cable resolves as missing: the sequencer runs past step 5 (max step " + juce::String(maxPos + 1) + ")");
    juce::MemoryBlock saved; h.p->getStateInformation(saved); Host r; r.p->setStateInformation(saved.getData(), (int) saved.getSize());
    CHECK(r.p->getCables().size() == 2 && r.p->getCables()[0].a == old + "/5:TRIG", "the missing cables survive a save and reload unchanged");
}

// ------------------------------------------------------------------ patterns
static void testPatterns()
{
    const auto v1 = xmlState("<BUSHIDO><PARAM id=\"CLOCK_SOURCE\" value=\"0\"/></BUSHIDO>");
    Host h; h.p->setStateInformation(v1.getData(), (int) v1.getSize());       // VINTAGE now
    h.set("STEPS:LAW A", 1.0f); h.set("MIDI:VEL A", 1.0f);
    h.p->loadPattern(0, 0);
    CHECK(h.get("CLOCK:SETTLE") == 0.0f && h.get("CLOCK:TRIG MODE") == 0.0f && h.get("STEPS:LAW A") == 0.0f && h.get("STEPS:LAW B") == 0.0f && h.get("MIDI:VEL A") == 0.0f
          && h.p->migrationLines().empty(), "loadPattern(0,0): factory INIT (format 1) gives TIGHT, STEP, V/OCT, whatever the panel held before");
    Host fresh;
    CHECK(fresh.get("CLOCK:SETTLE") == 0.0f && fresh.get("CLOCK:TRIG MODE") == 0.0f && fresh.get("STEPS:LAW A") == 0.0f && fresh.p->loadedBank() == 0 && fresh.p->loadedPattern() == 0,
          "a fresh instance opens on A001 INIT: TIGHT, STEP, V/OCT (not VINTAGE)");
    fresh.set("CLOCK:TRIG MODE", 1.0f); fresh.set("STEPS:LAW B", 1.0f);
    const int idx = fresh.p->savePattern(1, "MINE");
    const auto file = BushidoProcessor::userPatternFile(); const auto j = juce::JSON::parse(file);
    CHECK(idx == 0 && file.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory)) && (int) j["B"][0]["format"] == 1,
          "savePattern writes format 1 into the user file under the test's temp dir (" + file.getFullPathName() + ")");
    fresh.set("CLOCK:TRIG MODE", 0.0f); fresh.p->loadPattern(1, 0);
    CHECK(fresh.get("CLOCK:TRIG MODE") == 1.0f && fresh.get("STEPS:LAW B") == 1.0f, "a saved pattern carries the tab params back");
}

// ------------------------------------------------------------------ MIDI out on the INT clock
static std::vector<Ev> intRun(int hostBlock, int maxBlock, float law, const std::vector<float>& volts, long long n, Host** keep = nullptr)
{
    auto* h = new Host(hostBlock, maxBlock);
    h->set("MODE:MODE", 0.0f); h->set("STEPS:LAW A", law);
    for (int i = 0; i < 12; ++i) h->set((juce::String("A:") + juce::String(i + 1)).toRawUTF8(), volts[(size_t) i % volts.size()] / 5.0f);
    h->press("MODE:START/STOP"); h->run(n);
    auto ev = h->ev; if (keep) *keep = h; else delete h; return ev;
}

static void testMidi()
{
    Host* h = nullptr;
    const auto ev = intRun(512, 512, 0.0f, { 0.0f, 1.0f, 2.0f, 0.5f }, 12000 * 4 + 512, &h);
    CHECK(h->notesOn(4) == std::vector<int>({ 48, 60, 72, 54 }), "INT clock, V/OCT: 0/1/2/0.5 V -> MIDI 48/60/72/54 (48 + 12 V)");
    std::vector<long long> on, off; for (auto& e : ev) (e.on ? on : off).push_back(e.at);
    CHECK(on == h->riseA && off == std::vector<long long>(h->fallA.begin(), h->fallA.begin() + (long) std::min(off.size(), h->fallA.size())) && off.size() == h->fallA.size(),
          "note-on/off sample positions equal the GATE A rises/falls (" + juce::String((int) on.size()) + " on, first at " + juce::String(on.empty() ? -1 : on[0]) + ")");
    bool inBlock = true; for (auto& e : ev) inBlock &= e.ch == 1 && (e.on ? e.vel == 100 : true);
    CHECK(inBlock && ! on.empty() && on[0] % 512 != 0, "notes on channel 1, velocity 100; offsets land inside the block (first on at block offset " + juce::String(on.empty() ? -1 : (int) (on[0] % 512)) + ")");
    delete h;
    const auto lin = intRun(512, 512, 1.0f, { 0.5f, 1.0f, 2.0f, 4.0f }, 12000 * 4 + 512, &h);
    CHECK(h->notesOn(4) == std::vector<int>({ 36, 48, 60, 72 }), "INT clock, HZ/V LIN: 0.5/1/2/4 V -> MIDI 36/48/60/72 (48 + 12 log2 V)");
    delete h;
    for (int mb : { 128, 100, 16 }) {
        const auto sub = intRun(512, mb, 0.0f, { 0.0f, 1.0f, 2.0f, 0.5f }, 12000 * 4 + 512);
        CHECK(sub == ev, "512-sample host blocks run as " + juce::String(mb) + "-sample sub-blocks: identical notes at identical sample positions");
    }
    { const auto small = intRun(64, 512, 0.0f, { 0.0f, 1.0f, 2.0f, 0.5f }, 12000 * 4 + 512);
      CHECK(small == ev, "64-sample host blocks (smaller than maxBlock): identical notes at identical sample positions"); }
    { // PORTA must not change the note (target, not the glide)
        Host p; p.set("MODE:MODE", 0.0f); p.set("CH:PORTA A", 0.7f); const float vs[] = { 0.0f, 2.0f, 1.0f };
        for (int i = 0; i < 12; ++i) p.set((juce::String("A:") + juce::String(i + 1)).toRawUTF8(), vs[i % 3] / 5.0f);
        p.press("MODE:START/STOP"); p.run(12000 * 3 + 512);
        CHECK(p.notesOn(3) == std::vector<int>({ 48, 72, 60 }), "PORTA 0.7: notes still come from the target volts (48/72/60)");
    }
    { // ALT: row A on jack A (CH A), row B on jack B (CH B); VEL FROM C on A
        Host a(512, 512); a.set("MODE:MODE", 1.0f); a.set("CLOCK:TEMPO", 1.0f);         // 32 steps/s = 1500 samples per step
        for (int i = 1; i <= 12; ++i) { a.set((juce::String("A:") + juce::String(i)).toRawUTF8(), 0.2f); a.set((juce::String("B:") + juce::String(i)).toRawUTF8(), 0.4f);
                                       a.set((juce::String("C:") + juce::String(i)).toRawUTF8(), 0.5f); }
        a.set("MIDI:CH A", 2.0f / 15.0f); a.set("MIDI:CH B", 9.0f / 15.0f); a.set("MIDI:VEL A", 1.0f);
        a.press("MODE:START/STOP"); a.run(1500 * 25);
        int onA = 0, onB = 0; bool ok = true;
        for (auto& e : a.ev) { if (! e.on) continue; if (e.ch == 3) { ++onA; ok &= e.note == 60 && e.vel == 64; } else if (e.ch == 10) { ++onB; ok &= e.note == 72 && e.vel == 100; } else ok = false; }
        CHECK(ok && onA >= 12 && onB == 12, "MIDI CH A = 3 / CH B = 10 honoured; VEL FROM C = 64 (C = 2.5 V) on row A, 100 on row B (" + juce::String(onA) + " + " + juce::String(onB) + " notes)");
        a.set("CH:C MODE", 1.0f); a.ev.clear(); a.run(a.t + 1500 * 4);
        bool v100 = ! a.ev.empty(); for (auto& e : a.ev) if (e.on) v100 &= e.vel == 100;
        CHECK(v100, "VEL FROM C needs C MODE = CV: TIME mode sends velocity 100");
    }
}

// ------------------------------------------------------------------ bypass
static void testBypass()
{
    Host h; h.set("MODE:MODE", 0.0f); h.press("MODE:START/STOP");
    h.run(3072);                                                               // A1's gate (6000 samples) is held
    const bool held = ! h.ev.empty() && h.ev.back().on; const auto note = h.ev.empty() ? Ev{} : h.ev.back();
    const size_t before = h.ev.size();
    h.p->getBypassParameter()->setValueNotifyingHost(1.0f);
    h.block(); const std::vector<Ev> first(h.ev.begin() + (long) before, h.ev.end());
    CHECK(held && first.size() == 1 && ! first[0].on && first[0].note == note.note && first[0].ch == note.ch && first[0].at == 3072,
          "bypass on: one note-off for the held note, at the block's first sample");
    const size_t mid = h.ev.size(); h.run(h.t + 48000);
    CHECK(h.ev.size() == mid, "while bypassed: nothing more (no note-ons, no repeated note-offs) over 4 steps");
    h.p->getBypassParameter()->setValueNotifyingHost(0.0f); const size_t un = h.ev.size(); h.run(h.t + 24000);
    CHECK(h.ev.size() > un && h.ev[un].on, "bypass off: notes resume, starting with a note-on (no stray note-off)");
}

// ------------------------------------------------------------------ HOST sync
static void testHost()
{
    for (int mb : { 512, 128 }) {
        Host h(512, mb); h.set("CLOCK:SOURCE", 1.0f); h.set("CLOCK:EXT SOURCE", 1.0f); h.set("CLOCK:DIV", 0.5f); h.set("MODE:MODE", 0.0f);
        h.ph.playing = true; h.ph.bpm = 120.0; h.ph.ppq0 = 0.1;               // starts mid-step: the next 1/16 is at ppq 0.25 = sample 3600
        std::vector<long long> ticks; int last = -1;
        const int trig1 = h.jack("1:TRIG");
        auto onSample = [&](long long t, int i) { int s = -1; for (int k = 0; k < 12; ++k) if (h.p->graph.output(0, trig1 + k)[i] > 1) s = k; if (s >= 0 && s != last) ticks.push_back(t); last = s; };
        const long long stopBlock = 60416;                                     // GATE A is high here (tick 57600 + 1 .. 60601)
        while (h.t < stopBlock) h.block(onSample);
        bool even = ticks.size() >= 10 && ticks[0] == 0 && ticks[1] == 3600; for (size_t k = 2; k < ticks.size(); ++k) even &= ticks[k] - ticks[k - 1] == 6000;
        std::vector<long long> on; for (auto& e : h.ev) if (e.on) on.push_back(e.at);
        bool onOk = on.size() >= 10 && on[0] == 1; for (size_t k = 1; k < on.size(); ++k) onOk &= on[k] == 3600 + 6000 * (long long) (k - 1) + 1;
        if (mb == 512) CHECK(even, "HOST 120 BPM 1/16 from ppq 0.1: step 1 on the transport start, then ticks at 3600 and every 6000 samples (ppq-aligned)");
        CHECK(onOk, "HOST with " + juce::String(mb) + "-sample sub-blocks: note-ons at 1, 3601, then every 6000 (ppq advanced per sub-block)");
        const size_t n0 = h.ev.size(); h.ph.playing = false; h.ph.frozenAt = stopBlock;
        int hi = 0; h.block([&](long long, int i) { for (int k = 0; k < 12; ++k) hi += h.p->graph.output(0, trig1 + k)[i] > 1; hi += h.p->graph.output(0, BushidoModule::GATE_A)[i] > 1; });
        const std::vector<Ev> stopEv(h.ev.begin() + (long) n0, h.ev.end());
        if (mb == 512) CHECK(hi == 0 && ! h.p->sq.isRunning() && stopEv.size() == 1 && ! stopEv[0].on && stopEv[0].at == stopBlock,
                             "HOST transport stop: TRIGs and GATE low from the stop sample, the held note gets its note-off there");
        else CHECK(! h.p->sq.isRunning() && stopEv.size() == 1 && ! stopEv[0].on && stopEv[0].at == stopBlock, "HOST stop with sub-blocks: one note-off at the stop sample");
    }
}

// ------------------------------------------------------------------ factory bank (assets/bushido_patterns.json, embedded as binary data)
// Every factory pattern: loads, saves back equal to its entry, and runs for 8 s at 48 kHz with gates, MIDI notes, CVs inside the rails.
static void testFactory()
{
    const auto d = juce::JSON::parse(juce::String::fromUTF8(BinaryData::bushido_patterns_json, BinaryData::bushido_patterns_jsonSize));
    const auto* list = d["patterns"].getArray();
    Host first; const auto bankA = first.p->patternNames(0);
    const int n = list != nullptr ? list->size() : 0;
    bool order = n > 1 && bankA.size() >= n && (*list)[0]["name"].toString() == "INIT";
    for (int i = 0; order && i < n; ++i) order &= bankA[i] == (*list)[i]["name"].toString();
    CHECK(order, "factory bank: " + juce::String(n) + " patterns from the binary data, INIT first, in file order at the front of bank A");
    std::set<juce::String> ids; for (auto& prm : first.p->sq.params()) if (prm.positions != -1) ids.insert(juce::String(prm.id));
    const float rail = jidai::jcs::kRail;

    for (int i = 0; i < n; ++i) {
        const auto& fp = (*list)[i]; const juce::String name = fp["name"].toString(), tag = "factory " + juce::String(i + 1).paddedLeft('0', 3) + " " + name + ": ";
        auto* fpar = fp["params"].getDynamicObject(); const auto* fcab = fp["cables"].getArray();
        Host h; h.p->loadPattern(0, i);
        // 1. loads: format 1, every parameter listed, nothing migrated, every cable binds to a real jack
        std::set<juce::String> keys; bool vals = fpar != nullptr && fcab != nullptr && (int) fp["format"] == 1;
        if (fpar) for (auto& kv : fpar->getProperties()) { keys.insert(kv.name.toString()); auto* ap = h.p->parameter(kv.name.toString()); const bool eq = ap != nullptr && ap->getValue() == (float) kv.value; vals &= eq;
            if (! eq && ap) std::printf("  mismatch %s: pattern %.9g, parameter %.9g\n", kv.name.toString().toRawUTF8(), (double) (float) kv.value, (double) ap->getValue()); }
        const auto cs = h.p->getCables(); bool cab = fcab != nullptr && (int) cs.size() == fcab->size();
        for (size_t k = 0; cab && k < cs.size(); ++k)
            cab &= cs[k].a == "BUSHIDO/" + (*fcab)[(int) k][0].toString() && cs[k].b == "BUSHIDO/" + (*fcab)[(int) k][1].toString()
                   && h.jack(cs[k].a.fromFirstOccurrenceOf("/", false, false).toRawUTF8()) >= 0 && h.jack(cs[k].b.fromFirstOccurrenceOf("/", false, false).toRawUTF8()) >= 0;
        CHECK(vals && keys == ids && cab && h.p->migrationLines().empty() && ! h.p->isReadOnly() && h.p->loadedBank() == 0 && h.p->loadedPattern() == i,
              tag + "loads (format 1, all " + juce::String((int) ids.size()) + " parameters exact, " + juce::String((int) cs.size()) + " cables bound, no migration)");
        // 2. save -> reload: the saved entry equals the factory entry (name, format, params, cables), and loading it gives the same panel
        const int idx = h.p->savePattern(1, name);
        const auto uj = juce::JSON::parse(BushidoProcessor::userPatternFile()); const auto sv = uj["B"][idx];
        bool same = idx >= 0 && sv["name"].toString() == name && (int) sv["format"] == 1;
        if (auto* so = sv["params"].getDynamicObject()) { same &= so->getProperties().size() == (int) ids.size();
            for (auto& kv : so->getProperties()) same &= fpar->hasProperty(kv.name) && (float) kv.value == (float) fpar->getProperty(kv.name); } else same = false;
        same &= juce::JSON::toString(sv["cables"], true) == juce::JSON::toString(fp["cables"], true);
        std::map<juce::String, float> before; for (auto& id : ids) before[id] = h.p->parameter(id)->getValue();
        h.set("MODE:MODE", 0.0f); h.set("A:1", 0.123f); h.p->setCables({});
        h.p->loadPattern(1, idx);
        for (auto& id : ids) same &= h.p->parameter(id)->getValue() == before[id];
        const auto cs2 = h.p->getCables(); same &= cs2.size() == cs.size();
        for (size_t k = 0; same && k < cs.size(); ++k) same &= cs2[k].a == cs[k].a && cs2[k].b == cs[k].b && cs2[k].color == cs[k].color;
        CHECK(same, tag + "save -> reload round-trips (saved JSON equals the factory entry; reloading gives every parameter and cable back)");
        // 3. run 8 s at 48 kHz: INT patterns from START, HOST patterns from a playing transport at 120 BPM
        const bool hostClock = h.get("CLOCK:SOURCE") > 0.5f && h.get("CLOCK:EXT SOURCE") > 0.5f;
        h.p->loadPattern(0, i);
        if (hostClock) { h.ph.playing = true; h.ph.bpm = 120.0; h.ph.ppq0 = 0.0; } else h.press("MODE:START/STOP");
        bool railsOk = true; int maxStep = -1; std::set<int> steps;
        const int jA = BushidoModule::CV_A, jB = BushidoModule::CV_B, jC = BushidoModule::CV_C;
        while (h.t < 48000LL * 8) h.block([&](long long, int s) {
            for (int jk : { jA, jB, jC }) { const float v = h.p->graph.output(0, jk)[s]; railsOk &= std::isfinite(v) && v >= -rail && v <= rail; }
            const int st = h.p->sq.currentStep(); maxStep = std::max(maxStep, st); steps.insert(st); });
        const int chA = 1 + (int) std::lround(h.p->parameter("MIDI:CH A")->getValue() * 15.0f), chB = 1 + (int) std::lround(h.p->parameter("MIDI:CH B")->getValue() * 15.0f);
        const bool cv = h.get("CH:C MODE") < 0.5f;
        std::set<int> velA { 100 }, velB { 100 };
        auto fromC = [&](const char* vel, std::set<int>& out) { if (! cv || h.get(vel) < 0.5f) return; out.clear();
            for (int k = 1; k <= 12; ++k) { const float cvC = h.p->sq.getParam(BushidoModule::STEPS + 23 + k) * 5.0f; out.insert((int) std::clamp(std::lround(1.0 + 126.0 * (double) cvC / 5.0), 1L, 127L)); } };
        fromC("MIDI:VEL A", velA); fromC("MIDI:VEL B", velB);
        int onA = 0, onB = 0; bool midiOk = true;
        for (auto& e : h.ev) { midiOk &= e.note >= 0 && e.note <= 127 && (e.ch == chA || e.ch == chB); if (! e.on) continue;
            if (e.ch == chA) { ++onA; midiOk &= velA.count(e.vel) == 1; } else { ++onB; midiOk &= velB.count(e.vel) == 1; } }
        const size_t rises = h.riseA.size() + h.riseB.size(), falls = h.fallA.size() + h.fallB.size();
        CHECK(onA + onB > 0 && rises >= 8 && falls >= 8 && railsOk && midiOk && ! h.outNaN && h.peak <= 1.0f,
              tag + "plays: " + juce::String(onA + onB) + " note-ons (" + juce::String(onA) + " ch " + juce::String(chA) + ", " + juce::String(onB) + " ch " + juce::String(chB) + "), "
              + juce::String((int) rises) + " gate rises, CV A/B/C finite and within +/-" + juce::String(rail, 0) + " V, notes 0..127, velocities as set, audio out finite and <= 1");
        // 4. what the pattern is built to do
        juce::String what; bool ok = true;
        for (size_t k = 0; k < cs.size(); ++k) {
            const auto a = cs[k].a.fromFirstOccurrenceOf("/", false, false), b = cs[k].b.fromFirstOccurrenceOf("/", false, false);
            if (b == "INPUTS:RESET" && a.endsWith(":TRIG")) { const int len = a.getIntValue() - 1; bool all = true; for (int s2 = 0; s2 < len; ++s2) all &= steps.count(s2) == 1;
                ok &= maxStep == len - 1 && all; what << len << "-step loop (TRIG " << len + 1 << " -> RESET); "; }
            else if (b == "INPUTS:STEP" && a.endsWith(":TRIG")) what << "skips step " << a.getIntValue() << " (TRIG -> STEP); ";
            else if (b == "CLOCK:TEMPO CV") what << "row C bends the clock (CV C -> TEMPO CV); ";
        }
        bool rest = false; if (h.get("STEPS:LAW A") > 0.5f) for (int k = 0; k < 12; ++k) rest |= h.p->sq.getParam(BushidoModule::STEPS + k) <= 0.0f;
        if (rest) { ok &= onA < (int) h.riseA.size() && onA > 0; what << "LIN rests send no note (" << onA << " notes for " << (int) h.riseA.size() << " gates); "; }
        if (hostClock) {
            const int q = BushidoModule::stepsPerBeat(h.p->sq.getParam(BushidoModule::DIV)); const long long stepLen = 24000 / q, off = h.p->sq.settleSamples() - 1;
            bool grid = ! h.riseA.empty(); for (auto r : h.riseA) grid &= (r - off) % stepLen == 0 || (r - off) % stepLen == 1;   // +1: a step entered by a TRIG -> RESET cable
            ok &= grid; what << "HOST 120 BPM: every gate on the 1/" << q * 4 << " grid; "; }
        if (h.get("MODE:MODE") > 0.75f) { ok &= onA > 0 && onB > 0; what << "ALT plays both rows on their own channels; "; }
        if (what.isNotEmpty()) CHECK(ok, tag + what.trimCharactersAtEnd("; "));
    }
}

int main()
{
    juce::File home;
    {
        juce::ScopedJuceInitialiser_GUI juce;
        home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bushido_test_home_" + juce::String(juce::Time::currentTimeMillis()));
        home.createDirectory();
        BushidoProcessor::setUserPatternFile(home.getChildFile("user_patterns.json"));   // portable: no HOME or APPDATA games
        testState();
        testNoAliases();
        testPatterns();
        testMidi();
        testBypass();
        testHost();
        testFactory();
        home.deleteRecursively();
    }
    std::printf(fails ? "%d FAILED (%d passed)\n" : "ALL PASSED (%d)\n", fails ? fails : passes, passes);
    return fails ? 1 : 0;
}
