#include "BushidoTabs.h"
#include "PatternScreen.h"
#include "../engine/BushidoModule.h"
#include "../rack/PitchLaw.h"
#include <cmath>
#include <map>

namespace bushido_ui {

namespace {
const juce::Colour kGold(0xffc29f4c), kLabel(0xffece8dc), kDim(0xff8f8a7c), kInk(0xff1e2419), kStrip(0xff0c0c0d), kWarn(0xffe0533d);
const char* kTabNames[] = { "MAIN", "STEPS", "CLOCK", "MIDI", "SETUP" };
const char kRow[] = { 'A', 'B', 'C' };

juce::String stepId(int row, int step) { return juce::String::charToString((juce::juce_wchar) kRow[row]) + ":" + juce::String(step + 1); }
juce::String rowParam(const char* prefix, int row) { return juce::String(prefix) + " " + juce::String::charToString((juce::juce_wchar) kRow[row]); }

juce::String fmtBpm(double b)                       // like the front readout: "120", "118.4"
{
    const double r = std::round(b * 10.0) / 10.0;
    return std::abs(r - std::round(r)) < 0.05 ? juce::String((int) std::round(r)) : juce::String(r, 1);
}
juce::String noteText(int midi) { char buf[8]; return rack::pitch::noteName(midi, buf, (int) sizeof buf); }
}

std::vector<float> TabPage::clipboard;
std::vector<TabPage::Mark>* TabPage::recorder = nullptr;

// ---------------------------------------------------------------- strip
void TabStrip::paint(juce::Graphics& g)
{
    g.fillAll(kStrip);
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / kDesignW));
    TabPage::text(g, "BUSHIDO", 24, 23, 13, kDim, true, 3.0f, juce::Justification::topLeft);
    for (int i = 0; i < NUM_TABS; ++i) {
        const auto r = tabRect(i); const bool on = i == tab;
        g.setColour(on ? kGold : juce::Colour(0xff161618)); g.fillRoundedRectangle(r, 3);
        g.setColour(on ? kGold : juce::Colour(0xff3c3c3f)); g.drawRoundedRectangle(r.reduced(0.5f), 3, 1.0f);
        TabPage::text(g, kTabNames[i], r.getCentreX(), 23.5f, 12, on ? juce::Colour(0xff141414) : kLabel, true, 1.4f);
    }
    if (readOnly && readOnly()) TabPage::text(g, "READ-ONLY", 1576, 23, 11, kWarn, true, 1.6f, juce::Justification::topRight);
}

void TabStrip::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.position / ((float) getWidth() / kDesignW);
    for (int i = 0; i < NUM_TABS; ++i) if (tabRect(i).expanded(4, 6).contains(p)) { setTab(i); if (onSelect) onSelect(i); return; }
}

// ---------------------------------------------------------------- drawing helpers
void TabPage::text(juce::Graphics& g, const juce::String& s, float x, float baseline, float size, juce::Colour c, bool bold, float spacing, juce::Justification j)
{
    juce::Font f(juce::FontOptions("Liberation Sans", size, bold ? juce::Font::bold : juce::Font::plain).withPointHeight(size));
    f.setExtraKerningFactor(spacing / f.getHeight());
    g.setFont(f); g.setColour(c);
    const float top = baseline - f.getAscent(), h = f.getHeight() + 2, w = 1500;
    const auto hz = j.getOnlyHorizontalFlags();
    const juce::Rectangle<float> r = hz == juce::Justification::left ? juce::Rectangle<float>(x, top, w, h)
                                   : hz == juce::Justification::right ? juce::Rectangle<float>(x - w, top, w, h)
                                   : juce::Rectangle<float>(x - w / 2, top, w, h);
    g.drawText(s, r, juce::Justification(hz | juce::Justification::top), false);
    if (recorder != nullptr) {                         // the ink's extent (drawText puts the run inside r as justified)
        static std::map<juce::String, float> widths;   // measuring is the costly part of a tick: once per text and font
        const auto wk = juce::String(size) + (bold ? "b" : "p") + juce::String(spacing) + "|" + s;
        auto it = widths.find(wk);
        if (it == widths.end()) { if (widths.size() > 4096) widths.clear(); it = widths.emplace(wk, juce::GlyphArrangement::getStringWidth(f, s)).first; }
        const float tw = it->second;
        const float x0 = hz == juce::Justification::left ? x : hz == juce::Justification::right ? x - tw : x - tw / 2;
        mark({ x0 - 3, top - 1, tw + 6, h + 2 }, "t" + juce::String(size) + (bold ? "b" : "p") + c.toString() + s);
    }
}

void TabPage::plate(juce::Graphics& g)
{
    g.fillAll(kStrip);
    const juce::Rectangle<float> r(0, 0, kDesignW, kDesignH);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff242426), 0, 0, juce::Colour(0xff161618), 0, kDesignH, false));
    g.fillRoundedRectangle(r, 8);
    g.setColour(juce::Colour(0xff050506)); g.drawRoundedRectangle(r.reduced(4), 6, 2.0f);
    for (auto c : { juce::Point<float>(10, 10), { kDesignW - 10, 10 }, { 10, kDesignH - 10 }, { kDesignW - 10, kDesignH - 10 } }) {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe6e6e1), c.x - 2, c.y - 2, juce::Colour(0xff7d7d79), c.x + 5, c.y + 5, true));
        g.fillEllipse(c.x - 5, c.y - 5, 10, 10);
        g.setColour(juce::Colours::black); g.drawEllipse(c.x - 5, c.y - 5, 10, 10, 1.0f);
        g.setColour(juce::Colour(0xff4a4a48)); g.drawLine(c.x - 3, c.y + 3, c.x + 3, c.y - 3, 1.0f);   // slot
    }
}

