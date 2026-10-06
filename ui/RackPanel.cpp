#include "RackPanel.h"

RackPanel::RackPanel(PanelLayout l, juce::Image b, Binding& bd) : lay(std::move(l)), bg(std::move(b)), bind(bd)
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

void RackPanel::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(bg, juce::Rectangle<float>(0, 0, lay.width, lay.height));
    for (size_t i = 0; i < lay.controls.size(); ++i) {
        const auto& c = lay.controls[i]; const float v = bind.get(c.id);
        if (c.kind == "button") {
            const bool down = (int) i == pressedIdx; const float o = down ? 1.0f : 0.0f;
            juce::ColourGradient cap(juce::Colour(down ? 0xffd8d1bd : 0xfff4eedc), c.cx - 12, c.cy - 12, juce::Colour(0xffa9a18a), c.cx + 12, c.cy + 12, false);
            g.setGradientFill(cap); g.fillRoundedRectangle(c.cx - 12 + o, c.cy - 12 + o, 24, 24, 3);
            g.setColour(juce::Colour(0xff6f6a5a)); g.drawRoundedRectangle(c.cx - 12 + o, c.cy - 12 + o, 24, 24, 3, 0.8f);
            g.setColour(juce::Colour(down ? 0xffe4ddc8 : 0xfff6f1e2)); g.fillRoundedRectangle(c.cx - 9 + o, c.cy - 10 + o, 18, 17, 2);
        } else drawKnob(g, c.cx, c.cy, c.r, angleFor(c, v));
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
    dragIdx = i; dragStartY = e.position.y; dragStartV = bind.get(c.id); dragMoved = false; bind.gesture(c.id, true);
}

void RackPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx]; const float dy = dragStartY - e.position.y;
    if (std::abs(dy) > 3) dragMoved = true;
    const float range = c.kind == "switch" ? 60.0f : (e.mods.isShiftDown() ? 1000.0f : 200.0f);   // screen pixels for full travel
    bind.set(c.id, snap(c, dragStartV + dy / range)); repaint();
}

void RackPanel::mouseUp(const juce::MouseEvent&)
{
    if (pressedIdx >= 0) { bind.press(lay.controls[(size_t) pressedIdx].id, false); pressedIdx = -1; repaint(); return; }
    if (dragIdx < 0) return;
    const auto& c = lay.controls[(size_t) dragIdx];
    if (c.kind == "switch" && ! dragMoved) { const float step = 1.0f / (float) (c.positions - 1); float v = bind.get(c.id) + step; if (v > 1.0f + 1e-4f) v = 0; bind.set(c.id, snap(c, v)); }
    bind.gesture(c.id, false); dragIdx = -1; repaint();
}

void RackPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i]; if (c.kind == "button") return;
    bind.gesture(c.id, true); bind.set(c.id, c.def); bind.gesture(c.id, false); repaint();
}

void RackPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = controlAt(toDesign(e.position)); if (i < 0) return;
    const auto& c = lay.controls[(size_t) i]; if (c.kind == "button") return;
    const float d = c.kind == "switch" ? (w.deltaY > 0 ? 1.0f : -1.0f) / (float) (c.positions - 1) : w.deltaY * (e.mods.isShiftDown() ? 0.05f : 0.25f);
    bind.gesture(c.id, true); bind.set(c.id, snap(c, bind.get(c.id) + d)); bind.gesture(c.id, false); repaint();
}
