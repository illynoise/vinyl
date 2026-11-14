#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;
using namespace vs;

//============================================================================== About screen
class VinylStudioEditor::About : public Component
{
public:
    explicit About (VinylStudioProcessor& p) : processor (p), logo (loadLogo (col::cream)) { setInterceptsMouseClicks (true, false); }
    void mouseUp (const MouseEvent&) override { setVisible (false); }
    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.78f));
        auto card = getLocalBounds().toFloat().withSizeKeepingCentre (jmin (430.0f, (float) getWidth() - 30.0f), jmin (300.0f, (float) getHeight() - 20.0f));
        g.setGradientFill (ColourGradient (Colour (0xff1d2f2b), card.getX(), card.getY(), Colour (0xff0b1412), card.getX(), card.getBottom(), false));
        g.fillRoundedRectangle (card, 8.0f);
        g.setColour (col::gold); g.drawRoundedRectangle (card.reduced (1.0f), 8.0f, 1.5f);
        auto a = card.reduced (18.0f, 12.0f);
        // wordmark: script logo over spaced capitals
        auto top = a.removeFromTop (a.getHeight() * 0.40f);
        if (logo != nullptr) logo->drawWithin (g, top, RectanglePlacement::centred, 1.0f);
        g.setColour (col::gold); g.setFont (VinylLookAndFeel::font (11.0f));
        g.drawText ("P L U G I N S", a.removeFromTop (16.0f), Justification::centred, false);
        a.removeFromTop (6.0f);
        g.setColour (col::legend); g.setFont (VinylLookAndFeel::font (21.0f));
        g.drawText (processor.getName(), a.removeFromTop (27.0f), Justification::centred, false);
        g.setFont (VinylLookAndFeel::font (12.0f, false));
        g.drawText (String ("Version ") + VS_VERSION + "   -   built " + String (__DATE__), a.removeFromTop (17.0f), Justification::centred, false);
        g.drawText ("Base Jase Plugins", a.removeFromTop (17.0f), Justification::centred, false);
        a.removeFromTop (4.0f);
        g.setColour (col::legend.withAlpha (0.7f)); g.setFont (VinylLookAndFeel::font (10.5f, false));
        g.drawFittedText ("Design and DSP by Base Jase Plugins. Surface noise built from analysed recordings of real records.\n"
                          "Made with JUCE. VST is a registered trademark of Steinberg Media Technologies GmbH.\nClick anywhere to close.",
                          a.toNearestInt(), Justification::centred, 4);
    }
private:
    VinylStudioProcessor& processor; std::unique_ptr<Drawable> logo;
};

//============================================================================== preset bar
class VinylStudioEditor::TopBar : public Component
{
public:
    TopBar (VinylStudioProcessor& p, VinylStudioEditor& e) : processor (p), editor (e)
    {
        addAndMakeVisible (presets); presets.setTooltip ("Preset browser");
        presets.onChange = [this] { choose(); };
        auto setup = [this] (TextButton& b, const String& tip, std::function<void()> fn) { addAndMakeVisible (b); b.setTooltip (tip); b.onClick = std::move (fn); };
        setup (prev, "Previous preset", [this] { step (-1); });
        setup (next, "Next preset", [this] { step (1); });
        setup (save, "Save the current settings as a user preset", [this] { beginSave(); });
        setup (ab, "A/B compare: switch between two sets of settings", [this] { processor.toggleAB(); refresh(); });
        setup (undoB, "Undo", [this] { processor.undo.undo(); });
        setup (redoB, "Redo", [this] { processor.undo.redo(); });
        static const int sizes[] = { 75, 100, 150, 200 };
        for (int i = 0; i < 4; ++i)
        {
            sizeB[i].setButtonText (String (sizes[i]) + "%");
            setup (sizeB[i], "Window size " + String (sizes[i]) + "%", [this, i] { editor.setScalePercent (sizes[i]); });
        }
        addChildComponent (nameBox);
        nameBox.setFont (VinylLookAndFeel::font (12.5f)); nameBox.setTextToShowWhenEmpty ("Preset name, then Enter", col::legend.withAlpha (0.5f));
        nameBox.onReturnKey = [this] { if (processor.saveUserPreset (nameBox.getText())) { nameBox.setVisible (false); rebuild(); } };
        nameBox.onEscapeKey = [this] { nameBox.setVisible (false); };
        nameBox.onFocusLost = [this] { nameBox.setVisible (false); };
        rebuild();
    }