void TabPage::block(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour(kGold); g.drawRect(r, 1.6f);
    if (title.isNotEmpty()) text(g, title, r.getCentreX(), r.getY() + 20, 12, kLabel, true, 2.0f);
}

void TabPage::lcd(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, int chars)
{
    mark(r.expanded(1), "l" + juce::String(chars) + t);
    g.setColour(juce::Colour(0xff0a0a0b)); g.fillRoundedRectangle(r, 3);
    g.setColour(juce::Colour(0xff2a2a2c)); g.drawRoundedRectangle(r, 3, 1.2f);
    const auto glass = r.reduced(3);
    juce::ColourGradient cg(juce::Colour(0xff8f9a7c), 0, glass.getY(), juce::Colour(0xff94a083), 0, glass.getBottom(), false); cg.addColour(0.5, juce::Colour(0xffa6b192));
    g.setGradientFill(cg); g.fillRoundedRectangle(glass, 1.5f);
    auto s = t.toUpperCase(); const int n = juce::jmax(chars, s.length(), 1);
    s = juce::String::repeatedString(" ", (n - s.length()) / 2) + s;               // centred in the cells
    PatternScreen::drawDots(g, glass.reduced(2, 1), s, n, kInk, 0.09f);
}

void TabPage::segmented(juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& labels, const juce::String& id, int disabledSegment)
{
    const int n = labels.size(); const float w = r.getWidth() / (float) n;
    const int sel = (int) std::lround(host.value(id) * (float) (n - 1));
    g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.expanded(1.2f), 3.5f);
    for (int i = 0; i < n; ++i) {
        const juce::Rectangle<float> c(r.getX() + w * (float) i, r.getY(), w, r.getHeight());
        const bool on = i == sel, off = i == disabledSegment;
        mark(c, juce::String("s") + (on ? "1" : "0"));
        if (on) g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffbf9f1), 0, c.getY(), juce::Colour(0xffdcd7c6), 0, c.getBottom(), false));
        else g.setColour(juce::Colour(0xff0e0e10));
        g.fillRoundedRectangle(c.reduced(0.6f), 2.5f);
        auto col = on ? juce::Colour(0xff141414) : kLabel; if (off) col = col.withAlpha(0.35f);
        text(g, labels[i], c.getCentreX(), c.getCentreY() + 3.6f, 10, col, true, 0.8f);
        if (! off) { auto h = listParam(c, id, labels, disabledSegment >= 0 ? std::vector<int> { disabledSegment } : std::vector<int> {});
                     h.click = [this, id, i, n](juce::Point<float>) { setParam(id, (float) i / (float) (n - 1)); };   // a segment is picked directly
                     h.step = {}; hits.push_back(h); }
    }
}

void TabPage::button(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, std::function<void()> action, bool selected)
{
    mark(r.expanded(1), juce::String("b") + (selected ? "1" : "0"));
    if (selected) g.setColour(kGold);
    else g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4a4e), r.getX(), r.getY(), juce::Colour(0xff0e0e10), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, 3, 1.0f);
    text(g, label, r.getCentreX(), r.getCentreY() + 3.6f, 10, selected ? juce::Colour(0xff141414) : kLabel, true, 0.8f);
    Hit h; h.r = r; h.click = [action](juce::Point<float>) { action(); }; hits.push_back(h);
}

// A parameter with a list of named settings: click = next, Shift-click = previous (both wrap), right-click = the list.
TabPage::Hit TabPage::listParam(juce::Rectangle<float> r, const juce::String& id, const juce::StringArray& labels, std::vector<int> disabled)
{
    Hit h; h.r = r; h.listName = id;
    const int n = labels.size();
    auto current = [this, id, n] { return (int) std::lround(host.value(id) * (float) (n - 1)); };
    h.list = [labels, disabled, current] { listmenu::Choice c; c.items = labels; c.ticked = current(); c.disabled = disabled; return c; };
    h.choose = [this, id, n, disabled](int k) { if (k >= 0 && k < n && std::find(disabled.begin(), disabled.end(), k) == disabled.end()) setParam(id, (float) k / (float) (n - 1)); };
    h.step = [this, id, n, current, disabled](bool back) {
        int k = current();
        for (int tries = 0; tries < n; ++tries) { k = listmenu::step(k, n, back); if (std::find(disabled.begin(), disabled.end(), k) == disabled.end()) break; }
        setParam(id, (float) k / (float) (n - 1)); };
    h.click = [step = h.step](juce::Point<float>) { step(false); };
    return h;
}

// ---------------------------------------------------------------- page
TabPage::TabPage(TabHost& h) : host(h) { setOpaque(true); }
TabPage::~TabPage() = default;

void TabPage::setTab(int t)
{
    tab = t; typing.reset(); dragHit = -1;
    setLive(live);
    repaint();
}

