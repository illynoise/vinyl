// The record path of the full version, modelled stage by stage:
// console -> cutting (RIAA record, HF limiter, elliptical EQ) -> generation ->
// cartridge -> tone-arm position -> RIAA playback   [oversampled]
// then wow (pitch), flutter (level) and phase distortion at the host rate.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Filters.h"

namespace vs
{
class VinylChain
{
public:
    struct Params
    {
        bool console = true, phaseOn = true;
        int generation = 0, turntable = 0, cartridge = 0;
        float tonearm = 0;                                  // 0..1
        float wowRate = 50, wowDepth = 0, flutterRate = 50, flutterDepth = 0;
        float phaseLevelDb = 0, modHP = 200, modLP = 6000, noiseTiltDb = 0;
    };
    enum { maxChunk = 64 };

    void prepare (double sampleRate);
    void reset();
    int latencySamples() const noexcept { return latency; }
    void update (const Params& p) noexcept;                 // once per chunk
    void processRecord (float* l, float* r, int n) noexcept; // console .. RIAA playback
    void colourNoise (float* l, float* r, int n) noexcept;   // what the cartridge / arm do to surface noise
    void processPlayback (float* l, float* r, int n) noexcept; // wow, flutter, phase distortion

private:
    void designCartridge (Biquad& res, Biquad& eq, Biquad& hp, double rate) const noexcept;
    void designArm (Biquad& shelf, Biquad& lp, double rate, double pos) const noexcept;
    static inline float readDelay (const std::vector<float>& b, int w, double delay) noexcept
    {
        const int mask = (int) b.size() - 1;
        const double rp = (double) w - delay; const double fl = std::floor (rp);
        const int k = (int) fl; const float t = (float) (rp - fl);
        return hermite (b[(size_t) ((k - 1) & mask)], b[(size_t) (k & mask)], b[(size_t) ((k + 1) & mask)], b[(size_t) ((k + 2) & mask)], t);
    }

    Params p;
    double fs = 48000.0, fsOs = 192000.0;
    int latency = 0, osFactor = 4;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> chunk { 2, maxChunk };

    // oversampled stages
    Biquad conHp, conLo, conHi, conLp, pre, hfSplit, sideHp, genShelf, genLow, genMid, cartRes, cartEq, cartHp, armShelf, armLp, de, dc;
    double limEnv[2] {}, limAtk = 0, limRel = 0;
    float conMix = 1, d2 = 0, d3 = 0, xtalk = 0, armPos = 0;
    int lastCart = -1, lastGen = -1;

    // noise colour (host rate)
    Biquad nRes, nEq, nHp, nShelf, nLp, nGen, nTilt;

    // wow / flutter
    std::vector<float> wowBuf[2]; int wowW = 0; double wowCentre = 0;
    double wowPh = 0, eccPh = 0, flPh = 0, wowD = 0, eccD = 0, flA = 0, drift1 = 0, drift2 = 0, flN1 = 0, flN2 = 0;
    double wowInc = 0, flInc = 0, tgtWowD = 0, tgtEccD = 0, tgtFlA = 0;
    Rng rng;

    // phase distortion
    std::vector<float> phBuf[2]; int phW = 0; double phD0 = 0, phDepth = 0, tgtPhDepth = 0;
    Biquad modHp, modLp; float lastHP = -1, lastLP = -1;
};
} // namespace vs
