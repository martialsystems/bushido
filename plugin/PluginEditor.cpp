#include "PluginEditor.h"
#include "BinaryData.h"

// Four cable colours in the top bar; the selected one is used for new cables.
class Sq10Editor::Swatches : public juce::Component {
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

Sq10Editor::Sq10Editor(Sq10Processor& p) : AudioProcessorEditor(p), proc(p)
{
    auto layout = PanelLayout::fromJson(juce::String::fromUTF8(BinaryData::sq10_layout_json, BinaryData::sq10_layout_jsonSize));
    auto bg = juce::Drawable::createFromImageData(BinaryData::sq10_panel_bg_svg, BinaryData::sq10_panel_bg_svgSize);   // vector: sharp at any size
    panel = std::make_unique<RackPanel>(layout, std::move(bg), static_cast<RackPanel::Binding&>(*this));
    addAndMakeVisible(*panel);
    cables.setDesignSize(layout.width, layout.height);
    cables.addRack(panel.get(), 0.0f);
    cables.setPatch(proc.getCables());
    cables.onPatchChanged = [this](const std::vector<CableSpec>& c) { proc.setCables(c); };
    addAndMakeVisible(cables);
    swatches = std::make_unique<Swatches>(cables); addAndMakeVisible(*swatches);
    screen = std::make_unique<PatternScreen>(layout.screen, layout.width);       // on top of the cables, so its list covers them
    screen->names = [this](int bank) { return proc.patternNames(bank); };
    screen->loaded = [this] { return std::pair<int, int> { proc.loadedBank(), proc.loadedPattern() }; };
    screen->choose = [this](int bank, int i) { proc.loadPattern(bank, i); panel->repaint(); };
    screen->save = [this](int bank, const juce::String& name) { const int i = proc.savePattern(bank, name); if (i >= 0) proc.updateHostDisplay(); return i; };
    addAndMakeVisible(*screen);
    addMouseListener(&cables, true);            // cables see the pointer everywhere (hover push-away), not only over jacks
    proc.onStateLoaded = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<Sq10Editor>(this)] { if (sp) sp->cables.setPatch(sp->proc.getCables()); }); };

    setResizable(true, true);
    const double aspect = layout.width / layout.height;                          // 1600 x 434 design units
    setResizeLimits(1100, (int) (1100 / aspect), 1800, (int) (1800 / aspect));
    getConstrainer()->setFixedAspectRatio(aspect);
    setSize(1280, (int) std::round(1280 / aspect));
}

Sq10Editor::~Sq10Editor() { proc.onStateLoaded = nullptr; removeMouseListener(&cables); }

void Sq10Editor::resized()
{
    panel->setBounds(getLocalBounds()); cables.setBounds(getLocalBounds());
    const float s = getWidth() / 1600.0f;
    swatches->setBounds(juce::Rectangle<float>(1450 * s, 12 * s, 136 * s, 30 * s).toNearestInt());
    screen->placeIn(getLocalBounds());
}

juce::String Sq10Editor::readoutText(const juce::String&)
{
    if (! readoutEnabled({})) return "EXT";
    const double b = std::round(readoutValue({}) * 10.0) / 10.0;
    return std::abs(b - std::round(b)) < 0.05 ? juce::String((int) std::round(b)) : juce::String(b, 1);
}

float Sq10Editor::get(const juce::String& id) { if (auto* p = proc.parameter(id)) return p->getValue(); return 0.0f; }
void Sq10Editor::set(const juce::String& id, float v) { if (auto* p = proc.parameter(id)) p->setValueNotifyingHost(v); }
void Sq10Editor::gesture(const juce::String& id, bool begin) { if (auto* p = proc.parameter(id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); } }

juce::AudioProcessorEditor* Sq10Processor::createEditor() { return new Sq10Editor(*this); }