void TabPage::setLive(bool l)
{
    live = l;
    const bool run = live && tab != MAIN;
    if (run == isTimerRunning()) return;
    if (run) { startTimerHz(15); repaint(); } else stopTimer();
}

void TabPage::setParam(const juce::String& id, float v)
{
    host.edit(id, true); host.setValue(id, juce::jlimit(0.0f, 1.0f, v)); host.edit(id, false); repaint();
}

void TabPage::paint(juce::Graphics& g)
{
    hits.clear(); recorded.clear();
    const juce::ScopedValueSetter<std::vector<Mark>*> rec(recorder, &recorded);
    g.addTransform(juce::AffineTransform::scale(scale()));
    plate(g);
    switch (tab) { case STEPS: paintSteps(g); break; case CLOCK: paintClock(g); break; case MIDI: paintMidi(g); break; case SETUP: paintSetup(g); break; default: break; }
}

// ---------------------------------------------------------------- STEPS
// Three equal row blocks. Left column: the row's switches and readouts; centre: 12 steps (volts over note name, two views of
// the panel knob); right column: COPY / PASTE / RAND / CLEAR. Left and right columns are the same width, so the steps are centred.
void TabPage::paintSteps(juce::Graphics& g)
{
    for (int row = 0; row < 3; ++row) rowBlock(g, row, { 30, 34 + 130.0f * (float) row, 1540, 116 });
    text(g, juce::String::fromUTF8("values are the panel knobs (two views of one parameter) \xc2\xb7 drag, or double-click and type \"C4\" or \"2.5\" \xc2\xb7 note names follow each row's PITCH LAW"),
         800, 426, 10, kDim, false, 0.6f);
}

void TabPage::rowBlock(juce::Graphics& g, int row, juce::Rectangle<float> r)
{
    using rack::pitch::Law;
    block(g, r, {});
    const float y = r.getY(), cx = 128;
    const bool isC = row == 2, cTime = host.value("CH:C MODE") > 0.5f;
    const Law law = ! isC && host.value(rowParam("STEPS:LAW", row)) > 0.5f ? Law::HzvLin : Law::VOct;
    const bool quant = ! isC && host.value(rowParam("STEPS:QUANT", row)) > 0.5f;
    const float range = isC ? 5.0f : (host.value(rowParam("CH:RANGE", row)) > 0.5f ? 5.0f : 1.0f);
    text(g, juce::String("ROW ") + kRow[row], cx, y + 26, 14, kLabel, true, 2.0f);

    const juce::Rectangle<float> left(44, y + 50, 80, 24), right(132, y + 50, 80, 24);
    if (! isC) {
        text(g, "QUANT", left.getCentreX(), y + 46, 9, kDim, true, 0.8f);
        text(g, "PITCH LAW", right.getCentreX(), y + 46, 9, kDim, true, 0.8f);
        lcd(g, left, quant ? "SEMI" : "OFF", 5);
        lcd(g, right, law == Law::VOct ? "V/OCT" : "LIN", 5);
        hits.push_back(listParam(left, rowParam("STEPS:QUANT", row), { "OFF", "SEMI" }));
        hits.push_back(listParam(right, rowParam("STEPS:LAW", row), { "V/OCT", "LIN" }));
        if (host.lawMismatch(row)) {                           // a cable expects the other law (JCS R4: the role badge)
            mark({ right.getRight() - 9, right.getY() - 7, 15, 15 }, "badge");
            g.setColour(kWarn); g.fillEllipse(right.getRight() - 8, right.getY() - 6, 13, 13);
            text(g, juce::String::fromUTF8("\xe2\x89\xa0"), right.getRight() - 1.5f, right.getY() + 4.5f, 10, juce::Colours::white, true, 0);
        }
        const double tau = (double) host.value(rowParam("CH:PORTA", row)) * (double) host.value(rowParam("CH:PORTA", row)) * 2.0;   // BushidoModule: tau = PORTA^2 x 2 s
        const double t99 = std::log(100.0) * tau;
        auto secs = [](double s) { return s < 0.1 ? juce::String((int) std::round(s * 1000.0)) + " ms" : juce::String(s, 2) + " s"; };
        text(g, juce::String(law == Law::VOct ? "V/OCT: 0 V = C3" : "LIN: 1 V = C3") + juce::String::fromUTF8(" \xc2\xb7 RANGE ") + juce::String((int) range) + " V",
             cx, y + 91, 9, kDim, false, 0.6f);
        text(g, tau < 1e-4 ? juce::String("PORTA off (no glide)")
                           : juce::String::fromUTF8("\xcf\x84 = ") + secs(tau) + juce::String::fromUTF8(" \xc2\xb7 99 % in ") + secs(t99),
             cx, y + 106, 9, kLabel, true, 0.4f);
    } else {
        text(g, "C MODE", left.getCentreX(), y + 46, 9, kDim, true, 0.8f);
        text(g, "RANGE", right.getCentreX(), y + 46, 9, kDim, true, 0.8f);
        lcd(g, left, cTime ? "TIME" : "CV", 5);
        lcd(g, right, "5V", 5);
        hits.push_back(listParam(left, "CH:C MODE", { "CV", "TIME" }));
        text(g, cTime ? juce::String::fromUTF8("TIME: gate length 5\xe2\x80\x93" "95 %") : juce::String::fromUTF8("CV: 0\xe2\x80\x93" "5 V on CV C"), cx, y + 91, 9, kDim, false, 0.6f);
        text(g, cTime ? juce::String("never sent as CV") : juce::String::fromUTF8("VEL FROM C = round(1 + 126\xc2\xb7" "C/5)"), cx, y + 106, 9, kDim, false, 0.6f);
    }

    // which step is lit, and whether this row is the one being read (the panel lamps)
    int pos = -1; for (int s = 0; s < 12; ++s) if (host.lamp("STEP:" + juce::String(s + 1)) > 0.5f) pos = s;
    const int reading = host.lamp("CH:B") > 0.5f ? 1 : 0;
    for (int s = 0; s < 12; ++s) {
        const float x = 242 + 94.0f * (float) s;
        const juce::String id = stepId(row, s);
        const float knob = host.value(id);
        double v = (double) (knob * range); if (quant) v = rack::pitch::quantize(law, v);
        const juce::Rectangle<float> vr(x, y + 18, 82, 30), nr(x, y + 56, 82, 30);
        const bool lit = s == pos && (isC || row == reading);
        mark(vr.getUnion(nr).expanded(6), juce::String("h") + (lit ? "1" : "0"));
        if (lit) { g.setColour(kGold); g.drawRoundedRectangle(vr.getUnion(nr).expanded(4), 4, 1.4f); }
        lcd(g, vr, juce::String(v, 2) + "V", 5);
        lcd(g, nr, isC && cTime ? juce::String((int) std::round(100.0 * (0.05 + 0.9 * (double) knob))) + "%" : noteText(rack::pitch::midiNote(law, v)), 5);
        text(g, juce::String(s + 1), x + 41, y + 104, 10, kDim, true, 0.8f);
        const Law typedLaw = law; const float rng = range;
        auto type = [this, id, typedLaw, rng, vr, nr, v] {
            typeInto(vr.getUnion(nr), juce::String(v, 2), [this, id, typedLaw, rng](const juce::String& txt) {
                const double volts = parseStep(txt, typedLaw == Law::VOct ? 0 : 1);
                if (! std::isnan(volts)) setParam(id, (float) (volts / (double) rng));
            });
        };
        auto wheel = [this, id](int d) { setParam(id, host.value(id) + 0.01f * (float) d); };
        Hit h; h.r = vr.getUnion(nr); h.dragId = id; h.wheel = wheel; h.dbl = type; hits.push_back(h);
    }

    const float bx = 1381, by = y + 26;
    auto rowIds = [row] { juce::StringArray a; for (int s = 0; s < 12; ++s) a.add(stepId(row, s)); return a; };
    button(g, { bx, by, 86, 26 }, "COPY", [this, rowIds] { clipboard.clear(); for (auto& id : rowIds()) clipboard.push_back(host.value(id)); });
    button(g, { bx + 96, by, 86, 26 }, "PASTE", [this, rowIds] { if (clipboard.size() == 12) { auto ids = rowIds(); for (int s = 0; s < 12; ++s) setParam(ids[s], clipboard[(size_t) s]); } });
    button(g, { bx, by + 38, 86, 26 }, "RAND", [this, rowIds] { auto& rnd = juce::Random::getSystemRandom(); for (auto& id : rowIds()) setParam(id, rnd.nextFloat()); });
    button(g, { bx + 96, by + 38, 86, 26 }, "CLEAR", [this, rowIds] { for (auto& id : rowIds()) setParam(id, 0.0f); });
}

