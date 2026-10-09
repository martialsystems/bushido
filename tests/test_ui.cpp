// The editor's UI rules, headless (JUCE, the real editor on a processor; no window, so no timers run):
// - the list rule (ui/ListMenu.h): every list control, its items, the ticked item, a choice, click and Shift-click
// - repaint: the panel's face image paints exactly what a direct paint does, and a change repaints only the area it touches
//   (every changed pixel is inside what was repainted), on the panel and on every tab page
// - timers: none run while the editor is not on screen
#include "../plugin/PluginEditor.h"
#if defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // parameter values are compared exactly on purpose
#endif
#include <cstdio>
#include <functional>

static int fails = 0, passes = 0;
static void CHECK(bool c, const juce::String& msg) { std::printf("%s %s\n", c ? "PASS" : "FAIL", msg.toRawUTF8()); if (c) ++passes; else ++fails; }

static juce::MouseEvent mouse(juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), p, mods, juce::MouseInputSource::defaultPressure, 0, 0, 0, 0, &c, &c, now, p, now, 1, false };
}

static juce::Image snap(juce::Component& c, float k = 1.0f) { return c.createComponentSnapshot(c.getLocalBounds(), true, k); }
static juce::RectangleList<int> inEditor(juce::Component& ed, juce::Component& c, const juce::RectangleList<int>& l)
{
    juce::RectangleList<int> o; for (auto& r : l) o.add(ed.getLocalArea(&c, r)); return o;
}

// pixels that differ; `outside` counts those not inside `allowed` (component pixels at scale k)
static int diff(const juce::Image& a, const juce::Image& b, const juce::RectangleList<int>* allowed = nullptr, int* outside = nullptr, float k = 1.0f)
{
    if (a.getBounds() != b.getBounds()) return -1;
    const juce::Image::BitmapData da(a, juce::Image::BitmapData::readOnly), db(b, juce::Image::BitmapData::readOnly);
    int n = 0; if (outside) *outside = 0;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (da.getPixelColour(x, y) != db.getPixelColour(x, y)) {
            ++n;
            if (allowed && outside && ! allowed->containsPoint(juce::Point<int>((int) std::floor((float) x / k), (int) std::floor((float) y / k)))) ++*outside;
        }
    return n;
}

// Writes <temp>/bbw/ui_<name>.png (the system temp folder, so it works on every platform) for looking at a failure.
static void dump(const juce::Image& im, const juce::String& name)
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bbw");
    dir.createDirectory();
    const auto f = dir.getChildFile("ui_" + name + ".png"); f.deleteFile();
    juce::FileOutputStream o(f); juce::PNGImageFormat().writeImageToStream(im, o);
}

struct Rig {
    BushidoProcessor proc;
    std::unique_ptr<BushidoEditor> ed;
    Rig() { proc.setRateAndBufferSizeDetails(48000.0, 512); proc.prepareToPlay(48000.0, 512);
            ed.reset(static_cast<BushidoEditor*>(proc.createEditor())); ed->setScalePercent(100); }
    ~Rig() { ed.reset(); }
    float v(const char* id) { return proc.parameter(id)->getValue(); }
    void set(const char* id, float x) { proc.parameter(id)->setValueNotifyingHost(x); }
};

static void testListMenu()
{
    listmenu::Choice c; c.items = { "A", "B", "C" }; c.ticked = 1; c.disabled = { 2 };
    const auto m = listmenu::build(c);
    int n = 0, ticked = -1, disabledId = -1;
    for (juce::PopupMenu::MenuItemIterator it(m); it.next();) { auto& i = it.getItem(); if (i.itemID > 0) { ++n; if (i.isTicked) ticked = i.itemID; if (! i.isEnabled) disabledId = i.itemID; } }
    CHECK(n == 3 && ticked == 2 && disabledId == 3, "menu: one item per entry, id = index + 1, the current one ticked, unavailable ones greyed");
    CHECK(listmenu::step(0, 3, false) == 1 && listmenu::step(2, 3, false) == 0 && listmenu::step(0, 3, true) == 2 && listmenu::step(1, 3, true) == 0, "click steps forward, Shift-click back, both wrap");
}

