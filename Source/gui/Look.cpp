#include "Look.h"
#include "BinaryData.h"

namespace vs
{
using namespace juce;

VinylLookAndFeel::VinylLookAndFeel()
{
    setColour (ComboBox::backgroundColourId, Colour (0xff2a302e));
    setColour (ComboBox::textColourId, col::legend);
    setColour (ComboBox::outlineColourId, Colour (0xff4a5350));
    setColour (ComboBox::arrowColourId, col::amber);
    setColour (PopupMenu::backgroundColourId, Colour (0xff232827));
    setColour (PopupMenu::textColourId, col::legend);
    setColour (PopupMenu::highlightedBackgroundColourId, col::amber);
    setColour (PopupMenu::highlightedTextColourId, col::ink);
    setColour (PopupMenu::headerTextColourId, col::amber);
    setColour (TextButton::textColourOffId, col::legend);
    setColour (TextButton::textColourOnId, col::ink);
    setColour (TextEditor::backgroundColourId, Colour (0xff101312));
    setColour (TextEditor::textColourId, col::legend);
    setColour (TextEditor::outlineColourId, col::amber);
    setColour (TextEditor::focusedOutlineColourId, col::amber);
    setColour (TooltipWindow::backgroundColourId, Colour (0xff101312));
    setColour (TooltipWindow::textColourId, col::legend);
    setColour (TooltipWindow::outlineColourId, col::amber.withAlpha (0.6f));
}

Font VinylLookAndFeel::font (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

std::unique_ptr<Drawable> loadLogo (Colour colour)
{
    auto d = Drawable::createFromImageData (BinaryData::basejase_logo_svg, BinaryData::basejase_logo_svgSize);
    if (d != nullptr) d->replaceColour (Colours::black, colour);
    return d;
}

//------------------------------------------------------------------------------ knobs
void VinylLookAndFeel::drawTicks (Graphics& g, Point<float> c, float r, int count, bool bipolar)
{
    const float a0 = -MathConstants<float>::pi * 0.75f, a1 = MathConstants<float>::pi * 0.75f;
    for (int i = 0; i < count; ++i)
    {
        const float t = (float) i / (float) (count - 1), a = a0 + t * (a1 - a0);
        const bool major = i == 0 || i == count - 1 || (bipolar && i == count / 2) || (! bipolar && i % 5 == 0);
        const float r0 = r + 3.5f, r1 = r + (major ? 8.5f : 6.5f);
        g.setColour (col::legend.withAlpha (major ? 0.95f : 0.6f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), major ? 1.4f : 1.0f);
    }
}

void VinylLookAndFeel::drawScrewHead (Graphics& g, Point<float> c, float r, float angle)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - r, c.y - r + 1.0f, 2 * r, 2 * r);
    g.setGradientFill (ColourGradient (Colour (0xffe2e4e0), c.x - r * 0.5f, c.y - r * 0.6f, Colour (0xff767c78), c.x + r, c.y + r, true));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colour (0xff3a3e3c));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.7f);
    Path slot; slot.addRectangle (-r * 0.85f, -r * 0.14f, r * 1.7f, r * 0.28f);
    g.setColour (Colour (0xff24272a));
    g.fillPath (slot, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void VinylLookAndFeel::drawKnob (Graphics& g, Point<float> c, float r, float angle, KnobStyle style)
{
    if (style == KnobStyle::screw) { drawScrewHead (g, c, r, angle); return; }
    const auto rot = AffineTransform::rotation (angle).translated (c.x, c.y);

    // cast shadow on the panel
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.45f), c.x + r * 0.12f, c.y + r * 0.2f, Colours::transparentBlack, c.x + r * 0.12f, c.y + r * 1.45f, true));
    g.fillEllipse (c.x - r * 1.3f, c.y - r * 1.1f, r * 2.6f, r * 2.6f);

    if (style == KnobStyle::gold)
    {
        // knurled brass
        g.setGradientFill (ColourGradient (Colour (0xfff0d58a), c.x - r * 0.4f, c.y - r * 0.7f, Colour (0xff7a5c1c), c.x + r * 0.6f, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colour (0xff4a3810).withAlpha (0.75f));
        for (int i = 0; i < 44; ++i)
        {
            const float a = angle + (float) i * MathConstants<float>::twoPi / 44.0f;
            g.drawLine (c.x + r * 0.80f * std::sin (a), c.y - r * 0.80f * std::cos (a), c.x + r * 0.99f * std::sin (a), c.y - r * 0.99f * std::cos (a), 1.0f);
        }
        g.setGradientFill (ColourGradient (Colour (0xfff6e3a6), c.x - r * 0.3f, c.y - r * 0.5f, Colour (0xff9c7a2c), c.x + r * 0.5f, c.y + r * 0.7f, true));
        g.fillEllipse (c.x - r * 0.76f, c.y - r * 0.76f, r * 1.52f, r * 1.52f);
        g.setColour (Colour (0xff5a4414)); g.drawEllipse (c.x - r * 0.76f, c.y - r * 0.76f, r * 1.52f, r * 1.52f, 0.8f);
        Path p; p.addRoundedRectangle (-r * 0.05f, -r * 0.74f, r * 0.10f, r * 0.42f, r * 0.04f);
        g.setColour (col::ink); g.fillPath (p, rot);
        return;
    }

    if (style == KnobStyle::chicken)
    {
        // round bakelite base with a tapered pointer
        g.setGradientFill (ColourGradient (Colour (0xff3a3d40), c.x - r * 0.3f, c.y - r * 0.5f, col::bakelite, c.x + r * 0.4f, c.y + r * 0.7f, true));
        g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
        Path p;
        p.startNewSubPath (-r * 0.30f, r * 0.50f); p.lineTo (-r * 0.13f, -r * 0.98f); p.lineTo (r * 0.13f, -r * 0.98f); p.lineTo (r * 0.30f, r * 0.50f);
        p.closeSubPath();
        p = p.createPathWithRoundedCorners (r * 0.12f);
        g.setGradientFill (ColourGradient (Colour (0xff46494c), c.x - r * 0.3f, c.y - r * 0.4f, Colour (0xff0c0d0e), c.x + r * 0.4f, c.y + r * 0.5f, false));
        g.fillPath (p, rot);
        g.setColour (Colours::white.withAlpha (0.12f)); g.strokePath (p, PathStrokeType (0.8f), rot);
        Path line; line.addRoundedRectangle (-r * 0.035f, -r * 0.93f, r * 0.07f, r * 0.72f, r * 0.03f);
        g.setColour (col::legend); g.fillPath (line, rot);
        return;
    }

    // skirted knob (black, or with a red cap)
    g.setGradientFill (ColourGradient (Colour (0xff35383b), c.x - r * 0.4f, c.y - r * 0.7f, Colour (0xff0b0c0d), c.x + r * 0.5f, c.y + r * 0.9f, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colours::white.withAlpha (0.10f)); g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, 2 * r - 1, 2 * r - 1, 1.0f);
    Path idx; idx.addRoundedRectangle (-r * 0.03f, -r * 0.97f, r * 0.06f, r * 0.26f, r * 0.02f);
    g.setColour (col::legend); g.fillPath (idx, rot);
    const float cr = r * 0.68f;
    const bool redCap = style == KnobStyle::redCap;
    g.setGradientFill (ColourGradient (redCap ? Colour (0xffff7a66) : Colour (0xff4c5054), c.x - cr * 0.4f, c.y - cr * 0.6f,
                                       redCap ? Colour (0xff8e1c10) : Colour (0xff121315), c.x + cr * 0.6f, c.y + cr * 0.9f, true));
    g.fillEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr);
    g.setColour (Colours::black.withAlpha (0.5f)); g.drawEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr, 0.8f);
    Path line; line.addRoundedRectangle (-r * 0.04f, -cr * 0.95f, r * 0.08f, cr * 0.7f, r * 0.03f);
    g.setColour (redCap ? Colours::white : col::legend); g.fillPath (line, rot);
}