double TabPage::parseStep(const juce::String& in, int law)
{
    auto s = in.trim().removeCharacters(" ");
    if (s.isEmpty()) return std::nan("");
    const juce::String letters("CDEFGAB"); const int pcs[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int li = letters.indexOfChar(juce::CharacterFunctions::toUpperCase(s[0]));
    if (li >= 0 && s.length() >= 2) {                         // a note: C4, F#3, Bb2, C-1 (C3 = MIDI 48)
        int pc = pcs[li], k = 1;
        if (s[k] == '#') { ++pc; ++k; } else if (s[k] == 'b' || (s[k] == 'B' && s.length() > 2 && (juce::CharacterFunctions::isDigit(s[2]) || s[2] == '-'))) { --pc; ++k; }
        const auto oct = s.substring(k);
        if (oct.isNotEmpty() && oct.containsOnly("-0123456789")) {
            const int midi = 12 * (oct.getIntValue() + 1) + pc;
            return law == 0 ? (midi - 48) / 12.0 : std::exp2((midi - 48) / 12.0);
        }
        return std::nan("");
    }
    if (s.endsWithIgnoreCase("v")) s = s.dropLastCharacters(1);
    if (s.isEmpty() || ! s.containsOnly("-+.0123456789")) return std::nan("");
    return s.getDoubleValue();
}

void TabPage::typeInto(juce::Rectangle<float> cell, const juce::String& current, std::function<void(const juce::String&)> done)
{
    typing = std::make_unique<juce::TextEditor>();
    auto* te = typing.get();
    te->setBounds(cell.reduced(6, 18).transformedBy(juce::AffineTransform::scale(scale())).toNearestInt());
    te->setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 13.0f * scale() / 0.8f, juce::Font::bold)));
    te->setJustification(juce::Justification::centred);
    te->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xffa6b192));
    te->setColour(juce::TextEditor::textColourId, kInk);
    te->setColour(juce::TextEditor::outlineColourId, kGold);
    te->setColour(juce::TextEditor::focusedOutlineColourId, kGold);
    te->setText(current, false); te->selectAll();
    addAndMakeVisible(te); te->grabKeyboardFocus();
    auto finish = [this, done](bool apply) {
        if (! typing) return;
        const auto t = typing->getText();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<TabPage>(this)] { if (sp) sp->typing.reset(); });
        if (apply) done(t);
    };
    te->onReturnKey = [finish] { finish(true); };
    te->onEscapeKey = [finish] { finish(false); };
    te->onFocusLost = [finish] { finish(false); };
}