static void testPanelLists()
{
    Rig r; auto& p = r.ed->rackPanel();
    const char* ids[] = { "CH:RANGE A", "CH:RANGE B", "CH:C MODE", "CLOCK:SOURCE", "CLOCK:DIV", "MODE:MODE", "TOP:BYPASS" };
    int lists = 0;
    for (int i = 0; p.controlIndex(ids[0]) >= 0 && i < 64; ++i) if (p.isListControl(i)) ++lists;
    CHECK(lists == 7, "front panel: 7 list controls (2 RANGE rockers, C MODE, SOURCE, DIV, MODE, BYPASS), got " + juce::String(lists));
    for (auto id : ids) {
        const int i = p.controlIndex(id);
        CHECK(p.isListControl(i), juce::String(id) + " is a list control");
        const auto ch = p.listItems(i); const int n = ch.items.size();
        CHECK(n >= 2 && ch.ticked == (int) std::lround(r.v(id) * (float) (n - 1)), juce::String(id) + ": " + ch.items.joinIntoString(" / ") + ", current ticked");
        p.applyListChoice(i, n - 1);
        CHECK(p.listItems(i).ticked == n - 1 && r.v(id) == 1.0f, juce::String(id) + ": choosing the last item sets it");
        p.applyListChoice(i, 0);
        CHECK(r.v(id) == 0.0f, juce::String(id) + ": choosing the first item sets it");
    }
    CHECK(p.listItems(p.controlIndex("CLOCK:DIV")).items == juce::StringArray { "1/8", "1/16", "1/32" }, "DIV lists the panel's marks: 1/8, 1/16, 1/32");
    CHECK(p.listItems(p.controlIndex("MODE:MODE")).items == juce::StringArray { "A", "A+B", "ALT" }, "MODE lists A, A+B, ALT");
    CHECK(! p.isListControl(p.controlIndex("CLOCK:TEMPO")) && ! p.isListControl(p.controlIndex("A:1")), "knobs are not list controls");

    // click = next, Shift-click = previous, on the 3-way MODE switch (a rotary) and the C MODE bat toggle
    for (auto id : { "MODE:MODE", "CH:C MODE" }) {
        const int i = p.controlIndex(id); const int n = p.listItems(i).items.size();
        const auto at = p.controlArea(i).getCentre().toFloat();
        p.applyListChoice(i, 0);
        auto click = [&](bool shift) { const auto m = shift ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier) : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
                                       p.mouseDown(mouse(p, at, m)); p.mouseUp(mouse(p, at, shift ? juce::ModifierKeys(juce::ModifierKeys::shiftModifier) : juce::ModifierKeys())); };
        click(false); const int a = p.listItems(i).ticked;
        click(true);  const int b = p.listItems(i).ticked;
        click(true);  const int c = p.listItems(i).ticked;
        CHECK(a == 1 && b == 0 && c == n - 1, juce::String(id) + ": click 0 -> 1, Shift-click 1 -> 0, Shift-click wraps to " + juce::String(n - 1));
    }
    // a rocker keeps picking the half you press
    const int rk = p.controlIndex("CH:RANGE A"); const auto ra = p.controlArea(rk).toFloat();
    p.mouseDown(mouse(p, { ra.getRight() - 6, ra.getCentreY() }, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))); p.mouseUp(mouse(p, { ra.getRight() - 6, ra.getCentreY() }, {}));
    const float right = r.v("CH:RANGE A");
    p.mouseDown(mouse(p, { ra.getX() + 6, ra.getCentreY() }, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))); p.mouseUp(mouse(p, { ra.getX() + 6, ra.getCentreY() }, {}));
    CHECK(right == 1.0f && r.v("CH:RANGE A") == 0.0f, "RANGE rocker: the right half picks 5V, the left half 1V (unchanged)");
}

