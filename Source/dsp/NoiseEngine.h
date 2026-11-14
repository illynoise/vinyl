// Real-time vinyl surface noise: hiss loops, crackle grains, clicks, rumble.
// C++ port of the Stage 1 Python engine.  No allocation / locks in process().
#pragma once
#include "NoiseLibrary.h"
#include "Filters.h"

namespace vs
{
class NoiseEngine
{
public:
    struct Controls
    {
        float noiseGain = 1, crackleGain = 1, clickGain = 0, rumbleGain = 1;
        float clickRate = 0;          // clicks per second (0 = off)
        float duck = 1;               // crackle ducking under loud programme
    };

    void prepare (double sampleRate, const NoiseLibrary* library, uint64_t seed);
    void reset();
    void setProfile (const NoiseProfile& p);
    /** Writes n (<= maxChunk) samples of noise into l / r (overwrites). */
    void process (float* l, float* r, int n, const Controls& c) noexcept;

    enum { maxChunk = 64 };

    static float densityToRate (float density) noexcept   // 0..100 -> clicks per second
    {
        if (density < 0.05f) return 0.0f;
        const float lo = 1.0f / 7.5f, hi = 2.0f;
        return lo * std::pow (hi / lo, density / 100.0f);
    }
    static float clicksLevelToGain (float level) noexcept // Off..100, 50 = recorded level
    {
        return level < 0.05f ? 0.0f : (float) dbToGain ((level - 50.0f) * 0.36f);
    }

private:
    struct Voice { const int16_t* pcm = nullptr; int len = 0, delay = 0, layer = 0; double pos = 0, rate = 1; float m00 = 0, m01 = 0, m10 = 0, m11 = 0; bool on = false; };
    struct Scratch { bool on = false; double next = 0; int life = 0, k = 0; float amp = 0, angle = 0, drift = 0; int idx = 0; bool synth = false; };
    struct Layer
    {
        const std::vector<Event>* events = nullptr; const int16_t* pcm = nullptr; LayerStats st {};
        bool mono = false, synth = false, seeded = false;
        double rate = 0, nextBg = -1, nextCluster = -1, z = 0, revStart = 0;
        double pending[48] {}; int numPending = 0;
        Scratch scratches[24];
        float zones[4] {}; int numZones = 2;
    };
    struct HissVoice { double pos = 0; };

    void configureLayers();
    void scheduleLayer (int li, int n) noexcept;
    void startEvent (int li, double time, float amp, int idx, float angle, float semis, bool haveIdx) noexcept;
    float drawAmp (const Layer&) noexcept;
    int pick (const Layer&, float amp) noexcept;
    float drawAngle (const Layer&, int idx) noexcept;
    double drawGap (const Layer&, double rate) noexcept;
    void initScratches (Layer&) noexcept;
    void renderHiss (float* l, float* r, int n) noexcept;

    const NoiseLibrary* lib = nullptr;
    const SampleSet* set = nullptr;
    NoiseProfile profile;
    double fs = 48000.0, ratio = 1.0, now = 0, busyUntil = 0, revPeriod = 1.8 * 48000.0;
    Rng rng;
    Layer layers[2];
    Voice voices[24];
    HissVoice hiss[2]; double hissRemain = 0, hissFade = -1; int xfade = 12000;
    Biquad rumM[3], rumS[3]; double rumbleNorm = 1;
    float gNoise = 0, gCrackle = 0, gClick = 0, gRumble = 0, smooth = 0.001f;
    float hissDb = 1, crackleLin = 1, clickLin = 1, rumbleLin = 0;
    float lastClickRate = -1;
};
} // namespace vs