//------------------------------------------------------------------------------ buttons and switches
void VinylLookAndFeel::drawLamp (Graphics& g, Rectangle<float> b, Colour c, bool lit, bool down)
{
    if (lit)   // soft glow on the panel
    {
        const auto gb = b.expanded (b.getWidth() * 0.45f);
        g.setGradientFill (ColourGradient (c.withAlpha (0.42f), gb.getCentreX(), gb.getCentreY(), c.withAlpha (0.0f), gb.getX(), gb.getCentreY(), true));
        g.fillEllipse (gb);
    }
    g.setColour (Colour (0xff101213)); g.fillRoundedRectangle (b, 3.0f);                      // bezel
    auto cap = b.reduced (b.getWidth() * 0.11f).translated (0.0f, down ? 0.8f : 0.0f);
    const Colour top = lit ? c.brighter (0.75f) : c.withSaturation (0.35f).withBrightness (0.30f);
    const Colour bot = lit ? c.darker (0.15f)   : c.withSaturation (0.35f).withBrightness (0.17f);
    g.setGradientFill (ColourGradient (top, cap.getCentreX(), cap.getY(), bot, cap.getCentreX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.2f);
    if (lit)
    {
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), cap.getCentreX(), cap.getCentreY(), Colours::white.withAlpha (0.0f), cap.getX(), cap.getY(), true));
        g.fillRoundedRectangle (cap, 2.2f);
    }
    g.setColour (Colours::white.withAlpha (lit ? 0.35f : 0.10f));
    g.drawRoundedRectangle (cap.reduced (0.6f), 2.0f, 0.8f);
}

