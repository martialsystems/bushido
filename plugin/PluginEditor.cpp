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
    auto bg = juce::ImageCache::getFromMemory(BinaryData::sq10_bg2x_png, BinaryData::sq10_bg2x_pngSize);
    panel = std::make_unique<RackPanel>(layout, bg, static_cast<RackPanel::Binding&>(*this));
    addAndMakeVisible(*panel);
    cables.setDesignSize(layout.width, layout.height);
    cables.addRack(panel.get(), 0.0f);
    cables.setPatch(proc.getCables());
    cables.onPatchChanged = [this](const std::vector<CableSpec>& c) { proc.setCables(c); };
    addAndMakeVisible(cables);
    swatches = std::make_unique<Swatches>(cables); addAndMakeVisible(*swatches);
    addMouseListener(&cables, true);            // cables see the pointer everywhere (hover push-away), not only over jacks
    proc.onStateLoaded = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<Sq10Editor>(this)] { if (sp) sp->cables.setPatch(sp->proc.getCables()); }); };

    setResizable(true, true);
    setResizeLimits(1100, 440, 1800, 720);
    getConstrainer()->setFixedAspectRatio(layout.width / layout.height);
    setSize(1280, 512);
}

Sq10Editor::~Sq10Editor() { proc.onStateLoaded = nullptr; removeMouseListener(&cables); }

void Sq10Editor::resized()
{
    panel->setBounds(getLocalBounds()); cables.setBounds(getLocalBounds());
    const float s = getWidth() / 1600.0f;
    swatches->setBounds(juce::Rectangle<float>(1450 * s, 12 * s, 136 * s, 30 * s).toNearestInt());
}

float Sq10Editor::get(const juce::String& id) { if (auto* p = proc.parameter(id)) return p->getValue(); return 0.0f; }
void Sq10Editor::set(const juce::String& id, float v) { if (auto* p = proc.parameter(id)) p->setValueNotifyingHost(v); }
void Sq10Editor::gesture(const juce::String& id, bool begin) { if (auto* p = proc.parameter(id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); } }

juce::AudioProcessorEditor* Sq10Processor::createEditor() { return new Sq10Editor(*this); }