    void paint (Graphics& g) override
    {
        g.setGradientFill (ColourGradient (Colour (0xff262b2a), 0, 0, col::bar, 0, (float) getHeight(), false)); g.fillAll();
        g.setColour (Colours::black); g.drawLine (0, (float) getHeight() - 0.5f, (float) getWidth(), (float) getHeight() - 0.5f, 1.0f);
        g.setColour (col::legend.withAlpha (0.55f)); g.setFont (VinylLookAndFeel::font (10.5f));
        g.drawText (processor.getName().toUpperCase(), titleArea, Justification::centred, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        presets.setBounds (r.removeFromLeft (jmin (210, getWidth() / 4))); nameBox.setBounds (presets.getBounds());
        r.removeFromLeft (4); prev.setBounds (r.removeFromLeft (22)); r.removeFromLeft (2); next.setBounds (r.removeFromLeft (22));
        r.removeFromLeft (6); save.setBounds (r.removeFromLeft (44));
        r.removeFromLeft (6); ab.setBounds (r.removeFromLeft (40));
        r.removeFromLeft (6); undoB.setBounds (r.removeFromLeft (44)); r.removeFromLeft (2); redoB.setBounds (r.removeFromLeft (44));
        for (int i = 3; i >= 0; --i) { sizeB[i].setBounds (r.removeFromRight (42)); r.removeFromRight (2); }
        titleArea = r.toFloat();
    }

    void refresh()
    {
        ab.setButtonText (processor.isSlotB() ? "A | [B]" : "[A] | B");
        if (presets.getText() != processor.presetName && ! presets.isPopupActive()) presets.setText (processor.presetName, dontSendNotification);
        undoB.setEnabled (processor.undo.canUndo()); redoB.setEnabled (processor.undo.canRedo());
    }

private:
    void rebuild()
    {
        presets.clear (dontSendNotification);
        auto names = processor.getFactoryPresetNames();
        presets.addSectionHeading ("Factory");
        for (int i = 0; i < names.size(); ++i) presets.addItem (names[i], 1 + i);
        userFiles = processor.getUserPresets();
        if (! userFiles.isEmpty()) presets.addSectionHeading ("User");
        for (int i = 0; i < userFiles.size(); ++i) presets.addItem (userFiles[i].getFileNameWithoutExtension(), 1000 + i);
        presets.setText (processor.presetName, dontSendNotification);
    }
    void choose()
    {
        const int idv = presets.getSelectedId();
        if (idv >= 1000 && idv - 1000 < userFiles.size()) processor.loadUserPreset (userFiles[idv - 1000]);
        else if (idv >= 1) processor.loadFactoryPreset (idv - 1);
    }
    void step (int delta)
    {
        const int n = presets.getNumItems();
        if (n == 0) return;
        int idx = presets.getSelectedItemIndex();
        if (idx < 0) for (int i = 0; i < n; ++i) if (presets.getItemText (i) == processor.presetName) idx = i;
        presets.setSelectedItemIndex ((jmax (0, idx) + delta + n) % n, sendNotificationSync);
    }
    void beginSave()
    {
        nameBox.setText (processor.presetName == "Default" ? String() : processor.presetName, false);
        nameBox.setVisible (true); nameBox.toFront (true); nameBox.grabKeyboardFocus(); nameBox.selectAll();
    }

    VinylStudioProcessor& processor; VinylStudioEditor& editor;
    ComboBox presets; TextEditor nameBox;
    TextButton prev { "<" }, next { ">" }, save { "Save" }, ab { "A | B" }, undoB { "Undo" }, redoB { "Redo" }, sizeB[4];
    Array<File> userFiles; Rectangle<float> titleArea;
};

//============================================================================== console faceplate
class VinylStudioEditor::Console : public Component
{
public:
    Console (VinylStudioProcessor& p, std::function<void()> showAbout) : processor (p)
    {
        setOpaque (true);
        badge.onClick = std::move (showAbout);
        addAndMakeVisible (badge); addAndMakeVisible (vuL); addAndMakeVisible (vuR);
        if (p.lite) buildLite(); else buildFull();
        setBufferedToImage (false);
    }