void VinylLookAndFeel::drawToggle (Graphics& g, Rectangle<float> b, bool up)
{
    const auto c = b.getCentre(); const float r = jmin (b.getWidth(), b.getHeight()) * 0.21f;
    // hex nut + collar
    Path nut; nut.addPolygon (c, 6, r * 1.25f, MathConstants<float>::pi / 6.0f);
    g.setGradientFill (ColourGradient (Colour (0xffd9dcd8), c.x - r, c.y - r, Colour (0xff6c726e), c.x + r, c.y + r, false));
    g.fillPath (nut); g.setColour (Colour (0xff3a3e3c)); g.strokePath (nut, PathStrokeType (0.7f));
    g.setColour (Colour (0xff1b1d1e)); g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
    // lever (bat handle), seen slightly from above
    const float dir = up ? -1.0f : 1.0f, len = r * 2.1f;
    Path lever;
    lever.startNewSubPath (c.x - r * 0.28f, c.y); lever.lineTo (c.x - r * 0.42f, c.y + dir * len);
    lever.lineTo (c.x + r * 0.42f, c.y + dir * len); lever.lineTo (c.x + r * 0.28f, c.y); lever.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.3f)); g.fillPath (lever, AffineTransform::translation (1.5f, 2.0f));
    g.setGradientFill (ColourGradient (Colour (0xfff4f5f2), c.x - r * 0.4f, c.y, Colour (0xff8b918d), c.x + r * 0.45f, c.y, false));
    g.fillPath (lever);
    g.setGradientFill (ColourGradient (Colour (0xffffffff), c.x - r * 0.2f, c.y + dir * len - r * 0.2f, Colour (0xff8d938f), c.x + r * 0.5f, c.y + dir * len + r * 0.5f, true));
    g.fillEllipse (c.x - r * 0.46f, c.y + dir * len - r * 0.46f, r * 0.92f, r * 0.92f);
}

//------------------------------------------------------------------------------ panel
void VinylLookAndFeel::drawLegend (Graphics& g, const String& text, Rectangle<float> area, float height, Justification j, bool dark)
{
    g.setFont (font (height));
    g.setColour (dark ? col::ink : col::legend);
    g.drawText (text, area, j, false);
}

void VinylLookAndFeel::drawGroup (Graphics& g, Rectangle<float> b, const String& title)
{
    // engraved outline with the title breaking the top edge
    const float tw = title.isEmpty() ? 0.0f : GlyphArrangement::getStringWidth (font (9.5f), title) + 12.0f;
    Path p;
    const float x0 = b.getX(), y0 = b.getY(), x1 = b.getRight(), y1 = b.getBottom(), cx = b.getCentreX(), rr = 5.0f;
    p.startNewSubPath (cx - tw * 0.5f, y0); p.lineTo (x0 + rr, y0); p.quadraticTo (x0, y0, x0, y0 + rr); p.lineTo (x0, y1 - rr); p.quadraticTo (x0, y1, x0 + rr, y1);
    p.lineTo (x1 - rr, y1); p.quadraticTo (x1, y1, x1, y1 - rr); p.lineTo (x1, y0 + rr); p.quadraticTo (x1, y0, x1 - rr, y0); p.lineTo (cx + tw * 0.5f, y0);
    g.setColour (Colours::white.withAlpha (0.16f)); g.strokePath (p, PathStrokeType (1.0f), AffineTransform::translation (0.0f, 1.0f));
    g.setColour (Colours::black.withAlpha (0.42f)); g.strokePath (p, PathStrokeType (1.0f));
    if (title.isNotEmpty()) drawLegend (g, title, { cx - tw * 0.5f, y0 - 7.0f, tw, 14.0f }, 9.5f, Justification::centred, true);
}

