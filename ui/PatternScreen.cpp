#include "PatternScreen.h"

namespace {
struct Glyph { char c; juce::uint8 rows[7]; };
const Glyph kFont[] = {   // 5x7 dot-matrix, the MS-50 screen's character set
    {' ',{0,0,0,0,0,0,0}},{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{31,2,4,2,1,17,14}},
    {'4',{2,6,10,18,31,2,2}},{'5',{31,16,30,1,1,17,14}},{'6',{6,8,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,2,12}},{'A',{14,17,17,17,31,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{28,18,17,17,17,18,28}},
    {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},
    {'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},{'N',{17,17,25,21,19,17,17}},
    {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},
    {'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,17,10,4,4,4}},{'Z',{31,1,2,4,8,16,31}},{'&',{12,18,20,8,21,18,13}},{'-',{0,0,0,31,0,0,0}},{'+',{0,4,4,31,4,4,0}},
    {'/',{0,1,2,4,8,16,0}},{'.',{0,0,0,0,0,12,12}},{'>',{8,4,2,1,2,4,8}} };
const juce::uint8* glyph(juce::juce_wchar c) { for (auto& g : kFont) if (g.c == c) return g.rows; return kFont[0].rows; }
const juce::Colour kInk(0xff1e2419), kLit(0xffa6b192), kGold(0xffc29f4c);
juce::ColourGradient lcdFill(juce::Rectangle<float> r) {
    juce::ColourGradient cg(juce::Colour(0xff8f9a7c), 0, r.getY(), juce::Colour(0xff94a083), 0, r.getBottom(), false); cg.addColour(0.5, juce::Colour(0xffa6b192)); return cg; }
}

PatternScreen::PatternScreen(PanelLayout::Screen g, float designWidth) : scr(g), dw(designWidth)
{
    setWantsKeyboardFocus(true); startTimerHz(10);
}

void PatternScreen::drawDots(juce::Graphics& g, juce::Rectangle<float> a, const juce::String& text, int n, juce::Colour ink, float ghost)
{
    const float p = juce::jmin(a.getWidth() / (float) (n * 6), a.getHeight() / 8.0f), d = p * 0.86f;
    const float ox = a.getX() + (a.getWidth() - (float) n * 6 * p) / 2 + p * 0.5f, oy = a.getY() + (a.getHeight() - 7 * p) / 2;
    juce::RectangleList<float> lit, off;
    for (int c = 0; c < n; ++c) { const auto* f = glyph(c < text.length() ? text[c] : ' ');
        for (int r = 0; r < 7; ++r) for (int b = 0; b < 5; ++b) {
            const juce::Rectangle<float> dot(ox + (float) (c * 6 + b) * p, oy + (float) r * p, d, d);
            ((f[r] >> (4 - b)) & 1 ? lit : off).addWithoutMerging(dot); } }
    g.setColour(ink.withAlpha(ghost)); g.fillRectList(off);
    g.setColour(ink.withAlpha(0.9f)); g.fillRectList(lit);
}

juce::String PatternScreen::label(int i) const
{
    const auto n = names ? names() : juce::StringArray();
    if (! juce::isPositiveAndBelow(i, n.size())) return {};
    return (juce::String(i + 1).paddedLeft('0', 2) + " " + n[i]).toUpperCase().substring(0, scr.chars);
}

juce::Rectangle<float> PatternScreen::listBox() const
{
    const int n = names ? names().size() : 0;
    return { scr.bezel.getX(), scr.bezel.getBottom() + 3, scr.button.getRight() - scr.bezel.getX(), kPad * 2 + (float) n * kRow - 3 };
}

int PatternScreen::rowAt(juce::Point<float> p) const
{
    const auto b = listBox(); if (! b.contains(p)) return -1;
    const int i = (int) std::floor((p.y - b.getY() - kPad) / kRow), n = names ? names().size() : 0;
    return i >= 0 && i < n ? i : -1;
}

void PatternScreen::placeIn(juce::Rectangle<int> e)
{
    editor = e; const float s = scale();
    setBounds(open ? e : scr.bezel.getUnion(scr.button).transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void PatternScreen::setOpen(bool o)
{
    open = o; if (open) { hi = current ? current() : 0; grabKeyboardFocus(); }
    placeIn(editor); repaint();
}

bool PatternScreen::hitTest(int x, int y) { return open || getLocalBounds().contains(x, y); }

void PatternScreen::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()).translated((float) -getX(), (float) -getY()));   // design units -> this component
    shown = current ? current() : 0;
    drawDots(g, scr.lcd.reduced(2, 1), label(shown), scr.chars, kInk, 0.09f);         // the LCD glass is in the panel art
    if (! open) return;
    const auto b = listBox(); const int n = names ? names().size() : 0;
    g.setColour(juce::Colour(0xff0a0a0b)); g.fillRoundedRectangle(b, 4);
    g.setColour(kGold); g.drawRoundedRectangle(b, 4, 1.2f);
    for (int i = 0; i < n; ++i) {
        const juce::Rectangle<float> r(b.getX() + kPad, b.getY() + kPad + (float) i * kRow, b.getWidth() - kPad * 2, kRow - 3);
        const bool h = i == hi;
        if (h) g.setColour(kInk); else g.setGradientFill(lcdFill(r));
        g.fillRoundedRectangle(r, 1.5f);
        drawDots(g, r.reduced(3, 1), juce::String(i == shown ? ">" : " ") + label(i), scr.chars, h ? kLit : kInk, h ? 0.08f : 0.09f);
    }
}

void PatternScreen::mouseDown(const juce::MouseEvent& e)
{
    const auto p = (e.position + getPosition().toFloat()) / scale();
    if (! open) { setOpen(true); return; }
    const int r = rowAt(p);
    if (r >= 0 && choose) choose(r);
    setOpen(false);
}

void PatternScreen::mouseMove(const juce::MouseEvent& e)
{
    if (! open) { setMouseCursor(juce::MouseCursor::PointingHandCursor); return; }
    const int r = rowAt((e.position + getPosition().toFloat()) / scale());
    setMouseCursor(r >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (r >= 0 && r != hi) { hi = r; repaint(); }
}

bool PatternScreen::keyPressed(const juce::KeyPress& k)
{
    if (! open) return false;
    const int n = names ? names().size() : 0; if (n == 0) return false;
    if (k == juce::KeyPress::downKey) { hi = (hi + 1) % n; repaint(); return true; }
    if (k == juce::KeyPress::upKey)   { hi = (hi + n - 1) % n; repaint(); return true; }
    if (k == juce::KeyPress::returnKey) { if (choose) choose(hi); setOpen(false); return true; }
    if (k == juce::KeyPress::escapeKey) { setOpen(false); return true; }
    return false;
}
