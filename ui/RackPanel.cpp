#include "RackPanel.h"
#include "PatternScreen.h"
#include <cstring>

RackPanel::RackPanel(PanelLayout l, std::unique_ptr<juce::Drawable> b, Binding& bd) : lay(std::move(l)), bg(std::move(b)), bind(bd)
{
    setOpaque(true); startTimerHz(30);                 // LEDs and host automation (the editor stops it while the panel is hidden)
}

void RackPanel::setLive(bool live)
{
    if (live == isTimerRunning()) return;
    if (live) { startTimerHz(30); refresh(); } else stopTimer();
}

int RackPanel::controlIndex(const juce::String& id) const
{
    for (size_t i = 0; i < lay.controls.size(); ++i) if (lay.controls[i].id == id) return (int) i;
    return -1;
}

juce::Rectangle<int> RackPanel::controlArea(int i) const
{
    const auto& c = lay.controls[(size_t) i];
    juce::Rectangle<float> r;
    if (c.kind == "readout") r = c.lcd;
    else if (c.kind == "button") r = { c.cx - 14, c.cy - 14, 29, 29 };                       // key 26 x 26, 1 down when pressed
    else if (c.style == "rocker") r = c.rect;
    else if (c.style == "toggle") r = { c.cx - 17, c.cy - 7, 34, 13 };                       // lever to +-12, ball 3.6
    else r = { c.cx - c.r - 4, c.cy - c.r - 4, 2 * c.r + 9, 2 * c.r + 10 };                  // knob, ring, shadow (+1, +2)
    return r.expanded(2).transformedBy(juce::AffineTransform::scale(scale())).getSmallestIntegerContainer().expanded(1);
}

juce::Rectangle<int> RackPanel::ledArea(int i) const
{
    const auto& l = lay.leds[(size_t) i]; const float g = l.r * 2.2f;                         // the glow
    return juce::Rectangle<float>(l.cx - g, l.cy - g, 2 * g, 2 * g).expanded(1).transformedBy(juce::AffineTransform::scale(scale())).getSmallestIntegerContainer().expanded(1);
}

void RackPanel::invalidate(juce::Rectangle<int> a)
{
    a = a.getIntersection(getLocalBounds());
    if (a.isEmpty()) return;
    cacheDirty.add(a); invalidated.add(a); repaint(a);
}

