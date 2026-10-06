#pragma once
// The PATTERN screen in the top bar, in the style of RONIN's PRESET screen: a green 5x7 dot-matrix LCD, a cream dropdown key,
// two bank lamps (A and B) and a cream SAVE key.
// - Click the screen or the key: the list of the lit bank opens under the screen and the LCD becomes a search box. Type to filter,
//   arrows or the wheel to move, click a row or Enter to load, Esc or a click elsewhere to close.
// - Click lamp A or B: that bank is the one the list shows and SAVE writes to.
// - Click SAVE: the LCD shows the next number in the lit bank; type a name, Enter (or SAVE again) saves, Esc cancels.
// While closed the component covers only the top-bar parts; while open it covers the whole editor, so an outside click closes it.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Layout.h"

class PatternScreen : public juce::Component, private juce::Timer {
public:
    PatternScreen(PanelLayout::Screen geometry, float designWidth);
    std::function<juce::StringArray(int bank)> names;      // pattern names of bank 0 (A) or 1 (B), in order
    std::function<std::pair<int, int>()> loaded;            // bank and index of the loaded pattern
    std::function<void(int bank, int index)> choose;        // load a pattern
    std::function<int(int bank, const juce::String&)> save; // save the panel as the next pattern of a bank; -1 when full

    void placeIn(juce::Rectangle<int> editorBounds);        // call from the editor's resized()
    bool isOpen() const { return mode != Mode::closed; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool hitTest(int x, int y) override;

    static void drawDots(juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, int chars, juce::Colour ink, float ghost);

private:
    enum class Mode { closed, search, name };
    PanelLayout::Screen scr; float dw; juce::Rectangle<int> editor;
    Mode mode = Mode::closed; int view = 0, hi = 0, top = 0; juce::String query, nameBuf; bool blink = true;
    std::pair<int, int> shown { -1, -1 }; juce::String message;
    static constexpr float kRow = 21, kPad = 5;
    float scale() const { return (float) editor.getWidth() / dw; }
    juce::Point<float> design(const juce::MouseEvent& e) const { return (e.position + getPosition().toFloat()) / scale(); }
    juce::Array<int> matches() const;
    juce::Rectangle<float> listBox() const;
    int rowAt(juce::Point<float>) const;                   // index into matches(), or -1
    int bankAt(juce::Point<float>) const;
    juce::String row(int bank, int i) const;
    juce::String lcdText() const;
    void setMode(Mode);
    void keepVisible();
    void doSave();
    void timerCallback() override;
};