    void setMeters (float l, float r, float cal) { vuL.setLevel (l, cal); vuR.setLevel (r, cal); }

    void paint (Graphics& g) override
    {
        // the static faceplate is cached as an image at the display resolution
        const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (plate.isNull() || std::abs (scale - plateScale) > 0.01f)
        {
            plateScale = scale;
            plate = Image (Image::RGB, roundToInt ((float) getWidth() * scale), roundToInt ((float) getHeight() * scale), false);
            Graphics pg (plate); pg.addTransform (AffineTransform::scale (scale));
            pg.fillAll (Colour (0xff141716));
            VinylLookAndFeel::drawPanel (pg, getLocalBounds().toFloat().reduced (3.0f), processor.lite ? 7u : 3u);
            for (auto& gr : groups) VinylLookAndFeel::drawGroup (pg, gr.area, gr.title);
        }
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (plate, getLocalBounds().toFloat());
    }

private:
    struct Group { Rectangle<float> area; String title; };

    Knob* knob (const char* pid, const String& label, KnobStyle style, float dia, float cx, float cy)
    {
        auto* k = new Knob (processor, pid, label, style, dia);
        k->setBounds (Knob::boundsFor (cx, cy, dia)); addAndMakeVisible (k); owned.add (k); return k;
    }
    LampButton* lamp (const char* pid, const String& label, Colour c, int index, float cx, float cy, float size = 44.0f)
    {
        auto* b = new LampButton (processor, pid, label, c, index);
        const float w = jmax (size + 12.0f, GlyphArrangement::getStringWidth (VinylLookAndFeel::font (9.0f), label) + 6.0f);
        b->setBounds (Rectangle<float> (cx - w * 0.5f, cy - size * 0.5f - 3.0f, w, size + 19.0f).toNearestInt()); addAndMakeVisible (b); owned.add (b); return b;
    }
    ToggleSwitch* toggle (const char* pid, const String& up, const String& down, const String& title, float cx, float cy)
    {
        auto* t = new ToggleSwitch (processor, pid, up, down, title);
        t->setBounds (Rectangle<float> (cx - 30.0f, cy - 44.0f, 60.0f, 88.0f).toNearestInt()); addAndMakeVisible (t); owned.add (t); return t;
    }
    void slowDown (float x, float y)       // Stop/Play, Auto Resume, LED length, Time/Bars
    {
        lamp (id::stop, "STOP / PLAY", col::red, -1, x + 34.0f, y + 36.0f, 48.0f)->glyph = "stop";
        lamp (id::autoResume, "AUTO RESUME", col::amber, -1, x + 108.0f, y + 40.0f, 34.0f);
        auto* led = new LedReadout (processor); led->setBounds ((int) x + 154, (int) y + 14, 108, 42); addAndMakeVisible (led); owned.add (led);
        toggle (id::slowFormat, "BARS", "TIME", "", x + 292.0f, y + 40.0f);
        labels.add ({ { x + 154.0f, y + 58.0f, 108.0f, 12.0f }, "LENGTH" });
    }

