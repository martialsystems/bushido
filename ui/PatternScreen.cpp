#include "PatternScreen.h"

namespace {
struct Glyph { char c; juce::uint8 rows[7]; };
const Glyph kFont[] = {   // 5x7 dot-matrix, the Jidai Collection screen character set
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

PatternScreen::PatternScreen(PanelLayout::Screen g, float designWidth) : scr(std::move(g)), dw(designWidth)
{
    setWantsKeyboardFocus(true); startTimerHz(4);
}

void PatternScreen::timerCallback()
{
    if (mode != Mode::closed) { blink = ! blink; repaint(); }
    else if (loaded && loaded() != shown) repaint();               // a host program change shows up on the screen
}

void PatternScreen::drawDots(juce::Graphics& g, juce::Rectangle<float> a, const juce::String& text, int n, juce::Colour ink, float ghost)
{
    // When a dot is under 4 device pixels, the gap between dots only reads as speckle (each dot lands on a different
    // fraction of a pixel): draw the dots touching, as solid strokes, and start the text on a whole device pixel.
    const float k = g.getInternalContext().getPhysicalPixelScaleFactor();             // device pixels per design unit
    auto snapped = [k](float v) { return k > 0 ? std::round(v * k) / k : v; };
    const float p = juce::jmin(a.getWidth() / (float) (n * 6), a.getHeight() / 8.0f);
    const bool solid = p * k < 4.0f;
    const float d = solid ? p : snapped(p * 0.86f);
    const float ox = snapped(a.getX() + (a.getWidth() - (float) n * 6 * p) / 2 + p * 0.5f), oy = snapped(a.getY() + (a.getHeight() - 7 * p) / 2);
    if (solid) ghost = 0;                                                            // unlit cells would merge into a grey block
    juce::RectangleList<float> lit, off;
    for (int c = 0; c < n; ++c) { const auto* f = glyph(c < text.length() ? text[c] : ' ');
        for (int r = 0; r < 7; ++r) for (int b = 0; b < 5; ++b) {
            const juce::Rectangle<float> dot(ox + (float) (c * 6 + b) * p, oy + (float) r * p, d, d);
            ((f[r] >> (4 - b)) & 1 ? lit : off).addWithoutMerging(dot); } }
    g.setColour(ink.withAlpha(ghost)); g.fillRectList(off);
    g.setColour(ink.withAlpha(0.9f)); g.fillRectList(lit);
}

static juce::String pad3(int n) { return juce::String(n).paddedLeft('0', 3); }

juce::String PatternScreen::row(int bank, int i) const
{
    const auto n = names ? names(bank) : juce::StringArray();
    if (! juce::isPositiveAndBelow(i, n.size())) return {};
    return (pad3(i + 1) + " " + n[i]).toUpperCase().substring(0, scr.chars - 1);
}

juce::Array<int> PatternScreen::matches() const
{
    const auto n = names ? names(view) : juce::StringArray(); const auto q = query.trim(); juce::Array<int> m;
    for (int i = 0; i < n.size(); ++i) if (q.isEmpty() || (pad3(i + 1) + " " + n[i]).toUpperCase().contains(q)) m.add(i);
    return m;
}

juce::String PatternScreen::lcdText() const
{
    const juce::String cur(blink ? "_" : " ");
    if (mode == Mode::search) return ("FIND " + query + cur).getLastCharacters(scr.chars);
    if (mode == Mode::name) return (juce::String(view ? "B" : "A") + pad3((names ? names(view).size() : 0) + 1) + " " + nameBuf + cur).substring(0, scr.chars);
    if (message.isNotEmpty()) return message;
    const auto l = loaded ? loaded() : std::pair<int, int> { 0, 0 };
    const auto r = row(l.first, l.second);
    return r.isEmpty() ? juce::String() : (juce::String(l.first ? "B" : "A") + r).substring(0, scr.chars);
}

juce::Rectangle<float> PatternScreen::listBox() const
{
    const int n = juce::jlimit(1, scr.listRows, matches().size());
    return { scr.bezel.getX(), scr.bezel.getBottom() + 3, scr.button.getRight() - scr.bezel.getX(), kPad * 2 + (float) n * kRow - 3 };
}

int PatternScreen::rowAt(juce::Point<float> p) const
{
    if (mode != Mode::search) return -1;
    const auto b = listBox(); if (! b.contains(p)) return -1;
    const int i = (int) std::floor((p.y - b.getY() - kPad) / kRow);
    return i >= 0 && i < scr.listRows && top + i < matches().size() ? top + i : -1;
}

int PatternScreen::bankAt(juce::Point<float> p) const
{
    for (size_t i = 0; i < scr.banks.size(); ++i) if (scr.banks[i].hit.contains(p)) return (int) i;
    return -1;
}

void PatternScreen::placeIn(juce::Rectangle<int> e)
{
    editor = e; const float s = scale();
    auto closedArea = scr.bezel.getUnion(scr.button).getUnion(scr.save);
    for (auto& b : scr.banks) closedArea = closedArea.getUnion(b.hit);
    setBounds(isOpen() ? e : closedArea.transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void PatternScreen::keepVisible() { if (hi < top) top = hi; if (hi >= top + scr.listRows) top = hi - scr.listRows + 1; }

void PatternScreen::setMode(Mode m)
{
    mode = m; blink = true; message.clear();
    if (m == Mode::search) { query.clear(); const auto l = loaded ? loaded() : std::pair<int, int> { 0, 0 };
        hi = l.first == view ? juce::jmax(0, matches().indexOf(l.second)) : 0; top = 0; keepVisible(); }
    if (m == Mode::name) nameBuf.clear();
    if (m != Mode::closed) grabKeyboardFocus();
    placeIn(editor); repaint();
}

void PatternScreen::doSave()
{
    const int i = save ? save(view, nameBuf.trim().isEmpty() ? juce::String("PATTERN") : nameBuf.trim()) : -1;
    setMode(Mode::closed);
    if (i < 0) message = juce::String("BANK ") + (view ? "B" : "A") + " IS FULL";
}

bool PatternScreen::hitTest(int x, int y) { return isOpen() || getLocalBounds().contains(x, y); }

void PatternScreen::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()).translated((float) -getX(), (float) -getY()));   // design units -> this component
    shown = loaded ? loaded() : std::pair<int, int> { 0, 0 };
    drawDots(g, scr.lcd.reduced(2, 1), lcdText(), scr.chars, kInk, 0.09f);            // the LCD glass is in the panel art
    for (size_t i = 0; i < scr.banks.size(); ++i) {                                   // the lit lamp is the bank being browsed and saved to
        const auto& b = scr.banks[i]; const bool on = (int) i == view;
        if (on) { g.setColour(juce::Colour(0x55ff3b2b)); g.fillEllipse(b.cx - b.r * 2.2f, b.cy - b.r * 2.2f, b.r * 4.4f, b.r * 4.4f); }
        g.setColour(on ? juce::Colour(0xffff4a36) : juce::Colour(0xff4a0c08)); g.fillEllipse(b.cx - b.r, b.cy - b.r, 2 * b.r, 2 * b.r);
    }
    if (mode != Mode::search) return;
    const auto b = listBox(); const auto m = matches(); const auto l = shown;
    g.setColour(juce::Colour(0xff0a0a0b)); g.fillRoundedRectangle(b, 4);
    g.setColour(kGold); g.drawRoundedRectangle(b, 4, 1.2f);
    auto drawRow = [&](int k, const juce::String& txt, bool h) {
        const juce::Rectangle<float> r(b.getX() + kPad, b.getY() + kPad + (float) k * kRow, b.getWidth() - kPad * 2, kRow - 3);
        if (h) g.setColour(kInk); else g.setGradientFill(lcdFill(r));
        g.fillRoundedRectangle(r, 1.5f);
        drawDots(g, r.reduced(3, 1), txt, scr.chars, h ? kLit : kInk, h ? 0.08f : 0.09f);
    };
    if (m.isEmpty()) drawRow(0, (names && names(view).size() > 0) ? juce::String(" NO MATCH") : juce::String(" BANK ") + (view ? "B" : "A") + " IS EMPTY", false);
    for (int k = 0; k < scr.listRows && top + k < m.size(); ++k) { const int i = m[top + k];
        drawRow(k, juce::String(l.first == view && l.second == i ? ">" : " ") + row(view, i), top + k == hi); }
    if (m.size() > scr.listRows) {                                                    // scroll position
        const float h = b.getHeight() - 8;
        g.setColour(kGold.withAlpha(0.7f)); g.fillRoundedRectangle(b.getRight() - 4, b.getY() + 4 + h * (float) top / (float) m.size(), 2, h * (float) scr.listRows / (float) m.size(), 1);
    }
}

void PatternScreen::mouseDown(const juce::MouseEvent& e)
{
    const auto p = design(e);
    if (const int k = bankAt(p); k >= 0) { view = k; message.clear(); if (mode == Mode::search) { query.clear(); hi = top = 0; } repaint(); return; }
    if (mode == Mode::search) { const int r = rowAt(p); if (r >= 0 && choose) choose(view, matches()[r]); setMode(Mode::closed); return; }
    if (mode == Mode::name) { if (scr.save.contains(p)) doSave(); else setMode(Mode::closed); return; }
    if (scr.bezel.contains(p) || scr.button.contains(p)) { setMode(Mode::search); return; }
    if (scr.save.contains(p)) {
        if (names && names(view).size() >= scr.bankSize) { message = juce::String("BANK ") + (view ? "B" : "A") + " IS FULL"; repaint(); return; }
        setMode(Mode::name);
    }
}

void PatternScreen::mouseMove(const juce::MouseEvent& e)
{
    const auto p = design(e);
    if (mode != Mode::search) { setMouseCursor(juce::MouseCursor::PointingHandCursor); return; }
    const int r = rowAt(p);
    setMouseCursor(r >= 0 || bankAt(p) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (r >= 0 && r != hi) { hi = r; repaint(); }
}

void PatternScreen::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! isOpen()) { Component::mouseWheelMove(e, w); return; }          // closed: the wheel belongs to whatever holds the panel
    if (rowAt(design(e)) < 0) return;                                      // open: the list keeps every wheel, even off its rows
    top = juce::jlimit(0, juce::jmax(0, matches().size() - scr.listRows), top + (w.deltaY > 0 ? -1 : 1)); repaint();
}

