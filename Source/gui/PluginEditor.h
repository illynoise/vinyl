// Vinyl Studio editor: preset bar, lathe scene, console faceplate.
#pragma once
#include "Controls.h"
#include "LatheScene.h"

class VinylStudioEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VinylStudioEditor (VinylStudioProcessor&);
    ~VinylStudioEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Design size at 100 %. */
    static juce::Point<int> baseSize (bool lite) { return lite ? juce::Point<int> (760, 330) : juce::Point<int> (1000, 860); }
    void setScalePercent (int percent);

private:
    class Content; class TopBar; class Console; class About;
    void timerCallback() override;

    VinylStudioProcessor& processor;
    vs::VinylLookAndFeel look;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltips { this, 600 };
    double lastTick = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VinylStudioEditor)
};
