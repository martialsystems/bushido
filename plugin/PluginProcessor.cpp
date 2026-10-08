#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include <functional>

juce::String BushidoProcessor::paramIdFor(const std::string& id)
{
    juce::String s(id); juce::String out; for (auto ch : s) out << (juce::CharacterFunctions::isLetterOrDigit(ch) ? juce::String::charToString(ch) : juce::String("_"));
    return out;
}

juce::AudioProcessorValueTreeState::ParameterLayout BushidoProcessor::makeLayout(const BushidoModule& m)
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& p : m.params()) {
        if (p.positions == -1) continue;                                   // buttons are UI events, not automation
        const float step = p.positions >= 2 ? 1.0f / (float) (p.positions - 1) : 0.0f;
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { paramIdFor(p.id), 1 }, juce::String(p.id), juce::NormalisableRange<float>(0.0f, 1.0f, step), p.def));
    }
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "TOP_BYPASS", 1 }, "Bypass", false));   // not an engine param: the processor applies it
    return l;
}

BushidoProcessor::BushidoProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "BUSHIDO", makeLayout(sq))
{
    sqIndex = graph.addModule(&sq);
    for (auto& p : sq.params()) raw.push_back(p.positions == -1 ? nullptr : apvts.getRawParameterValue(paramIdFor(p.id)));
    lastSent.assign(raw.size(), -1.0f);
    bypass = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("TOP_BYPASS"));

    const auto d = juce::JSON::parse(juce::String::fromUTF8(BinaryData::bushido_patterns_json, BinaryData::bushido_patterns_jsonSize));
    if (auto* list = d["patterns"].getArray()) for (auto& pv : *list) banks[0].push_back(patternFromVar(pv));
    factoryCount = (int) banks[0].size();
    const auto u = juce::JSON::parse(userPatternFile());                 // patterns saved earlier, from any instance
    for (int b = 0; b < 2; ++b) if (auto* list = u[b == 0 ? "A" : "B"].getArray())
        for (auto& pv : *list) if ((int) banks[b].size() < kBankSize) banks[b].push_back(patternFromVar(pv));
    loadPattern(0, 0);                          // a fresh instance opens on A001; a saved session replaces it
}

static const char* kColourNames[] = { "red", "white", "yellow", "green" };

BushidoProcessor::Pattern BushidoProcessor::patternFromVar(const juce::var& pv)
{
    Pattern pat; pat.name = pv["name"].toString();
    pat.format = pv.hasProperty("format") ? (int) pv["format"] : 0;     // unversioned patterns are v1 (format 0) and migrate on load
    if (auto* o = pv["params"].getDynamicObject()) for (auto& kv : o->getProperties()) pat.params.push_back({ kv.name.toString(), (float) kv.value });
    auto bare = [](const juce::String& j) { return j.startsWith("BUSHIDO/") ? j.fromFirstOccurrenceOf("/", false, false) : j; };
    if (auto* cl = pv["cables"].getArray())
        for (auto& c : *cl) { int col = 0; for (int k = 0; k < 4; ++k) if (c[2].toString() == kColourNames[k]) col = k;
            pat.cables.push_back({ "BUSHIDO/" + bare(c[0].toString()), "BUSHIDO/" + bare(c[1].toString()), col, (int) pat.cables.size() }); }
    return pat;
}

juce::var BushidoProcessor::patternToVar(const Pattern& pat)
{
    auto* o = new juce::DynamicObject(); o->setProperty("name", pat.name); o->setProperty("format", pat.format);
    auto* params = new juce::DynamicObject(); for (auto& [id, v] : pat.params) params->setProperty(id, v);
    o->setProperty("params", juce::var(params));
    juce::Array<juce::var> cl;
    for (auto& c : pat.cables) cl.add(juce::var(juce::Array<juce::var> { c.a.fromFirstOccurrenceOf("/", false, false), c.b.fromFirstOccurrenceOf("/", false, false), kColourNames[juce::jlimit(0, 3, c.color)] }));
    o->setProperty("cables", cl);
    return juce::var(o);
}