    void buildFull()
    {
        setSize (1000, 500);
        badge.setBounds (26, 26, 128, 104);
        // --- top row
        lamp (id::console, "CONSOLE", col::green, -1, 200.0f, 78.0f);
        groups.add ({ { 246.0f, 30.0f, 112.0f, 100.0f }, "GENERATION" });
        lamp (id::generation, "LACQUER", col::amber, 0, 275.0f, 78.0f); lamp (id::generation, "PRINT", col::amber, 1, 329.0f, 78.0f);
        groups.add ({ { 370.0f, 30.0f, 112.0f, 100.0f }, "TURNTABLE" });
        lamp (id::turntable, "STUDIO", col::amber, 0, 399.0f, 78.0f); lamp (id::turntable, "DJ", col::amber, 1, 453.0f, 78.0f);
        groups.add ({ { 494.0f, 30.0f, 166.0f, 100.0f }, "CARTRIDGE" });
        lamp (id::cartridge, "MM", col::amber, 0, 523.0f, 78.0f); lamp (id::cartridge, "MC", col::amber, 1, 577.0f, 78.0f); lamp (id::cartridge, "DJ", col::amber, 2, 631.0f, 78.0f);
        groups.add ({ { 672.0f, 30.0f, 304.0f, 100.0f }, "SURFACE NOISE" });
        knob (id::noise, "NOISE", KnobStyle::chicken, 44.0f, 706.0f, 76.0f);
        knob (id::crackle, "CRACKLE", KnobStyle::chicken, 44.0f, 768.0f, 76.0f);
        knob (id::density, "DENSITY", KnobStyle::chicken, 44.0f, 830.0f, 76.0f);
        knob (id::clicks, "CLICKS", KnobStyle::redCap, 44.0f, 892.0f, 76.0f)->setResetValue (50.0f);     // Off by default, double-click = 50
        knob (id::clicksGain, "GAIN", KnobStyle::skirted, 26.0f, 946.0f, 82.0f)->setBipolar (true);
        // --- middle row
        knob (id::input, "INPUT", KnobStyle::skirted, 62.0f, 84.0f, 232.0f)->setBipolar (true);
        knob (id::drive, "DRIVE", KnobStyle::gold, 62.0f, 190.0f, 232.0f);
        vuL.setBounds (262, 156, 226, 150); vuR.setBounds (512, 156, 226, 150);
        knob (id::vuCal, "VU CAL", KnobStyle::screw, 16.0f, 500.0f, 300.0f);
        toggle (id::meterSource, "OUT", "IN", "METER", 786.0f, 232.0f);
        knob (id::output, "OUTPUT", KnobStyle::skirted, 62.0f, 862.0f, 232.0f)->setBipolar (true);
        knob (id::monitor, "MONITOR", KnobStyle::chicken, 40.0f, 944.0f, 236.0f)->setPositions ({ "ST", "MONO", "L", "R" });
        // --- bottom row
        groups.add ({ { 24.0f, 340.0f, 140.0f, 136.0f }, "WOW" });
        knob (id::wowRate, "RATE", KnobStyle::chicken, 42.0f, 60.0f, 400.0f); knob (id::wowDepth, "DEPTH", KnobStyle::chicken, 42.0f, 128.0f, 400.0f)->setBipolar (true);
        groups.add ({ { 174.0f, 340.0f, 140.0f, 136.0f }, "FLUTTER" });
        knob (id::flutterRate, "RATE", KnobStyle::chicken, 42.0f, 210.0f, 400.0f); knob (id::flutterDepth, "DEPTH", KnobStyle::chicken, 42.0f, 278.0f, 400.0f)->setBipolar (true);
        groups.add ({ { 324.0f, 340.0f, 330.0f, 136.0f }, "SLOW DOWN" });
        slowDown (332.0f, 366.0f);
        knob (id::tonearm, "TONE ARM", KnobStyle::skirted, 56.0f, 704.0f, 402.0f);
        groups.add ({ { 754.0f, 340.0f, 222.0f, 136.0f }, "PHASE DISTORTION" });
        toggle (id::phaseOn, "ON", "OFF", "", 782.0f, 404.0f);
        knob (id::phaseLevel, "LEVEL", KnobStyle::chicken, 38.0f, 836.0f, 392.0f)->setBipolar (true);
        knob (id::modHP, "MOD HP", KnobStyle::chicken, 38.0f, 890.0f, 418.0f); knob (id::modLP, "MOD LP", KnobStyle::chicken, 38.0f, 944.0f, 392.0f);
    }

