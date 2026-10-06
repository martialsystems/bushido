#include "CableModel.h"
#include <cmath>
#include <algorithm>
#include <map>

namespace rack {

static const float kR = 40.0f, kRP = 28.0f, kPad = 10.0f, kPlugR = 12.0f;

void CableModel::setScene(std::vector<V2> j, std::vector<RectF> l, float f) { jacks = std::move(j); labels = std::move(l); floorY = f; relayout(); }

int CableModel::add(int a, int b, int color) { Cable c; c.a = a; c.b = b; c.color = color; list.push_back(c); relayout(); initRope(list.back()); for (int i = 0; i < 160; ++i) step(); return (int) list.size() - 1; }
void CableModel::remove(int i) { list.erase(list.begin() + i); if (carried == i) carried = -1; else if (carried > i) --carried; relayout(); }
void CableModel::clear() { list.clear(); carried = -1; }

std::vector<std::pair<int, int>> CableModel::plugsAt(int jack) const
{
    std::vector<std::pair<int, int>> o;
    for (int i = 0; i < (int) list.size(); ++i) { if (list[(size_t) i].a == jack) o.push_back({ i, 0 }); if (list[(size_t) i].b == jack) o.push_back({ i, 1 }); }
    return o;
}

// Stack order is the order of the cable list, so reordering one jack's stack = permuting those cables among their own slots.
void CableModel::reorderStack(int jack, const std::vector<int>& order)
{
    auto at = plugsAt(jack); std::vector<int> slots; for (auto& p : at) slots.push_back(p.first);
    std::sort(slots.begin(), slots.end());
    if (order.size() != slots.size()) return;
    auto next = list; for (size_t k = 0; k < slots.size(); ++k) next[(size_t) slots[k]] = list[(size_t) order[k]];
    if (carried >= 0) for (size_t k = 0; k < slots.size(); ++k) if (order[k] == carried) { carried = slots[k]; break; }
    list = std::move(next); relayout();
}

void CableModel::pickUp(int cable, int end)
{
    Cable& c = list[(size_t) cable]; carried = cable; carriedEnd = end; carriedNew = false;
    carriedFrom = end == 0 ? c.a : c.b; (end == 0 ? c.a : c.b) = -1; relayout();
}

void CableModel::newFrom(int jack, int color)
{
    Cable c; c.a = jack; c.b = -1; c.color = color; list.push_back(c);
    carried = (int) list.size() - 1; carriedEnd = 1; carriedFrom = -1; carriedNew = true; relayout(); initRope(list.back());
}

CableModel::Drop CableModel::drop(int t)
{
    Cable& c = list[(size_t) carried]; const int other = carriedEnd == 0 ? c.b : c.a; int& end = carriedEnd == 0 ? c.a : c.b;
    Drop r;
    if (t >= 0 && t != other) { end = t; r = Drop::Moved; }
    else if (t >= 0 && ! carriedNew) { end = carriedFrom; r = Drop::Returned; }
    else { const int i = carried; carried = -1; remove(i); return Drop::Removed; }
    carried = -1; relayout(); return r;
}

void CableModel::cancel()
{
    if (carried < 0) return;
    if (carriedNew) { const int i = carried; carried = -1; remove(i); return; }
    (carriedEnd == 0 ? list[(size_t) carried].a : list[(size_t) carried].b) = carriedFrom; carried = -1; relayout();
}

void CableModel::setPointer(bool inside, V2 p) { ptrIn = inside; ptr = p; if (carried >= 0) pins(list[(size_t) carried]); }

V2 CableModel::plugPos(int cable, int end) const
{
    const Cable& c = list[(size_t) cable]; const int j = end == 0 ? c.a : c.b; const int k = end == 0 ? c.ka : c.kb;
    if (j < 0) return ptr;
    return { jacks[(size_t) j].x - 2.0f * (float) k, jacks[(size_t) j].y - 3.0f * (float) k };
}
float CableModel::plugRadius(int cable, int end) const { const Cable& c = list[(size_t) cable]; return kPlugR * (1.0f + 0.04f * (float) (end == 0 ? c.ka : c.kb)); }

void CableModel::pins(Cable& c)
{
    const int ci = (int) (&c - list.data());
    const V2 A = plugPos(ci, 0), B = plugPos(ci, 1);
    c.pa = { A.x, A.y + 4 }; c.pb = { B.x, B.y + 4 };
    const float d = std::hypot(c.pb.x - c.pa.x, c.pb.y - c.pa.y);
    c.L = d + std::min(d * 0.3f + 40.0f, 140.0f);   // slack capped so long cables hang instead of piling on the floor
}

void CableModel::relayout()
{
    std::map<int, int> cnt;
    for (auto& c : list) {
        c.ka = c.a >= 0 ? cnt[c.a]++ : 0;
        c.kb = c.b >= 0 ? cnt[c.b]++ : 0;
    }
    for (auto& c : list) pins(c);
}

void CableModel::initRope(Cable& c)
{
    c.p.resize(N); for (int i = 0; i < N; ++i) { const float t = (float) i / (N - 1); c.p[(size_t) i] = { c.pa.x + (c.pb.x - c.pa.x) * t, c.pa.y + (c.pb.y - c.pa.y) * t + std::sin(3.14159265f * t) * 40.0f }; }
    c.q = c.p;
}

void CableModel::push(const Cable& c, V2& n) const
{
    auto repel = [&](float x, float y, float r) {
        const float dx = n.x - x, dy = n.y - y, d = std::hypot(dx, dy);
        if (d < r) { if (d > 0) { const float f = (r - d) / d * 0.6f; n.x += dx * f; n.y += dy * f; } else n.y += r * 0.6f; }
    };
    if (hoverJack >= 0 && hoverJack < (int) jacks.size()) repel(jacks[(size_t) hoverJack].x, jacks[(size_t) hoverJack].y, kR);
    if (ptrIn && ! (carried >= 0 && &list[(size_t) carried] == &c)) repel(ptr.x, ptr.y, kRP);
    if (hoverLabel >= 0 && hoverLabel < (int) labels.size()) {
        const RectF& r = labels[(size_t) hoverLabel];
        if (r.contains(n, kPad)) {
            const float l = n.x - (r.x - kPad), rr = r.x + r.w + kPad - n.x, t = n.y - (r.y - kPad), b = r.y + r.h + kPad - n.y, m = std::min({ l, rr, t, b });
            if (m == l) n.x -= l * 0.6f; else if (m == rr) n.x += rr * 0.6f; else if (m == t) n.y -= t * 0.6f; else n.y += b * 0.6f;
        }
    }
    if (n.y > floorY) n.y = floorY;
}

void CableModel::step()
{
    for (auto& c : list) {
        if (c.p.size() != (size_t) N) initRope(c);
        auto& p = c.p; auto& q = c.q; const float seg = c.L / (N - 1);
        for (int i = 1; i < N - 1; ++i) { const V2 v = p[(size_t) i]; p[(size_t) i] = { v.x + (v.x - q[(size_t) i].x) * 0.985f, v.y + (v.y - q[(size_t) i].y) * 0.985f + 0.45f }; q[(size_t) i] = v; }
        for (int it = 0; it < 10; ++it) {
            p[0] = c.pa; p[N - 1] = c.pb;
            for (int i = 0; i < N - 1; ++i) {
                V2& a = p[(size_t) i]; V2& b = p[(size_t) i + 1];
                const float dx = b.x - a.x, dy = b.y - a.y, d = std::max(1e-3f, std::hypot(dx, dy)), k = (d - seg) / d;
                const float wa = i ? 1.0f : 0.0f, wb = i < N - 2 ? 1.0f : 0.0f, w = wa + wb;
                a.x += dx * k * wa / w; a.y += dy * k * wa / w; b.x -= dx * k * wb / w; b.y -= dy * k * wb / w;
            }
            for (int i = 1; i < N - 1; ++i) push(c, p[(size_t) i]);
        }
    }
}

} // namespace rack