static juce::File& userPatternFileOverride() { static juce::File f; return f; }
void BushidoProcessor::setUserPatternFile(const juce::File& f) { userPatternFileOverride() = f; }

juce::File BushidoProcessor::userPatternFile()
{
    if (userPatternFileOverride() != juce::File()) return userPatternFileOverride();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("BUSHIDO").getChildFile("user_patterns.json");
}

void BushidoProcessor::writeUserFile() const                                // bank A's factory patterns are not written
{
    auto* o = new juce::DynamicObject();
    for (int b = 0; b < 2; ++b) { juce::Array<juce::var> l; for (size_t i = b == 0 ? (size_t) factoryCount : 0; i < banks[b].size(); ++i) l.add(patternToVar(banks[b][i])); o->setProperty(b == 0 ? "A" : "B", l); }
    const auto f = userPatternFile(); f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

juce::StringArray BushidoProcessor::patternNames(int b) const
{
    const juce::ScopedLock sl(bankLock); juce::StringArray n;
    if (b == 0 || b == 1) for (auto& p : banks[b]) n.add(p.name);
    return n;
}

void BushidoProcessor::loadPattern(int b, int i)
{
    Pattern pat;
    { const juce::ScopedLock sl(bankLock); if ((b != 0 && b != 1) || ! juce::isPositiveAndBelow(i, (int) banks[b].size())) return; pat = banks[b][(size_t) i]; }
    std::map<std::string, float> values;                                  // every parameter is set: a missing one takes its default
    for (auto& p : sq.params()) if (p.positions != -1) values[p.id] = p.def;
    for (auto& [id, v] : pat.params) values[id.toStdString()] = v;
    migrateParams(pat.format, values, pat.cables);
    for (auto& [id, v] : values) if (auto* p = parameter(juce::String(id))) { p->beginChangeGesture(); p->setValueNotifyingHost(v); p->endChangeGesture(); }   // BYPASS is never in a pattern
    readOnly = false; readOnlyState.reset();
    curBank = b; curPattern = i;
    setCables(pat.cables);
    if (onStateLoaded) onStateLoaded();
}

int BushidoProcessor::savePattern(int b, const juce::String& name)
{
    if (b != 0 && b != 1) return -1;
    Pattern pat; pat.name = name.trim().isEmpty() ? juce::String("PATTERN") : name.trim(); pat.format = bushido::kFormat;
    for (auto& p : sq.params()) if (p.positions != -1) if (auto* ap = parameter(p.id)) pat.params.push_back({ juce::String(p.id), ap->getValue() });
    for (auto& c : cables) if (c.a.isNotEmpty() && c.b.isNotEmpty()) pat.cables.push_back(c);
    int index = -1;
    { const juce::ScopedLock sl(bankLock); if ((int) banks[b].size() >= kBankSize) return -1; banks[b].push_back(std::move(pat)); index = (int) banks[b].size() - 1; writeUserFile(); }
    curBank = b; curPattern = index;
    return index;
}

int BushidoProcessor::getNumPrograms() { const juce::ScopedLock sl(bankLock); return juce::jmax(1, (int) (banks[0].size() + banks[1].size())); }
int BushidoProcessor::getCurrentProgram() { const juce::ScopedLock sl(bankLock); return (curBank.load() == 1 ? (int) banks[0].size() : 0) + curPattern.load(); }
void BushidoProcessor::setCurrentProgram(int i)
{
    int b = 0; { const juce::ScopedLock sl(bankLock); if (i >= (int) banks[0].size()) { b = 1; i -= (int) banks[0].size(); } }
    loadPattern(b, i);
}
const juce::String BushidoProcessor::getProgramName(int i)
{
    const juce::ScopedLock sl(bankLock); const int b = i >= (int) banks[0].size() ? 1 : 0; if (b) i -= (int) banks[0].size();
    if (! juce::isPositiveAndBelow(i, (int) banks[b].size())) return {};
    return juce::String(b ? "B" : "A") + juce::String(i + 1).paddedLeft('0', 3) + " " + banks[b][(size_t) i].name;
}

bool BushidoProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void BushidoProcessor::prepareToPlay(double sr, int block)
{
    maxBlock = juce::jmax(16, block);
    graph.prepare(sr, maxBlock);
    hostL.assign((size_t) maxBlock, 0.0f); hostR.assign((size_t) maxBlock, 0.0f);
    graph.setNormal(sqIndex, BushidoModule::MIX_IN1, hostL.data()); graph.setNormal(sqIndex, BushidoModule::MIX_IN2, hostR.data());
    std::fill(lastSent.begin(), lastSent.end(), -1.0f);
    applyCables();
}

juce::RangedAudioParameter* BushidoProcessor::parameter(const juce::String& id) const { return apvts.getParameter(paramIdFor(id.toStdString())); }

void BushidoProcessor::pressButton(const juce::String& id, bool down) { const int i = rack::findParam(sq, id.toStdString()); if (i >= 0) sq.setParam(i, down ? 1.0f : 0.0f); }

float BushidoProcessor::indicator(const juce::String& id) const
{
    const auto& ind = sq.indicators(); for (size_t i = 0; i < ind.size(); ++i) if (ind[i].id == id.toStdString()) return sq.indicator((int) i);
    return 0.0f;
}

void BushidoProcessor::setCables(const std::vector<CableSpec>& c) { cables = c; applyCables(); }

void BushidoProcessor::applyCables()
{
    std::vector<rack::Cable> out;
    auto resolve = [this](const juce::String& gid, int& mod, int& jack) {
        const auto rackName = gid.upToFirstOccurrenceOf("/", false, false), jackId = gid.fromFirstOccurrenceOf("/", false, false);
        for (int m = 0; m < graph.moduleCount(); ++m)
            if (rackName == graph.module(m)->name()) { mod = m; jack = rack::findJack(*graph.module(m), jackId.toStdString()); return jack >= 0; }
        return false;
    };
    auto byAge = cables;                                                  // the graph wants cables oldest first (stack order is visual only)
    std::stable_sort(byAge.begin(), byAge.end(), [](const CableSpec& x, const CableSpec& y) { return x.age < y.age; });
    for (auto& c : byAge) { int ma, ja, mb, jb; if (resolve(c.a, ma, ja) && resolve(c.b, mb, jb)) out.push_back({ ma, ja, mb, jb }); }
    graph.setCables(out);
}

void BushidoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    for (size_t i = 0; i < raw.size(); ++i) if (raw[i]) { const float v = raw[i]->load(); if (std::not_equal_to<float>{}(v, lastSent[i])) { sq.setParam((int) i, v); lastSent[i] = v; } }   // exact change test (same as !=)
    rack::Transport t;                                                    // HOST clock (CLOCK tab): the DAW's play state, tempo and position
    if (auto* ph = getPlayHead()) if (const auto pos = ph->getPosition()) {
        t.valid = pos->getBpm().hasValue() && pos->getPpqPosition().hasValue();
        t.playing = pos->getIsPlaying();
        t.bpm = pos->getBpm().orFallback(120.0); t.ppq = pos->getPpqPosition().orFallback(0.0);
        t.samplePos = (long long) pos->getTimeInSamples().orFallback(0);
    }
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const int total = buffer.getNumSamples(), inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int o = 0; o < total; o += maxBlock) {
        const int n = juce::jmin(maxBlock, total - o);
        for (int i = 0; i < n; ++i) {                                     // host audio is normalled into MIXER IN 1 / IN 2 (1.0 = 5 V)
            hostL[(size_t) i] = inCh > 0 ? buffer.getSample(0, o + i) * 5.0f : 0.0f;
            hostR[(size_t) i] = inCh > 1 ? buffer.getSample(1, o + i) * 5.0f : hostL[(size_t) i];
        }
        rack::Transport ts = t;                                           // a sub-block starts o samples into the host block
        if (ts.playing) ts.ppq += (double) o * ts.bpm / (60.0 * sr);
        ts.samplePos += o;
        sq.setTransport(ts);
        const long long base = sq.samplesProcessed();
        graph.process(n);                                                 // keeps running while bypassed, so the lamps and clock carry on
        const int ne = sq.takeGateEvents(events, BushidoModule::kMaxEvents);
        auto emit = [&](const BushidoMidiOut::Msg& m) {
            const int at = o + juce::jlimit(0, n - 1, (int) (m.sample - base));
            midi.addEvent(m.on ? juce::MidiMessage::noteOn(m.channel, m.note, (juce::uint8) m.velocity) : juce::MidiMessage::noteOff(m.channel, m.note), at);
        };
        if (isBypassed()) {                                               // BYPASS: host audio passes through dry and no notes are sent
            for (int c = inCh; c < outCh; ++c) { if (inCh > 0) buffer.copyFrom(c, o, buffer, 0, o, n); else buffer.clear(c, o, n); }
            if (! wasBypassed) midiOut.allOff(base, emit);                // the held notes end where the bypass starts
            wasBypassed = true;
            continue;
        }
        wasBypassed = false;
        const float* mix = graph.output(sqIndex, BushidoModule::MIX_OUT);
        for (int c = 0; c < outCh; ++c) for (int i = 0; i < n; ++i) buffer.setSample(c, o + i, mix[i] * 0.2f);
        // MIDI out is a convenience, not the patch: a gate rise on CV/GATE A or B sends a note-on, its fall the note-off.
        // The note comes from the row's target volts under its PITCH LAW (C3 = 0 V = MIDI 48), on the MIDI tab's channel and velocity.
        midiOut.handle(sq, events, ne, emit);
    }
}

void BushidoProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (readOnly.load() && readOnlyState.getSize() > 0) { dest = readOnlyState; return; }   // a newer format is never saved over
    auto state = apvts.copyState();
    state.setProperty("format", bushido::kFormat, nullptr);
    juce::ValueTree cv("CABLES");
    for (auto& c : cables) cv.appendChild(juce::ValueTree("CABLE").setProperty("a", c.a, nullptr).setProperty("b", c.b, nullptr).setProperty("color", c.color, nullptr).setProperty("age", c.age, nullptr), nullptr);
    state.removeChild(state.getChildWithName("CABLES"), nullptr); state.appendChild(cv, nullptr);
    state.setProperty("bank", curBank.load(), nullptr); state.setProperty("pattern", curPattern.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void BushidoProcessor::migrateParams(int fromFormat, std::map<std::string, float>& params, const std::vector<CableSpec>& cs)
{
    std::vector<std::pair<std::string, std::string>> pairs;
    for (auto& c : cs) pairs.push_back({ c.a.toStdString(), c.b.toStdString() });
    const auto r = bushido::migrate(fromFormat, params, pairs);
    std::vector<juce::String> lines; for (auto& l : r.lines) lines.push_back(juce::String(l));
    { const juce::ScopedLock sl(bankLock); migration = std::move(lines); }
    for (int row = 0; row < 2; ++row) mismatch[row] = r.lawMismatch[row];
    readOnly = r.readOnly;
}

void BushidoProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size); if (! xml) return;
    auto state = juce::ValueTree::fromXml(*xml); if (! state.hasType(apvts.state.getType())) return;
    const int format = (int) state.getProperty("format", 0);             // unversioned states are v1 (format 0)
    std::vector<CableSpec> loaded;
    for (auto c : state.getChildWithName("CABLES"))                      // older states have no age: keep their list order; ids as stored
        loaded.push_back({ c["a"].toString(), c["b"].toString(), (int) c["color"], c.hasProperty("age") ? (int) c["age"] : (int) loaded.size() });
    state.removeChild(state.getChildWithName("CABLES"), nullptr);
    curBank = juce::jlimit(0, 1, (int) state.getProperty("bank", 0)); curPattern = juce::jmax(0, (int) state.getProperty("pattern", 0));
    apvts.replaceState(state);
    std::map<std::string, float> migrated;                                // only the parameters migration sets are written back
    migrateParams(format, migrated, loaded);
    for (auto& [id, v] : migrated) if (auto* p = parameter(juce::String(id))) p->setValueNotifyingHost(v);
    if (readOnly.load()) readOnlyState = juce::MemoryBlock(data, (size_t) size); else readOnlyState.reset();
    setCables(loaded);
    if (onStateLoaded) onStateLoaded();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BushidoProcessor(); }
