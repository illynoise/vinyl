// Offline checks for Vinyl Studio: CPU load, glitch tests (tone-arm sweep, slow down),
// state round trip, noise levels per setup, GUI render cost and screenshots.
//   vs_tests [output-folder]
#include "../Source/PluginProcessor.h"
#include "../Source/gui/PluginEditor.h"
#include <iostream>

using namespace juce;

static void setP (VinylStudioProcessor& p, const char* pid, float v)
{
    if (auto* prm = p.apvts.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
static double dB (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

struct Runner
{
    Runner (VinylStudioProcessor& proc, double sampleRate, int block) : p (proc), fs (sampleRate), bs (block), buf (proc.mono ? 1 : 2, block)
    {
        p.setRateAndBufferSizeDetails (fs, bs); p.prepareToPlay (fs, bs);
    }
    /** gen(n) -> input sample; returns left channel output. */
    std::vector<float> run (double seconds, const std::function<float (int64)>& gen, const std::function<void (int64)>& perBlock = {})
    {
        std::vector<float> out; MidiBuffer midi; const int64 total = (int64) (seconds * fs);
        for (int64 pos = 0; pos < total; pos += bs)
        {
            if (perBlock) perBlock (pos);
            for (int i = 0; i < bs; ++i) { const float x = gen (n++); for (int c = 0; c < buf.getNumChannels(); ++c) buf.setSample (c, i, x); }
            const auto t0 = Time::getHighResolutionTicks();
            p.processBlock (buf, midi);
            ticks += Time::getHighResolutionTicks() - t0;
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + bs);
        }
        processed += (double) total / fs;
        return out;
    }
    double cpuPercent() const { return 100.0 * Time::highResolutionTicksToSeconds (ticks) / processed; }
    VinylStudioProcessor& p; double fs; int bs; AudioBuffer<float> buf; int64 n = 0, ticks = 0; double processed = 0;
};

static double rmsOf (const std::vector<float>& v, size_t from = 0)
{
    double a = 0; for (size_t i = from; i < v.size(); ++i) a += (double) v[i] * v[i];
    return std::sqrt (a / (double) std::max<size_t> (1, v.size() - from));
}
static float programme (int64 n, double fs, Random& r)       // music-like test signal at about -18 dBFS rms
{
    const double t = (double) n / fs;
    return (float) (0.09 * std::sin (2 * MathConstants<double>::pi * 110.0 * t) + 0.07 * std::sin (2 * MathConstants<double>::pi * 1320.0 * t)
                    + 0.05 * std::sin (2 * MathConstants<double>::pi * 6100.0 * t) + 0.03 * (r.nextDouble() * 2.0 - 1.0));
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    File outDir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1]) : File::getCurrentWorkingDirectory().getChildFile ("test-output");
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const String& what) { std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl; if (! ok) ++failures; };
    Random rnd (1);

    //-------------------------------------------------------------------------- CPU
    std::cout << "\n== CPU (single core, % of real time, 512-sample blocks, all noise layers + clicks on) ==" << std::endl;
    struct V { bool lite, mono; };
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (V v : { V { false, false }, V { false, true }, V { true, false }, V { true, true } })
        {
            VinylStudioProcessor p (v.lite, v.mono);
            setP (p, vs::id::clicks, 60); setP (p, vs::id::density, 80); setP (p, vs::id::generation, 1); setP (p, vs::id::drive, 30); setP (p, vs::id::tonearm, 50);
            Runner r (p, fs, 512);
            r.run (1.0, [&] (int64 n) { return programme (n, fs, rnd); });
            r.ticks = 0; r.processed = 0;
            r.run (20.0, [&] (int64 n) { return programme (n, fs, rnd); });
            std::cout << "  " << String (p.getName()).paddedRight (' ', 24) << String (fs / 1000.0, 1).paddedLeft (' ', 6) << " kHz  " << String (r.cpuPercent(), 2).paddedLeft (' ', 6)
                      << " %   latency " << p.getLatencySamples() << " samples" << std::endl;
        }

    //-------------------------------------------------------------------------- transparency / drive
    std::cout << "\n== Signal path ==" << std::endl;
    auto quiet = [] (VinylStudioProcessor& p) { setP (p, vs::id::noise, -60); setP (p, vs::id::crackle, -60); setP (p, vs::id::clicks, 0); };
    {
        const double fs = 48000.0;
        auto sine = [fs] (int64 n) { return (float) (0.126 * std::sin (2 * MathConstants<double>::pi * 1000.0 * (double) n / fs)); };
        VinylStudioProcessor a (false, false); quiet (a); Runner ra (a, fs, 256);
        const double l0 = dB (rmsOf (ra.run (3.0, sine), 48000));
        VinylStudioProcessor b (false, false); quiet (b); setP (b, vs::id::drive, 100); Runner rb (b, fs, 256);
        const double l1 = dB (rmsOf (rb.run (3.0, sine), 48000));
        std::cout << "  1 kHz -18 dBFS in: out " << String (l0, 2) << " dBFS (Drive 0), " << String (l1, 2) << " dBFS (Drive 100)" << std::endl;
        check (std::abs (l0 - (-21.0)) < 1.5, "unity gain through the default chain (within 1.5 dB)");
        check (std::abs (l1 - l0) < 3.0, "Drive keeps loudness roughly constant (< 3 dB change at full drive)");
        VinylStudioProcessor c (true, false); quiet (c); Runner rc (c, fs, 256);
        const auto o = rc.run (1.0, sine); double err = 0; for (size_t i = 0; i < o.size(); ++i) err = std::max (err, (double) std::abs (o[i] - sine ((int64) i)));
        check (err < 1e-5, "Lite passes audio bit-transparently with the noise off (max error " + String (err, 8) + ")");
    }

    //-------------------------------------------------------------------------- glitch tests
    std::cout << "\n== Automation / slow-down glitch tests ==" << std::endl;
    {
        // full-side tone-arm travel: 0 -> 100 over 20 s while a 3 kHz tone plays; look for discontinuities
        const double fs = 48000.0, f = 3000.0, amp = 0.126;
        VinylStudioProcessor p (false, false); quiet (p); setP (p, vs::id::wowRate, 0); setP (p, vs::id::flutterRate, 0); setP (p, vs::id::phaseOn, 0);
        Runner r (p, fs, 64);
        const auto o = r.run (22.0, [&] (int64 n) { return (float) (amp * std::sin (2 * MathConstants<double>::pi * f * (double) n / fs)); },
                              [&] (int64 pos) { setP (p, vs::id::tonearm, (float) jlimit (0.0, 100.0, ((double) pos / fs - 1.0) * 5.0)); });
        const double w = 2 * MathConstants<double>::pi * f / fs; double worst = 0, env0 = 0, env1 = 0;
        for (size_t i = 48000 + 2; i < o.size(); ++i) worst = std::max (worst, (double) std::abs (o[i] - 2.0f * o[i - 1] + o[i - 2]));
        for (size_t i = 48000; i < 96000; ++i) env0 = std::max (env0, (double) std::abs (o[i]));
        for (size_t i = o.size() - 48000; i < o.size(); ++i) env1 = std::max (env1, (double) std::abs (o[i]));
        const double expected = env0 * w * w;         // curvature of a clean sine
        std::cout << "  tone-arm sweep: worst curvature " << String (worst / expected, 2) << " x a clean sine; 3 kHz level " << String (dB (env0), 1) << " -> " << String (dB (env1), 1) << " dBFS" << std::endl;
        check (worst < expected * 1.6, "tone-arm automation over a full side is free of clicks");
        check (env1 < env0 * 0.95, "moving the arm inward reduces high frequencies");
    }
    for (bool lite : { false, true })
    {
        // slow down, stop, restart
        const double fs = 48000.0, f = 440.0, amp = 0.25;
        VinylStudioProcessor p (lite, false); quiet (p); setP (p, vs::id::slowLength, 2.0f);
        if (! lite) { setP (p, vs::id::wowRate, 0); setP (p, vs::id::flutterRate, 0); setP (p, vs::id::phaseOn, 0); }
        Runner r (p, fs, 128);
        double minSpeed = 1.0;
        const auto o = r.run (9.0, [&] (int64 n) { return (float) (amp * std::sin (2 * MathConstants<double>::pi * f * (double) n / fs)); },
                              [&] (int64 pos) { const double t = (double) pos / fs; setP (p, vs::id::stop, (t > 1.5 && t < 5.0) ? 1.0f : 0.0f); minSpeed = std::min (minSpeed, (double) p.platterSpeed.load()); });
        double worst = 0, peak = 0; for (size_t i = 24000; i < o.size(); ++i) { worst = std::max (worst, (double) std::abs (o[i] - o[i - 1])); peak = std::max (peak, (double) std::abs (o[i])); }
        const double clean = peak * 2 * MathConstants<double>::pi * f / fs;
        double stoppedRms = 0; for (size_t i = (size_t) (4.0 * fs); i < (size_t) (4.9 * fs); ++i) stoppedRms += (double) o[i] * o[i];
        std::cout << "  " << p.getName() << " slow down: worst step " << String (worst / clean, 2) << " x a clean 440 Hz sine, platter minimum speed " << String (minSpeed, 3)
                  << ", level while stopped " << String (dB (std::sqrt (stoppedRms / (0.9 * fs))), 1) << " dBFS" << std::endl;
        check (worst < clean * 1.8, p.getName() + ": stop / restart is free of clicks");
        check (minSpeed < 0.001 && p.platterSpeed.load() > 0.999, p.getName() + ": platter stops and returns to full speed");
    }

    //-------------------------------------------------------------------------- noise per setup
    std::cout << "\n== Surface noise, silence in, default knobs (12 setups) ==" << std::endl;
    {
        static const char* gn[] = { "Lacquer", "Print" }; static const char* tn[] = { "Studio", "DJ" }; static const char* cn[] = { "MM", "MC", "DJ" };
        double lo = 0, hi = -200;
        for (int g = 0; g < 2; ++g) for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c)
        {
            VinylStudioProcessor p (false, false); setP (p, vs::id::generation, (float) g); setP (p, vs::id::turntable, (float) t); setP (p, vs::id::cartridge, (float) c);
            Runner r (p, 48000.0, 512);
            const auto o = r.run (12.0, [] (int64) { return 0.0f; });
            double pk = 0; for (size_t i = 48000; i < o.size(); ++i) pk = std::max (pk, (double) std::abs (o[i]));
            const double l = dB (rmsOf (o, 48000)); lo = std::min (lo, l); hi = std::max (hi, l);
            std::cout << "  " << String (gn[g]).paddedRight (' ', 8) << String (tn[t]).paddedRight (' ', 7) << String (cn[c]).paddedRight (' ', 3) << "  rms " << String (l, 1).paddedLeft (' ', 6)
                      << " dBFS   peak " << String (dB (pk), 1).paddedLeft (' ', 6) << " dBFS" << std::endl;
        }
        check (hi - lo > 6.0 && hi < -30.0, "noise differs between setups and stays below -30 dBFS rms at default");
        VinylStudioProcessor p (true, false); setP (p, vs::id::noise, -60); setP (p, vs::id::crackle, -60); setP (p, vs::id::clicks, 80); setP (p, vs::id::density, 100);
        Runner r (p, 48000.0, 512); const auto o = r.run (30.0, [] (int64) { return 0.0f; });
        int clicks = 0; size_t lastHit = 0; for (size_t i = 0; i < o.size(); ++i) if (std::abs (o[i]) > 0.002f && i > lastHit + 2400) { ++clicks; lastHit = i; }
        std::cout << "  clicks only, density 100: about " << String (clicks / 30.0, 2) << " clicks per second" << std::endl;
        check (clicks > 20 && clicks < 120, "click density at maximum is near 2 per second");
    }

    //-------------------------------------------------------------------------- state
    std::cout << "\n== State ==" << std::endl;
    for (bool lite : { false, true })
    {
        VinylStudioProcessor a (lite, false);
        for (auto* prm : a.getParameters()) prm->setValueNotifyingHost (rnd.nextFloat());
        StringArray before; for (auto* prm : a.getParameters()) before.add (prm->getCurrentValueAsText());
        a.guiWidth = 1234;
        MemoryBlock mb; a.getStateInformation (mb);
        VinylStudioProcessor b (lite, false); b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = b.guiWidth == 1234;
        for (int i = 0; i < a.getParameters().size(); ++i) same = same && before[i] == b.getParameters()[i]->getCurrentValueAsText();
        check (same, String (a.getName()) + ": all " + String (a.getParameters().size()) + " parameters and the window size survive save / restore");
    }
    {
        VinylStudioProcessor p (false, false);
        for (int i = 0; i < p.getNumPrograms(); ++i) p.setCurrentProgram (i);
        check (p.getNumPrograms() >= 7, String (p.getNumPrograms()) + " factory presets load");
        setP (p, vs::id::cartridge, 2);
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (std::abs (p.param (vs::id::modLP) - 4000.0f) < 1.0f && std::abs (p.param (vs::id::phaseLevel) - 6.0f) < 0.1f, "changing the cartridge updates the phase-distortion / modulator defaults");
    }

    //-------------------------------------------------------------------------- GUI
    std::cout << "\n== GUI ==" << std::endl;
    for (bool lite : { false, true })
    {
        VinylStudioProcessor p (lite, false);
        if (! lite) { setP (p, vs::id::tonearm, 35); setP (p, vs::id::clicks, 45); setP (p, vs::id::crackle, 6); }
        p.setRateAndBufferSizeDetails (48000.0, 512); p.prepareToPlay (48000.0, 512);
        AudioBuffer<float> buf (2, 512); MidiBuffer midi;
        for (int i = 0; i < 60; ++i) { for (int s = 0; s < 512; ++s) { const float x = programme (i * 512 + s, 48000.0, rnd) * 0.8f; buf.setSample (0, s, x); buf.setSample (1, s, x * 0.8f); } p.processBlock (buf, midi); }
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        const auto base = VinylStudioEditor::baseSize (lite);
        for (int pct : { 75, 100, 150, 200 })
        {
            ed->setSize (base.x * pct / 100, base.y * pct / 100);
            MessageManager::getInstance()->runDispatchLoopUntil (120);
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto f = outDir.getChildFile (String (lite ? "lite" : "full") + "_" + String (pct) + ".png");
            f.deleteFile(); FileOutputStream os (f); PNGImageFormat png; png.writeImageToStream (img, os);
            check (img.getWidth() == base.x * pct / 100 && ed->getHeight() == base.y * pct / 100, String (p.getName()) + " renders at " + String (pct) + "% (" + String (img.getWidth()) + " x " + String (img.getHeight()) + ")");
        }
        for (int pct : { 100, 200 })
        {
            ed->setSize (base.x * pct / 100, base.y * pct / 100);
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            const Rectangle<int> area = lite ? ed->getLocalBounds() : Rectangle<int> (0, 30 * pct / 100, ed->getWidth(), 330 * pct / 100);
            const int frames = 40; const auto t0 = Time::getMillisecondCounterHiRes();
            for (int i = 0; i < frames; ++i) ed->createComponentSnapshot (area, true, 1.0f);
            const double ms = (Time::getMillisecondCounterHiRes() - t0) / frames;
            std::cout << "  " << p.getName() << " at " << pct << "%: " << String (ms, 2) << " ms to redraw the " << (lite ? "whole window" : "animated lathe scene")
                      << " (software renderer) = " << String (ms * 30.0 / 10.0, 1) << " % of one core at 30 fps" << std::endl;
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : String (failures) + " CHECK(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
