#pragma once
// The PATTERN screen in the top bar: a green 5x7 dot-matrix LCD with a cream dropdown key, the same look as the
// MS-50 PRESET screen. Clicking the screen or the key opens a list of LCD rows under it; click a row (or use the
// arrow keys and Enter) to load that pattern, click anywhere else or press Esc to close it.
// While closed the component covers only the screen; while open it covers the whole editor, so an outside click closes it.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Layout.h"

class PatternScreen : public juce::Component, private juce::Timer {
public:
    PatternScreen(PanelLayout::Screen geometry, float designWidth);
    std::function<juce::StringArray()> names;      // pattern names, in order
    std::function<int()> current;                  // index shown on the screen
    std::function<void(int)> choose;               // load a pattern

    void placeIn(juce::Rectangle<int> editorBounds);   // call from the editor's resized()
    bool isOpen() const { return open; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool hitTest(int x, int y) override;

    static void drawDots(juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, int chars, juce::Colour ink, float ghost);

private:
    PanelLayout::Screen scr; float dw; juce::Rectangle<int> editor;
    bool open = false; int hi = 0, shown = -1;
    static constexpr float kRow = 21, kPad = 5;
    float scale() const { return (float) editor.getWidth() / dw; }
    juce::Rectangle<float> listBox() const;
    int rowAt(juce::Point<float> design) const;
    juce::String label(int i) const;
    void setOpen(bool);
    void timerCallback() override { if (current && current() != shown) repaint(); }   // a host program change shows up on the screen
};
