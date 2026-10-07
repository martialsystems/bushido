#include "RackPanel.h"
#include "PatternScreen.h"

RackPanel::RackPanel(PanelLayout l, std::unique_ptr<juce::Drawable> b, Binding& bd) : lay(std::move(l)), bg(std::move(b)), bind(bd)
{
    setOpaque(true); startTimerHz(30);                 // LEDs and host automation
}

int RackPanel::controlAt(juce::Point<float> p) const
{
    for (size_t i = 0; i < lay.controls.size(); ++i) if (lay.controls[i].hit.contains(p)) return (int) i;
    return -1;
}

float RackPanel::snap(const PanelLayout::Control& c, float v) const
{
    v = juce::jlimit(0.0f, 1.0f, v);
    if (c.kind == "switch" && c.positions >= 2) { const float n = (float) (c.positions - 1); v = std::round(v * n) / n; }
    return v;
}

float RackPanel::angleFor(const PanelLayout::Control& c, float v) const
{
    if (c.kind == "switch" && c.positions >= 2 && ! c.angles.empty()) return c.angles[(size_t) juce::jlimit(0, (int) c.angles.size() - 1, (int) std::round(v * (float) (c.positions - 1)))];
    return -135.0f + 270.0f * v;
}

void RackPanel::drawKnob(juce::Graphics& g, float cx, float cy, float r, float ang)
{
    using C = juce::Colour;
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillEllipse(cx + 1 - (r + 4), cy + 2 - (r + 4), 2 * (r + 4), 2 * (r + 4));
    g.setColour(C(0xff08080a)); g.fillEllipse(cx - (r + 3), cy - (r + 3), 2 * (r + 3), 2 * (r + 3));
    g.setColour(C(0xff3c3c3f).withAlpha(0.75f));
    for (int i = 0; i < 48; ++i) { const float a = (float) i * juce::MathConstants<float>::twoPi / 48.0f;
        g.drawLine(cx + r * std::cos(a), cy + r * std::sin(a), cx + (r + 2.4f) * std::cos(a), cy + (r + 2.4f) * std::sin(a), 0.8f); }
    juce::ColourGradient body(C(0xff4b4b4e), cx - r, cy - r, C(0xff060607), cx + r, cy + r, false); body.addColour(0.5, C(0xff1a1a1b));
    g.setGradientFill(body); g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);
    const float t = r * 0.8f; juce::ColourGradient top(C(0xff2a2a2c), cx - t, cy - t, C(0xff131314), cx + t, cy + t, false);
    g.setGradientFill(top); g.fillEllipse(cx - t, cy - t, 2 * t, 2 * t);
    g.setColour(C(0xff050505)); g.drawEllipse(cx - t, cy - t, 2 * t, 2 * t, 0.8f);
    juce::Path hl; hl.addEllipse(-r * 0.45f, -r * 0.28f, r * 0.9f, r * 0.56f);
    hl.applyTransform(juce::AffineTransform::rotation(juce::degreesToRadians(-35.0f)).translated(cx - r * 0.28f, cy - r * 0.32f));
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.16f), cx - r * 0.28f, cy - r * 0.32f, juce::Colours::transparentWhite, cx - r * 0.28f + r * 0.45f, cy - r * 0.32f, true));
    g.fillPath(hl);
    const float a = juce::degreesToRadians(ang);
    g.setColour(C(0xfff1ede0)); g.drawLine(cx + std::sin(a) * r * 0.1f, cy - std::cos(a) * r * 0.1f, cx + std::sin(a) * (r - 1.5f), cy - std::cos(a) * (r - 1.5f), 2.4f);
}

void RackPanel::drawToggle(juce::Graphics& g, float cx, float cy, bool right)
{
    const float lx = cx + (right ? 12.0f : -12.0f), ly = cy - 2.0f;
    g.setColour(juce::Colour(0xffd8d8d2)); g.drawLine(cx, cy, lx, ly, 3.2f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe6e6e1), lx - 1.5f, ly - 1.5f, juce::Colour(0xff7d7d79), lx + 3.6f, ly + 3.6f, true));
    g.fillEllipse(lx - 3.6f, ly - 3.6f, 7.2f, 7.2f);
}

