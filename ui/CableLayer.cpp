#include "CableLayer.h"

juce::Colour CableLayer::cableColour(int c, int shade)
{
    static const juce::uint32 t[4][3] = { { 0xffd23a30, 0xff6e100c, 0xffff9a8a }, { 0xffece8da, 0xff86836f, 0xffffffff },
                                          { 0xffeabd2c, 0xff80600a, 0xfffff2a8 }, { 0xff33a352, 0xff10521f, 0xff98e6aa } };
    return juce::Colour(t[juce::jlimit(0, 3, c)][juce::jlimit(0, 2, shade)]);
}

// ---------------- stack chooser: click a row to pick that cable up, drag rows to reorder the stack ----------------
class CableLayer::Chooser : public juce::Component {
public:
    Chooser(CableLayer& o, int j) : owner(o), jack(j) { rebuild(); }
    void rebuild() {
        rows.clear(); for (auto& p : owner.cm.plugsAt(jack)) rows.insert(rows.begin(), p);   // top of stack first
        setSize(280, kHead + kRow * ((int) rows.size() + 1) + 8);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff1b1b1e)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        g.setColour(juce::Colour(0xff3a3a3e)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8, 1);
        g.setFont(juce::FontOptions(11.5f)); g.setColour(juce::Colour(0x99dcd6c2));
        g.drawText(owner.names[jack] + "  (top first, drag to reorder)", 10, 4, getWidth() - 20, kHead - 6, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(13.0f));
        for (int i = 0; i <= (int) rows.size(); ++i) {
            auto r = rowRect(i);
            if (i == dragRow && moved) { g.setColour(juce::Colour(0xff34343a)); g.fillRoundedRectangle(r.toFloat(), 5); }
            if (i < (int) rows.size()) {
                const auto& c = owner.cm.cables()[(size_t) rows[(size_t) i].first]; const int other = rows[(size_t) i].second == 0 ? c.b : c.a;
                g.setColour(cableColour(c.color, 0)); g.fillEllipse((float) r.getX() + 8, (float) r.getCentreY() - 6, 12, 12);
                g.setColour(juce::Colour(0xffdcd6c2)); g.drawText(other >= 0 ? "to " + owner.names[other] : juce::String("loose end"), r.withTrimmedLeft(28).withTrimmedRight(22), juce::Justification::centredLeft);
                g.setColour(juce::Colour(0x77dcd6c2)); g.drawText(juce::String::fromUTF8("\xe2\x89\xa1"), r.withTrimmedRight(8), juce::Justification::centredRight);
            } else {
                g.setColour(cableColour(owner.colour, 0).withAlpha(0.5f)); g.fillEllipse((float) r.getX() + 8, (float) r.getCentreY() - 6, 12, 12);
                g.setColour(juce::Colour(0xffdcd6c2)); g.drawText("+ New cable here", r.withTrimmedLeft(28), juce::Justification::centredLeft);
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { dragRow = rowAt(e.y); startY = e.y; moved = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragRow < 0 || dragRow >= (int) rows.size()) return;
        if (std::abs(e.y - startY) > 4) moved = true;
        const int target = juce::jlimit(0, (int) rows.size() - 1, (e.y - kHead) / kRow);
        if (moved && target != dragRow) { auto r = rows[(size_t) dragRow]; rows.erase(rows.begin() + dragRow); rows.insert(rows.begin() + target, r); dragRow = target; }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        const int r = dragRow; dragRow = -1;
        if (moved) {                                                   // apply: rows are top-first, model wants bottom-first
            std::vector<int> order; for (auto it = rows.rbegin(); it != rows.rend(); ++it) order.push_back(it->first);
            owner.cm.reorderStack(jack, order); owner.commit(); rebuild(); repaint(); return;
        }
        if (r < 0) return;
        auto& o = owner; const int j = jack;
        if (r == (int) rows.size()) { o.cm.newFrom(j, o.colour); }
        else if (r < (int) rows.size()) { o.cm.pickUp(rows[(size_t) r].first, rows[(size_t) r].second); }
        else return;
        o.clickCarry = true; o.grabKeyboardFocus(); setVisible(false);
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<CableLayer>(&o)] { if (sp != nullptr) sp->chooser.reset(); });   // not deleted inside its own callback
    }
