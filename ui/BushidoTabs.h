#pragma once
// BUSHIDO's full-face tabs (docs/REFERENCE.md, Tab controls). The front panel art is never touched:
// - TabStrip is a separate 36-design-px strip above the art (29 px at the 1280 default): MAIN · STEPS · CLOCK · MIDI · SETUP.
// - TabPage replaces the whole face (same size as the panel) for every tab but MAIN. Each page is a set of equal,
//   gold-ruled blocks, mirrored left to right, in the panel's own language (plate, gold rules, cream labels, LCD green).
// Pages draw in design units (1600 x 434, like the panel) and scale with the editor.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace bushido_ui {

enum Tab { MAIN = 0, STEPS, CLOCK, MIDI, SETUP, NUM_TABS };

// How the pages reach the plugin. Parameter ids are the module ids ("STEPS:LAW A"); values are normalised 0..1.
struct TabHost {
    virtual ~TabHost() = default;
    virtual float value(const juce::String& id) = 0;
    virtual void  setValue(const juce::String& id, float v) = 0;
    virtual void  edit(const juce::String& id, bool begin) = 0;        // begin / end a change gesture
    virtual float lamp(const juce::String& id) = 0;                    // panel indicators, 0..1
    virtual double extPeriod() = 0;                                    // measured EXT period in seconds per step, 0 = none yet
    virtual double hostBpm() = 0;                                      // DAW tempo, 0 = none
    virtual int   settleSamples() = 0;                                 // the CV-to-gate settle at the current rate
    virtual std::vector<juce::String> migrationLines() = 0;
    virtual bool  lawMismatch(int row) = 0;
    virtual bool  readOnly() = 0;
    virtual int   scalePercent() = 0;
    virtual void  setScalePercent(int percent) = 0;
};

class TabStrip : public juce::Component {
public:
    static constexpr float kDesignW = 1600.0f, kDesignH = 36.0f;
    std::function<void(int)> onSelect;
    std::function<bool()> readOnly;
    void setTab(int t) { if (t != tab) { tab = t; repaint(); } }
    int  current() const { return tab; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    static juce::Rectangle<float> tabRect(int i) { return { 484.0f + 128.0f * (float) i, 6.0f, 120.0f, 26.0f }; }   // 5 x 120 + 4 x 8, centred
private:
    int tab = MAIN;
};

class TabPage : public juce::Component, private juce::Timer {
public:
    static constexpr float kDesignW = 1600.0f, kDesignH = 434.0f;
    explicit TabPage(TabHost& host);
    ~TabPage() override;
    void setTab(int t);
    int  current() const { return tab; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // shared drawing (design units)
    static void plate(juce::Graphics&);
    static void block(juce::Graphics&, juce::Rectangle<float> r, const juce::String& title);
    static void lcd(juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, int chars = 0);   // bezel, glass, 5x7 dot matrix
    static void text(juce::Graphics&, const juce::String& s, float x, float baseline, float size, juce::Colour c,
                     bool bold = true, float spacing = 0.8f, juce::Justification j = juce::Justification::centredTop);

    // Parse a typed step value: "2.5", "2.5V" (volts) or a note "C4", "F#3", "Bb2" under a pitch law (0 = V/OCT, 1 = HZ/V LIN).
    // Returns volts, or NaN when the text is neither.
    static double parseStep(const juce::String& s, int law);

private:
    struct Hit {
        juce::Rectangle<float> r;
        std::function<void(juce::Point<float>)> click;   // design point
        juce::String dragId;                             // a continuous parameter dragged vertically (200 px full travel, Shift = fine)
        std::function<void(int)> wheel;                  // +1 / -1
        std::function<void()> dbl;                       // double-click
    };
    TabHost& host;
    int tab = STEPS;
    std::vector<Hit> hits;                               // rebuilt by every paint, in design units
    int dragHit = -1; float dragY = 0, dragV = 0;
    std::unique_ptr<juce::TextEditor> typing;
    static std::vector<float> clipboard;                 // COPY / PASTE, one row of 12 knob values

    float scale() const { return (float) getWidth() / kDesignW; }
    juce::Point<float> design(juce::Point<float> p) const { return p / scale(); }
    int hitAt(juce::Point<float> design) const;
    void setParam(const juce::String& id, float v);      // one gesture
    void segmented(juce::Graphics&, juce::Rectangle<float> r, const juce::StringArray& labels, const juce::String& id, int disabledSegment = -1);
    void button(juce::Graphics&, juce::Rectangle<float> r, const juce::String& label, std::function<void()> action, bool selected = false);
    void typeInto(juce::Rectangle<float> cell, const juce::String& current, std::function<void(const juce::String&)> done);

    void paintSteps(juce::Graphics&);
    void paintClock(juce::Graphics&);
    void paintMidi(juce::Graphics&);
    void paintSetup(juce::Graphics&);
    void rowBlock(juce::Graphics&, int row, juce::Rectangle<float> r);
    void timerCallback() override { repaint(); }       // lamps, the measured tempo, the monitor
};

} // namespace bushido_ui