static void testTabLists()
{
    Rig r; auto& ed = *r.ed; auto& page = ed.tabPage();
    const std::map<int, juce::StringArray> expect {
        { bushido_ui::STEPS, { "STEPS:QUANT A", "STEPS:LAW A", "STEPS:QUANT B", "STEPS:LAW B", "CH:C MODE" } },
        { bushido_ui::CLOCK, { "CLOCK:EXT SOURCE", "CLOCK:DIV", "CLOCK:SETTLE", "CLOCK:TRIG MODE" } },
        { bushido_ui::MIDI,  { "MIDI:CH A", "MIDI:VEL A", "MIDI:CH B", "MIDI:VEL B" } },
        { bushido_ui::SETUP, { "UI SCALE" } } };
    for (auto& [tab, names] : expect) {
        ed.showTab(tab); page.layout();
        const auto got = page.listNames();
        CHECK(got == names, "tab " + juce::String(tab) + " list controls: " + got.joinIntoString(", "));
        for (int k = 0; k < got.size(); ++k) {
            const auto c = page.listItems(k);
            CHECK(c.items.size() >= 2 && c.ticked >= 0 && c.ticked < c.items.size(), "  " + got[k] + ": " + c.items.joinIntoString(" / ") + " (ticked " + juce::String(c.ticked) + ")");
        }
    }
    // a 2-way LCD: click and Shift-click both flip; a choice sets
    ed.showTab(bushido_ui::STEPS); page.layout();
    const int q = page.listNames().indexOf("STEPS:QUANT A");
    page.applyListChoice(q, 1); page.layout(); CHECK(r.v("STEPS:QUANT A") == 1.0f && page.listItems(q).ticked == 1, "QUANT A: choosing SEMI sets it, and it is ticked");
    page.stepList(q, false); page.layout(); CHECK(r.v("STEPS:QUANT A") == 0.0f, "QUANT A: click steps to OFF");
    page.stepList(q, true);  page.layout(); CHECK(r.v("STEPS:QUANT A") == 1.0f, "QUANT A: Shift-click steps back (wraps) to SEMI");
    // segments: a click picks the segment (unchanged); the menu picks any
    ed.showTab(bushido_ui::CLOCK); page.layout();
    const int dv = page.listNames().indexOf("CLOCK:DIV");
    page.applyListChoice(dv, 2); page.layout(); CHECK(page.listItems(dv).ticked == 2 && page.listItems(dv).items[2] == "1/32", "HOST DIV: choosing 1/32 sets it");
    // MIDI channel: 16 items; VELOCITY FROM C is greyed while C MODE = TIME
    ed.showTab(bushido_ui::MIDI); page.layout();
    CHECK(page.listItems(page.listNames().indexOf("MIDI:CH A")).items.size() == 16, "MIDI CH: 16 channels");
    page.applyListChoice(page.listNames().indexOf("MIDI:CH A"), 9); CHECK(std::lround(r.v("MIDI:CH A") * 15.0f) == 9, "MIDI CH A: choosing CH 10 sets it");
    r.set("CH:C MODE", 1.0f); page.layout();
    const int vel = page.listNames().indexOf("MIDI:VEL A");
    CHECK(page.listItems(vel).isDisabled(1), "VELOCITY: FROM C is greyed while C MODE is TIME");
    page.applyListChoice(vel, 1); CHECK(r.v("MIDI:VEL A") == 0.0f, "VELOCITY: a greyed item is not applied");
    r.set("CH:C MODE", 0.0f);
    // UI SCALE
    ed.showTab(bushido_ui::SETUP); page.layout();
    page.applyListChoice(page.listNames().indexOf("UI SCALE"), 2); CHECK(ed.getWidth() == 1600, "UI SCALE: choosing 125 % makes the editor 1600 wide");
    page.layout(); CHECK(page.listItems(page.listNames().indexOf("UI SCALE")).ticked == 2, "UI SCALE: 125 % is ticked");
}

static void testPatternList()
{
    Rig r; auto& s = r.ed->patternScreen();
    const auto c = s.listItems();
    const int nA = r.proc.patternNames(0).size(), nB = r.proc.patternNames(1).size();
    CHECK(c.items.size() == nA + nB && c.sections.size() == 2 && c.sectionStart.size() == 2 && c.sectionStart[1] == nA, "pattern list: bank A then bank B, a heading each (" + juce::String(nA) + " + " + juce::String(nB) + ")");
    CHECK(c.ticked == (r.proc.loadedBank() == 0 ? r.proc.loadedPattern() : nA + r.proc.loadedPattern()), "pattern list: the loaded pattern is ticked");
    if (nA >= 2) {
        s.applyListChoice(1);
        CHECK(r.proc.loadedBank() == 0 && r.proc.loadedPattern() == 1 && s.listItems().ticked == 1, "pattern list: choosing A002 loads it and ticks it");
    }
}