bool PatternScreen::keyPressed(const juce::KeyPress& k)
{
    if (mode == Mode::closed) return false;
    if (k == juce::KeyPress::escapeKey) { setMode(Mode::closed); return true; }
    const auto c = juce::CharacterFunctions::toUpperCase(k.getTextCharacter());
    const bool printable = c >= ' ' && glyph(c) != glyph(' ') ? true : c == ' ';
    blink = true;
    if (mode == Mode::name) {
        if (k == juce::KeyPress::returnKey) { doSave(); return true; }
        if (k == juce::KeyPress::backspaceKey) nameBuf = nameBuf.dropLastCharacters(1);
        else if (printable && nameBuf.length() < scr.chars - 5 && ! (c == ' ' && nameBuf.isEmpty())) nameBuf += juce::String::charToString(c);
        repaint(); return true;
    }
    const auto m = matches();
    if (k == juce::KeyPress::downKey) { if (! m.isEmpty()) { hi = (hi + 1) % m.size(); keepVisible(); } }
    else if (k == juce::KeyPress::upKey) { if (! m.isEmpty()) { hi = (hi + m.size() - 1) % m.size(); keepVisible(); } }
    else if (k == juce::KeyPress::returnKey) { if (! m.isEmpty() && choose) choose(view, m[hi]); setMode(Mode::closed); return true; }
    else if (k == juce::KeyPress::backspaceKey) { query = query.dropLastCharacters(1); hi = top = 0; }
    else if (printable && query.length() < scr.chars - 6) { query += juce::String::charToString(c); hi = top = 0; }
    repaint(); return true;
}