private:
    static constexpr int kHead = 24, kRow = 26;
    CableLayer& owner; int jack; std::vector<std::pair<int, int>> rows; int dragRow = -1, startY = 0; bool moved = false;
    juce::Rectangle<int> rowRect(int i) const { return { 4, kHead + i * kRow, getWidth() - 8, kRow - 2 }; }
    int rowAt(int y) const { const int i = (y - kHead) / kRow; return (y < kHead || i > (int) rows.size()) ? -1 : i; }
};

// ---------------- layer ----------------
CableLayer::CableLayer() { setWantsKeyboardFocus(true); startTimerHz(60); }
CableLayer::~CableLayer() = default;

void CableLayer::addRack(RackPanel* p, float y) { racks.push_back({ p, y }); rebuildScene(); }
void CableLayer::setDesignSize(float w, float h) { dw = w; dh = h; rebuildScene(); }
void CableLayer::resized() { if (chooser) chooser.reset(); }

void CableLayer::rebuildScene()
{
    ids.clear(); names.clear(); jacks.clear(); jackHits.clear(); labels.clear();
    for (auto& r : racks) {
        const auto& L = r.panel->layout();
        for (auto& j : L.jacks) { ids.add(L.rack + "/" + j.id); names.add(j.id.replaceCharacter(':', ' ')); jacks.push_back({ j.x, j.y + r.y }); jackHits.push_back(j.hit.translated(0, r.y)); }
        for (auto& t : L.labels) labels.push_back({ t.getX(), t.getY() + r.y, t.getWidth(), t.getHeight() });
    }
    cm.setScene(jacks, labels, dh - 8);
}

int CableLayer::jackAtDesign(juce::Point<float> p) const
{
    // Every jack takes at least 16 x 16 screen px, whatever the scale (22 design units are only 13 px at 75 %).
    const float m = getWidth() > 0 ? kMinJackHitPx / scale() : 0.0f;
    for (size_t i = 0; i < jackHits.size(); ++i) {
        auto h = jackHits[i];
        if (h.getWidth() < m || h.getHeight() < m) h = juce::Rectangle<float>(juce::jmax(h.getWidth(), m), juce::jmax(h.getHeight(), m)).withCentre({ jacks[i].x, jacks[i].y });
        if (h.contains(p)) return (int) i;
    }
    return -1;
}

void CableLayer::setPatch(const std::vector<CableSpec>& cs)
{
    cm.clear();
    for (auto& c : cs) { const int a = ids.indexOf(c.a), b = ids.indexOf(c.b); if (a >= 0 && b >= 0 && a != b) cm.add(a, b, c.color, c.age); }
}

std::vector<CableSpec> CableLayer::patch() const
{
    std::vector<CableSpec> out;
    for (auto& c : cm.cables()) if (c.a >= 0 && c.b >= 0) out.push_back({ ids[c.a], ids[c.b], c.color, c.age });
    return out;
}

void CableLayer::commit() { if (onPatchChanged) onPatchChanged(patch()); }

bool CableLayer::hitTest(int x, int y)      // take the mouse only where cables live, so knobs underneath keep working
{
    if (cm.carrying() || chooser) return true;
    return jackAtDesign(toDesign({ (float) x, (float) y })) >= 0;
}

void CableLayer::track(const juce::MouseEvent& e)
{
    const auto p = toDesign(e.getEventRelativeTo(this).position);
    const bool inside = getLocalBounds().toFloat().contains(p * scale());
    cm.setPointer(inside, { p.x, p.y });
    cm.setHoverJack(inside ? jackAtDesign(p) : -1);
    int lab = -1; if (inside) for (size_t i = 0; i < labels.size(); ++i) if (labels[i].contains({ p.x, p.y }, 3)) { lab = (int) i; break; }
    cm.setHoverLabel(lab);
}

void CableLayer::mouseMove(const juce::MouseEvent& e) { track(e); }          // also receives moves over the rack panels (see editor)
void CableLayer::mouseExit(const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains(e.getEventRelativeTo(this).getPosition())) { cm.setPointer(false, {}); cm.setHoverJack(-1); cm.setHoverLabel(-1); }
}

void CableLayer::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    track(e); if (chooser) { chooser.reset(); }
    const auto p = toDesign(e.position);
    if (cm.carrying() && clickCarry) { cm.drop(jackAtDesign(p)); clickCarry = false; swallowUp = true; commit(); return; }
    down = { jackAtDesign(p), p, false, e.mods.isShiftDown() };
}

