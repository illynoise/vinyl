// Parameter-bound controls: knobs, lamp buttons, toggles, LED readout, VU meter.
#pragma once
#include "Look.h"
#include "../PluginProcessor.h"

namespace vs
{
/** Shared plumbing: attachment, tooltip with the exact value, double-click reset. */
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (VinylStudioProcessor& proc, const char* paramID);
    juce::String getTooltip() override;
    void setResetValue (float realValue) { resetValue = realValue; hasReset = true; }
    float value() const { return param.convertFrom0to1 (param.getValue()); }
    juce::String valueText() const { return param.getCurrentValueAsText(); }
    bool isChoice() const { return dynamic_cast<juce::AudioParameterChoice*> (&param) != nullptr; }
    int numSteps() const { return param.getNumSteps(); }

protected:
    void beginGesture();
    void setNorm (float norm, bool gesture);
    void resetToDefault();
    VinylStudioProcessor& processor;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float resetValue = 0; bool hasReset = false;
};

class Knob : public ParamControl
{
public:
    Knob (VinylStudioProcessor&, const char* paramID, const juce::String& label, KnobStyle, float diameter);
    void setBipolar (bool b) { bipolar = b; }
    void setPositions (juce::StringArray names) { positions = std::move (names); }     // rotary selector
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    static juce::Rectangle<int> boundsFor (float cx, float cy, float diameter);

private:
    juce::String label; KnobStyle style; float dia; bool bipolar = false, dragging = false;
    float dragNorm = 0; juce::Point<float> last;
    juce::StringArray positions;
};

/** Square illuminated push button: toggles a bool, or selects one value of a choice. */
class LampButton : public ParamControl
{
public:
    LampButton (VinylStudioProcessor&, const char* paramID, const juce::String& label, juce::Colour, int choiceIndex = -1);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    bool isLit() const;
    juce::String glyph;                       // optional symbol drawn on the cap ("stop")
private:
    juce::String label; juce::Colour colour; int index; bool down = false;
};

/** Two-position metal toggle switch. */
class ToggleSwitch : public ParamControl
{
public:
    ToggleSwitch (VinylStudioProcessor&, const char* paramID, const juce::String& upLabel, const juce::String& downLabel, const juce::String& title);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::String up, down, title;
};

/** Red LED segment readout for the Slow Down length (drag to change). */
class LedReadout : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LedReadout (VinylStudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
private:
    void timerCallback() override;
    juce::RangedAudioParameter& active() const;
    VinylStudioProcessor& processor;
    juce::RangedAudioParameter& length, &beats;
    float dragNorm = 0; juce::Point<float> last; juce::String shown;
};

/** Analog VU meter: cream face, black needle, warm backlight. */
class VuMeter : public juce::Component
{
public:
    explicit VuMeter (const juce::String& channelName) : name (channelName) {}
    void setLevel (float sineEquivalentPeak, float calibrationDb);
    void paint (juce::Graphics&) override;
private:
    juce::String name; float needle = -40.0f;     // VU units (dB re 0 VU)
};

/** Manufacturer's plate; click opens the About screen. */
class Badge : public juce::Component, public juce::SettableTooltipClient
{
public:
    Badge() : logo (loadLogo (col::cream)) { setTooltip ("Base Jase Plugins - click for About"); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { VinylLookAndFeel::drawBadge (g, getLocalBounds().toFloat().reduced (1.0f), logo.get()); }
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;
private:
    std::unique_ptr<juce::Drawable> logo;
};
} // namespace vs
