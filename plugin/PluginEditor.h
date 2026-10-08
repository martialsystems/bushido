#pragma once
#include "PluginProcessor.h"
#include "../ui/RackPanel.h"
#include "../ui/CableLayer.h"
#include "../ui/PatternScreen.h"

class BushidoEditor : public juce::AudioProcessorEditor, private RackPanel::Binding {
public:
    explicit BushidoEditor(BushidoProcessor&);
    ~BushidoEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override {}
    CableLayer& cableLayer() { return cables; }
    RackPanel& rackPanel() { return *panel; }

private:
    class Swatches;
    BushidoProcessor& proc;
    std::unique_ptr<RackPanel> panel;
    CableLayer cables;
    std::unique_ptr<Swatches> swatches;
    std::unique_ptr<PatternScreen> screen;
    float get(const juce::String& id) override;
    void set(const juce::String& id, float v) override;
    void gesture(const juce::String& id, bool begin) override;
    void press(const juce::String& id, bool down) override { proc.pressButton(id, down); }
    float indicator(const juce::String& id) override { return proc.indicator(id); }
    // the BPM readout: the TEMPO parameter shown as BPM at the DIV setting (see BushidoModule::bpm)
    juce::String readoutText(const juce::String& id) override;
    bool readoutEnabled(const juce::String&) override { return get("CLOCK:SOURCE") < 0.5f; }   // EXT ignores TEMPO
    double readoutValue(const juce::String&) override { return BushidoModule::bpm(get("CLOCK:TEMPO"), get("CLOCK:DIV")); }
    void setReadoutValue(const juce::String&, double bpm) override { set("CLOCK:TEMPO", BushidoModule::tempoForBpm(juce::jmax(0.1, bpm), get("CLOCK:DIV"))); }
};