void CableLayer::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    track(e); const auto p = toDesign(e.position);
    if (down.jack >= 0 && ! cm.carrying() && p.getDistanceFrom(down.pos) > 6) {
        down.moved = true; auto st = cm.plugsAt(down.jack);
        if (! st.empty() && ! down.shift) cm.pickUp(st.back().first, st.back().second); else cm.newFrom(down.jack, colour);
        clickCarry = false;
    }
}

void CableLayer::mouseUp(const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    if (swallowUp) { swallowUp = false; return; }
    const auto p = toDesign(e.position);
    if (cm.carrying() && ! clickCarry) { cm.drop(jackAtDesign(p)); commit(); down = {}; return; }
    if (down.jack >= 0 && ! down.moved && ! cm.plugsAt(down.jack).empty()) openChooser(down.jack);
    down = {};
}

bool CableLayer::keyPressed(const juce::KeyPress& k)
{
    if (k != juce::KeyPress::escapeKey) return false;
    if (chooser) { chooser.reset(); return true; }
    if (cm.carrying()) { cm.cancel(); clickCarry = false; commit(); return true; }
    return false;
}

void CableLayer::openChooser(int jack)
{
    chooser = std::make_unique<Chooser>(*this, jack); addAndMakeVisible(*chooser);
    const auto jp = jackPosDesign(jack) * scale();
    int x = juce::jlimit(4, getWidth() - chooser->getWidth() - 4, (int) jp.x);
    int y = (int) jp.y + 16; if (y + chooser->getHeight() > getHeight()) y = (int) jp.y - chooser->getHeight() - 16;
    chooser->setTopLeftPosition(x, juce::jmax(4, y));
}

static juce::Path ropePath(const std::vector<rack::V2>& p)
{
    juce::Path path; if (p.size() < 2) return path;
    path.startNewSubPath(p[0].x, p[0].y);
    for (size_t i = 1; i + 1 < p.size(); ++i) path.quadraticTo(p[i].x, p[i].y, (p[i].x + p[i + 1].x) / 2, (p[i].y + p[i + 1].y) / 2);
    path.lineTo(p.back().x, p.back().y);
    return path;
}

static void drawPlug(juce::Graphics& g, juce::Point<float> c, float r, int col)
{
    g.setColour(juce::Colours::black.withAlpha(0.4f)); g.fillEllipse(c.x + 2 - (r + 1.5f), c.y + 3 - (r + 1.5f), 2 * (r + 1.5f), 2 * (r + 1.5f));
    juce::ColourGradient boot(CableLayer::cableColour(col, 2), c.x - r * 0.4f, c.y - r * 0.5f, CableLayer::cableColour(col, 1), c.x + r * 0.7f, c.y + r * 0.8f, true);
    boot.addColour(0.35, CableLayer::cableColour(col, 0)); g.setGradientFill(boot); g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour(CableLayer::cableColour(col, 1)); g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.4f);
    const float s = r * 0.62f; juce::ColourGradient sil(juce::Colour(0xfff2f2f2), c.x - s * 0.3f, c.y - s * 0.4f, juce::Colour(0xff7a7a7a), c.x + s, c.y + s, true);
    g.setGradientFill(sil); g.fillEllipse(c.x - s, c.y - s, 2 * s, 2 * s);
    g.setColour(juce::Colour(0xff050505)); g.fillEllipse(c.x - r * 0.36f, c.y - r * 0.36f, r * 0.72f, r * 0.72f);
    g.setColour(juce::Colours::white.withAlpha(0.35f)); g.fillEllipse(c.x - r * 0.75f, c.y - r * 0.75f, r * 0.7f, r * 0.4f);
}

void CableLayer::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()));
    const auto& cs = cm.cables();
    std::vector<juce::Path> paths; for (auto& c : cs) paths.push_back(ropePath(c.p));
    g.setColour(juce::Colours::black.withAlpha(0.35f));                 // soft shadow under all cables
    for (auto& p : paths) g.strokePath(p, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation(3, 5));
    for (size_t i = 0; i < cs.size(); ++i) {
        const auto& c = cs[i];
        g.setColour(cableColour(c.color, 1)); g.strokePath(paths[i], juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(cableColour(c.color, 0)); g.strokePath(paths[i], juce::PathStrokeType(5.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(cableColour(c.color, 2).withAlpha(0.55f)); g.strokePath(paths[i], juce::PathStrokeType(1.6f), juce::AffineTransform::translation(-0.8f, -1.2f));
        for (int end = 0; end < 2; ++end) { const auto pp = cm.plugPos((int) i, end); drawPlug(g, { pp.x, pp.y }, cm.plugRadius((int) i, end), c.color); }
    }
}