// ---------------------------------------------------------------- CLOCK
// EXT SOURCE | TIMING | TRANSPORT: three equal blocks, the middle one centred.
void TabPage::paintClock(juce::Graphics& g)
{
    const juce::Rectangle<float> a(30, 34, 480, 370), b(560, 34, 480, 370), c(1090, 34, 480, 370);
    block(g, a, "EXT SOURCE"); block(g, b, "TIMING"); block(g, c, "TRANSPORT");

    // EXT SOURCE
    const bool ext = host.value("CLOCK:SOURCE") > 0.5f, hostSrc = host.value("CLOCK:EXT SOURCE") > 0.5f;
    segmented(g, { 206, 92, 128, 24 }, { "JACK", "HOST" }, "CLOCK:EXT SOURCE");
    text(g, ext ? juce::String("front SOURCE = EXT: this picks the clock") : juce::String("front SOURCE = INT: this applies when it is at EXT"), 270, 138, 10, kDim, false, 0.5f);
    text(g, "HOST is preset only for a new rack instance made while the DAW plays", 270, 153, 10, kDim, false, 0.5f);
    const float div = host.value("CLOCK:DIV");
    juce::String bpmText;
    if (hostSrc) { const double hb = host.hostBpm(); bpmText = hb > 0 ? "HOST " + fmtBpm(hb) : juce::String("HOST --"); }
    else { const double per = host.extPeriod(); bpmText = per > 0 ? "EXT " + fmtBpm(60.0 / (per * BushidoModule::stepsPerBeat(div))) : juce::String("EXT --"); }
    lcd(g, { 160, 176, 220, 40 }, bpmText, 10);
    text(g, hostSrc ? "DAW TEMPO" : "MEASURED BPM", 270, 236, 10, kDim, true, 0.8f);
    text(g, "HOST DIV", 270, 278, 11, kLabel, true, 1.0f);
    segmented(g, { 180, 288, 180, 24 }, { "1/8", "1/16", "1/32" }, "CLOCK:DIV");
    text(g, juce::String::fromUTF8("HOST: 2 / 4 / 8 steps per quarter \xc2\xb7 INT / EXT: the BPM unit"), 270, 334, 10, kDim, false, 0.5f);

    // TIMING
    const bool vintage = host.value("CLOCK:SETTLE") > 0.5f, pulse = host.value("CLOCK:TRIG MODE") > 0.5f;
    text(g, "SETTLE", 800, 84, 9, kDim, true, 0.8f);
    segmented(g, { 710, 92, 180, 24 }, { "TIGHT", "VINTAGE" }, "CLOCK:SETTLE");
    text(g, vintage ? juce::String("VINTAGE: CV and gate 0.6 ms after the step, as v1") : juce::String("TIGHT: CV and gate 2 samples after the step"), 800, 138, 10, kDim, false, 0.5f);
    text(g, "TRIG MODE", 800, 178, 9, kDim, true, 0.8f);
    segmented(g, { 710, 186, 180, 24 }, { "STEP", "PULSE" }, "CLOCK:TRIG MODE");
    text(g, pulse ? juce::String("PULSE: each TRIG is a 5 ms trigger") : juce::String("STEP (default): each TRIG is high for its whole step"), 800, 232, 10, kDim, false, 0.5f);
    lcd(g, { 690, 270, 220, 36 }, "SETTLE " + juce::String(host.settleSamples()) + " SMP", 12);
    text(g, "CV-to-gate settle at this sample rate", 800, 326, 10, kDim, false, 0.5f);

    // TRANSPORT: the fixed JCS R5 rules, as lamps you can read; and the run state
    const char* rules[] = { "START plays step 1", "START absorbs an EXT edge within 2 samples", "RESET absorbs a coincident clock", "STOP sends gates and TRIGs low" };
    for (int i = 0; i < 4; ++i) {
        const float ly = 96 + 46.0f * (float) i;
        g.setColour(juce::Colour(0x4462d050)); g.fillEllipse(1146, ly - 10, 12, 12);
        g.setColour(juce::Colour(0xff6cd05a)); g.fillEllipse(1148, ly - 8, 8, 8);
        text(g, rules[i], 1168, ly, 12, kLabel, true, 0.4f, juce::Justification::topLeft);
    }
    int pos = -1; for (int s = 0; s < 12; ++s) if (host.lamp("STEP:" + juce::String(s + 1)) > 0.5f) pos = s;
    const bool run = host.lamp("MODE:RUN") > 0.5f;
    const juce::String rowName(host.lamp("CH:B") > 0.5f ? "B" : "A");
    lcd(g, { 1220, 288, 220, 36 }, run ? "RUN " + rowName + juce::String(pos + 1) : (pos >= 0 ? "STOP " + rowName + juce::String(pos + 1) : juce::String("STOP")), 10);
    text(g, "fixed rules, shown so you can see them", 1330, 346, 10, kDim, false, 0.5f);
}

