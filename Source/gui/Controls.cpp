#include "Controls.h"

namespace vs
{
using namespace juce;

//============================================================================== ParamControl
ParamControl::ParamControl (VinylStudioProcessor& proc, const char* paramID)
    : processor (proc), param (*proc.apvts.getParameter (paramID)),
      attachment (param, [this] (float) { repaint(); }, &proc.undo)
{
    resetValue = param.convertFrom0to1 (param.getDefaultValue());
}

String ParamControl::getTooltip() { return param.getName (64) + ": " + valueText(); }
void ParamControl::beginGesture() { processor.undo.beginNewTransaction (param.getName (64)); attachment.beginGesture(); }
void ParamControl::setNorm (float norm, bool gesture)
{
    const float real = param.convertFrom0to1 (jlimit (0.0f, 1.0f, norm));
    if (gesture) attachment.setValueAsPartOfGesture (real); else attachment.setValueAsCompleteGesture (real);
}
void ParamControl::resetToDefault()
{
    processor.undo.beginNewTransaction (param.getName (64));
    attachment.setValueAsCompleteGesture (resetValue);
}

//============================================================================== Knob
Rectangle<int> Knob::boundsFor (float cx, float cy, float diameter)
{
    const float w = diameter + 34.0f, top = diameter * 0.5f + 12.0f, bottom = diameter * 0.5f + 26.0f;
    return Rectangle<float> (cx - w * 0.5f, cy - top, w, top + bottom).toNearestInt();
}

Knob::Knob (VinylStudioProcessor& p, const char* paramID, const String& l, KnobStyle s, float diameter)
    : ParamControl (p, paramID), label (l), style (s), dia (diameter)
{
    setBounds (0, 0, (int) diameter + 34, (int) diameter + 38);
    setRepaintsOnMouseActivity (false);
}

void Knob::paint (Graphics& g)
{
    const Point<float> c ((float) getWidth() * 0.5f, dia * 0.5f + 12.0f);
    const float r = dia * (positions.size() > 1 ? 0.36f : 0.5f), norm = param.getValue();
    const float a0 = -MathConstants<float>::pi * 0.75f, span = MathConstants<float>::pi * 1.5f;
    float angle = a0 + norm * span;

    if (positions.size() > 1)      // rotary selector: engraved position names instead of ticks
    {
        const int n = positions.size(); const float sel = MathConstants<float>::pi * 0.72f;
        angle = -sel * 0.5f + norm * sel;
        for (int i = 0; i < n; ++i)
        {
            const float a = -sel * 0.5f + sel * (float) i / (float) (n - 1), rr = r + 10.0f;
            const Point<float> p (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
            g.setColour (col::legend.withAlpha (0.8f)); g.fillEllipse (c.x + (r + 3.0f) * std::sin (a) - 1.2f, c.y - (r + 3.0f) * std::cos (a) - 1.2f, 2.4f, 2.4f);
            VinylLookAndFeel::drawLegend (g, positions[i], { p.x - 16.0f, p.y - 6.0f, 32.0f, 10.0f }, 7.5f);
        }
    }
    else if (style != KnobStyle::screw)
        VinylLookAndFeel::drawTicks (g, c, r, bipolar ? 11 : 11, bipolar);
    else
        angle = norm * MathConstants<float>::pi * 1.5f;

    VinylLookAndFeel::drawKnob (g, c, r, angle, style);

    // label, replaced by a small value readout while the knob is being moved
    Rectangle<float> lab (0.0f, c.y + dia * 0.5f + 9.0f, (float) getWidth(), 13.0f);
    if (dragging)
    {
        const String t = valueText();
        const float w = jmin ((float) getWidth(), GlyphArrangement::getStringWidth (VinylLookAndFeel::font (9.5f), t) + 10.0f);
        auto box = lab.withSizeKeepingCentre (w, 13.0f);
        g.setColour (col::ink.withAlpha (0.92f)); g.fillRoundedRectangle (box, 2.5f);
        g.setColour (col::amber); g.setFont (VinylLookAndFeel::font (9.5f)); g.drawText (t, box, Justification::centred, false);
    }
    else
        VinylLookAndFeel::drawLegend (g, label, lab, style == KnobStyle::screw ? 8.0f : 9.5f);
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    beginGesture(); dragging = true; dragNorm = param.getValue(); last = e.position; repaint();
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    // Shift-drag or Cmd/Ctrl-drag = fine adjustment
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    const float delta = (last.y - e.position.y) + (e.position.x - last.x) * 0.35f;
    last = e.position;
    dragNorm = jlimit (0.0f, 1.0f, dragNorm + delta / (positions.size() > 1 ? 70.0f : 170.0f) * (fine ? 0.1f : 1.0f));
    setNorm (dragNorm, true);
}

void Knob::mouseUp (const MouseEvent&)
{
    if (dragging) { attachment.endGesture(); dragging = false; repaint(); }
}

void Knob::mouseDoubleClick (const MouseEvent&) { resetToDefault(); }

void Knob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    const int steps = numSteps();
    float d = w.deltaY * (fine ? 0.02f : 0.2f);
    if (steps > 1 && steps < 20) d = (w.deltaY > 0 ? 1.0f : -1.0f) / (float) (steps - 1);
    if (std::abs (w.deltaY) > 0.0001f) setNorm (param.getValue() + d, false);
}

//============================================================================== LampButton
LampButton::LampButton (VinylStudioProcessor& p, const char* paramID, const String& l, Colour c, int choiceIndex)
    : ParamControl (p, paramID), label (l), colour (c), index (choiceIndex) {}

bool LampButton::isLit() const
{
    return index < 0 ? param.getValue() > 0.5f : roundToInt (value()) == index;
}

void LampButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto lab = b.removeFromBottom (13.0f);
    const float s = jmin (b.getWidth(), b.getHeight()) - 6.0f;
    auto lamp = b.withSizeKeepingCentre (s, s);
    VinylLookAndFeel::drawLamp (g, lamp, colour, isLit(), down);
    if (glyph == "stop")
    {
        g.setColour (isLit() ? Colours::white : Colours::white.withAlpha (0.45f));
        if (isLit()) { Path t; const auto c = lamp.getCentre(); t.addTriangle (c.x - s * 0.13f, c.y - s * 0.17f, c.x - s * 0.13f, c.y + s * 0.17f, c.x + s * 0.18f, c.y); g.fillPath (t); }
        else g.fillRect (lamp.withSizeKeepingCentre (s * 0.28f, s * 0.28f));
    }
    VinylLookAndFeel::drawLegend (g, label, lab, 9.0f);
}

void LampButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    down = true;
    processor.undo.beginNewTransaction (param.getName (64));
    if (index < 0) attachment.setValueAsCompleteGesture (param.getValue() > 0.5f ? 0.0f : 1.0f);
    else           attachment.setValueAsCompleteGesture ((float) index);
    repaint();
}

//============================================================================== ToggleSwitch
ToggleSwitch::ToggleSwitch (VinylStudioProcessor& p, const char* paramID, const String& u, const String& d, const String& t)
    : ParamControl (p, paramID), up (u), down (d), title (t) {}

void ToggleSwitch::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (title.isNotEmpty()) VinylLookAndFeel::drawLegend (g, title, b.removeFromTop (12.0f), 9.5f);
    VinylLookAndFeel::drawLegend (g, up, b.removeFromTop (11.0f), 8.5f);
    VinylLookAndFeel::drawLegend (g, down, b.removeFromBottom (11.0f), 8.5f);
    VinylLookAndFeel::drawToggle (g, b, param.getValue() > 0.5f);
}

void ToggleSwitch::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    processor.undo.beginNewTransaction (param.getName (64));
    attachment.setValueAsCompleteGesture (param.getValue() > 0.5f ? 0.0f : 1.0f);
}

//============================================================================== LedReadout
LedReadout::LedReadout (VinylStudioProcessor& p)
    : processor (p), length (*p.apvts.getParameter (id::slowLength)), beats (*p.apvts.getParameter (id::slowBeats))
{
    startTimerHz (15);
}

RangedAudioParameter& LedReadout::active() const { return processor.param (id::slowFormat) > 0.5f ? beats : length; }

String LedReadout::getTooltip() { return "Slow Down Length: " + active().getCurrentValueAsText() + " (drag to change)"; }

void LedReadout::timerCallback()
{
    String t;
    if (processor.param (id::slowFormat) > 0.5f)
    {
        static const char* names[] = { "1 bt", "2 bt", "1 bAr", "2 bAr", "4 bAr", "8 bAr" };
        t = names[jlimit (0, 5, (int) processor.param (id::slowBeats))];
    }
    else
    {
        const float s = processor.param (id::slowLength);
        t = s < 10.0f ? String (s, 3) : String (s, 2);
    }
    if (t != shown) { shown = t; repaint(); }
}

void LedReadout::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0a0b0b)); g.fillRoundedRectangle (b, 3.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2a0705), b.getX(), b.getY(), Colour (0xff120302), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b.reduced (2.0f), 2.0f);
    VinylLookAndFeel::drawSevenSegment (g, b.reduced (7.0f, 6.0f), shown);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), b.getX(), b.getY(), Colours::transparentWhite, b.getX(), b.getCentreY(), false));
    g.fillRoundedRectangle (b.reduced (2.0f).withHeight (b.getHeight() * 0.45f), 2.0f);
}

void LedReadout::mouseDown (const MouseEvent& e)
{
    processor.undo.beginNewTransaction ("Slow Down Length");
    active().beginChangeGesture(); dragNorm = active().getValue(); last = e.position;
}
void LedReadout::mouseDrag (const MouseEvent& e)
{
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    dragNorm = jlimit (0.0f, 1.0f, dragNorm + (last.y - e.position.y) / 170.0f * (fine ? 0.1f : 1.0f)); last = e.position;
    auto& p = active();
    p.setValueNotifyingHost (p.convertTo0to1 (p.convertFrom0to1 (dragNorm)));
}
void LedReadout::mouseUp (const MouseEvent&) { active().endChangeGesture(); }
void LedReadout::mouseDoubleClick (const MouseEvent&) { auto& p = active(); p.beginChangeGesture(); p.setValueNotifyingHost (p.getDefaultValue()); p.endChangeGesture(); }