void RackPanel::drawRocker(juce::Graphics& g, juce::Rectangle<float> r, bool right, bool dark)
{
    const auto left = r.withWidth(r.getWidth() / 2), rightHalf = left.translated(left.getWidth(), 0);
    auto half = [&](juce::Rectangle<float> h, bool pressed) {
        if (dark) g.setColour(juce::Colour(pressed ? 0xff101012 : 0xff3a3a3e));          // the dark rocker (as on RONIN)
        else g.setGradientFill(pressed ? juce::ColourGradient(juce::Colour(0xff8e897a), 0, h.getY(), juce::Colour(0xffbdb8a6), 0, h.getBottom(), false)
                                       : juce::ColourGradient(juce::Colour(0xfffbf9f1), 0, h.getY(), juce::Colour(0xffdcd7c6), 0, h.getBottom(), false));
        g.fillRoundedRectangle(h, 2.5f);
        if (! pressed) { g.setColour(juce::Colour(dark ? 0xff77777c : 0xfffffdf4)); g.drawLine(h.getX() + 2, h.getY() + 1.2f, h.getRight() - 2, h.getY() + 1.2f, 1.0f); }
    };
    half(left, right); half(rightHalf, ! right);                       // the selected side is the raised, lit half
    g.setColour(juce::Colour(dark ? 0xff000000 : 0xff6b675a)); g.drawLine(r.getCentreX(), r.getY() + 1, r.getCentreX(), r.getBottom() - 1, 1.0f);
}

void RackPanel::drawKey(juce::Graphics& g, float cx, float cy, bool black, bool down)
{
    const float o = down ? 1.0f : 0.0f;
    const juce::Colour a(black ? 0xff4a4a4e : 0xfff4eedc), b(black ? 0xff0e0e10 : 0xffb3ab94), c(black ? 0xff36363a : 0xfff8f3e4), d(black ? 0xff161618 : 0xffd0c8b2);
    g.setGradientFill(juce::ColourGradient(down ? a.darker(0.15f) : a, cx - 13, cy - 13, b, cx + 13, cy + 13, false)); g.fillRoundedRectangle(cx - 13 + o, cy - 13 + o, 26, 26, 3);
    g.setColour(juce::Colour(black ? 0xff3a3a3e : 0xff6f6a5a)); g.drawRoundedRectangle(cx - 13 + o, cy - 13 + o, 26, 26, 3, 0.9f);
    g.setColour((black ? juce::Colours::black : juce::Colour(0xff7e7764)).withAlpha(0.55f)); g.fillRoundedRectangle(cx - 13 + o, cy + 9 + o, 26, 4, 2);
    g.setGradientFill(juce::ColourGradient(c, cx - 3, cy - 5, d, cx + 9, cy + 7, true)); g.fillRoundedRectangle(cx - 9.5f + o, cy - 10 + o, 19, 17, 2.5f);
}

void RackPanel::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()));
    // The SVG is drawn in design units (its viewBox is the layout canvas), so it scales with the editor and stays sharp.
    // Not drawWithin(): that fits the drawing's content bounds, and the wear layer reaches past the panel edge.
    g.reduceClipRegion(juce::Rectangle<float>(0, 0, lay.width, lay.height).toNearestInt());
    if (bg) bg->draw(g, 1.0f);
    for (size_t i = 0; i < lay.controls.size(); ++i) {
        const auto& c = lay.controls[i]; const float v = bind.get(c.id);
        if (c.kind == "readout") { PatternScreen::drawDots(g, c.lcd.reduced(2, 1), bind.readoutText(c.id).paddedLeft(' ', c.chars), c.chars, juce::Colour(0xff1e2419), 0.09f); continue; }
        if (c.kind == "button") drawKey(g, c.cx, c.cy, c.style == "black", (int) i == pressedIdx);
        else if (c.style == "rocker") drawRocker(g, c.rect, v > 0.5f, c.tone == "dark");
        else if (c.style == "toggle") drawToggle(g, c.cx, c.cy, v > 0.5f);
        else drawKnob(g, c.cx, c.cy, c.r, angleFor(c, v));
    }
    for (const auto& l : lay.leds) {
        const float v = bind.indicator(l.id);
        if (v > 0.5f) { g.setColour(juce::Colour(0x55ff3b2b)); g.fillEllipse(l.cx - l.r * 2.2f, l.cy - l.r * 2.2f, l.r * 4.4f, l.r * 4.4f); }
        g.setColour(v > 0.5f ? juce::Colour(0xffff4a36) : juce::Colour(0xff4a0c08)); g.fillEllipse(l.cx - l.r, l.cy - l.r, 2 * l.r, 2 * l.r);
    }
}

