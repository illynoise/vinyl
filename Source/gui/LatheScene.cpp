#include "LatheScene.h"

namespace vs
{
using namespace juce;

namespace
{
const Colour mint (0xffcfe3d3), mintDark (0xff9fb8a6), mintLight (0xffe4f1e6), frame (0xff6f8086), outline (0xff1a2426);

void block (Graphics& g, Rectangle<float> r, float corner, Colour top = mintLight, Colour bottom = mintDark)
{
    g.setColour (Colours::black.withAlpha (0.28f)); g.fillRoundedRectangle (r.translated (3.0f, 5.0f), corner);
    g.setGradientFill (ColourGradient (top, r.getX(), r.getY(), bottom, r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (outline); g.drawRoundedRectangle (r, corner, 1.2f);
    g.setColour (Colours::white.withAlpha (0.35f)); g.drawLine (r.getX() + corner, r.getY() + 2.0f, r.getRight() - corner, r.getY() + 2.0f, 1.0f);
}
void metal (Graphics& g, Rectangle<float> r, float corner)
{
    g.setGradientFill (ColourGradient (Colour (0xfff2f4f3), r.getX(), r.getY(), Colour (0xff8d979a), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (outline); g.drawRoundedRectangle (r, corner, 1.0f);
}
void knobTop (Graphics& g, Point<float> c, float r, Colour a, Colour b)
{
    g.setColour (Colours::black.withAlpha (0.3f)); g.fillEllipse (c.x - r + 2.0f, c.y - r + 3.0f, 2 * r, 2 * r);
    g.setGradientFill (ColourGradient (a, c.x - r * 0.4f, c.y - r * 0.5f, b, c.x + r, c.y + r, true));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (outline); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
}
}

LatheScene::LatheScene (VinylStudioProcessor& p) : processor (p), logo (loadLogo (Colour (0xff2a1a10)))
{
    setOpaque (true);
    setTooltip ("Click the left side of the record to stop or start it");
}

bool LatheScene::onLeftOfRecord (Point<float> p) const
{
    return p.getDistanceFrom (centre) < discR && p.x < centre.x;
}

void LatheScene::mouseMove (const MouseEvent& e)
{
    setMouseCursor (onLeftOfRecord (e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void LatheScene::mouseDown (const MouseEvent& e)
{
    if (onLeftOfRecord (e.position))
        processor.setParam (id::stop, processor.param (id::stop) > 0.5f ? 0.0f : 1.0f);
}

void LatheScene::tick (double seconds)
{
    generation = (int) processor.param (id::generation); turntable = (int) processor.param (id::turntable); cartridge = (int) processor.param (id::cartridge);
    const float target = processor.param (id::tonearm) * 0.01f;
    arm += (target - arm) * 0.35f;
    if (std::abs (target - arm) < 0.0005f) arm = target;
    angle = std::fmod (angle + seconds * processor.platterSpeed.load() * (100.0 / 3.0 / 60.0) * MathConstants<double>::twoPi, MathConstants<double>::twoPi);   // true 33 1/3 rpm
    const int key = generation * 100 + turntable * 10 + cartridge;
    if (std::abs (angle - shownAngle) > 1e-4 || std::abs (arm - shownArm) > 1e-4f || key != shownKey)
    {
        shownAngle = angle; shownArm = arm; shownKey = key;
        repaint (Rectangle<int> (215, 0, 580, (int) H));          // only the lathe moves
    }
}

//------------------------------------------------------------------------------ static artwork
void LatheScene::drawStatic (Graphics& g) const
{
    const Rectangle<float> all (0, 0, W, H);
    // desk in a dark room
    g.setGradientFill (ColourGradient (Colour (0xff231a14), 0, 0, Colour (0xff0d0a08), 0, H, false)); g.fillRect (all);
    { Random rnd (11);                                            // wood grain
      for (int i = 0; i < 90; ++i) { const float y = rnd.nextFloat() * H; g.setColour (Colour (0xff3a2a1e).withAlpha (0.10f + 0.15f * rnd.nextFloat()));
        Path p; p.startNewSubPath (0, y); p.cubicTo (W * 0.3f, y + 6.0f * (rnd.nextFloat() - 0.5f), W * 0.7f, y + 6.0f * (rnd.nextFloat() - 0.5f), W, y + 4.0f * (rnd.nextFloat() - 0.5f));
        g.strokePath (p, PathStrokeType (0.6f + rnd.nextFloat())); } }
    // light through window blinds: slanted bars falling across the left of the desk
    { Graphics::ScopedSaveState ss (g);
      g.addTransform (AffineTransform::shear (-0.45f, 0.0f).translated (95.0f, 0.0f));
      for (int i = 0; i < 9; ++i)
      {
          const float y = 8.0f + (float) i * 37.0f;
          g.setGradientFill (ColourGradient (Colour (0x30cfd8ff), 0.0f, y, Colour (0x00cfd8ff), 330.0f, y, false));
          g.fillRect (-60.0f, y, 400.0f, 17.0f);
      } }
    // the lamp's pool of warm light on the platter
    g.setGradientFill (ColourGradient (Colour (0x70ffca7a), 560.0f, 150.0f, Colour (0x00ffca7a), 560.0f, 520.0f, true)); g.fillRect (all);

    // ---- lathe: outer frame + mint deck
    const Rectangle<float> fr (215.0f, 12.0f, 570.0f, 306.0f);
    g.setColour (Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (fr.translated (6.0f, 9.0f).expanded (2.0f), 12.0f);
    g.setGradientFill (ColourGradient (frame.brighter (0.25f), fr.getX(), fr.getY(), frame.darker (0.25f), fr.getX(), fr.getBottom(), false));
    g.fillRoundedRectangle (fr, 10.0f); g.setColour (outline); g.drawRoundedRectangle (fr, 10.0f, 1.5f);
    const auto deck = fr.reduced (11.0f);
    g.setGradientFill (ColourGradient (mintLight, deck.getX(), deck.getY(), mint.darker (0.08f), deck.getRight(), deck.getBottom(), false));
    g.fillRoundedRectangle (deck, 6.0f); g.setColour (outline); g.drawRoundedRectangle (deck, 6.0f, 1.2f);
    for (auto p : { deck.getTopLeft().translated (9, 9), deck.getTopRight().translated (-9, 9), deck.getBottomLeft().translated (9, -9), deck.getBottomRight().translated (-9, -9) })
        VinylLookAndFeel::drawScrewHead (g, p, 3.2f, 0.6f);

    // platter (heavy, machined) - the record itself is an animated layer
    const float pr = discR + 9.0f;
    g.setColour (Colours::black.withAlpha (0.35f)); g.fillEllipse (centre.x - pr + 4.0f, centre.y - pr + 7.0f, 2 * pr, 2 * pr);
    g.setGradientFill (ColourGradient (Colour (0xffe9ecea), centre.x - pr, centre.y - pr, Colour (0xff7d878a), centre.x + pr, centre.y + pr, false));
    g.fillEllipse (centre.x - pr, centre.y - pr, 2 * pr, 2 * pr);
    g.setColour (outline); g.drawEllipse (centre.x - pr, centre.y - pr, 2 * pr, 2 * pr, 1.2f);

    // left motor housing and feed gearbox, right bearing block
    block (g, { 236.0f, 52.0f, 74.0f, 100.0f }, 9.0f);
    block (g, { 242.0f, 148.0f, 62.0f, 46.0f }, 5.0f, mint, mintDark.darker (0.1f));
    block (g, { 704.0f, 46.0f, 66.0f, 112.0f }, 9.0f);
    g.setColour (outline.withAlpha (0.5f)); g.drawLine (730.0f, 50.0f, 730.0f, 154.0f, 1.0f);

    // chrome ball knob, tone-arm rest pad
    knobTop (g, { 252.0f, 284.0f }, 9.0f, Colours::white, Colour (0xff7f8a8d));
    g.setColour (outline); g.fillRoundedRectangle (249.0f, 292.0f, 6.0f, 9.0f, 2.0f);

    if (turntable == 1)      // DJ deck: pitch fader and a start/stop key
    {
        g.setColour (Colour (0xff22282a)); g.fillRoundedRectangle (752.0f, 176.0f, 10.0f, 56.0f, 3.0f);
        metal (g, { 747.0f, 196.0f, 20.0f, 9.0f }, 2.0f);
        g.setColour (Colour (0xff22282a)); g.fillRoundedRectangle (236.0f, 218.0f, 34.0f, 26.0f, 3.0f);
        g.setColour (col::red.withAlpha (0.9f)); g.fillEllipse (242.0f, 222.0f, 5.0f, 5.0f);
        VinylLookAndFeel::drawLegend (g, "START", { 236.0f, 229.0f, 34.0f, 12.0f }, 7.0f);
        VinylLookAndFeel::drawLegend (g, "PITCH", { 740.0f, 234.0f, 34.0f, 10.0f }, 7.0f, Justification::centred, true);
    }
    VinylLookAndFeel::drawLegend (g, "BASE JASE  CUTTING LATHE  MODEL VS-1", { deck.getX() + 34.0f, deck.getBottom() - 15.0f, 260.0f, 10.0f }, 7.5f, Justification::centredLeft, true);

    // desk lamp (top view): weighted base, jointed arm, enamel shade over the top-right of the lathe
    g.setColour (Colours::black.withAlpha (0.45f)); g.fillEllipse (905.0f, 196.0f, 76.0f, 76.0f);
    knobTop (g, { 938.0f, 228.0f }, 33.0f, Colour (0xff3a4a44), Colour (0xff101614));
    g.setColour (Colour (0xff0b0e0d)); g.drawLine (938.0f, 228.0f, 900.0f, 130.0f, 6.0f); g.drawLine (900.0f, 130.0f, 842.0f, 78.0f, 6.0f);
    g.setColour (Colour (0xff59665f)); g.drawLine (938.0f, 228.0f, 900.0f, 130.0f, 2.0f); g.drawLine (900.0f, 130.0f, 842.0f, 78.0f, 2.0f);
    knobTop (g, { 900.0f, 130.0f }, 6.0f, Colour (0xffc9a24a), Colour (0xff5a4414));
    { Path shade; shade.addEllipse (-52.0f, -34.0f, 104.0f, 68.0f);
      const auto t = AffineTransform::rotation (0.72f).translated (828.0f, 66.0f);
      g.setColour (Colours::black.withAlpha (0.4f)); g.fillPath (shade, t.translated (5.0f, 8.0f));
      g.setGradientFill (ColourGradient (Colour (0xff3f6b5a), 800.0f, 40.0f, Colour (0xff12241d), 860.0f, 100.0f, false)); g.fillPath (shade, t);
      g.setColour (Colour (0xffffe2a8).withAlpha (0.55f)); g.strokePath (shade, PathStrokeType (1.6f), t);
      g.setGradientFill (ColourGradient (Colour (0x55ffd591), 805.0f, 78.0f, Colour (0x00ffd591), 760.0f, 120.0f, true)); g.fillEllipse (730.0f, 30.0f, 160.0f, 130.0f); }

    // vignette
    g.setGradientFill (ColourGradient (Colours::transparentBlack, W * 0.55f, H * 0.5f, Colours::black.withAlpha (0.62f), 0.0f, 0.0f, true)); g.fillRect (all);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), 0.0f, H, Colours::transparentBlack, 0.0f, H - 26.0f, false)); g.fillRect (all);
}

//------------------------------------------------------------------------------ animated layers
void LatheScene::drawDisc (Graphics& g) const
{
    const float r = discR, cx = centre.x, cy = centre.y;
    const bool print = generation == 1;
    const auto spin = AffineTransform::rotation ((float) angle, cx, cy);

    // strobe marks on the platter rim show the true speed
    { const int n = turntable == 1 ? 72 : 36; const float rr = r + 5.0f;
      g.setColour (turntable == 1 ? Colour (0xff20282a) : Colour (0xff5d686b));
      for (int i = 0; i < n; ++i) { const float a = (float) angle + (float) i * MathConstants<float>::twoPi / (float) n;
        g.fillEllipse (cx + rr * std::cos (a) - 1.2f, cy + rr * std::sin (a) - 1.2f, 2.4f, 2.4f); } }

    if (! print)
    {
        // lacquer blank: glossy acetate on an aluminium core, warm gold under the lamp
        g.setGradientFill (ColourGradient (Colour (0xfff0d27a), cx - r * 0.4f, cy - r * 0.5f, Colour (0xffa87f2c), cx + r, cy + r, true));
        g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
        for (int i = 0; i < 12; ++i)      // freshly cut bands
        {
            const float rr = r * (0.36f + 0.60f * (float) i / 12.0f);
            g.setColour ((i % 2 ? Colour (0xff8b6a22) : Colour (0xfffbe6a2)).withAlpha (i % 2 ? 0.55f : 0.35f));
            g.drawEllipse (cx - rr, cy - rr, 2 * rr, 2 * rr, i % 2 ? 2.2f : 1.0f);
        }
        g.setGradientFill (ColourGradient (Colour (0xffe6c873), cx - 20.0f, cy - 20.0f, Colour (0xffb89038), cx + 30.0f, cy + 30.0f, true));
        g.fillEllipse (cx - r * 0.30f, cy - r * 0.30f, r * 0.60f, r * 0.60f);
        g.setColour (Colour (0xff6d521a)); g.drawEllipse (cx - r * 0.30f, cy - r * 0.30f, r * 0.60f, r * 0.60f, 1.0f);
        // hand-scribed matrix number rotates with the disc
        Graphics::ScopedSaveState ss (g); g.addTransform (spin);
        g.setColour (Colour (0xff5a4314).withAlpha (0.8f)); g.setFont (VinylLookAndFeel::font (7.0f, false));
        g.drawText ("BJ-001 A", Rectangle<float> (cx - 30.0f, cy - r * 0.26f, 60.0f, 9.0f), Justification::centred, false);
        g.drawLine (cx + r * 0.33f, cy, cx + r * 0.97f, cy + 1.5f, 0.5f);          // faint lead-in scribe
    }
    else
    {
        // pressed record: black vinyl, track gaps and a paper label
        g.setGradientFill (ColourGradient (Colour (0xff2c2e31), cx - r * 0.5f, cy - r * 0.6f, Colour (0xff050506), cx + r, cy + r, true));
        g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
        for (int i = 0; i < 30; ++i)
        {
            const float rr = r * (0.37f + 0.60f * (float) i / 30.0f);
            const bool gap = i == 7 || i == 15 || i == 22;
            g.setColour (gap ? Colour (0xff000000).withAlpha (0.9f) : Colours::white.withAlpha (i % 2 ? 0.045f : 0.02f));
            g.drawEllipse (cx - rr, cy - rr, 2 * rr, 2 * rr, gap ? 1.6f : 0.8f);
        }
        Graphics::ScopedSaveState ss (g); g.addTransform (spin);
        const float lr = r * 0.33f;
        g.setGradientFill (ColourGradient (Colour (0xfff0e2bd), cx, cy - lr, Colour (0xffd9c48b), cx, cy + lr, false)); g.fillEllipse (cx - lr, cy - lr, 2 * lr, 2 * lr);
        { Path half; half.addPieSegment (cx - lr, cy - lr, 2 * lr, 2 * lr, MathConstants<float>::halfPi, MathConstants<float>::pi * 1.5f, 0.0f); g.setColour (Colour (0xff1d3a34)); g.fillPath (half); }
        if (logo != nullptr) logo->drawWithin (g, Rectangle<float> (cx - lr * 0.62f, cy - lr * 0.86f, lr * 1.24f, lr * 0.76f), RectanglePlacement::centred, 1.0f);
        g.setColour (col::cream); g.setFont (VinylLookAndFeel::font (6.2f));
        g.drawText ("VINYL STUDIO", Rectangle<float> (cx - lr, cy + lr * 0.22f, 2 * lr, 8.0f), Justification::centred, false);
        g.setFont (VinylLookAndFeel::font (5.0f, false));
        g.drawText ("SIDE A   33 1/3", Rectangle<float> (cx - lr, cy + lr * 0.50f, 2 * lr, 7.0f), Justification::centred, false);
    }
    g.setColour (outline); g.drawEllipse (cx - r, cy - r, 2 * r, 2 * r, 1.0f);
    // spindle
    g.setGradientFill (ColourGradient (Colours::white, cx - 2.0f, cy - 2.0f, Colour (0xff6c7679), cx + 4.0f, cy + 4.0f, true)); g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    g.setColour (outline); g.drawEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 0.8f);

    // lamp reflection: two opposed wedges of sheen that breathe as the (never perfectly flat) disc turns
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addEllipse (cx - r, cy - r, 2 * r, 2 * r); g.reduceClipRegion (clip);
        const float wob = 0.10f * std::sin ((float) angle) + 0.04f * std::sin (2.0f * (float) angle + 1.0f);
        const float base = -0.62f + wob, width = 0.30f + 0.05f * std::sin ((float) angle * 3.0f);
        const float alpha = print ? 0.34f : 0.42f;
        for (int k = 0; k < 2; ++k)
        {
            const float a = base + (float) k * MathConstants<float>::pi;
            for (int j = 0; j < 5; ++j)       // feathered edges
            {
                const float w = width * (1.0f - (float) j * 0.17f);
                Path wedge; wedge.addPieSegment (cx - r, cy - r, 2 * r, 2 * r, a - w * 0.5f, a + w * 0.5f, print ? 0.34f : 0.31f);
                g.setColour (Colours::white.withAlpha (alpha * 0.26f * (k == 0 ? 1.0f : 0.55f)));
                g.fillPath (wedge);
            }
        }
    }
}

void LatheScene::drawCarriage (Graphics& g) const
{
    // overhead carriage: two ground rails between the housings, lead screw, cutter head
    g.setColour (Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (306.0f, 88.0f, 402.0f, 34.0f, 3.0f);
    metal (g, { 304.0f, 76.0f, 404.0f, 16.0f }, 2.0f);
    metal (g, { 304.0f, 97.0f, 404.0f, 11.0f }, 2.0f);
    g.setColour (outline.withAlpha (0.55f));
    for (int i = 0; i < 40; ++i) g.drawLine (312.0f + (float) i * 10.0f, 99.0f, 316.0f + (float) i * 10.0f, 106.0f, 0.7f);   // lead screw thread
    for (float x : { 534.0f, 546.0f, 640.0f, 652.0f }) metal (g, { x, 72.0f, 9.0f, 9.0f }, 1.5f);                           // limit stops
    // black cueing lever on the left
    { Path lever; lever.addRoundedRectangle (-4.0f, -22.0f, 8.0f, 30.0f, 3.0f); g.setColour (Colour (0xff15191a)); g.fillPath (lever, AffineTransform::rotation (-0.7f).translated (338.0f, 80.0f));
      g.fillEllipse (329.0f, 71.0f, 18.0f, 18.0f); }
    // depth-of-cut rod from the gearbox
    g.setColour (outline); g.drawLine (304.0f, 168.0f, 520.0f, 168.0f, 1.6f);
    g.setColour (Colour (0xffcfd6d6)); g.drawLine (304.0f, 167.0f, 520.0f, 167.0f, 0.6f);

    // inspection microscope (looking straight down the tube)
    knobTop (g, { 632.0f, 58.0f }, 22.0f, Colour (0xffe6eae8), Colour (0xff7d878a));
    knobTop (g, { 632.0f, 58.0f }, 12.0f, Colour (0xff596366), Colour (0xff101416));
    g.setColour (Colours::white.withAlpha (0.5f)); g.fillEllipse (626.0f, 52.0f, 5.0f, 4.0f);
    metal (g, { 622.0f, 78.0f, 20.0f, 12.0f }, 2.0f);

    // cutter head carriage
    block (g, { 566.0f, 100.0f, 86.0f, 60.0f }, 5.0f, mint, mintDark);
    knobTop (g, { 590.0f, 122.0f }, 6.0f, Colour (0xff3a4245), Colour (0xff0d1011));
    knobTop (g, { 628.0f, 122.0f }, 6.0f, Colour (0xff3a4245), Colour (0xff0d1011));
    g.setColour (outline.withAlpha (0.7f)); g.setFont (VinylLookAndFeel::font (6.5f)); g.drawText ("VS-1", Rectangle<float> (574.0f, 143.0f, 40.0f, 10.0f), Justification::centredLeft, false);
    block (g, { 548.0f, 118.0f, 20.0f, 22.0f }, 2.0f, Colour (0xff3d4649), Colour (0xff15191a));       // head body
    { Path st; st.addTriangle (553.0f, 140.0f, 563.0f, 140.0f, 558.0f, 154.0f); g.setColour (Colour (0xffd9dede)); g.fillPath (st); g.setColour (outline); g.strokePath (st, PathStrokeType (0.8f)); }
    // chip / suction tube arcing away from the stylus
    { Path tube; tube.startNewSubPath (560.0f, 150.0f); tube.cubicTo (590.0f, 172.0f, 650.0f, 176.0f, 672.0f, 150.0f); tube.cubicTo (684.0f, 134.0f, 690.0f, 120.0f, 706.0f, 116.0f);
      g.setColour (outline); g.strokePath (tube, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
      g.setColour (Colour (0xff59646a)); g.strokePath (tube, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
      g.setColour (Colours::white.withAlpha (0.35f)); g.strokePath (tube, PathStrokeType (0.8f), AffineTransform::translation (-0.6f, -0.9f)); }
    // side clamp on the right of the carriage
    { Path c; c.addRoundedRectangle (-3.0f, -9.0f, 6.0f, 18.0f, 2.5f); g.setColour (Colour (0xff15191a)); g.fillPath (c, AffineTransform::rotation (0.5f).translated (656.0f, 150.0f)); }
}

void LatheScene::drawToneArm (Graphics& g) const
{
    // playback arm: stylus radius goes from the outer edge (0) to the inner groove (100)
    const float La = 246.0f, d = pivot.getDistanceFrom (centre);
    const float rs = discR * (0.93f - 0.56f * arm);
    const float th = std::acos (jlimit (-1.0f, 1.0f, (La * La + d * d - rs * rs) / (2.0f * La * d)));
    const float toCentre = std::atan2 (centre.y - pivot.y, centre.x - pivot.x);
    const float a = toCentre - th;                                 // the side that keeps the arm clear of the carriage
    const Point<float> tip (pivot.x + La * std::cos (a), pivot.y + La * std::sin (a));
    const auto rot = AffineTransform::rotation (a).translated (pivot.x, pivot.y);      // local +x runs along the arm

    // base
    knobTop (g, pivot, 24.0f, Colour (0xff343b3e), Colour (0xff090b0c));
    // shadow of the arm on the record
    { Path sh; sh.addRoundedRectangle (-46.0f, -3.0f, La + 46.0f, 6.0f, 3.0f); g.setColour (Colours::black.withAlpha (0.22f)); g.fillPath (sh, rot.translated (4.0f, 7.0f)); }
    // counterweight, tube, headshell
    { Path cw; cw.addRoundedRectangle (-52.0f, -11.0f, 30.0f, 22.0f, 5.0f);
      g.setGradientFill (ColourGradient (Colour (0xfff0f2f1), pivot.x, pivot.y - 12.0f, Colour (0xff6f7a7d), pivot.x, pivot.y + 12.0f, false)); g.fillPath (cw, rot);
      g.setColour (outline); g.strokePath (cw, PathStrokeType (1.0f), rot); }
    { Path tube; tube.addRoundedRectangle (-24.0f, -2.6f, La - 2.0f, 5.2f, 2.6f);
      g.setColour (Colour (0xffd5dbda)); g.fillPath (tube, rot); g.setColour (outline); g.strokePath (tube, PathStrokeType (0.9f), rot);
      Path hi; hi.addRectangle (-20.0f, -1.6f, La - 12.0f, 1.0f); g.setColour (Colours::white.withAlpha (0.8f)); g.fillPath (hi, rot); }
    knobTop (g, pivot, 9.0f, Colour (0xffe9ecea), Colour (0xff6f7a7d));

    // cartridge: the body changes with the Cartridge selection
    const auto head = AffineTransform::rotation (a + 0.38f).translated (tip.x, tip.y);          // offset angle of the headshell
    Path shell; shell.addRoundedRectangle (-30.0f, -8.0f, 34.0f, 16.0f, 2.0f);
    g.setColour (Colour (0xff1b2022)); g.fillPath (shell, head);
    Path lift; lift.addRoundedRectangle (-22.0f, 8.0f, 4.0f, 14.0f, 2.0f); g.fillPath (lift, head);                   // finger lift
    Path body;
    Colour c1, c2;
    switch (cartridge)
    {
        case 1:  body.addRoundedRectangle (-22.0f, -5.5f, 24.0f, 11.0f, 1.5f); c1 = Colour (0xfff3dc92); c2 = Colour (0xff8f6f24); break;   // MC: slim gold body
        case 2:  body.addRoundedRectangle (-27.0f, -9.5f, 31.0f, 19.0f, 4.0f); c1 = Colour (0xff3b4143); c2 = Colour (0xff050606); break;   // DJ: chunky black
        default: body.addRoundedRectangle (-24.0f, -7.0f, 27.0f, 14.0f, 2.0f); c1 = Colour (0xfff4f6f5); c2 = Colour (0xff808b8e); break;   // MM: silver
    }
    g.setGradientFill (ColourGradient (c1, tip.x - 8.0f, tip.y - 8.0f, c2, tip.x + 8.0f, tip.y + 10.0f, false)); g.fillPath (body, head);
    g.setColour (outline); g.strokePath (body, PathStrokeType (0.9f), head);
    if (cartridge == 2) { Path guard; guard.addRoundedRectangle (-3.0f, -6.0f, 6.0f, 12.0f, 2.0f); g.setColour (Colours::white); g.fillPath (guard, head); }
    else { g.setColour (cartridge == 1 ? Colour (0xff2a2a2a) : col::red); Path dot; dot.addEllipse (-3.0f, -2.0f, 4.0f, 4.0f); g.fillPath (dot, head); }
    g.setColour (outline); g.setFont (VinylLookAndFeel::font (5.5f));
    { Graphics::ScopedSaveState ss (g); g.addTransform (head);
      g.setColour (cartridge == 2 ? Colours::white.withAlpha (0.85f) : outline);
      g.drawText (cartridge == 1 ? "MC" : cartridge == 2 ? "DJ" : "MM", Rectangle<float> (-22.0f, -4.0f, 16.0f, 8.0f), Justification::centred, false); }
}

void LatheScene::paint (Graphics& g)
{
    // static artwork cache, re-rendered only when the size or the turntable type changes
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (cache.isNull() || std::abs (scale - cacheScale) > 0.01f || cacheKey != turntable)
    {
        cacheScale = scale; cacheKey = turntable;
        cache = Image (Image::RGB, roundToInt (W * scale), roundToInt (H * scale), false);
        Graphics cg (cache); cg.addTransform (AffineTransform::scale (scale)); drawStatic (cg);
    }
    g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
    g.drawImage (cache, Rectangle<float> (0, 0, W, H));
    drawDisc (g);
    drawCarriage (g);
    drawToneArm (g);
    if (processor.param (id::stop) > 0.5f && processor.platterSpeed.load() < 0.01f)
    {
        g.setColour (Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (centre.x - 34.0f, centre.y + 44.0f, 68.0f, 16.0f, 4.0f);
        VinylLookAndFeel::drawLegend (g, "STOPPED", { centre.x - 34.0f, centre.y + 44.0f, 68.0f, 16.0f }, 9.5f);
    }
}
} // namespace vs
