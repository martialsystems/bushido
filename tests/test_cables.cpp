// CableModel tests. Build: g++ -std=c++17 -O2 -I. tests/test_cables.cpp rack/CableModel.cpp
#include "../rack/CableModel.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
using namespace rack;
static int fails = 0;
#define CHECK(c, msg) do { bool _ok = (c); std::printf("%s %s\n", _ok ? "PASS" : "FAIL", msg); if (!_ok) ++fails; } while (0)
int main() {
    // four jacks in a row and one label under jack 1
    CableModel m; m.setScene({ { 100, 300 }, { 300, 300 }, { 500, 300 }, { 700, 300 } }, { { 285, 315, 30, 9 } }, 600);
    int c0 = m.add(0, 2, 0);
    CHECK(m.plugPos(c0, 0).x == 100 && m.plugPos(c0, 0).y == 300, "plug sits on its jack");
    int c1 = m.add(0, 3, 1);
    auto st = m.plugsAt(0);
    CHECK(st.size() == 2 && st[1].first == c1 && m.plugPos(c1, 0).x == 98 && m.plugPos(c1, 0).y == 297, "second plug stacks on top, offset 2 px left / 3 px up");
    m.reorderStack(0, { c1, c0 });
    st = m.plugsAt(0); CHECK(m.cables()[(size_t) st[1].first].color == 0 && m.cables()[(size_t) st[0].first].color == 1, "reorder puts the first cable back on top");
    // carry: move the top plug on jack 0 to jack 1
    m.pickUp(st[1].first, st[1].second); CHECK(m.carrying(), "picking up a plug starts carrying");
    CHECK(m.drop(1) == CableModel::Drop::Moved && m.plugsAt(1).size() == 1 && m.plugsAt(0).size() == 1, "dropping on another jack moves it");
    auto j1 = m.plugsAt(1)[0]; m.pickUp(j1.first, j1.second);
    CHECK(m.drop(-1) == CableModel::Drop::Removed && m.cables().size() == 1, "dropping on empty space unplugs it");
    auto j0 = m.plugsAt(0)[0]; m.pickUp(j0.first, j0.second);
    CHECK(m.drop(3) == CableModel::Drop::Returned && m.plugsAt(0).size() == 1, "dropping on its own other end puts it back");
    m.newFrom(1, 2); CHECK(m.drop(1) == CableModel::Drop::Removed, "a new cable dropped where it started is discarded");
    m.newFrom(1, 2); m.cancel(); CHECK(m.cables().size() == 1, "Esc while carrying a new cable discards it");
    // age: when a cable was patched (the graph delays only the newest cable of a feedback loop); stack order is separate
    { CableModel a; a.setScene({ { 100, 300 }, { 300, 300 }, { 500, 300 } }, {}, 600);
      int x = a.add(0, 1, 0), y = a.add(0, 2, 1);
      CHECK(a.cables()[(size_t) y].age > a.cables()[(size_t) x].age, "a later cable is newer");
      a.reorderStack(0, { y, x }); auto s0 = a.plugsAt(0);
      CHECK(a.cables()[(size_t) s0[1].first].color == 0 && a.cables()[(size_t) s0[1].first].age < a.cables()[(size_t) s0[0].first].age, "reordering the stack keeps each cable's age");
      int old = s0[1].first; a.pickUp(old, s0[1].second); CHECK(a.drop(2) == CableModel::Drop::Moved, "(setup) plug moved");
      int newest = 0; for (auto& c : a.cables()) newest = std::max(newest, c.age);
      CHECK(a.cables()[(size_t) old].age == newest, "moving a plug to another jack makes that cable the newest");
      CableModel b; b.setScene({ { 100, 300 }, { 300, 300 } }, {}, 600); b.add(0, 1, 0, 7);
      CHECK(b.cables()[0].age == 7, "a restored cable keeps its saved age"); }
    // physics
    CableModel h; h.setScene({ { 100, 300 }, { 700, 300 }, { 400, 300 } }, { { 385, 330, 30, 9 } }, 600);
    h.add(0, 1, 0); for (int i = 0; i < 300; ++i) h.step();
    auto nodes = [&] { return h.cables()[0].p; };
    float low = 0; for (auto& n : nodes()) low = std::max(low, n.y); CHECK(low > 380 && low <= 600, "cable hangs below the jacks, above the floor");
    bool coveredBefore = false; for (auto& n : nodes()) coveredBefore |= RectF{ 385, 330, 30, 9 }.contains(n, 6);
    h.setHoverLabel(0); for (int i = 0; i < 300; ++i) h.step();
    int inside = 0; auto p = nodes(); for (size_t i = 0; i + 1 < p.size(); ++i) { V2 mid{ (p[i].x + p[i + 1].x) / 2, (p[i].y + p[i + 1].y) / 2 }; inside += RectF{ 385, 330, 30, 9 }.contains(p[i]) + RectF{ 385, 330, 30, 9 }.contains(mid); }
    CHECK(inside == 0, "hovering a label clears every rope node and segment off it");
    h.setHoverLabel(-1); for (int i = 0; i < 300; ++i) h.step();
    V2 t = nodes()[CableModel::N / 2]; h.setPointer(true, t); for (int i = 0; i < 200; ++i) h.step();
    float md = 1e9f; for (auto& n : nodes()) md = std::min(md, std::hypot(n.x - t.x, n.y - t.y));
    CHECK(md > 18, "hovering near a cord pushes it away");
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails); return fails ? 1 : 0;
}
