// Embedded sample library: click / grain sets and hiss loops analysed in Stage 1.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <cstdint>

namespace vs
{
struct LayerStats { float rate, mu, sigma, pmin, pmax, frac, meanN, tau, shape, slow, lock, surv; };
struct Event      { uint32_t offset, length; float peak, angle, scale; };

struct SampleSet
{
    bool mono = false;
    float rotationPeriod = 1.8f, loopScale = 1.0f;
    LayerStats grain {}, click {};
    std::vector<Event> grains, clicks;          // sorted by peak level
    std::vector<int16_t> pcm;                   // interleaved stereo, 48 kHz
    std::vector<int16_t> loop;                  // hiss loop, interleaved (loopCh channels), 48 kHz
    int loopLen = 0, loopCh = 2;
};

/** Twelve setups (Generation x Turntable x Cartridge) -> how the noise behaves. */
struct NoiseProfile
{
    int set = 0;                 // sample set (which record was sampled)
    float hissDb = 0, crackleDb = 0, clickDb = 0, rumbleDb = -70;
    float crackleRateMul = 1, sigmaMul = 1, pitchSemis = 0, wobbleDb = 0.4f, tiltDb = 0;
};

inline NoiseProfile noiseProfile (int generation, int turntable, int cartridge)
{
    // sample set + level trim so the default hiss sits where that kind of disc would
    static const NoiseProfile base[2][2] = {
        //            set  hiss  crackle click rumble rate sig pitch wobble tilt
        /* Lacquer */ { { 0, -10.0f, -10.0f, -9.0f, -74.0f, 1.0f, 1.0f, 0.0f, 0.3f, 0.0f },     // Studio: quiet acetate
                        { 1, -13.0f, -13.0f, -12.0f, -64.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.0f } },   // DJ: played dub plate
        /* Print   */ { { 2, -21.0f, -19.0f, -15.0f, -70.0f, 1.0f, 1.0f, 0.0f, 0.7f, 0.0f },  // Studio: hissy pressing, repeating ticks
                        { 3, -11.5f, -11.5f, -12.0f, -60.0f, 1.0f, 1.0f, 0.0f, 0.9f, 0.0f } } }; // DJ: battered pressing
    NoiseProfile p = base[juce::jlimit (0, 1, generation)][juce::jlimit (0, 1, turntable)];
    if (cartridge == 1)       // MC: lowest noise, finer ticks, brighter
    { p.hissDb -= 3.0f; p.crackleDb -= 2.0f; p.clickDb -= 1.0f; p.sigmaMul = 0.9f; p.pitchSemis = 1.0f; p.crackleRateMul = 0.85f; p.tiltDb = 1.0f; }
    else if (cartridge == 2)  // DJ: heavy spherical stylus - louder, denser, duller
    { p.hissDb += 2.0f; p.crackleDb += 3.0f; p.clickDb += 2.0f; p.sigmaMul = 1.15f; p.pitchSemis = -2.0f; p.crackleRateMul = 1.4f; p.rumbleDb += 2.0f; p.tiltDb = -1.5f; }
    return p;
}

class NoiseLibrary
{
public:
    enum { numSets = 4 };
    NoiseLibrary();
    SampleSet sets[numSets];
    SampleSet synth;        // fallback clicks: impulse -> stylus resonance -> RIAA de-emphasis
private:
    void load (SampleSet&, const char* bin, int binSize, const char* flac, int flacSize);
    void buildSynth();
};
} // namespace vs
