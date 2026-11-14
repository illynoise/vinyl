#include "VinylChain.h"

namespace vs
{
void VinylChain::prepare (double sampleRate)
{
    fs = sampleRate;
    // oversample the nonlinear stages: 4x at 44.1/48 k, 2x at 88.2/96 k, none above
    const int order = fs < 50000.0 ? 2 : (fs < 100000.0 ? 1 : 0);
    osFactor = 1 << order; fsOs = fs * osFactor;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing (maxChunk);

    conHp.highpass (fsOs, 18.0, 0.7); conLo.lowShelf (fsOs, 90.0, 0.6); conHi.highShelf (fsOs, 11000.0, 0.8); conLp.lowpass (fsOs, 28000.0, 0.62);
    pre.riaaRecord (fsOs); de.riaaPlayback (fsOs);
    hfSplit.highpass (fsOs, 5000.0, 0.7); sideHp.highpass (fsOs, 150.0, 0.7071);
    genShelf.highShelf (fsOs, 12000.0, -1.5); genLow.peak (fsOs, 180.0, 0.9, 0.7); genMid.peak (fsOs, 3000.0, 1.0, -0.5);
    dc.highpass (fsOs, 6.0, 0.7);
    limAtk = std::exp (-1.0 / (fsOs * 0.0002)); limRel = std::exp (-1.0 / (fsOs * 0.03));

    int sz = 1; while (sz < (int) (fs * 0.04)) sz <<= 1;
    for (auto& b : wowBuf) b.assign ((size_t) sz, 0.0f);
    wowCentre = 0.016 * fs;
    int sz2 = 1; while (sz2 < (int) (fs * 0.004)) sz2 <<= 1;
    for (auto& b : phBuf) b.assign ((size_t) sz2, 0.0f);
    phD0 = std::round (0.001 * fs);

    latency = (int) std::lround (os->getLatencyInSamples()) + (int) std::lround (wowCentre) + (int) phD0;
    rng.seed (99);
    lastCart = lastGen = -1; lastHP = lastLP = -1;
    armPos = p.tonearm;
    update (p);
    reset();
}

void VinylChain::reset()
{
    if (os) os->reset();
    for (auto* b : { &conHp, &conLo, &conHi, &conLp, &pre, &hfSplit, &sideHp, &genShelf, &genLow, &genMid, &cartRes, &cartEq, &cartHp, &armShelf, &armLp, &de, &dc,
                     &nRes, &nEq, &nHp, &nShelf, &nLp, &nGen, &nTilt, &modHp, &modLp })
        b->reset();
    limEnv[0] = limEnv[1] = 0;
    for (auto& b : wowBuf) std::fill (b.begin(), b.end(), 0.0f);
    for (auto& b : phBuf) std::fill (b.begin(), b.end(), 0.0f);
    wowD = tgtWowD; eccD = tgtEccD; flA = tgtFlA; phDepth = tgtPhDepth; conMix = p.console ? 1.0f : 0.0f;
}

void VinylChain::designCartridge (Biquad& res, Biquad& eq, Biquad& hp, double rate) const noexcept
{
    switch (p.cartridge)
    {
        case 1:  res.lowpass (rate, 30000.0, 1.25); eq.lowShelf (rate, 45.0, 1.0);        hp.highpass (rate, 8.0, 0.7);  break;   // MC: extended both ends
        case 2:  res.lowpass (rate, 12500.0, 0.85); eq.peak (rate, 2400.0, 0.7, 3.0);     hp.highpass (rate, 45.0, 0.7); break;   // DJ: mid-forward
        default: res.lowpass (rate, 19500.0, 0.72); eq.peak (rate, 9000.0, 0.8, -0.4);    hp.highpass (rate, 14.0, 0.7); break;   // MM: flat
    }
}

void VinylChain::designArm (Biquad& shelf, Biquad& lp, double rate, double pos) const noexcept
{
    // towards the label the groove moves slower under the stylus: short wavelengths are lost
    shelf.highShelf (rate, 7000.0, -7.5 * std::pow (pos, 1.4), 0.6);
    lp.lowpass (rate, 24000.0 - 12500.0 * pos * pos, 0.6);
}

void VinylChain::update (const Params& np) noexcept
{
    p = np;
    if (p.cartridge != lastCart)
    {
        lastCart = p.cartridge;
        designCartridge (cartRes, cartEq, cartHp, fsOs);
        designCartridge (nRes, nEq, nHp, fs);
    }
    if (p.generation != lastGen)
    {
        lastGen = p.generation;
        if (p.generation == 1) nGen.highShelf (fs, 12000.0, -1.5); else nGen.bypass();
    }
    nTilt.highShelf (fs, 6000.0, p.noiseTiltDb);
    armPos += 0.08f * (p.tonearm - armPos);                      // smooth enough to automate a full side
    designArm (armShelf, armLp, fsOs, armPos);
    designArm (nShelf, nLp, fs, armPos);

    static const float cd2[] = { 0.010f, 0.005f, 0.050f }, cd3[] = { 0.020f, 0.010f, 0.120f }, cx[] = { 0.040f, 0.025f, 0.100f };
    const int c = juce::jlimit (0, 2, p.cartridge);
    const float inner = armPos * armPos;                          // inner-groove (tracing) distortion
    d2 = cd2[c] * (1.0f + 3.0f * inner); d3 = cd3[c] * (1.0f + 6.0f * inner); xtalk = cx[c];

    // wow: pitch deviation -> delay excursion D = dev / (2 pi f)
    auto depthMul = [] (float d) { return d <= -49.5f ? 0.0 : std::pow (2.0, d / 12.5); };
    const double wowHz = 0.5556 * std::pow (4.0, (p.wowRate - 50.0) / 50.0);
    const double wowDev = (p.turntable == 1 ? 0.0012 : 0.0003) * depthMul (p.wowDepth) * (p.wowRate < 0.05f ? 0.0 : 1.0);
    const double eccDev = (p.generation == 1 ? 0.0008 : 0.00015) * depthMul (p.wowDepth) * (p.wowRate < 0.05f ? 0.0 : 1.0);
    wowInc = 2 * kPi * wowHz / fs;
    tgtWowD = wowDev / (2 * kPi * wowHz) * fs;
    tgtEccD = eccDev / (2 * kPi * 0.5556) * fs;                   // off-centre pressing: exactly once per revolution
    const double flHz = 8.0 * std::pow (4.0, (p.flutterRate - 50.0) / 50.0);
    flInc = 2 * kPi * flHz / fs;
    tgtFlA = std::min (0.9, ((p.turntable == 1 ? 0.008 : 0.002) + (p.generation == 1 ? 0.002 : 0.0)) * depthMul (p.flutterDepth) * (p.flutterRate < 0.05f ? 0.0 : 1.0));

    if (p.modHP != lastHP) { lastHP = p.modHP; modHp.highpass (fs, p.modHP, 0.7071); }
    if (p.modLP != lastLP) { lastLP = p.modLP; modLp.lowpass (fs, p.modLP, 0.7071); }
    tgtPhDepth = p.phaseOn ? 0.00001 * fs * dbToGain (p.phaseLevelDb) : 0.0;     // samples of delay per unit amplitude
}

void VinylChain::processRecord (float* l, float* r, int n) noexcept
{
    float* ch[2] = { l, r };
    juce::dsp::AudioBlock<float> in (ch, 2, (size_t) n);
    auto up = os->processSamplesUp (in);
    float* u[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
    const int m = (int) up.getNumSamples();
    const float conTarget = p.console ? 1.0f : 0.0f, conStep = (conTarget - conMix) / (float) m;
    const double thr = 2.0;

    for (int i = 0; i < m; ++i)
    {
        double x[2] = { u[0][i], u[1][i] };
        conMix += conStep;
        for (int c = 0; c < 2; ++c)
        {
            // 1. console: EQ curve + gentle valve-style saturation (even + odd harmonics)
            if (conMix > 0.0001f)
            {
                double y = conLp.process (conHi.process (conLo.process (conHp.process (x[c], c), c), c), c);
                const double k = 0.5, b = 0.1, tb = std::tanh (k * b);
                y = (std::tanh (k * (y + b)) - tb) / (k * (1.0 - tb * tb));
                x[c] += conMix * (y - x[c]);
            }
            // 3. cutting: RIAA pre-emphasis, then the cutter head's HF (acceleration) limiter
            double y = pre.process (x[c], c);
            const double hf = hfSplit.process (y, c), a = std::abs (hf);
            limEnv[c] = a > limEnv[c] ? a + limAtk * (limEnv[c] - a) : a + limRel * (limEnv[c] - a);
            if (limEnv[c] > thr) y -= hf * (1.0 - (thr + (limEnv[c] - thr) * 0.25) / limEnv[c]);
            x[c] = 6.0 * std::tanh (y / 6.0);                     // cutter amplifier headroom
        }
        // elliptical EQ: bass below ~150 Hz summed to mono (no vertical modulation)
        double mid = 0.5 * (x[0] + x[1]), side = sideHp.process (0.5 * (x[0] - x[1]), 0);
        x[0] = mid + side; x[1] = mid - side;

        for (int c = 0; c < 2; ++c)
        {
            double y = x[c];
            // 4. generation: a pressing loses a little top and gains some colour
            if (p.generation == 1) y = genMid.process (genLow.process (genShelf.process (y, c), c), c);
            // 7. tone-arm position: HF loss + tracing distortion grow towards the centre
            y = armLp.process (armShelf.process (y, c), c);
            // 6. cartridge: tracing nonlinearity (bounded), then its response
            const double v = y * 0.25, v2 = v * v;
            y = 4.0 * (v + (d2 * v2 - d3 * v2 * v) / (1.0 + v2));
            x[c] = cartHp.process (cartEq.process (cartRes.process (y, c), c), c);
        }
        // channel separation of the cartridge
        const double xl = x[0] + xtalk * x[1], xr = x[1] + xtalk * x[0];
        // 10. RIAA playback de-emphasis
        u[0][i] = (float) dc.process (de.process (xl, 0), 0);
        u[1][i] = (float) dc.process (de.process (xr, 1), 1);
    }
    os->processSamplesDown (in);
}

void VinylChain::colourNoise (float* l, float* r, int n) noexcept
{
    // Surface noise is picked up by the stylus, so it gets the cartridge response, the
    // arm-position HF loss and the crosstalk.  (The library was recorded after playback
    // EQ; cartridge and RIAA are linear, so applying the colour here is equivalent to
    // injecting the noise ahead of them.)
    float* ch[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        double x[2];
        for (int c = 0; c < 2; ++c)
        {
            double y = nTilt.process (nGen.process (ch[c][i], c), c);
            y = nLp.process (nShelf.process (y, c), c);
            x[c] = nHp.process (nEq.process (nRes.process (y, c), c), c);
        }
        l[i] = (float) (x[0] + xtalk * x[1]); r[i] = (float) (x[1] + xtalk * x[0]);
    }
}

void VinylChain::processPlayback (float* l, float* r, int n) noexcept
{
    float* ch[2] = { l, r };
    const int wm = (int) wowBuf[0].size() - 1, pm = (int) phBuf[0].size() - 1;
    const double sm = 1.0 - std::exp (-1.0 / (0.05 * fs));
    const double driftA = std::exp (-2 * kPi * 0.8 / fs), flNA = std::exp (-2 * kPi * 25.0 / fs);
    const double eccInc = 2 * kPi * 0.5556 / fs;

    for (int i = 0; i < n; ++i)
    {
        wowD += sm * (tgtWowD - wowD); eccD += sm * (tgtEccD - eccD); flA += sm * (tgtFlA - flA); phDepth += sm * (tgtPhDepth - phDepth);
        wowPh += wowInc; if (wowPh > 2 * kPi) wowPh -= 2 * kPi;
        eccPh += eccInc; if (eccPh > 2 * kPi) eccPh -= 2 * kPi;
        flPh += flInc;   if (flPh > 2 * kPi) flPh -= 2 * kPi;
        // slow random drift so the wow is never a pure sine
        const double wn = rng.bip();
        drift1 = wn + driftA * (drift1 - wn); drift2 = drift1 + driftA * (drift2 - drift1);
        const double drift = drift2 * 60.0;
        double delay = wowCentre + wowD * (std::sin (wowPh) + 0.15 * std::sin (2 * wowPh + 0.7) + 0.35 * drift) + eccD * std::sin (eccPh);
        delay = std::clamp (delay, 2.0, 2.0 * wowCentre - 2.0);
        // flutter = amplitude modulation, sine plus a band of noise
        const double fnz = rng.bip();
        flN1 = fnz + flNA * (flN1 - fnz); flN2 = flN1 + flNA * (flN2 - flN1);
        const double gain = 1.0 + flA * (0.75 * std::sin (flPh) + 0.25 * std::clamp (flN2 * 12.0, -1.0, 1.0));

        for (int c = 0; c < 2; ++c)
        {
            wowBuf[c][(size_t) wowW] = ch[c][i];
            const float x = readDelay (wowBuf[c], wowW, delay) * (float) gain;
            // phase distortion: a copy of the signal is delay-modulated by its own filtered
            // waveform (as a stylus tracing a groove does) and the difference is mixed back
            phBuf[c][(size_t) phW] = x;
            const double mod = modLp.process (modHp.process (x, c), c);
            const double e = phD0 * 0.9 * std::tanh (phDepth * mod / (phD0 * 0.9));
            const float dry = phBuf[c][(size_t) ((phW - (int) phD0) & pm)];
            ch[c][i] = phDepth > 1e-9 ? dry + (readDelay (phBuf[c], phW, phD0 + e) - dry) : dry;
        }
        wowW = (wowW + 1) & wm; phW = (phW + 1) & pm;
    }
}
} // namespace vs