    void buildLite()
    {
        setSize (760, 300);
        badge.setBounds (22, 20, 112, 92);
        knob (id::input, "INPUT", KnobStyle::skirted, 50.0f, 186.0f, 62.0f)->setBipolar (true);
        groups.add ({ { 232.0f, 16.0f, 296.0f, 98.0f }, "SURFACE NOISE" });
        knob (id::noise, "NOISE", KnobStyle::chicken, 44.0f, 272.0f, 62.0f);
        knob (id::crackle, "CRACKLE", KnobStyle::chicken, 44.0f, 342.0f, 62.0f);
        knob (id::density, "DENSITY", KnobStyle::chicken, 44.0f, 412.0f, 62.0f);
        knob (id::clicks, "CLICKS", KnobStyle::redCap, 44.0f, 482.0f, 62.0f)->setResetValue (50.0f);
        knob (id::output, "OUTPUT", KnobStyle::skirted, 50.0f, 584.0f, 62.0f)->setBipolar (true);
        toggle (id::meterSource, "OUT", "IN", "METER", 686.0f, 64.0f);
        vuL.setBounds (22, 132, 200, 140); vuR.setBounds (232, 132, 200, 140);
        knob (id::vuCal, "VU CAL", KnobStyle::screw, 16.0f, 452.0f, 250.0f);
        groups.add ({ { 470.0f, 132.0f, 268.0f, 146.0f }, "SLOW DOWN" });
        lamp (id::stop, "STOP / PLAY", col::red, -1, 516.0f, 176.0f, 48.0f)->glyph = "stop";
        lamp (id::autoResume, "AUTO RESUME", col::amber, -1, 516.0f, 240.0f, 30.0f);
        auto* led = new LedReadout (processor); led->setBounds (566, 154, 112, 42); addAndMakeVisible (led); owned.add (led);
        toggle (id::slowFormat, "BARS", "TIME", "", 622.0f, 236.0f);
        labels.add ({ { 680.0f, 168.0f, 56.0f, 12.0f }, "LENGTH" });
    }

    void paintOverChildren (Graphics& g) override
    {
        for (auto& l : labels) VinylLookAndFeel::drawLegend (g, l.title, l.area, 9.0f);
    }

    VinylStudioProcessor& processor;
    Badge badge; VuMeter vuL { "L" }, vuR { "R" };
    OwnedArray<Component> owned; Array<Group> groups, labels;
    Image plate; float plateScale = 0;
};

//============================================================================== scaled content
class VinylStudioEditor::Content : public Component
{
public:
    Content (VinylStudioProcessor& p, VinylStudioEditor& e)
        : bar (p, e), console (p, [this] { about.setVisible (true); about.toFront (false); }), about (p)
    {
        const auto base = VinylStudioEditor::baseSize (p.lite);
        setSize (base.x, base.y);
        addAndMakeVisible (bar); bar.setBounds (0, 0, base.x, 30);
        int y = 30;
        if (! p.lite) { scene = std::make_unique<LatheScene> (p); addAndMakeVisible (*scene); scene->setBounds (0, y, base.x, (int) LatheScene::H); y += (int) LatheScene::H; }
        addAndMakeVisible (console); console.setTopLeftPosition (0, y);
        addChildComponent (about); about.setBounds (getLocalBounds());
    }
    TopBar bar; Console console; std::unique_ptr<LatheScene> scene; About about;
};

//============================================================================== editor
VinylStudioEditor::VinylStudioEditor (VinylStudioProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&look);
    content = std::make_unique<Content> (p, *this);
    addAndMakeVisible (*content);
    tooltips.setLookAndFeel (&look);

    const auto base = baseSize (p.lite);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) base.x / (double) base.y);     // every element scales together
    setResizeLimits (roundToInt (base.x * 0.6), roundToInt (base.y * 0.6), roundToInt (base.x * 2.5), roundToInt (base.y * 2.5));
    const int w = p.guiWidth > 0 ? jlimit (roundToInt (base.x * 0.6), roundToInt (base.x * 2.5), p.guiWidth) : base.x;
    setSize (w, roundToInt ((double) w * base.y / base.x));                         // last window size comes back with the session
    lastTick = Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

VinylStudioEditor::~VinylStudioEditor()
{
    stopTimer(); tooltips.setLookAndFeel (nullptr); setLookAndFeel (nullptr);
}

void VinylStudioEditor::setScalePercent (int percent)
{
    const auto base = baseSize (processor.lite);
    setSize (roundToInt (base.x * percent / 100.0), roundToInt (base.y * percent / 100.0));
}

void VinylStudioEditor::paint (Graphics& g) { g.fillAll (Colour (0xff141716)); }

void VinylStudioEditor::resized()
{
    const auto base = baseSize (processor.lite);
    const float scale = (float) getWidth() / (float) base.x;
    content->setTransform (AffineTransform::scale (scale));
    content->setBounds (0, 0, base.x, base.y);
    processor.guiWidth = getWidth();
}

void VinylStudioEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes(), dt = jlimit (0.0, 0.2, (now - lastTick) * 0.001);
    lastTick = now;
    if (content->scene) content->scene->tick (dt);
    content->console.setMeters (processor.vuL.load(), processor.vuR.load(), processor.param (id::vuCal));
    content->bar.refresh();
}