// ---------------------------------------------------------------- MIDI
// ROW A | REFERENCE | ROW B over a full-width monitor. The note comes from the step target under the row's law.
void TabPage::paintMidi(juce::Graphics& g)
{
    using rack::pitch::Law;
    const bool cTime = host.value("CH:C MODE") > 0.5f;
    for (int row = 0; row < 2; ++row) {
        const juce::Rectangle<float> r(row == 0 ? 30.0f : 1070.0f, 34, 500, 250);
        const float cx = r.getCentreX();
        block(g, r, juce::String("ROW ") + kRow[row] + juce::String::fromUTF8(" \xc2\xb7 NOTE OUT"));
        const juce::String chId = rowParam("MIDI:CH", row);
        const int ch = 1 + (int) std::lround(host.value(chId) * 15.0f);
        text(g, "CHANNEL", cx - 100, 90, 10, kDim, true, 0.8f);
        const juce::Rectangle<float> chr(cx - 165, 98, 130, 34);
        lcd(g, chr, "< CH " + juce::String(ch) + " >", 9);
        {   // the < and > halves step down and up (no wrap); right-click lists all 16
            juce::StringArray chans; for (int k = 1; k <= 16; ++k) chans.add("CH " + juce::String(k));
            auto h = listParam(chr, chId, chans);
            h.click = [this, chId, chr](juce::Point<float> p) { const int c = 1 + (int) std::lround(host.value(chId) * 15.0f) + (p.x < chr.getCentreX() ? -1 : 1);
                                                               setParam(chId, (float) (juce::jlimit(1, 16, c) - 1) / 15.0f); };
            h.step = {};
            h.wheel = [this, chId](int d) { const int c = 1 + (int) std::lround(host.value(chId) * 15.0f) + d; setParam(chId, (float) (juce::jlimit(1, 16, c) - 1) / 15.0f); };
            hits.push_back(h);
        }
        text(g, "VELOCITY", cx + 100, 90, 10, kDim, true, 0.8f);
        segmented(g, { cx + 30, 103, 140, 24 }, { "100", "FROM C" }, rowParam("MIDI:VEL", row), cTime ? 1 : -1);
        const bool lin = host.value(rowParam("STEPS:LAW", row)) > 0.5f;
        text(g, lin ? juce::String::fromUTF8("LIN: note = round(48 + 12\xc2\xb7log2 V), none at 0 V") : juce::String::fromUTF8("V/OCT: note = round(48 + 12\xc2\xb7V)"), cx, 172, 10, kDim, false, 0.5f);
        text(g, "of the step target (not the slewed CV)", cx, 188, 10, kDim, false, 0.5f);
        text(g, cTime ? juce::String("FROM C needs C MODE = CV (it is TIME now)") : juce::String::fromUTF8("FROM C: velocity = round(1 + 126\xc2\xb7" "C / 5)"),
             cx, 214, 10, cTime ? kWarn.withAlpha(0.8f) : kDim, false, 0.5f);
        text(g, juce::String("plays on the CV / GATE ") + kRow[row] + " jacks", cx, 250, 10, kDim, false, 0.5f);
    }
    const juce::Rectangle<float> ref(550, 34, 500, 250);
    block(g, ref, "REFERENCE");
    lcd(g, { 640, 82, 320, 34 }, "C3 = 130.81 HZ = 48", 19);
    lcd(g, { 640, 124, 320, 34 }, "V/OCT 0V  LIN 1V", 19);
    text(g, "fixed by the Jidai pitch standard", 800, 184, 10, kDim, false, 0.5f);
    text(g, "the 55 Hz / MIDI 33 reference is retired", 800, 200, 10, kDim, false, 0.5f);
    text(g, "in the rack, no MIDI goes to the DAW: patch the jacks", 800, 250, 10, kDim, false, 0.5f);

    // monitor: what each jack pair is sending now (from the panel lamps and knobs, so it costs the audio thread nothing)
    block(g, { 30, 298, 1540, 106 }, "MIDI MONITOR");
    int pos = -1; for (int s = 0; s < 12; ++s) if (host.lamp("STEP:" + juce::String(s + 1)) > 0.5f) pos = s;
    const bool run = host.lamp("MODE:RUN") > 0.5f;
    const int chan = host.lamp("CH:B") > 0.5f ? 1 : 0, mode = (int) std::lround(host.value("MODE:MODE") * 2.0f), jack = mode == 2 ? chan : 0;
    juce::String mon;
    for (int j = 0; j < 2; ++j) {
        const int ch = 1 + (int) std::lround(host.value(rowParam("MIDI:CH", j)) * 15.0f);
        mon << kRow[j] << " CH" << ch << " ";
        if (! run || pos < 0 || j != jack) { mon << "--        "; continue; }
        const Law law = host.value(rowParam("STEPS:LAW", j)) > 0.5f ? Law::HzvLin : Law::VOct;
        double v = (double) host.value(stepId(chan, pos)) * (host.value(rowParam("CH:RANGE", j)) > 0.5f ? 5.0 : 1.0);
        if (host.value(rowParam("STEPS:QUANT", j)) > 0.5f) v = rack::pitch::quantize(law, v);
        const int note = rack::pitch::midiNote(law, v);
        const bool fromC = host.value(rowParam("MIDI:VEL", j)) > 0.5f && ! cTime;
        const int vel = fromC ? juce::jlimit(1, 127, (int) std::lround(1.0 + 126.0 * (double) host.value(stepId(2, pos)))) : 100;
        if (note < 0) mon << "--        "; else mon << noteText(note) << " " << note << " V" << vel << "   ";
    }
    lcd(g, { 60, 324, 1480, 36 }, mon.trimEnd(), 44);
    text(g, run ? juce::String("now playing: the step target under each row's law, on its channel and velocity") : juce::String("stopped: no notes"), 800, 384, 10, kDim, false, 0.5f);
}

