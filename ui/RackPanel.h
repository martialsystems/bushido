#pragma once
// Draws one rack: its background image plus live knobs, switches, buttons and LEDs, and handles turning them.
// Generic: any rack with a layout file works (SQ-10 now, MS-50 later).
#include <juce_gui_basics/juce_gui_basics.h>
#include "Layout.h"

class RackPanel : public juce::Component, private juce::Timer {
public:
    struct Binding {                                   // how the panel reaches the plugin's parameters
        virtual ~Binding() = default;
        virtual float get(const juce::String& id) = 0;                 // 0..1
        virtual void  set(const juce::String& id, float v) = 0;
        virtual void  gesture(const juce::String& id, bool begin) { juce::ignoreUnused(id, begin); }
        virtual void  press(const juce::String& id, bool down) = 0;     // momentary buttons
        virtual float indicator(const juce::String& id) = 0;            // LEDs, 0..1
    };
    RackPanel(PanelLayout layout, juce::Image background, Binding& binding);
    const PanelLayout& layout() const { return lay; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static void drawKnob(juce::Graphics&, float cx, float cy, float r, float angleDeg);

private:
    PanelLayout lay; juce::Image bg; Binding& bind;
    int dragIdx = -1; float dragStartY = 0, dragStartV = 0; bool dragMoved = false; int pressedIdx = -1;
    float scale() const { return getWidth() / lay.width; }
    juce::Point<float> toDesign(juce::Point<float> p) const { return p / scale(); }
    int controlAt(juce::Point<float> design) const;
    float angleFor(const PanelLayout::Control&, float v) const;
    float snap(const PanelLayout::Control&, float v) const;
    void timerCallback() override { repaint(); }
};
