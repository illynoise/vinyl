// The lathe scene: a cutting lathe on a desk, seen from above, under a desk lamp.
// Static artwork is rendered once into a cache at the display resolution (up to 4x);
// the record, the light reflection and the tone arm are separate animated layers.
#pragma once
#include "Look.h"
#include "../PluginProcessor.h"

namespace vs
{
class LatheScene : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LatheScene (VinylStudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    /** Called from the editor's timer; advances the platter and repaints only if something moved. */
    void tick (double seconds);

    static constexpr float W = 1000.0f, H = 330.0f;

private:
    void drawStatic (juce::Graphics&) const;
    void drawDisc (juce::Graphics&) const;
    void drawCarriage (juce::Graphics&) const;
    void drawToneArm (juce::Graphics&) const;
    bool onLeftOfRecord (juce::Point<float>) const;

    VinylStudioProcessor& processor;
    juce::Image cache; float cacheScale = 0; int cacheKey = -1;
    double angle = 0;
    float arm = 0, shownArm = -1; int generation = 0, turntable = 0, cartridge = 0, shownKey = -1;
    double shownAngle = -1;
    std::unique_ptr<juce::Drawable> logo;
    const juce::Point<float> centre { 470.0f, 172.0f }, pivot { 722.0f, 262.0f };
    static constexpr float discR = 128.0f;
};
} // namespace vs