//============================================================================== VuMeter
void VuMeter::setLevel (float level, float calDb)
{
    // 0 VU = a sine wave at -calDb dBFS
    const float vuDb = jlimit (-40.0f, 6.0f, Decibels::gainToDecibels (level, -90.0f) + calDb);
    if (std::abs (vuDb - needle) > 0.02f) { needle = vuDb; repaint(); }
}

void VuMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    // housing
    g.setColour (Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 6.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2b2e30), b.getX(), b.getY(), Colour (0xff0e0f10), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 6.0f);
    auto face = b.reduced (7.0f);
    // warm backlight on a cream scale
    g.setGradientFill (ColourGradient (Colour (0xfffff3c9), face.getCentreX(), face.getBottom(), Colour (0xffd9c48b), face.getX(), face.getY(), true));
    g.fillRoundedRectangle (face, 3.0f);
    g.setGradientFill (ColourGradient (Colour (0x66ffb347), face.getCentreX(), face.getBottom() + 10.0f, Colour (0x00ffb347), face.getCentreX(), face.getY() + face.getHeight() * 0.25f, true));
    g.fillRoundedRectangle (face, 3.0f);

    const float R = jmin (face.getHeight() * 0.98f, face.getWidth() * 0.60f);
    const Point<float> pivot (face.getCentreX(), face.getY() + R * 1.20f);
    const float half = 0.82f;                                      // needle sweep +-47 degrees
    auto angleFor = [half] (float vu)
    {
        // classic VU law: the scale is linear in voltage, 0 VU at ~71 % of the sweep
        const float v = std::pow (10.0f, vu / 20.0f);              // 1.0 at 0 VU
        const float t = jlimit (-0.03f, 1.04f, v * 0.708f);        // +3 VU = 1.0
        return -half + t * 2.0f * half;
    };
    auto pt = [&] (float a, float r) { return Point<float> (pivot.x + r * std::sin (a), pivot.y - r * std::cos (a)); };

    // red zone arc above 0 VU
    { Path arc; arc.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
      g.setColour (Colour (0xffc22a1c)); g.strokePath (arc, PathStrokeType (R * 0.045f)); }
    { Path arc; arc.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
      g.setColour (col::ink); g.strokePath (arc, PathStrokeType (1.2f)); }
    static const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    for (float m : marks)
    {
        const float a = angleFor (m);
        g.setColour (m > 0 ? Colour (0xffc22a1c) : col::ink);
        g.drawLine (Line<float> (pt (a, R * 0.90f), pt (a, R * 0.98f)), 1.3f);
        if (m == -20 || m == -10 || m == -5 || m == -3 || m == 0 || m == 3)
        {
            const auto p = pt (a, R * 1.06f);
            g.setFont (VinylLookAndFeel::font (R * 0.085f));
            g.drawText (String (std::abs ((int) m)), Rectangle<float> (p.x - 10.0f, p.y - 6.0f, 20.0f, 12.0f), Justification::centred, false);
        }
    }
    g.setColour (col::ink); g.setFont (VinylLookAndFeel::font (R * 0.13f));
    g.drawText ("VU", Rectangle<float> (face.getX(), pivot.y - R * 0.62f, face.getWidth(), R * 0.16f), Justification::centred, false);
    g.setFont (VinylLookAndFeel::font (R * 0.075f));
    g.drawText (name, face.reduced (6.0f, 4.0f), Justification::bottomLeft, false);
    g.drawText ("BASE JASE", face.reduced (6.0f, 4.0f), Justification::bottomRight, false);

    // needle (clipped to the face) with a soft shadow
    {
        Graphics::ScopedSaveState ss (g); g.reduceClipRegion (face.toNearestInt());
        const float a = angleFor (needle);
        g.setColour (Colours::black.withAlpha (0.18f)); g.drawLine (Line<float> (pt (a, R * 0.1f).translated (2.0f, 2.5f), pt (a, R * 0.95f).translated (2.0f, 2.5f)), 2.0f);
        g.setColour (col::ink); g.drawLine (Line<float> (pivot, pt (a, R * 0.95f)), 1.5f);
        g.setColour (Colour (0xff1a1c1d)); g.fillEllipse (pivot.x - R * 0.2f, pivot.y - R * 0.2f, R * 0.4f, R * 0.4f);
    }
    // glass
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), face.getX(), face.getY(), Colours::transparentWhite, face.getX() + face.getWidth() * 0.4f, face.getCentreY(), false));
    g.fillRoundedRectangle (face, 3.0f);
    g.setColour (Colours::black.withAlpha (0.6f)); g.drawRoundedRectangle (face, 3.0f, 1.0f);
}
} // namespace vs
