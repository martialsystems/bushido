#pragma once
// Parsed panel layout file (assets/*_layout.json, written by panel/build_panel.py). Coordinates are design units.
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

struct PanelLayout {
    struct Control { juce::String id, kind; float cx = 0, cy = 0, r = 0, def = 0; int positions = 0; std::vector<float> angles; juce::Rectangle<float> hit; };
    struct Led     { juce::String id; float cx = 0, cy = 0, r = 0; };
    struct Jack    { juce::String id, dir; float x = 0, y = 0, r = 0; juce::Rectangle<float> hit; };
    juce::String rack;
    float width = 0, height = 0;
    juce::Rectangle<float> lane;
    std::vector<Control> controls;
    std::vector<Led> leds;
    std::vector<Jack> jacks;
    std::vector<juce::Rectangle<float>> labels;
    static PanelLayout fromJson(const juce::String& json);
};