// The face image vs a direct paint, and what a change repaints.
static void testPanelRepaint()
{
    Rig r; auto& ed = *r.ed; auto& p = ed.rackPanel();
    for (float k : { 1.0f, 2.0f }) {
        p.setImageCache(false); const auto direct = snap(ed, k);
        p.setImageCache(true);  const auto cached = snap(ed, k);
        dump(direct, "direct" + juce::String(k)); dump(cached, "cached" + juce::String(k));
        CHECK(diff(direct, cached) == 0, "panel x" + juce::String(k) + ": the face image paints exactly what a direct paint does");
    }
    p.refresh(); p.takeInvalidated();
    struct Change { const char* what; std::function<void()> apply; };
    std::vector<Change> changes {
        { "a step knob", [&] { r.set("A:5", 0.73f); } },
        { "the TEMPO knob and BPM readout", [&] { r.set("CLOCK:TEMPO", 0.31f); } },
        { "the DIV switch (and the readout)", [&] { r.set("CLOCK:DIV", 1.0f); } },
        { "the MODE switch", [&] { r.set("MODE:MODE", r.v("MODE:MODE") > 0.75f ? 0.0f : 1.0f); } },
        { "a RANGE rocker", [&] { r.set("CH:RANGE B", r.v("CH:RANGE B") > 0.5f ? 0.0f : 1.0f); } },
        { "the C MODE toggle", [&] { r.set("CH:C MODE", 1.0f); } },
        { "SOURCE (the readout greys to EXT)", [&] { r.set("CLOCK:SOURCE", 1.0f); } },
        { "a pattern load (many parts)", [&] { r.proc.loadPattern(0, 2); } },
    };
    for (auto& c : changes) {
        p.setImageCache(false); const auto before = snap(ed); p.setImageCache(true); snap(ed);   // the image shows `before`
        c.apply(); p.refresh();
        auto dirty = inEditor(ed, p, p.takeInvalidated());
        const auto own = dirty.getBounds();
        dirty.add(ed.getLocalArea(&ed.patternScreen(), ed.patternScreen().getLocalBounds()));   // the pattern name is the screen's own
        p.setImageCache(false); const auto truth = snap(ed); p.setImageCache(true);
        int outside = 0; const int changed = diff(before, truth, &dirty, &outside);
        const auto cached = snap(ed);                   // the image, redrawn only inside `dirty`, must match a direct paint everywhere
        CHECK(changed > 0 && outside == 0, juce::String("panel: ") + c.what + ": " + juce::String(changed) + " px changed, " + juce::String(outside) + " outside the repainted area (" + juce::String(own.getWidth()) + "x" + juce::String(own.getHeight()) + " px)");
        if (diff(cached, truth) != 0) { dump(cached, "inc_cached"); dump(truth, "inc_truth"); }
        CHECK(diff(cached, truth) == 0, juce::String("panel: ") + c.what + ": the image matches a direct paint");
    }
    // incremental: keep one image across a run of changes (no rebuild), then compare with a direct paint
    p.setImageCache(true); snap(ed); p.refresh(); p.takeInvalidated();
    for (int i = 0; i < 12; ++i) { r.set(("A:" + juce::String(i + 1)).toRawUTF8(), (float) i / 11.0f); r.set("CLOCK:DIV", (float) (i % 3) / 2.0f); p.refresh(); snap(ed); }
    const auto inc = snap(ed); p.setImageCache(false); const auto truth = snap(ed); p.setImageCache(true);
    CHECK(diff(inc, truth) == 0, "panel: after 12 partial updates of one image, it matches a direct paint");
    // the LEDs: a STEP lamp lights while running
    p.setImageCache(false); const auto stopped = snap(ed); p.setImageCache(true); snap(ed); p.refresh(); p.takeInvalidated();
    r.proc.pressButton("MODE:START/STOP", true); r.proc.pressButton("MODE:START/STOP", false);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < 4; ++i) { b.clear(); r.proc.processBlock(b, m); } }
    p.refresh(); const auto dirty = inEditor(ed, p, p.takeInvalidated());
    p.setImageCache(false); const auto running = snap(ed); p.setImageCache(true);
    int outside = 0; const int changed = diff(stopped, running, &dirty, &outside);
    CHECK(outside == 0, "panel: START: " + juce::String(changed) + " px changed (lamps), all inside the repainted area");
    CHECK(diff(snap(ed), running) == 0, "panel: START: the image matches a direct paint");
}

