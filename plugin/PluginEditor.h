#pragma once
#include "PluginProcessor.h"
#include "../ui/RackPanel.h"
#include "../ui/CableLayer.h"
#include "../ui/PatternScreen.h"
#include "../ui/BushidoTabs.h"

// MAIN is today's panel and editor, untouched, under a tab strip (BUSHIDO_Redesign §4.0): MAIN · STEPS · CLOCK · MIDI · SETUP.
// The strip is 36 design px above the 1600 x 434 art; a non-MAIN tab replaces the whole face at the same size.
class BushidoEditor : public juce::AudioProcessorEditor, private RackPanel::Binding, private bushido_ui::TabHost {
public:
    explicit BushidoEditor(BushidoProcessor&);
    ~BushidoEditor() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0c0c0d)); }
    CableLayer& cableLayer() { return cables; }
    RackPanel& rackPanel() { return *panel; }
    void showTab(int tab);                                   // bushido_ui::Tab
    int  currentTab() const { return strip.current(); }
    void setScalePercent(int percent) override;              // 75 / 100 / 125 / 150 / 200 % of 1280 wide
    int  scalePercent() override { return juce::roundToInt(getWidth() * 100.0 / 1280.0); }
    static int stripHeight(int width) { return juce::roundToInt(36.0 * width / 1600.0); }   // 29 px at 1280
    static int panelHeight(int width) { return juce::roundToInt(434.0 * width / 1600.0); }  // 347 px at 1280, as before

private:
    class Swatches;
    BushidoProcessor& proc;
    juce::Component mainFace;                    // the MAIN tab: panel, cables, swatches and pattern screen, exactly as before
    bushido_ui::TabStrip strip;
    std::unique_ptr<bushido_ui::TabPage> page;
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
    bool readoutEnabled(const juce::String&) override { return get("CLOCK:SOURCE") < 0.5f; }   // EXT ignores TEMPO (the LCD shows the measured or HOST tempo)
    double readoutValue(const juce::String&) override { return BushidoModule::bpm(get("CLOCK:TEMPO"), get("CLOCK:DIV")); }
    void setReadoutValue(const juce::String&, double bpm) override { set("CLOCK:TEMPO", BushidoModule::tempoForBpm(juce::jmax(0.1, bpm), get("CLOCK:DIV"))); }
    // bushido_ui::TabHost
    float value(const juce::String& id) override { return get(id); }
    void  setValue(const juce::String& id, float v) override { set(id, v); }
    void  edit(const juce::String& id, bool begin) override { gesture(id, begin); }
    float lamp(const juce::String& id) override { return proc.indicator(id); }
    double extPeriod() override { return proc.sq.measuredExtPeriod(); }
    double hostBpm() override { return proc.sq.hostBpm(); }
    int   settleSamples() override { return proc.sq.settleSamples(); }
    std::vector<juce::String> migrationLines() override { return proc.migrationLines(); }
    bool  lawMismatch(int row) override { return proc.lawMismatch(row); }
    bool  readOnly() override { return proc.isReadOnly(); }
};
