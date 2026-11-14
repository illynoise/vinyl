// Vinyl Studio - Base Jase Plugins
// Parameter IDs, ranges and the setup-dependent default tables.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace vs
{
namespace id
{
    #define VS_ID(n) inline constexpr const char* n = #n;
    VS_ID(console) VS_ID(input) VS_ID(drive) VS_ID(generation) VS_ID(turntable) VS_ID(cartridge)
    VS_ID(tonearm) VS_ID(wowRate) VS_ID(wowDepth) VS_ID(flutterRate) VS_ID(flutterDepth)
    VS_ID(phaseOn) VS_ID(phaseLevel) VS_ID(modHP) VS_ID(modLP)
    VS_ID(noise) VS_ID(crackle) VS_ID(clicks) VS_ID(density) VS_ID(clicksGain)
    VS_ID(slowLength) VS_ID(slowBeats) VS_ID(slowFormat) VS_ID(stop) VS_ID(autoResume)
    VS_ID(output) VS_ID(meterSource) VS_ID(vuCal) VS_ID(monitor)
    #undef VS_ID
}

inline constexpr float kNoiseMinDb = -60.0f;          // shown as -inf
inline const juce::StringArray kBeatNames { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars" };
inline constexpr float kBeatCounts[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };   // quarter notes (4/4 bars)

/** Values that follow the Generation / Turntable / Cartridge setup automatically. */
struct SetupDefaults { float wowRate, flutterRate, phaseLevel, modHP, modLP; };

inline SetupDefaults setupDefaults (int generation, int turntable, int cartridge)
{
    SetupDefaults d { 50.0f, 50.0f, 0.0f, 200.0f, 6000.0f };
    if (turntable == 1)  { d.wowRate = 56.0f; d.flutterRate = 60.0f; d.phaseLevel += 2.0f; }
    if (generation == 1) { d.phaseLevel += 3.0f; d.flutterRate += 4.0f; }
    if (cartridge == 1)  { d.phaseLevel -= 6.0f; d.modHP = 120.0f; d.modLP = 9000.0f; }
    if (cartridge == 2)  { d.phaseLevel += 6.0f; d.modHP = 400.0f; d.modLP = 4000.0f; }
    if (generation == 1) d.modLP *= 0.8f;
    return d;
}

inline juce::String dbText (float v, float minDb = -1000.0f)
{
    if (v <= minDb + 0.01f) return "-inf dB";
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool lite)
{
    using namespace juce;
    using F = AudioParameterFloat; using B = AudioParameterBool; using C = AudioParameterChoice;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const char* s) { return ParameterID { s, 1 }; };

    auto dB = [&] (const char* s, const String& name, float lo, float hi, float def, float minShown = -1000.0f)
    {
        return std::make_unique<F> (pid (s), name, NormalisableRange<float> (lo, hi, 0.1f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([minShown] (float v, int) { return dbText (v, minShown); })
                                           .withValueFromStringFunction ([lo] (const String& t) { return t.containsIgnoreCase ("inf") ? lo : t.getFloatValue(); }));
    };
    auto offTo100 = [&] (const char* s, const String& name, float def)
    {
        return std::make_unique<F> (pid (s), name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Off") : String (v, 1); })
                                           .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("off") ? 0.0f : t.getFloatValue(); }));
    };
    auto plain = [&] (const char* s, const String& name, float lo, float hi, float def, const String& unit, int decimals = 1)
    {
        return std::make_unique<F> (pid (s), name, NormalisableRange<float> (lo, hi, 0.1f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([unit, decimals] (float v, int) { return String (v, decimals) + unit; }));
    };
    auto freq = [&] (const char* s, const String& name, float def)
    {
        NormalisableRange<float> r (16.0f, 20000.0f, 0.0f, 0.25f);
        return std::make_unique<F> (pid (s), name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; })
                                           .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("k") ? t.getFloatValue() * 1000.0f : t.getFloatValue(); }));
    };

    l.add (dB (id::input,  "Input",  -18.0f, 18.0f, 0.0f));
    l.add (dB (id::output, "Output", -18.0f, 18.0f, 0.0f));
    l.add (dB (id::noise,   "Noise Level",   kNoiseMinDb, 48.0f, 0.0f, kNoiseMinDb));
    l.add (dB (id::crackle, "Crackle Level", kNoiseMinDb, 48.0f, 0.0f, kNoiseMinDb));
    l.add (offTo100 (id::clicks,  "Clicks Level", 0.0f));
    l.add (offTo100 (id::density, "Clicks Density", 20.0f));

    NormalisableRange<float> len (0.001f, 30.0f, 0.0f, 0.4f);
    l.add (std::make_unique<F> (pid (id::slowLength), "Slow Down Length", len, 12.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; })
                                       .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("ms") ? t.getFloatValue() / 1000.0f : t.getFloatValue(); })));
    l.add (std::make_unique<C> (pid (id::slowBeats),  "Slow Down Bars", kBeatNames, 2));
    l.add (std::make_unique<C> (pid (id::slowFormat), "Slow Down Format", StringArray { "Time", "Bars" }, 0));
    l.add (std::make_unique<B> (pid (id::stop),       "Stop", false));
    l.add (std::make_unique<B> (pid (id::autoResume), "Auto Resume", false));
    l.add (std::make_unique<C> (pid (id::meterSource), "Meter", StringArray { "In", "Out" }, 1));
    l.add (plain (id::vuCal, "VU Calibration", 8.0f, 24.0f, 18.0f, " dB"));

    if (! lite)
    {
        l.add (std::make_unique<B> (pid (id::console), "Console", true));
        l.add (plain (id::drive, "Drive", 0.0f, 100.0f, 0.0f, ""));
        l.add (std::make_unique<C> (pid (id::generation), "Generation", StringArray { "Lacquer", "Print" }, 0));
        l.add (std::make_unique<C> (pid (id::turntable),  "Turntable",  StringArray { "Studio", "DJ" }, 0));
        l.add (std::make_unique<C> (pid (id::cartridge),  "Cartridge",  StringArray { "MM", "MC", "DJ" }, 0));
        l.add (plain (id::tonearm, "Tone Arm Position", 0.0f, 100.0f, 0.0f, ""));
        l.add (offTo100 (id::wowRate, "Wow Rate", 50.0f));
        l.add (plain (id::wowDepth, "Wow Depth", -50.0f, 50.0f, 0.0f, ""));
        l.add (offTo100 (id::flutterRate, "Flutter Rate", 50.0f));
        l.add (plain (id::flutterDepth, "Flutter Depth", -50.0f, 50.0f, 0.0f, ""));
        l.add (std::make_unique<B> (pid (id::phaseOn), "Phase Distortion", true));
        l.add (dB (id::phaseLevel, "Phase Distortion Level", -48.0f, 48.0f, 0.0f));
        l.add (freq (id::modHP, "Modulator HP", 200.0f));
        l.add (freq (id::modLP, "Modulator LP", 6000.0f));
        l.add (dB (id::clicksGain, "Clicks Gain", -24.0f, 24.0f, 0.0f));
        l.add (std::make_unique<C> (pid (id::monitor), "Monitor", StringArray { "Stereo", "Mono", "Left", "Right" }, 0));
    }
    return l;
}
} // namespace vs