void VinylLookAndFeel::drawPanel (Graphics& g, Rectangle<float> b, uint32 seed)
{
    // raised bezel
    g.setGradientFill (ColourGradient (Colour (0xff59615c), b.getX(), b.getY(), Colour (0xff2c312f), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 8.0f);
    auto face = b.reduced (7.0f);
    g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (face.expanded (1.2f), 5.0f);
    g.setGradientFill (ColourGradient (col::panelLight, face.getX(), face.getY(), col::panelDark, face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 4.0f);

    // paint wear: faint speckle, scuffs near the edges, a few fine scratches
    Random rnd ((int64) seed);
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addRoundedRectangle (face, 4.0f); g.reduceClipRegion (clip);
        for (int i = 0; i < 1400; ++i)
        {
            const float x = face.getX() + rnd.nextFloat() * face.getWidth(), y = face.getY() + rnd.nextFloat() * face.getHeight();
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.035f + 0.04f * rnd.nextFloat()));
            g.fillRect (x, y, 1.0f + rnd.nextFloat(), 1.0f);
        }
        for (int i = 0; i < 26; ++i)
        {
            const float x = face.getX() + rnd.nextFloat() * face.getWidth(), y = face.getY() + rnd.nextFloat() * face.getHeight();
            const float len = 8.0f + 60.0f * rnd.nextFloat(), a = (rnd.nextFloat() - 0.5f) * 0.5f;
            g.setColour (Colours::white.withAlpha (0.05f + 0.05f * rnd.nextFloat()));
            g.drawLine (x, y, x + len * std::cos (a), y + len * std::sin (a), 0.6f);
        }
        for (int i = 0; i < 60; ++i)      // chipped paint along the edges
        {
            const bool horiz = rnd.nextBool(); const float t = rnd.nextFloat(), e = rnd.nextBool() ? 0.0f : 1.0f, s = 1.5f + 3.0f * rnd.nextFloat();
            const float x = horiz ? face.getX() + t * face.getWidth() : face.getX() + e * (face.getWidth() - 3.0f);
            const float y = horiz ? face.getY() + e * (face.getHeight() - 3.0f) : face.getY() + t * face.getHeight();
            g.setColour (Colour (0xff3c4340).withAlpha (0.25f + 0.3f * rnd.nextFloat()));
            g.fillEllipse (x, y, s, s * (0.5f + rnd.nextFloat()));
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), face.getX(), face.getY(), Colours::transparentWhite, face.getX(), face.getY() + 60.0f, false));
        g.fillRect (face.withHeight (60.0f));
    }
    for (auto p : { face.getTopLeft().translated (11, 11), face.getTopRight().translated (-11, 11), face.getBottomLeft().translated (11, -11), face.getBottomRight().translated (-11, -11) })
        drawScrewHead (g, p, 5.0f, rnd.nextFloat() * 3.0f);
}