static void testPageRepaint()
{
    Rig r; auto& ed = *r.ed; auto& page = ed.tabPage();
    struct Case { int tab; const char* what; std::function<void()> apply; };
    std::vector<Case> cases {
        { bushido_ui::STEPS, "a step value", [&] { r.set("B:7", 0.4f); } },
        { bushido_ui::STEPS, "QUANT A", [&] { r.set("STEPS:QUANT A", 1.0f); } },
        { bushido_ui::STEPS, "PORTA A (the tau line)", [&] { r.set("CH:PORTA A", 0.5f); } },
        { bushido_ui::STEPS, "the lit step (START)", [&] { r.proc.pressButton("MODE:START/STOP", true); r.proc.pressButton("MODE:START/STOP", false);
                                                         juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < 4; ++i) { b.clear(); r.proc.processBlock(b, m); } } },
        { bushido_ui::CLOCK, "HOST DIV", [&] { r.set("CLOCK:DIV", 0.0f); } },
        { bushido_ui::CLOCK, "TRIG MODE (and its line)", [&] { r.set("CLOCK:TRIG MODE", 1.0f); } },
        { bushido_ui::CLOCK, "EXT SOURCE HOST (LCD, caption)", [&] { r.set("CLOCK:EXT SOURCE", 1.0f); } },
        { bushido_ui::MIDI,  "MIDI CH B", [&] { r.set("MIDI:CH B", 0.6f); } },
        { bushido_ui::MIDI,  "C MODE TIME (VELOCITY greyed, warning)", [&] { r.set("CH:C MODE", 1.0f); } },
        { bushido_ui::MIDI,  "the monitor (step moves)", [&] { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < 40; ++i) { b.clear(); r.proc.processBlock(b, m); } } },
    };
    for (auto& c : cases) {
        ed.showTab(c.tab); page.refresh();
        const auto before = snap(page);
        c.apply();
        const auto dirty = page.refresh();
        const auto after = snap(page);
        int outside = 0; const int changed = diff(before, after, &dirty, &outside);
        const float frac = (float) dirty.getBounds().getWidth() * (float) dirty.getBounds().getHeight() / (float) (page.getWidth() * page.getHeight());
        CHECK(outside == 0, "tab " + juce::String(c.tab) + ": " + c.what + ": " + juce::String(changed) + " px changed, all inside the repainted area (" + juce::String(frac * 100.0f, 1) + " % of the page)");
    }
    ed.showTab(bushido_ui::STEPS); page.refresh();
    CHECK(page.refresh().isEmpty(), "tab: nothing changed, nothing repainted");
}

