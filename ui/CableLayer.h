#pragma once
// One overlay above every RackPanel in an editor. Owns the visible cables (rack::CableModel), draws them,
// and handles patching. Racks are stacked vertically in one design space, so cables can join different racks.
#include <juce_gui_basics/juce_gui_basics.h>
#include "RackPanel.h"
#include "../rack/CableModel.h"
#include <functional>

struct CableSpec { juce::String a, b; int color = 0; int age = 0; };   // a/b = global jack ids "RACK/SECTION:LABEL"; age: larger = patched later

class CableLayer : public juce::Component, private juce::Timer {
public:
    CableLayer();
    ~CableLayer() override;
    // Register racks top to bottom. yOffset is in design units (first rack = 0, a second rack underneath = 640, for example).
    void addRack(RackPanel* panel, float yOffset);
    void setDesignSize(float w, float h);
    void setPatch(const std::vector<CableSpec>& cables);       // e.g. from saved state
    std::vector<CableSpec> patch() const;
    std::function<void(const std::vector<CableSpec>&)> onPatchChanged;   // every structural change (patch, move, unplug, reorder)
    void setColour(int c) { colour = c; }
    int  getColour() const { return colour; }
    static juce::Colour cableColour(int c, int shade);      // shade 0 base, 1 dark, 2 light

    void paint(juce::Graphics&) override;
    bool hitTest(int x, int y) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void resized() override;

    rack::CableModel& model() { return cm; }                  // tests and tools
    int jackAtDesign(juce::Point<float> p) const;               // hit area at least kMinJackHitPx square on screen
    static constexpr float kMinJackHitPx = 16.0f;
    juce::Point<float> jackPosDesign(int j) const { return { jacks[(size_t) j].x, jacks[(size_t) j].y }; }
    int jackIndex(const juce::String& globalId) const { return ids.indexOf(globalId); }

private:
    struct Rack { RackPanel* panel; float y; };
    class Chooser;
    std::vector<Rack> racks; juce::StringArray ids, names; std::vector<rack::V2> jacks; std::vector<juce::Rectangle<float>> jackHits; std::vector<rack::RectF> labels;
    float dw = 1600, dh = 640; int colour = 0;
    rack::CableModel cm;
    struct Down { int jack = -1; juce::Point<float> pos; bool moved = false, shift = false; } down;
    bool clickCarry = false, swallowUp = false;
    std::unique_ptr<Chooser> chooser;

    float scale() const { return getWidth() / dw; }
    juce::Point<float> toDesign(juce::Point<float> p) const { return p / scale(); }
    void rebuildScene();
    void track(const juce::MouseEvent&);
    void commit();
    void openChooser(int jack);
    void timerCallback() override { cm.step(); repaint(); }
    friend class Chooser;
};
