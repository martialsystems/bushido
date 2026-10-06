#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String Sq10Processor::paramIdFor(const std::string& id)
{
    juce::String s(id); juce::String out; for (auto ch : s) out << (juce::CharacterFunctions::isLetterOrDigit(ch) ? juce::String::charToString(ch) : juce::String("_"));
    return out;
}

juce::AudioProcessorValueTreeState::ParameterLayout Sq10Processor::makeLayout(const Sq10Module& m)
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& p : m.params()) {
        if (p.positions == -1) continue;                                   // buttons are UI events, not automation
        const float step = p.positions >= 2 ? 1.0f / (float) (p.positions - 1) : 0.0f;
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { paramIdFor(p.id), 1 }, juce::String(p.id), juce::NormalisableRange<float>(0.0f, 1.0f, step), p.def));
    }
    return l;
}

Sq10Processor::Sq10Processor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SQ10", makeLayout(sq))
{
    sqIndex = graph.addModule(&sq);
    for (auto& p : sq.params()) raw.push_back(p.positions == -1 ? nullptr : apvts.getRawParameterValue(paramIdFor(p.id)));
    lastSent.assign(raw.size(), -1.0f);
}

bool Sq10Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void Sq10Processor::prepareToPlay(double sr, int block)
{
    maxBlock = juce::jmax(16, block);
    graph.prepare(sr, maxBlock);
    hostL.assign((size_t) maxBlock, 0.0f); hostR.assign((size_t) maxBlock, 0.0f);
    graph.setNormal(sqIndex, Sq10Module::MIX_IN1, hostL.data()); graph.setNormal(sqIndex, Sq10Module::MIX_IN2, hostR.data());
    std::fill(lastSent.begin(), lastSent.end(), -1.0f);
    applyCables();
}

juce::RangedAudioParameter* Sq10Processor::parameter(const juce::String& id) const { return apvts.getParameter(paramIdFor(id.toStdString())); }

void Sq10Processor::pressButton(const juce::String& id, bool down) { const int i = rack::findParam(sq, id.toStdString()); if (i >= 0) sq.setParam(i, down ? 1.0f : 0.0f); }

float Sq10Processor::indicator(const juce::String& id) const
{
    const auto& ind = sq.indicators(); for (size_t i = 0; i < ind.size(); ++i) if (ind[i].id == id.toStdString()) return sq.indicator((int) i);
    return 0.0f;
}

void Sq10Processor::setCables(const std::vector<CableSpec>& c) { cables = c; applyCables(); }

void Sq10Processor::applyCables()
{
    std::vector<rack::Cable> out;
    auto resolve = [this](const juce::String& gid, int& mod, int& jack) {
        const auto rackName = gid.upToFirstOccurrenceOf("/", false, false), jackId = gid.fromFirstOccurrenceOf("/", false, false);
        for (int m = 0; m < graph.moduleCount(); ++m)
            if (rackName == graph.module(m)->name()) { mod = m; jack = rack::findJack(*graph.module(m), jackId.toStdString()); return jack >= 0; }
        return false;
    };
    for (auto& c : cables) { int ma, ja, mb, jb; if (resolve(c.a, ma, ja) && resolve(c.b, mb, jb)) out.push_back({ ma, ja, mb, jb }); }
    graph.setCables(out);
}

void Sq10Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    for (size_t i = 0; i < raw.size(); ++i) if (raw[i]) { const float v = raw[i]->load(); if (v != lastSent[i]) { sq.setParam((int) i, v); lastSent[i] = v; } }
    const int total = buffer.getNumSamples(), inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int o = 0; o < total; o += maxBlock) {
        const int n = juce::jmin(maxBlock, total - o);
        for (int i = 0; i < n; ++i) {                                     // host audio is normalled into MIXER IN 1 / IN 2 (1.0 = 5 V)
            hostL[(size_t) i] = inCh > 0 ? buffer.getSample(0, o + i) * 5.0f : 0.0f;
            hostR[(size_t) i] = inCh > 1 ? buffer.getSample(1, o + i) * 5.0f : hostL[(size_t) i];
        }
        graph.process(n);
        const float* mix = graph.output(sqIndex, Sq10Module::MIX_OUT);
        for (int c = 0; c < outCh; ++c) for (int i = 0; i < n; ++i) buffer.setSample(c, o + i, mix[i] * 0.2f);
        // MIDI out: channel A gates -> MIDI channel 1, B -> channel 2. Note = 36 + CV * 12 (treats the CV as volts per octave).
        for (int ch = 0; ch < 2; ++ch) {
            const float* g = graph.output(sqIndex, ch == 0 ? Sq10Module::GATE_A : Sq10Module::GATE_B);
            const float* cv = graph.output(sqIndex, ch == 0 ? Sq10Module::CV_A : Sq10Module::CV_B);
            for (int i = 0; i < n; ++i) {
                const bool gh = g[i] > 1.0f;
                if (gh && ! gatePrev[ch]) {
                    if (midiNote[ch] >= 0) midi.addEvent(juce::MidiMessage::noteOff(ch + 1, midiNote[ch]), o + i);
                    midiNote[ch] = juce::jlimit(0, 127, 36 + (int) std::lround(cv[i] * 12.0f));
                    midi.addEvent(juce::MidiMessage::noteOn(ch + 1, midiNote[ch], (juce::uint8) 100), o + i);
                } else if (! gh && gatePrev[ch] && midiNote[ch] >= 0) { midi.addEvent(juce::MidiMessage::noteOff(ch + 1, midiNote[ch]), o + i); midiNote[ch] = -1; }
                gatePrev[ch] = gh;
            }
        }
    }
}

void Sq10Processor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree cv("CABLES");
    for (auto& c : cables) cv.appendChild(juce::ValueTree("CABLE").setProperty("a", c.a, nullptr).setProperty("b", c.b, nullptr).setProperty("color", c.color, nullptr), nullptr);
    state.removeChild(state.getChildWithName("CABLES"), nullptr); state.appendChild(cv, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void Sq10Processor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size); if (! xml) return;
    auto state = juce::ValueTree::fromXml(*xml); if (! state.hasType(apvts.state.getType())) return;
    std::vector<CableSpec> loaded;
    for (auto c : state.getChildWithName("CABLES")) loaded.push_back({ c["a"].toString(), c["b"].toString(), (int) c["color"] });
    state.removeChild(state.getChildWithName("CABLES"), nullptr);
    apvts.replaceState(state);
    setCables(loaded);
    if (onStateLoaded) onStateLoaded();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Sq10Processor(); }
