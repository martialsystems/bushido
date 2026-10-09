#pragma once
// The editor's look: JUCE's standard look with two changes, so the panel and tabs draw exactly as before.
// - Menus (the right-click lists) use a plain sans font at a readable size, in the panel's colours.
// - Tooltips (help text shown larger after a 0.5 s hover, see TabPage::help) use a larger plain font and wrap.
#include <juce_gui_basics/juce_gui_basics.h>

class BushidoLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static constexpr float kMenuFont = 15.0f, kTipFont = 17.0f, kTipWidth = 520.0f;
    static juce::Font plainFont(float height) { return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), height, juce::Font::plain)); }

    BushidoLookAndFeel()
    {
        const juce::Colour bg(0xff1b1b1e), fg(0xffece8dc), gold(0xffc29f4c);
        setColour(juce::PopupMenu::backgroundColourId, bg);
        setColour(juce::PopupMenu::textColourId, fg);
        setColour(juce::PopupMenu::headerTextColourId, gold);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, gold);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff141414));
        setColour(juce::TooltipWindow::backgroundColourId, bg);
        setColour(juce::TooltipWindow::textColourId, fg);
        setColour(juce::TooltipWindow::outlineColourId, gold);
    }

    juce::Font getPopupMenuFont() override { return plainFont(kMenuFont); }

    juce::TextLayout tipLayout(const juce::String& tip)
    {
        juce::AttributedString s; s.setJustification(juce::Justification::centredLeft);
        s.append(tip, plainFont(kTipFont), findColour(juce::TooltipWindow::textColourId));
        juce::TextLayout t; t.createLayoutWithBalancedLineLengths(s, kTipWidth); return t;
    }

    juce::Rectangle<int> getTooltipBounds(const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area) override
    {
        const auto t = tipLayout(tip);
        const int w = (int) std::ceil(t.getWidth()) + 20, h = (int) std::ceil(t.getHeight()) + 12;
        return juce::Rectangle<int>(pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y > area.getCentreY() ? pos.y - (h + 8) : pos.y + 20, w, h)
                   .constrainedWithin(area);
    }

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        const juce::Rectangle<float> r(0, 0, (float) width, (float) height);
        g.setColour(findColour(juce::TooltipWindow::backgroundColourId)); g.fillRoundedRectangle(r, 4.0f);
        g.setColour(findColour(juce::TooltipWindow::outlineColourId)); g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        tipLayout(text).draw(g, r.reduced(10, 6));
    }
};
