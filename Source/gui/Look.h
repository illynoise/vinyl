// Vinyl Studio look: every control is drawn as vector graphics so it stays
// sharp at any window size and on HiDPI displays.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace vs
{
namespace col
{
    const juce::Colour panel       { 0xff8f9a92 };   // grey-green painted steel
    const juce::Colour panelDark   { 0xff6f7a73 };
    const juce::Colour panelLight  { 0xffa9b3ab };
    const juce::Colour ink         { 0xff14181a };
    const juce::Colour legend      { 0xfff2f3ee };
    const juce::Colour amber       { 0xffffa726 };
    const juce::Colour green       { 0xff58d66a };
    const juce::Colour red         { 0xffe5402e };
    const juce::Colour cream       { 0xfff3e7c3 };
    const juce::Colour bakelite    { 0xff17181a };
    const juce::Colour gold        { 0xffc9a24a };
    const juce::Colour bar         { 0xff1b1f1e };
}

enum class KnobStyle { chicken, skirted, gold, redCap, screw };

class VinylLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VinylLookAndFeel();

    static juce::Font font (float height, bool bold = true);
    static void drawKnob (juce::Graphics&, juce::Point<float> centre, float radius, float angle, KnobStyle);
    static void drawTicks (juce::Graphics&, juce::Point<float> centre, float radius, int count, bool bipolar);
    static void drawLamp (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool lit, bool down);
    static void drawToggle (juce::Graphics&, juce::Rectangle<float>, bool up);
    static void drawScrewHead (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
    static void drawPanel (juce::Graphics&, juce::Rectangle<float>, juce::uint32 seed);
    static void drawGroup (juce::Graphics&, juce::Rectangle<float>, const juce::String& title);
    static void drawLegend (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height, juce::Justification = juce::Justification::centred, bool dark = false);
    static void drawSevenSegment (juce::Graphics&, juce::Rectangle<float>, const juce::String& text);
    static void drawBadge (juce::Graphics&, juce::Rectangle<float>, const juce::Drawable* logo);

    // top bar widgets
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f); }
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f); }
    juce::Font getPopupMenuFont() override { return font (13.0f, false); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

std::unique_ptr<juce::Drawable> loadLogo (juce::Colour colour);
} // namespace vs
