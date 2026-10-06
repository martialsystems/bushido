#pragma once
#include "PluginProcessor.h"
#include "../ui/RackPanel.h"
#include "../ui/CableLayer.h"
#include "../ui/PatternScreen.h"

class Sq10Editor : public juce::AudioProcessorEditor, private RackPanel::Binding {
public:
    explicit Sq10Editor(Sq10Processor&);
    ~Sq10Editor() override;
    void resized() override;
    void paint(juce::Graphics&) override {}
    CableLayer& cableLayer() { return cables; }
    RackPanel& rackPanel() { return *panel; }

private:
    class Swatches;
    Sq10Processor& proc;
    std::unique_ptr<RackPanel> panel;
    CableLayer cables;
    std::unique_ptr<Swatches> swatches;
    std::unique_ptr<PatternScreen> screen;
    float get(const juce::String& id) override;
    void set(const juce::String& id, float v) override;
    void gesture(const juce::String& id, bool begin) override;
    void press(const juce::String& id, bool down) override { proc.pressButton(id, down); }
    float indicator(const juce::String& id) override { return proc.indicator(id); }
};