void RackPanel::mouseDown(const juce::MouseEvent& e)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i];
    if (c.kind == "button") { pressedIdx = i; bind.press(c.id, true); repaint(); return; }
    if (c.kind == "readout") {                         // drag the digits like a knob: 1 unit per 2 px, Shift = 0.1
        if (! bind.readoutEnabled(c.id)) return;
        dragIdx = i; dragStartY = e.position.y; dragStartR = std::round(bind.readoutValue(c.id) * 10.0) / 10.0; dragMoved = false; bind.gesture(c.param, true); return;
    }
    if (c.style == "rocker") {                         // press the left half for the first position, the right half for the second
        bind.gesture(c.id, true); bind.set(c.id, toDesign(e.position).x > c.cx ? 1.0f : 0.0f); bind.gesture(c.id, false); repaint(); return;
    }
    dragIdx = i; dragStartY = e.position.y; dragStartV = bind.get(c.id); dragMoved = false; bind.gesture(c.id, true);
}

void RackPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx]; const float dy = dragStartY - e.position.y;
    if (std::abs(dy) > 3) dragMoved = true;
    if (c.kind == "readout") { const double st = e.mods.isShiftDown() ? 0.1 : 1.0, v = dragStartR + dy * (e.mods.isShiftDown() ? 0.05 : 0.5);
        bind.setReadoutValue(c.id, std::round(v / st) * st); repaint(); return; }
    const float range = c.kind == "switch" ? 60.0f : (e.mods.isShiftDown() ? 1000.0f : 200.0f);   // screen pixels for full travel
    bind.set(c.id, snap(c, dragStartV + dy / range)); repaint();
}

void RackPanel::mouseUp(const juce::MouseEvent&)
{
    if (pressedIdx >= 0) { bind.press(lay.controls[(size_t) pressedIdx].id, false); pressedIdx = -1; repaint(); return; }
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx];
    if (c.kind == "readout") { bind.gesture(c.param, false); dragIdx = -1; repaint(); return; }
    if (c.kind == "switch" && ! dragMoved) { const float step = 1.0f / (float) (c.positions - 1); float v = bind.get(c.id) + step; if (v > 1.0f + 1e-4f) v = 0; bind.set(c.id, snap(c, v)); }
    bind.gesture(c.id, false); dragIdx = -1; repaint();
}

void RackPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i]; if (c.kind == "button") return;
    if (c.kind == "readout") {                         // double-click resets the parameter behind the readout
        for (auto& k : lay.controls) if (k.id == c.param) { bind.gesture(k.id, true); bind.set(k.id, k.def); bind.gesture(k.id, false); }
        repaint(); return;
    }
    bind.gesture(c.id, true); bind.set(c.id, c.def); bind.gesture(c.id, false); repaint();
}

void RackPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = controlAt(toDesign(e.position));
    if (i < 0 || lay.controls[(size_t) i].kind == "button") { Component::mouseWheelMove(e, w); return; }   // not a control that turns: a host's scroll view gets the wheel
    const auto& c = lay.controls[(size_t) i];
    if (c.kind == "readout") { if (! bind.readoutEnabled(c.id)) return; const double st = e.mods.isShiftDown() ? 0.1 : 1.0;
        bind.gesture(c.param, true); bind.setReadoutValue(c.id, std::round(bind.readoutValue(c.id) / st) * st + (w.deltaY > 0 ? st : -st)); bind.gesture(c.param, false); repaint(); return; }
    const float d = c.kind == "switch" ? (w.deltaY > 0 ? 1.0f : -1.0f) / (float) (c.positions - 1) : w.deltaY * (e.mods.isShiftDown() ? 0.05f : 0.25f);
    bind.gesture(c.id, true); bind.set(c.id, snap(c, bind.get(c.id) + d)); bind.gesture(c.id, false); repaint();
}