// ---------------------------------------------------------------- SETUP
// INTERFACE | PATCH / MIGRATION: two equal blocks.
void TabPage::paintSetup(juce::Graphics& g)
{
    const juce::Rectangle<float> a(30, 34, 760, 370), b(810, 34, 760, 370);
    block(g, a, "INTERFACE"); block(g, b, "PATCH / MIGRATION");

    text(g, "UI SCALE", 410, 84, 9, kDim, true, 0.8f);
    const int cur = host.scalePercent();
    const int steps[] = { 75, 100, 125, 150, 200 };
    juce::StringArray scaleNames; int curIdx = -1;
    for (int i = 0; i < 5; ++i) { scaleNames.add(juce::String(steps[i]) + " %"); if (steps[i] == cur) curIdx = i; }
    for (int i = 0; i < 5; ++i) { const int pc = steps[i];
        button(g, { 98 + 130.0f * (float) i, 92, 104, 26 }, juce::String(pc) + " %", [this, pc] { host.setScalePercent(pc); }, pc == cur);
        auto& h = hits.back(); h.listName = "UI SCALE";                // each button picks directly; right-click lists the five
        h.list = [scaleNames, curIdx] { listmenu::Choice c; c.items = scaleNames; c.ticked = curIdx; return c; };
        h.choose = [this](int k) { const int pcs[] = { 75, 100, 125, 150, 200 }; if (k >= 0 && k < 5) host.setScalePercent(pcs[k]); }; }
    const int w = juce::roundToInt(1280.0 * cur / 100.0), h = juce::roundToInt(36.0 * w / 1600.0) + juce::roundToInt(434.0 * w / 1600.0);
    lcd(g, { 290, 146, 240, 36 }, juce::String(w) + " X " + juce::String(h), 11);
    text(g, juce::String::fromUTF8("default 1280 \xc3\x97 376: the panel (1280 \xc3\x97 347) plus a 29 px tab strip \xc2\xb7 960 to 2560 wide"), 410, 210, 11, kDim, false, 0.5f);
    text(g, juce::String::fromUTF8("jack hit area \xe2\x89\xa5 16 px at every scale"), 410, 240, 11, kLabel, false, 0.5f);
    text(g, "MAIN is the unchanged front panel; every new control lives on a tab", 410, 266, 11, kDim, false, 0.5f);
    text(g, "the scale is saved with the session", 410, 346, 10, kDim, false, 0.5f);

    float y = 64;
    if (host.readOnly()) {
        const juce::Rectangle<float> ban(840, y, 700, 30);
        mark(ban.expanded(1), "banner");
        g.setColour(juce::Colour(0xff3a1410)); g.fillRoundedRectangle(ban, 3);
        g.setColour(kWarn); g.drawRoundedRectangle(ban, 3, 1.4f);
        text(g, juce::String::fromUTF8("READ-ONLY \xc2\xb7 saved by a newer BUSHIDO \xc2\xb7 this session is handed back unchanged"), 1190, y + 19.5f, 11, kLabel, true, 0.6f);
        y += 42;
    }
    auto lines = host.migrationLines();
    for (int row = 0; row < 2; ++row) if (host.lawMismatch(row))
        lines.push_back(juce::String("ROW ") + kRow[row] + juce::String::fromUTF8(": \xe2\x89\xa0 a cable on CV ") + kRow[row] + " expects the other PITCH LAW (STEPS tab)");
    if (lines.empty()) { lines.push_back(juce::String::fromUTF8("format 1 \xc2\xb7 nothing to migrate"));
                         lines.push_back(juce::String::fromUTF8("new patches: SETTLE TIGHT \xc2\xb7 TRIG MODE STEP \xc2\xb7 PITCH LAW V/OCT")); }
    const int maxLines = host.readOnly() ? 5 : 6;
    juce::Font mono(juce::FontOptions("DejaVu Sans Mono", 12.0f, juce::Font::bold).withPointHeight(12.0f));
    for (int i = 0; i < (int) lines.size() && i < maxLines; ++i) {
        const juce::Rectangle<float> lr(840, y + 46.0f * (float) i, 700, 32);
        g.setColour(juce::Colour(0xff0a0a0b)); g.fillRoundedRectangle(lr, 3);
        juce::ColourGradient cg(juce::Colour(0xff8f9a7c), 0, lr.getY(), juce::Colour(0xff94a083), 0, lr.getBottom(), false); cg.addColour(0.5, juce::Colour(0xffa6b192));
        g.setGradientFill(cg); g.fillRoundedRectangle(lr.reduced(3), 1.5f);
        g.setColour(kInk); g.setFont(mono);
        const auto t = i == maxLines - 1 && (int) lines.size() > maxLines ? juce::String::fromUTF8("\xe2\x80\xa6 ") + juce::String((int) lines.size() - i) + " more" : lines[(size_t) i];
        g.drawFittedText(t, lr.reduced(12, 3).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        mark(lr.expanded(1), "m" + t);
    }
    text(g, juce::String::fromUTF8("the last load's report (format 0 \xe2\x86\x92 1); cables and knob volts never change"), 1190, 390, 10, kDim, false, 0.5f);
}

// ---------------------------------------------------------------- lists
void TabPage::layout()
{
    juce::Image scratch(juce::Image::ARGB, 1, 1, true);
    juce::Graphics g(scratch);
    g.reduceClipRegion(juce::Rectangle<int>());        // nothing is drawn: the hits and marks are rebuilt
    paint(g);
}

juce::Rectangle<int> TabPage::toComponent(juce::Rectangle<float> r) const
{
    return r.expanded(2).transformedBy(juce::AffineTransform::scale(scale())).getSmallestIntegerContainer().expanded(1).getIntersection(getLocalBounds());
}

juce::RectangleList<int> TabPage::refresh()
{
    layout();
    juce::RectangleList<int> dirty;
    if (recorded.size() != shown.size()) dirty.add(getLocalBounds());
    else for (size_t i = 0; i < recorded.size(); ++i)
        if (! (recorded[i] == shown[i])) { dirty.add(toComponent(recorded[i].r)); dirty.add(toComponent(shown[i].r)); }
    shown = recorded;
    for (auto& r : dirty) repaint(r);
    return dirty;
}

juce::StringArray TabPage::listNames() const
{
    juce::StringArray a; for (auto& h : hits) if (h.list) a.addIfNotAlreadyThere(h.listName);
    return a;
}

int TabPage::firstHitOf(const juce::String& name) const
{
    for (size_t i = 0; i < hits.size(); ++i) if (hits[i].list && hits[i].listName == name) return (int) i;
    return -1;
}

listmenu::Choice TabPage::listItems(int list) const
{
    const int i = firstHitOf(listNames()[list]); return i < 0 ? listmenu::Choice {} : hits[(size_t) i].list();
}

void TabPage::applyListChoice(int list, int index)
{
    const int i = firstHitOf(listNames()[list]); if (i < 0) return;
    auto f = hits[(size_t) i].choose; if (f) f(index);
}

void TabPage::stepList(int list, bool back)
{
    const int i = firstHitOf(listNames()[list]); if (i < 0) return;
    const auto h = hits[(size_t) i];
    if (h.step) h.step(back);
    else if (h.click && ! back) h.click(h.r.getCentre());
}

// ---------------------------------------------------------------- mouse
int TabPage::hitAt(juce::Point<float> p) const
{
    for (int i = (int) hits.size() - 1; i >= 0; --i) if (hits[(size_t) i].r.contains(p)) return i;
    return -1;
}

void TabPage::mouseDown(const juce::MouseEvent& e)
{
    const int i = hitAt(design(e.position)); dragHit = -1;
    if (i < 0) return;
    const auto h = hits[(size_t) i];                   // a copy: the action may repaint
    if (h.list && e.mods.isPopupMenu()) {               // right-click: the whole list
        const auto area = localAreaToGlobal(h.r.transformedBy(juce::AffineTransform::scale(scale())).getSmallestIntegerContainer());
        listmenu::show(h.list(), *this, area, h.choose);
        return;
    }
    if (h.step && e.mods.isShiftDown()) { h.step(true); return; }
    if (h.dragId.isNotEmpty()) { dragHit = i; dragY = e.position.y; dragV = host.value(h.dragId); host.edit(h.dragId, true); return; }
    if (h.click) h.click(design(e.position));
}

void TabPage::mouseDrag(const juce::MouseEvent& e)
{
    if (dragHit < 0 || dragHit >= (int) hits.size()) return;
    const float range = e.mods.isShiftDown() ? 1000.0f : 200.0f;          // screen pixels for full travel, like the panel knobs
    host.setValue(hits[(size_t) dragHit].dragId, juce::jlimit(0.0f, 1.0f, dragV + (dragY - e.position.y) / range)); repaint();
}

void TabPage::mouseUp(const juce::MouseEvent&)
{
    if (dragHit >= 0 && dragHit < (int) hits.size()) host.edit(hits[(size_t) dragHit].dragId, false);
    dragHit = -1;
}

void TabPage::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = hitAt(design(e.position));
    if (i >= 0 && hits[(size_t) i].dbl) { auto f = hits[(size_t) i].dbl; f(); }
}

void TabPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = hitAt(design(e.position));
    if (i < 0 || ! hits[(size_t) i].wheel) { Component::mouseWheelMove(e, w); return; }
    auto f = hits[(size_t) i].wheel; f(w.deltaY > 0 ? 1 : -1);
}

void TabPage::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(hitAt(design(e.position)) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

} // namespace bushido_ui