void RackPanel::refresh()
{
    if (shownCtl.size() != lay.controls.size()) shownCtl.assign(lay.controls.size(), {});
    if (shownLed.size() != lay.leds.size()) shownLed.assign(lay.leds.size(), -1);
    for (size_t i = 0; i < lay.controls.size(); ++i) {
        const auto& c = lay.controls[i]; Shown now; now.known = true;
        if (c.kind == "readout") now.text = bind.readoutText(c.id);
        else if (c.kind == "button") now.pressed = (int) i == pressedIdx;
        else now.v = bind.get(c.id);
        auto& was = shownCtl[i];
        if (! was.known || ! juce::exactlyEqual(was.v, now.v) || was.pressed != now.pressed || was.text != now.text) { was = now; invalidate(controlArea((int) i)); }
    }
    for (size_t i = 0; i < lay.leds.size(); ++i) {
        const int on = bind.indicator(lay.leds[i].id) > 0.5f ? 1 : 0;
        if (on != shownLed[i]) { shownLed[i] = on; invalidate(ledArea((int) i)); }
    }
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
    refresh();                                         // a part that changed since the last tick is redrawn now (and repainted)
    // The image holds the face in device pixels. It is drawn with the same transform steps a direct paint gets (device
    // scale, then each parent's transform and origin, top-down), so each pixel is the one a direct paint makes, and
    // showing it is a 1:1 copy. A parent that rotates or shears paints directly.
    const float k = g.getInternalContext().getPhysicalPixelScaleFactor();
    std::vector<juce::Component*> chain;               // this and its parents up to (not including) the top level
    for (auto* c = static_cast<juce::Component*>(this); c->getParentComponent() != nullptr; c = c->getParentComponent()) chain.insert(chain.begin(), c);
    bool direct = ! useCache || ! (k > 0);
    CacheKey key { k, {}, getWidth(), getHeight(), {} };
    auto m = juce::AffineTransform::scale(k);          // panel point -> device pixel, as the paint calls compose it
    for (auto* c : chain) {
        if (c->isTransformed()) { const auto t = c->getTransform(); if (! juce::exactlyEqual(t.mat01, 0.0f) || ! juce::exactlyEqual(t.mat10, 0.0f)) direct = true;
                                  m = t.followedBy(m); key.xf.push_back(t); }
        else key.xf.push_back({});
        m = juce::AffineTransform::translation(c->getPosition().toFloat()).followedBy(m); key.chain.push_back(c->getPosition());
    }
    // The image starts at the top level's origin, so every part lands on the same device coordinates as in a direct
    // paint of the window (the rasteriser's rounding depends on them).
    const auto dev = getLocalBounds().toFloat().transformedBy(m).getSmallestIntegerContainer();
    if (dev.getX() < 0 || dev.getY() < 0) direct = true;
    // Two images: the art alone (drawn once per size), and the face (the art plus the parts). A changed part's area is
    // copied back from the art and its parts drawn again, so the art never has to be drawn under a small clip.
    if (direct) { drawFace(g, true, true); return; }
    auto inImage = [&](juce::Graphics& ig) {           // the same steps the parents' paints take
        if (! juce::exactlyEqual(k, 1.0f)) ig.addTransform(juce::AffineTransform::scale(k));
        for (size_t i = 0; i < chain.size(); ++i) { if (chain[i]->isTransformed()) ig.addTransform(key.xf[i]); ig.setOrigin(key.chain[i]); }
    };
    if (! (key == cacheKey) || ! cache.isValid() || ! art.isValid()) {
        cacheKey = key;
        art = juce::Image(juce::Image::ARGB, juce::jmax(1, dev.getRight()), juce::jmax(1, dev.getBottom()), true);
        { juce::Graphics ag(art); ag.reduceClipRegion(dev); inImage(ag); drawFace(ag, true, false); }   // the static art, once
        cache = juce::Image(juce::Image::ARGB, art.getWidth(), art.getHeight(), true);
        cacheDirty.clear(); cacheDirty.add(getLocalBounds());
    }
    if (! cacheDirty.isEmpty()) {
        juce::RectangleList<int> px;                   // whole device pixels: the art copied back, the parts drawn over it
        for (auto& r : cacheDirty) px.add(r.toFloat().transformedBy(m).getSmallestIntegerContainer().getIntersection(cache.getBounds()));
        {
            const juce::Image::BitmapData src(art, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dst(cache, juce::Image::BitmapData::writeOnly);
            for (auto& r : px) for (int y = r.getY(); y < r.getBottom(); ++y)
                std::memcpy(dst.getPixelPointer(r.getX(), y), src.getPixelPointer(r.getX(), y), (size_t) (r.getWidth() * src.pixelStride));
        }
        // One rectangle clip at a time (px holds no overlaps, so nothing is drawn twice). Direct2D (Windows) draws
        // under a many-rectangle clip through a layer, which rounds some pixels 1/255 away from a direct paint.
        for (auto& r : px) { juce::Graphics cg(cache); cg.reduceClipRegion(r); inImage(cg); drawFace(cg, false, true); }
        cacheDirty.clear();
    }
    g.drawImageTransformed(cache, m.inverted());
}

void RackPanel::drawFace(juce::Graphics& g, bool drawArt, bool drawParts)
{
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(scale()));
    // The SVG is drawn in design units (its viewBox is the layout canvas), so it scales with the editor and stays sharp.
    // Not drawWithin(): that fits the drawing's content bounds, and the wear layer reaches past the panel edge.
    g.reduceClipRegion(juce::Rectangle<float>(0, 0, lay.width, lay.height).toNearestInt());
    if (drawArt && bg) bg->draw(g, 1.0f);
    if (! drawParts) return;
    for (size_t i = 0; i < lay.controls.size(); ++i) {
        const auto& c = lay.controls[i]; const float v = bind.get(c.id);
        if (c.kind == "readout") {                     // a longer text ("EXT 118.4") gets smaller cells in the same LCD
            const auto t = bind.readoutText(c.id); const int n = juce::jmax(c.chars, t.length());
            PatternScreen::drawDots(g, c.lcd.reduced(2, 1), t.paddedLeft(' ', n), n, juce::Colour(0xff1e2419), 0.09f); continue; }
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

bool RackPanel::isListControl(int i) const
{
    if (i < 0 || i >= (int) lay.controls.size()) return false;
    const auto& c = lay.controls[(size_t) i];
    return c.kind == "switch" && c.positions >= 2;
}

listmenu::Choice RackPanel::listItems(int i) const
{
    listmenu::Choice ch; if (! isListControl(i)) return ch;
    const auto& c = lay.controls[(size_t) i];
    for (int k = 0; k < c.positions; ++k) ch.items.add(k < c.marks.size() ? c.marks[k] : juce::String(k + 1));
    ch.ticked = (int) std::lround(bind.get(c.id) * (float) (c.positions - 1));
    return ch;
}

void RackPanel::applyListChoice(int i, int index)
{
    if (! isListControl(i)) return;
    const auto& c = lay.controls[(size_t) i];
    if (index < 0 || index >= c.positions) return;
    bind.gesture(c.id, true); bind.set(c.id, (float) index / (float) (c.positions - 1)); bind.gesture(c.id, false); refresh();
}

void RackPanel::showList(int i)
{
    listmenu::show(listItems(i), *this, localAreaToGlobal(controlArea(i)), [this, i](int index) { applyListChoice(i, index); });
}

void RackPanel::mouseDown(const juce::MouseEvent& e)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i];
    if (e.mods.isPopupMenu() && isListControl(i)) { showList(i); return; }   // right-click: the whole list
    if (c.kind == "button") { pressedIdx = i; bind.press(c.id, true); refresh(); return; }
    if (c.kind == "readout") {                         // drag the digits like a knob: 1 unit per 2 px, Shift = 0.1
        if (! bind.readoutEnabled(c.id)) return;
        dragIdx = i; dragStartY = e.position.y; dragStartR = std::round(bind.readoutValue(c.id) * 10.0) / 10.0; dragMoved = false; bind.gesture(c.param, true); return;
    }
    if (c.style == "rocker") {                         // press the left half for the first position, the right half for the second
        bind.gesture(c.id, true); bind.set(c.id, toDesign(e.position).x > c.cx ? 1.0f : 0.0f); bind.gesture(c.id, false); refresh(); return;
    }
    dragIdx = i; dragStartY = e.position.y; dragStartV = bind.get(c.id); dragMoved = false; bind.gesture(c.id, true);
}

void RackPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx]; const float dy = dragStartY - e.position.y;
    if (std::abs(dy) > 3) dragMoved = true;
    if (c.kind == "readout") { const double st = e.mods.isShiftDown() ? 0.1 : 1.0, v = dragStartR + (double) dy * (e.mods.isShiftDown() ? 0.05 : 0.5);
        bind.setReadoutValue(c.id, std::round(v / st) * st); refresh(); return; }
    const float range = c.kind == "switch" ? 60.0f : (e.mods.isShiftDown() ? 1000.0f : 200.0f);   // screen pixels for full travel
    bind.set(c.id, snap(c, dragStartV + dy / range)); refresh();
}

void RackPanel::mouseUp(const juce::MouseEvent& e)
{
    if (pressedIdx >= 0) { bind.press(lay.controls[(size_t) pressedIdx].id, false); pressedIdx = -1; refresh(); return; }
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx];
    if (c.kind == "readout") { bind.gesture(c.param, false); dragIdx = -1; refresh(); return; }
    if (c.kind == "switch" && ! dragMoved) {           // click: the next position, Shift-click: the previous one (both wrap)
        const int n = c.positions, cur = (int) std::lround(bind.get(c.id) * (float) (n - 1));
        bind.set(c.id, snap(c, (float) listmenu::step(cur, n, e.mods.isShiftDown()) / (float) (n - 1)));
    }
    bind.gesture(c.id, false); dragIdx = -1; refresh();
}

void RackPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i]; if (c.kind == "button") return;
    if (c.kind == "readout") {                         // double-click resets the parameter behind the readout
        for (auto& k : lay.controls) if (k.id == c.param) { bind.gesture(k.id, true); bind.set(k.id, k.def); bind.gesture(k.id, false); }
        refresh(); return;
    }
    bind.gesture(c.id, true); bind.set(c.id, c.def); bind.gesture(c.id, false); refresh();
}

void RackPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = controlAt(toDesign(e.position));
    if (i < 0 || lay.controls[(size_t) i].kind == "button") { Component::mouseWheelMove(e, w); return; }   // not a control that turns: a host's scroll view gets the wheel
    const auto& c = lay.controls[(size_t) i];
    if (c.kind == "readout") { if (! bind.readoutEnabled(c.id)) return; const double st = e.mods.isShiftDown() ? 0.1 : 1.0;
        bind.gesture(c.param, true); bind.setReadoutValue(c.id, std::round(bind.readoutValue(c.id) / st) * st + (w.deltaY > 0 ? st : -st)); bind.gesture(c.param, false); refresh(); return; }
    const float d = c.kind == "switch" ? (w.deltaY > 0 ? 1.0f : -1.0f) / (float) (c.positions - 1) : w.deltaY * (e.mods.isShiftDown() ? 0.05f : 0.25f);
    bind.gesture(c.id, true); bind.set(c.id, snap(c, bind.get(c.id) + d)); bind.gesture(c.id, false); refresh();
}
