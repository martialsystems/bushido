#pragma once
// Draws one rack: its vector background (SVG, so it stays sharp at any editor size) plus live knobs, switches,
// buttons and LEDs, and handles turning them.
// Generic: any rack with a layout file works.
// Repaint: the whole face (art and parts) is kept as one image in device pixels, built with the same transforms as a direct
// paint, so a paint is a copy. Each tick compares every part with what the image shows and redraws only the parts that
// changed (a knob, a lamp, a readout), in the image and on screen. The timer runs only while the panel is live (setLive).
#include <juce_gui_basics/juce_gui_basics.h>
#include "Layout.h"
#include "ListMenu.h"

class RackPanel : public juce::Component, private juce::Timer {
public:
    struct Binding {                                   // how the panel reaches the plugin's parameters
        virtual ~Binding() = default;
        virtual float get(const juce::String& id) = 0;                 // 0..1
        virtual void  set(const juce::String& id, float v) = 0;
        virtual void  gesture(const juce::String& id, bool begin) { juce::ignoreUnused(id, begin); }
        virtual void  press(const juce::String& id, bool down) = 0;     // momentary buttons
        virtual float indicator(const juce::String& id) = 0;            // LEDs, 0..1
        // readouts (kind "readout"): the text shown, the value dragged (in the readout's unit), and setting it
        virtual juce::String readoutText(const juce::String& id) { juce::ignoreUnused(id); return {}; }
        virtual bool   readoutEnabled(const juce::String& id) { juce::ignoreUnused(id); return true; }
        virtual double readoutValue(const juce::String& id) { juce::ignoreUnused(id); return 0.0; }
        virtual void   setReadoutValue(const juce::String& id, double v) { juce::ignoreUnused(id, v); }
    };
    RackPanel(PanelLayout layout, std::unique_ptr<juce::Drawable> background, Binding& binding);
    const PanelLayout& layout() const { return lay; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // List controls (switches, toggles, rockers; see ListMenu.h): click = next, Shift-click = previous, right-click = menu.
    bool isListControl(int control) const;
    listmenu::Choice listItems(int control) const;      // the positions as printed, the current one ticked
    void applyListChoice(int control, int index);       // one gesture
    int  controlIndex(const juce::String& id) const;

    // Repaint
    void setLive(bool live);                            // the editor is showing this panel: run the 30 Hz check, else stop it
    bool isLive() const { return isTimerRunning(); }
    void refresh();                                     // compare every part with the image and repaint what changed
    juce::Rectangle<int> controlArea(int control) const;   // what a part can touch, in component pixels
    juce::Rectangle<int> ledArea(int led) const;
    juce::RectangleList<int> takeInvalidated() { auto r = invalidated; invalidated.clear(); return r; }   // tests: what refresh marked
    void setImageCache(bool on) { useCache = on; repaint(); }   // tests: off paints the art and parts directly (the image keeps updating)

    static void drawKnob(juce::Graphics&, float cx, float cy, float r, float angleDeg);
    static void drawToggle(juce::Graphics&, float cx, float cy, bool right);    // lever only; the plate is in the background art
    static void drawRocker(juce::Graphics&, juce::Rectangle<float> r, bool right, bool dark = false);  // white (or dark) rocker; the selected side is the raised, lit half
    static void drawKey(juce::Graphics&, float cx, float cy, bool black, bool down);

private:
    PanelLayout lay; std::unique_ptr<juce::Drawable> bg; Binding& bind;
    int dragIdx = -1; float dragStartY = 0, dragStartV = 0; bool dragMoved = false; int pressedIdx = -1; double dragStartR = 0;
    float scale() const { return static_cast<float>(getWidth()) / lay.width; }
    juce::Point<float> toDesign(juce::Point<float> p) const { return p / scale(); }
    int controlAt(juce::Point<float> design) const;
    float angleFor(const PanelLayout::Control&, float v) const;
    float snap(const PanelLayout::Control&, float v) const;
    void timerCallback() override { refresh(); }

    // what the image shows, per part
    struct Shown { float v = -1.0f; bool pressed = false; juce::String text; bool known = false; };
    std::vector<Shown> shownCtl; std::vector<int> shownLed;
    // the face image, in the top-level component's device pixels (see paint)
    struct CacheKey { float k = 0; std::vector<juce::Point<int>> chain; int w = 0, h = 0; std::vector<juce::AffineTransform> xf;
                      bool operator==(const CacheKey& o) const { return juce::exactlyEqual(k, o.k) && chain == o.chain && w == o.w && h == o.h && xf == o.xf; } };
    juce::Image art, cache; CacheKey cacheKey; juce::RectangleList<int> cacheDirty, invalidated; bool useCache = true;
    void drawFace(juce::Graphics&, bool art, bool parts);   // the art and/or every part, as paint() drew them before the images
    void invalidate(juce::Rectangle<int> area);           // redraw this area in the image and on screen
    void invalidateAll() { invalidate(getLocalBounds()); }
    void showList(int control);
};
