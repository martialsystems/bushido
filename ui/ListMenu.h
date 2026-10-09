#pragma once
// The suite-wide list rule, for every control that steps through a list on click (switches, selectors, the pattern picker):
//   click            the next item (wraps)
//   Shift-click      the previous item (wraps)
//   right-click      the whole list as a menu, the current item ticked
//   wheel            unchanged
// Each control offers two functions so the rule is testable without a mouse: one builds the items and the ticked index
// (listmenu::Choice), one applies a choice by index. This header turns a Choice into the menu.
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace listmenu {

struct Choice {
    juce::StringArray items;                 // in list order
    int ticked = -1;                         // the current item, -1 = none
    juce::StringArray sections;              // optional headings: section s starts at item sectionStart[s]
    std::vector<int> sectionStart;
    std::vector<int> disabled;               // items shown greyed (not available in the current state)
    bool isDisabled(int i) const { return std::find(disabled.begin(), disabled.end(), i) != disabled.end(); }
};

// Menu item ids are index + 1 (0 means dismissed). A section heading is a non-selectable header item.
inline juce::PopupMenu build(const Choice& c)
{
    juce::PopupMenu m;
    for (int i = 0; i < c.items.size(); ++i) {
        for (size_t s = 0; s < c.sectionStart.size(); ++s)
            if (c.sectionStart[s] == i && (int) s < c.sections.size()) { if (i > 0) m.addSeparator(); m.addSectionHeader(c.sections[(int) s]); }
        m.addItem(i + 1, c.items[i], ! c.isDisabled(i), i == c.ticked);
    }
    return m;
}

// Click and Shift-click: one step forward or back, wrapping.
inline int step(int current, int count, bool back) { return count <= 0 ? -1 : ((current + (back ? -1 : 1)) % count + count) % count; }

// Show the menu for a control at `area` (screen coordinates) and apply the chosen index.
inline void show(const Choice& c, juce::Component& owner, juce::Rectangle<int> screenArea, std::function<void(int)> apply)
{
    build(c).showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner).withTargetScreenArea(screenArea).withStandardItemHeight(22),
                           [sp = juce::Component::SafePointer<juce::Component>(&owner), apply](int r) { if (sp != nullptr && r > 0) apply(r - 1); });
}

} // namespace listmenu