void VinylLookAndFeel::drawBadge (Graphics& g, Rectangle<float> b, const Drawable* logo)
{
    // enamel manufacturer's plate: brass rim, dark enamel field, logo in cream
    g.setColour (Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (b.translated (1.0f, 2.0f), 6.0f);
    g.setGradientFill (ColourGradient (Colour (0xfff0d58a), b.getX(), b.getY(), Colour (0xff8a6a24), b.getRight(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 6.0f);
    auto in = b.reduced (3.0f);
    g.setGradientFill (ColourGradient (Colour (0xff1d2f2b), in.getX(), in.getY(), Colour (0xff0c1614), in.getX(), in.getBottom(), false));
    g.fillRoundedRectangle (in, 4.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), in.getX(), in.getY(), Colours::transparentWhite, in.getX(), in.getCentreY(), false));
    g.fillRoundedRectangle (in.withHeight (in.getHeight() * 0.5f), 4.0f);
    auto text = in.removeFromBottom (in.getHeight() * 0.2f);
    if (logo != nullptr) logo->drawWithin (g, in.reduced (6.0f, 3.0f), RectanglePlacement::centred, 1.0f);
    g.setColour (Colour (0xffe8cf8a)); g.setFont (font (text.getHeight() * 0.72f));
    g.drawText ("P L U G I N S", text.translated (0.0f, -2.0f), Justification::centred, false);
    for (auto p : { b.getTopLeft().translated (6, 6), b.getTopRight().translated (-6, 6), b.getBottomLeft().translated (6, -6), b.getBottomRight().translated (-6, -6) })
    { g.setColour (Colour (0xff5a4414)); g.fillEllipse (p.x - 1.6f, p.y - 1.6f, 3.2f, 3.2f); }
}

//------------------------------------------------------------------------------ LED readout
void VinylLookAndFeel::drawSevenSegment (Graphics& g, Rectangle<float> b, const String& text)
{
    //  segments: a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
    auto mask = [] (juce_wchar ch) -> int
    {
        switch (ch)
        {
            case '0': return 0x3f; case '1': return 0x06; case '2': return 0x5b; case '3': return 0x4f; case '4': return 0x66;
            case '5': return 0x6d; case '6': return 0x7d; case '7': return 0x07; case '8': return 0x7f; case '9': return 0x6f;
            case 'b': return 0x7c; case 'r': return 0x50; case 't': return 0x78; case 'A': return 0x77; case 'S': return 0x6d;
            case 'E': return 0x79; case 'n': return 0x54; case 'o': return 0x5c; case 'F': return 0x71; case '-': return 0x40;
            default:  return 0;
        }
    };
    int cells = 0;
    for (auto ch : text) if (ch != '.') ++cells;
    if (cells == 0) return;
    const float cw = jmin (b.getWidth() / (float) cells, b.getHeight() * 0.62f), h = b.getHeight(), w = cw * 0.72f, t = h * 0.105f;
    float x = b.getRight() - cw * (float) cells;
    const Colour on (0xffff3b2a), off (0xff4a0d08);
    auto seg = [&] (bool lit, Rectangle<float> r)
    {
        if (lit) { g.setColour (on.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (t * 0.5f), t * 0.7f); }
        g.setColour (lit ? on : off.withAlpha (0.55f)); g.fillRoundedRectangle (r, t * 0.45f);
    };
    const auto skew = AffineTransform::shear (-0.08f, 0.0f).translated (h * 0.04f, 0.0f);
    Graphics::ScopedSaveState ss (g); g.addTransform (skew);
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text[i];
        if (ch == '.') { g.setColour (on); g.fillEllipse (x - t * 1.0f, b.getBottom() - t * 1.1f, t * 1.1f, t * 1.1f); continue; }
        const int m = mask (ch); const float y = b.getY(), hh = (h - t) * 0.5f;
        seg (m & 1,  { x + t * 0.6f, y, w - t * 1.2f, t });
        seg (m & 2,  { x + w - t, y + t * 0.6f, t, hh - t * 0.7f });
        seg (m & 4,  { x + w - t, y + hh + t * 0.6f, t, hh - t * 0.7f });
        seg (m & 8,  { x + t * 0.6f, y + h - t, w - t * 1.2f, t });
        seg (m & 16, { x, y + hh + t * 0.6f, t, hh - t * 0.7f });
        seg (m & 32, { x, y + t * 0.6f, t, hh - t * 0.7f });
        seg (m & 64, { x + t * 0.6f, y + hh, w - t * 1.2f, t });
        x += cw;
    }
}

//------------------------------------------------------------------------------ top bar
void VinylLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? col::amber : Colour (0xff2a302e).brighter (over ? 0.12f : 0.0f).darker (down ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? col::amber.darker (0.4f) : Colour (0xff4a5350));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void VinylLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&)
{
    Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (Colour (0xff101312)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Colour (0xff4a5350)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    Path a; a.addTriangle ((float) w - 15.0f, (float) h * 0.4f, (float) w - 7.0f, (float) h * 0.4f, (float) w - 11.0f, (float) h * 0.66f);
    g.setColour (col::amber); g.fillPath (a);
}

void VinylLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
} // namespace vs
