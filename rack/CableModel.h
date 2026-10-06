#pragma once
// Cables as the user sees them: which jacks they join, how plugs stack, and how the ropes hang and move aside.
// Framework-free so it can be unit-tested; the JUCE CableLayer only draws it and feeds it mouse input.
// All coordinates are in one shared "design" space covering every rack in the editor.
#include <vector>
#include <utility>

namespace rack {

struct V2 { float x = 0, y = 0; };
struct RectF { float x = 0, y = 0, w = 0, h = 0; bool contains(V2 p, float pad = 0) const { return p.x > x - pad && p.x < x + w + pad && p.y > y - pad && p.y < y + h + pad; } };

class CableModel {
public:
    static constexpr int N = 36;                   // rope nodes per cable
    struct Cable { int a = -1, b = -1, color = 0, age = 0; std::vector<V2> p, q; V2 pa, pb; float L = 0; int ka = 0, kb = 0; };
    enum class Drop { Moved, Returned, Removed };

    void setScene(std::vector<V2> jackPositions, std::vector<RectF> labelRects, float floorY);

    // age orders cables by when they were patched (larger = newer); the patch graph delays only the newest cable of a
    // feedback loop. Stack order is separate and visual only. -1 = newest now.
    int  add(int jackA, int jackB, int color, int age = -1);     // returns cable index
    void remove(int cable);
    void clear();
    std::vector<std::pair<int, int>> plugsAt(int jack) const;            // (cable, end 0=a/1=b), bottom of stack first
    void reorderStack(int jack, const std::vector<int>& cablesBottomToTop);

    void pickUp(int cable, int end);               // that plug leaves its jack and follows the pointer
    void newFrom(int jack, int color);             // new cable from a jack, free end follows the pointer
    bool carrying() const { return carried >= 0; }
    int  carriedCable() const { return carried; }
    Drop drop(int jackUnderPointer);               // -1 = empty space
    void cancel();

    void setPointer(bool inside, V2 p);
    void setHoverJack(int jack) { hoverJack = jack; }
    void setHoverLabel(int label) { hoverLabel = label; }
    void step();                                   // one physics step (call ~60x per second)

    const std::vector<Cable>& cables() const { return list; }
    V2    plugPos(int cable, int end) const;       // where the plug is drawn
    float plugRadius(int cable, int end) const;

private:
    std::vector<V2> jacks; std::vector<RectF> labels; float floorY = 1e9f;
    std::vector<Cable> list;
    int nextAge = 0;
    int carried = -1, carriedEnd = 0, carriedFrom = -1; bool carriedNew = false;
    bool ptrIn = false; V2 ptr; int hoverJack = -1, hoverLabel = -1;
    void relayout();                               // stack levels, pins, rope length
    void pins(Cable& c);
    void initRope(Cable& c);
    void push(const Cable& c, V2& n) const;
};

} // namespace rack