// Help text: never under the minimum, a tooltip each, inside its block, over nothing else, at 1366 wide.
static void testHelpText()
{
    Rig r; auto& ed = *r.ed; auto& page = ed.tabPage();
    ed.setSize(1366, BushidoEditor::stripHeight(1366) + BushidoEditor::panelHeight(1366));
    CHECK(page.getWidth() == 1366, "editor at 1366 wide (fits a 1366 x 768 screen: " + juce::String(ed.getWidth()) + " x " + juce::String(ed.getHeight()) + ")");
    using TP = bushido_ui::TabPage;
    int helps = 0, small = 0, plainNotHelp = 0, outside = 0, overlaps = 0, noTip = 0;
    for (int tab : { bushido_ui::STEPS, bushido_ui::CLOCK, bushido_ui::MIDI, bushido_ui::SETUP })
        for (int variant = 0; variant < 2; ++variant) {                   // both texts of the lines that change with a setting
            for (auto id : { "CLOCK:SOURCE", "CLOCK:SETTLE", "CLOCK:TRIG MODE", "CH:C MODE", "STEPS:LAW A", "STEPS:LAW B" }) r.set(id, (float) variant);
            ed.showTab(tab); page.layout();
            const auto& ms = page.marks();
            std::vector<juce::Rectangle<float>> blocks; for (auto& m : ms) if (m.key == "block") blocks.push_back(m.r);
            for (auto& m : ms) {
                if (m.size > 0 && ! m.bold && ! m.help) { ++plainNotHelp; std::printf("  plain text that is not help: %s\n", m.key.toRawUTF8()); }
                if (! m.help) continue;
                ++helps;
                if (m.size < TP::kHelpMin) { ++small; std::printf("  small: %s\n", m.tip.toRawUTF8()); }
                if (page.tooltipAt(m.r.getCentre()) != m.tip) ++noTip;
                const auto ink = m.r.reduced(3, 1);
                const juce::Rectangle<float> pageArea(0, 0, TP::kDesignW, TP::kDesignH);
                const juce::Rectangle<float>* in = nullptr;
                for (auto& b : blocks) if (b.contains(ink.getCentre()) && (in == nullptr || b.getWidth() < in->getWidth())) in = &b;
                const auto room = in ? in->reduced(4, 0) : pageArea.reduced(8, 0);
                if (! room.contains(ink)) { ++outside; std::printf("  overflows its block: %s (%.0f..%.0f in %.0f..%.0f)\n", m.tip.toRawUTF8(), ink.getX(), ink.getRight(), room.getX(), room.getRight()); }
                for (auto& o : ms) {
                    if (&o == &m || o.key == "block") continue;
                    if (o.r.reduced(3, 1).intersects(ink) && ! (o.key.startsWith("t") ? o.r.reduced(3, 1) : o.r).getIntersection(ink).isEmpty()) {
                        ++overlaps; std::printf("  overlaps: %s  <->  %s\n", m.tip.toRawUTF8(), o.key.substring(0, 60).toRawUTF8()); }
                }
            }
        }
    CHECK(helps > 40, juce::String(helps) + " help lines checked on 4 tabs (both texts of the switching lines)");
    CHECK(small == 0, "every help line is at least " + juce::String(TP::kHelpMin) + " design units (" + juce::String(TP::kHelpMin * 0.8f, 1) + " pt at 100 %)");
    CHECK(plainNotHelp == 0, "every plain (non-bold) text on the tabs is help text");
    CHECK(noTip == 0, "every help line has itself as its tooltip");
    CHECK(outside == 0, "no help line runs out of its block at 1366 wide");
    CHECK(overlaps == 0, "no help line overlaps another part at 1366 wide");
}

static void testFonts()
{
    Rig r; auto& ed = *r.ed;
    const auto mf = ed.lookAndFeel().getPopupMenuFont();
    CHECK(! mf.isBold() && mf.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && mf.getHeight() >= 15.0f, "menus: plain sans, " + juce::String(mf.getHeight()) + " px (" + mf.getTypefaceName() + ")");
    CHECK(PatternScreen::dotted(PatternScreen::Part::closedScreen), "pattern screen, closed: the dot-matrix display (as before)");
    CHECK(! PatternScreen::dotted(PatternScreen::Part::listRow) && ! PatternScreen::dotted(PatternScreen::Part::findField) && ! PatternScreen::dotted(PatternScreen::Part::nameField),
          "pattern list rows, FIND and the name field: plain font");
    const auto lf = PatternScreen::listFont(0.8f);
    CHECK(! lf.isBold() && lf.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && lf.getHeight() * 0.8f >= 12.0f, "pattern list font: plain sans, " + juce::String(lf.getHeight() * 0.8f) + " px at 100 %");
    CHECK(BushidoLookAndFeel::kTipFont > bushido_ui::TabPage::kHelpMin * 0.8f * 1.5f, "tooltips: " + juce::String(BushidoLookAndFeel::kTipFont) + " px, over 1.5 x the smallest help text on screen");
    CHECK(ed.tooltipWindow().getParentComponent() == &ed, "the editor has a tooltip window");
}

static void testTimers()
{
    Rig r; auto& ed = *r.ed;
    ed.refreshTimers();
    CHECK(! ed.isShowing() && ! ed.rackPanel().isLive() && ! ed.tabPage().isLive() && ! ed.patternScreen().isLive(), "an editor that is not on screen runs no panel, page or screen timer");
    ed.showTab(bushido_ui::CLOCK);
    CHECK(! ed.tabPage().isLive() && ! ed.rackPanel().isLive(), "switching tabs off screen starts nothing");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testListMenu();
    testPanelLists();
    testTabLists();
    testPatternList();
    testPanelRepaint();
    testPageRepaint();
    testTimers();
    testHelpText();
    testFonts();
    std::printf("\n%d PASS, %d FAIL\n%s\n", passes, fails, fails ? "SOME FAILED" : "ALL PASSED");
    return fails ? 1 : 0;
}
