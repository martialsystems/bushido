#include "Layout.h"

static juce::Rectangle<float> rectOf(const juce::var& v) { return { (float) v[0], (float) v[1], (float) v[2], (float) v[3] }; }

PanelLayout PanelLayout::fromJson(const juce::String& json)
{
    PanelLayout L; const juce::var d = juce::JSON::parse(json);
    L.rack = d["rack"].toString(); L.width = (float) d["canvas"][0]; L.height = (float) d["canvas"][1]; L.lane = rectOf(d["lane"]);
    for (auto& c : *d["controls"].getArray()) {
        Control k; k.id = c["id"].toString(); k.kind = c["kind"].toString(); k.style = c["style"].toString(); k.cx = (float) c["cx"]; k.cy = (float) c["cy"]; k.r = (float) c["r"];
        k.def = (float) c["default"]; k.positions = c.hasProperty("positions") ? (int) c["positions"] : 0; k.hit = rectOf(c["hit"]);
        k.param = c["param"].toString(); k.chars = (int) c["chars"]; if (c.hasProperty("lcd")) k.lcd = rectOf(c["lcd"]);
        k.tone = c["tone"].toString(); k.rect = c.hasProperty("rect") ? rectOf(c["rect"]) : juce::Rectangle<float>(k.cx - 17, k.cy - 9, 34, 18);
        if (auto* a = c["angles"].getArray()) for (auto& x : *a) k.angles.push_back((float) x);
        if (auto* m = c["marks"].getArray()) for (auto& x : *m) k.marks.add(x.toString());
        L.controls.push_back(k);
    }
    for (auto& l : *d["leds"].getArray())  L.leds.push_back({ l["id"].toString(), (float) l["cx"], (float) l["cy"], (float) l["r"] });
    for (auto& j : *d["jacks"].getArray()) L.jacks.push_back({ j["id"].toString(), j["dir"].toString(), (float) j["x"], (float) j["y"], (float) j["radius"], rectOf(j["hit"]) });
    if (auto s = d["screen"]; s.isObject()) { L.screen.bezel = rectOf(s["bezel"]); L.screen.lcd = rectOf(s["lcd"]); L.screen.button = rectOf(s["button"]); L.screen.chars = (int) s["chars"];
        L.screen.save = rectOf(s["save"]); L.screen.listRows = (int) s["listRows"]; L.screen.bankSize = (int) s["bankSize"];
        if (auto* b = s["banks"].getArray()) for (auto& k : *b) L.screen.banks.push_back({ k["id"].toString(), (float) k["cx"], (float) k["cy"], (float) k["r"], rectOf(k["hit"]) }); }
    for (auto& t : *d["labels"].getArray()) L.labels.push_back(rectOf(t["rect"]));
    return L;
}
