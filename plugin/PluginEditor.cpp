#include "PluginEditor.h"
#include "BinaryData.h"

// Four cable colours in the top bar; the selected one is used for new cables.
class BushidoEditor::Swatches : public juce::Component {
public:
    explicit Swatches(CableLayer& c) : cl(c) {}
    void paint(juce::Graphics& g) override {
        const float d = getHeight() * 0.62f;
        for (int i = 0; i < 4; ++i) { auto r = cell(i).withSizeKeepingCentre(d, d);
            g.setColour(CableLayer::cableColour(i, 0)); g.fillEllipse(r);
            if (i == cl.getColour()) { g.setColour(juce::Colours::white); g.drawEllipse(r.expanded(2.5f), 1.5f); } }
    }
    void mouseDown(const juce::MouseEvent& e) override { for (int i = 0; i < 4; ++i) if (cell(i).contains(e.position)) { cl.setColour(i); repaint(); } }
private:
    CableLayer& cl;
    juce::Rectangle<float> cell(int i) const { const float w = getWidth() / 4.0f; return { w * (float) i, 0, w, (float) getHeight() }; }
};

BushidoEditor::BushidoEditor(BushidoProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    auto layout = PanelLayout::fromJson(juce::String::fromUTF8(BinaryData::bushido_layout_json, BinaryData::bushido_layout_jsonSize));
    auto bg = juce::Drawable::createFromImageData(BinaryData::bushido_panel_bg_svg, BinaryData::bushido_panel_bg_svgSize);   // vector: sharp at any size
    panel = std::make_unique<RackPanel>(layout, std::move(bg), static_cast<RackPanel::Binding&>(*this));
    mainFace.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(mainFace);
    mainFace.addAndMakeVisible(*panel);
    cables.setDesignSize(layout.width, layout.height);
    cables.addRack(panel.get(), 0.0f);
    cables.setPatch(proc.getCables());
    cables.onPatchChanged = [this](const std::vector<CableSpec>& c) { proc.setCables(c); };
    mainFace.addAndMakeVisible(cables);
    swatches = std::make_unique<Swatches>(cables); mainFace.addAndMakeVisible(*swatches);
    screen = std::make_unique<PatternScreen>(layout.screen, layout.width);       // on top of the cables, so its list covers them
    screen->names = [this](int bank) { return proc.patternNames(bank); };
    screen->loaded = [this] { return std::pair<int, int> { proc.loadedBank(), proc.loadedPattern() }; };
    screen->choose = [this](int bank, int i) { proc.loadPattern(bank, i); panel->repaint(); };
    screen->save = [this](int bank, const juce::String& name) { const int i = proc.savePattern(bank, name); if (i >= 0) proc.updateHostDisplay(); return i; };
    mainFace.addAndMakeVisible(*screen);
    mainFace.addMouseListener(&cables, true);   // cables see the pointer everywhere on the face (hover push-away), not only over jacks
    proc.onStateLoaded = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<BushidoEditor>(this)] { if (sp) { sp->cables.setPatch(sp->proc.getCables()); sp->strip.repaint(); sp->page->repaint(); } }); };

    page = std::make_unique<bushido_ui::TabPage>(static_cast<bushido_ui::TabHost&>(*this));
    addChildComponent(*page);
    strip.onSelect = [this](int t) { showTab(t); };
    strip.readOnly = [this] { return proc.isReadOnly(); };
    addAndMakeVisible(strip);

    // 1280 x 376 by default: the panel's 1280 x 347 plus the 29 px strip. 75..200 % = 960..2560 wide. The width is kept
    // in the plugin state (a property of the parameter tree), so a session reopens at its scale.
    setResizable(true, true);
    const double aspect = 1600.0 / (434.0 + 36.0);
    setResizeLimits(960, 960 * 470 / 1600, 2560, 2560 * 470 / 1600);
    getConstrainer()->setFixedAspectRatio(aspect);
    const int w = juce::jlimit(960, 2560, (int) proc.apvts.state.getProperty("uiWidth", 1280));
    setSize(w, stripHeight(w) + panelHeight(w));
}

BushidoEditor::~BushidoEditor() { proc.onStateLoaded = nullptr; mainFace.removeMouseListener(&cables); }

void BushidoEditor::resized()
{
    const int w = getWidth(), top = stripHeight(w);
    strip.setBounds(0, 0, w, top);
    // The face sits on a whole pixel under the strip and keeps the old editor's size, so the art renders exactly as before.
    const juce::Rectangle<int> face(0, top, w, panelHeight(w));
    mainFace.setBounds(face); page->setBounds(face);
    const auto local = mainFace.getLocalBounds();
    panel->setBounds(local); cables.setBounds(local);
    const float s = w / 1600.0f;
    swatches->setBounds(juce::Rectangle<float>(1450 * s, 12 * s, 136 * s, 30 * s).toNearestInt());
    screen->placeIn(local);
    if (w != (int) proc.apvts.state.getProperty("uiWidth", 1280)) proc.apvts.state.setProperty("uiWidth", w, nullptr);
}

void BushidoEditor::showTab(int t)
{
    t = juce::jlimit(0, bushido_ui::NUM_TABS - 1, t);
    strip.setTab(t);
    mainFace.setVisible(t == bushido_ui::MAIN);
    page->setTab(t); page->setVisible(t != bushido_ui::MAIN);
}

void BushidoEditor::setScalePercent(int percent)
{
    const int w = juce::jlimit(960, 2560, juce::roundToInt(1280.0 * percent / 100.0));
    setSize(w, stripHeight(w) + panelHeight(w));
}

static juce::String bpmText(double bpm)
{
    const double b = std::round(bpm * 10.0) / 10.0;
    return std::abs(b - std::round(b)) < 0.05 ? juce::String((int) std::round(b)) : juce::String(b, 1);
}

juce::String BushidoEditor::readoutText(const juce::String&)
{
    if (! readoutEnabled({})) {                    // SOURCE = EXT: content only, the layout never changes (§4.0)
        if (get("CLOCK:EXT SOURCE") > 0.5f) { const double hb = proc.sq.hostBpm(); return hb > 0 ? "HOST " + bpmText(hb) : juce::String("HOST"); }
        const double per = proc.sq.measuredExtPeriod();                  // seconds per step; the BPM is shown at the DIV setting
        return per > 0 ? "EXT " + bpmText(60.0 / (per * BushidoModule::stepsPerBeat(get("CLOCK:DIV")))) : juce::String("EXT");
    }
    return bpmText(readoutValue({}));
}

float BushidoEditor::get(const juce::String& id) { if (auto* p = proc.parameter(id)) return p->getValue(); return 0.0f; }
void BushidoEditor::set(const juce::String& id, float v) { if (auto* p = proc.parameter(id)) p->setValueNotifyingHost(v); }
void BushidoEditor::gesture(const juce::String& id, bool begin) { if (auto* p = proc.parameter(id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); } }

juce::AudioProcessorEditor* BushidoProcessor::createEditor() { return new BushidoEditor(*this); }
