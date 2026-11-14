#include "NoiseEngine.h"

namespace vs
{
static inline float frame (const int16_t* p, int len, int i, int ch) noexcept
{
    return (i < 0 || i >= len) ? 0.0f : (float) p[i * 2 + ch];
}

void NoiseEngine::prepare (double sampleRate, const NoiseLibrary* library, uint64_t seed)
{
    fs = sampleRate; lib = library; ratio = 48000.0 / fs;
    rng.seed (seed);
    xfade = (int) (0.25 * fs);
    smooth = (float) (1.0 - std::exp (-1.0 / (0.03 * fs)));
    revPeriod = 1.8 * fs;                       // 33 1/3 rpm

    // rumble: arm/cartridge resonance near 10 Hz plus motor + bearing noise below ~35 Hz
    for (auto* b : { rumM, rumS })
    {
        b[0].highpass (fs, 6.0, 0.7);
        b[1].peak (fs, 10.0, 1.2, 9.0);
        b[2].lowpass (fs, 32.0, 0.6);
    }
    // calibrate so the rumble layer has unit RMS before the profile gain (off the audio thread)
    Rng cal; cal.seed (5); double acc = 0; const int n = (int) (fs * 3.0);
    for (auto& b : rumM) b.reset();
    for (int i = 0; i < n; ++i)
    {
        double x = cal.bip();
        for (auto& b : rumM) x = b.process (x, 0);
        if (i > n / 3) acc += x * x;
    }
    rumbleNorm = 1.0 / std::sqrt (acc / (n - n / 3) + 1e-30);
    if (set == nullptr) setProfile (noiseProfile (0, 0, 0));
    reset();
}

void NoiseEngine::reset()
{
    now = 0; busyUntil = 0;
    for (auto& v : voices) v.on = false;
    for (auto* b : { rumM, rumS }) for (int i = 0; i < 3; ++i) b[i].reset();
    gNoise = gCrackle = gClick = gRumble = 0;
    hissRemain = 5.0 * fs; hissFade = -1;
    if (set != nullptr && set->loopLen > 0) hiss[0].pos = rng.uni() * set->loopLen;
    configureLayers();
}

void NoiseEngine::setProfile (const NoiseProfile& p)
{
    const bool newSet = (set == nullptr) || p.set != profile.set;
    profile = p;
    if (lib == nullptr) return;
    set = &lib->sets[juce::jlimit (0, (int) NoiseLibrary::numSets - 1, p.set)];
    hissDb = (float) dbToGain (p.hissDb) * set->loopScale / 32767.0f;
    crackleLin = (float) dbToGain (p.crackleDb);
    clickLin = (float) dbToGain (p.clickDb);
    rumbleLin = (float) (dbToGain (p.rumbleDb) * rumbleNorm);
    if (newSet)
    {
        // running voices keep their own data pointers; only the hiss player restarts (crossfaded by gain smoothing)
        gNoise = 0;
        hiss[0].pos = rng.uni() * std::max (1, set->loopLen); hissFade = -1; hissRemain = 5.0 * fs;
        configureLayers();
    }
    else
    {
        layers[0].rate = set->grain.rate * profile.crackleRateMul;
    }
}

void NoiseEngine::configureLayers()
{
    if (set == nullptr) return;
    auto& g = layers[0];
    g.events = &set->grains; g.pcm = set->pcm.data(); g.st = set->grain; g.mono = set->mono; g.synth = false;
    g.rate = set->grain.rate * profile.crackleRateMul;

    auto& c = layers[1];
    if (set->clicks.size() >= 8)
    {
        c.events = &set->clicks; c.pcm = set->pcm.data(); c.st = set->click; c.mono = set->mono; c.synth = false;
    }
    else    // this record has no large clicks of its own: synthetic fallback, scaled from its grains
    {
        c.events = &lib->synth.clicks; c.pcm = lib->synth.pcm.data(); c.synth = true; c.mono = false;
        const float x0 = set->grain.pmax * 0.7f;
        c.st = { 1.0f, std::log (x0), 0.8f, x0, std::min (x0 * 8.0f, 0.9f), 0.3f, 2.0f, 0.08f, 1.0f, 0.0f, set->grain.lock, 0.85f };
    }
    for (auto& l : layers)
    {
        l.nextBg = l.nextCluster = -1; l.numPending = 0; l.z = 0; l.revStart = now; l.seeded = false;
        l.numZones = 2 + rng.range (3);
        for (auto& zn : l.zones) zn = (float) rng.uni();
        initScratches (l);
    }
    lastClickRate = -1;
}

void NoiseEngine::initScratches (Layer& l) noexcept
{
    for (auto& s : l.scratches) s.on = false;
}

//------------------------------------------------------------------------------ random draws
float NoiseEngine::drawAmp (const Layer& l) noexcept
{
    // heavy-tailed: log-normal truncated to the measured range (rejection, bounded tries)
    const float sg = l.st.sigma * profile.sigmaMul, lo = std::log (l.st.pmin), hi = std::log (l.st.pmax);
    float y = lo;
    for (int i = 0; i < 12; ++i)
    {
        y = l.st.mu + sg * (float) rng.normal();
        if (y >= lo && y <= hi) break;
        y = lo + (hi - lo) * (float) rng.uni() * (float) rng.uni();       // fallback leans to the quiet end
    }
    return std::exp (y);
}

int NoiseEngine::pick (const Layer& l, float amp) noexcept
{
    // library sample recorded at a similar level, so big pops keep their thump
    const auto& ev = *l.events;
    const int n = (int) ev.size();
    if (l.synth) return rng.range (n);
    int lo = 0, hi = n;
    while (lo < hi) { const int mid = (lo + hi) / 2; if (ev[(size_t) mid].peak < amp) lo = mid + 1; else hi = mid; }
    const int a = std::max (0, lo - 8), b = std::min (n, lo + 8);
    return a + rng.range (b - a);
}

float NoiseEngine::drawAngle (const Layer& l, int idx) noexcept
{
    // lateral defect = in-phase (+45 deg), vertical = out-of-phase (-45), single wall = one channel
    if (l.mono || l.synth)
    {
        static const float base[] = { 45.0f, 45.0f, 45.0f, 45.0f, -45.0f, 0.0f, 90.0f };
        return base[rng.range (7)] + 9.0f * (float) rng.normal();
    }
    const auto& ev = *l.events;
    const int other = pick (l, ev[(size_t) idx].peak);
    return ev[(size_t) other].angle + 6.0f * (float) rng.normal();
}

double NoiseEngine::drawGap (const Layer& l, double rate) noexcept
{
    const double mean = fs / std::max (rate, 1e-6);
    const double k = l.st.shape;
    if (k <= 1.01) return rng.expo (mean);
    // gamma(k) by Marsaglia-Tsang: more regular than Poisson
    const double d = k - 1.0 / 3.0, c = 1.0 / std::sqrt (9.0 * d);
    for (int i = 0; i < 16; ++i)
    {
        const double x = rng.normal(), v = (1 + c * x) * (1 + c * x) * (1 + c * x);
        if (v > 0 && std::log (std::max (rng.uni(), 1e-12)) < 0.5 * x * x + d - d * v + d * std::log (v))
            return d * v * mean / k;
    }
    return mean;
}

//------------------------------------------------------------------------------ events
void NoiseEngine::startEvent (int li, double time, float amp, int idx, float angle, float semis, bool haveIdx) noexcept
{
    auto& l = layers[li];
    if (l.events == nullptr || l.events->empty()) return;
    if (! haveIdx) { idx = pick (l, amp); angle = drawAngle (l, idx); }
    const auto& e = (*l.events)[(size_t) idx];

    Voice* v = nullptr;
    for (auto& c : voices) if (! c.on) { v = &c; break; }
    if (v == nullptr) return;

    v->rate = ratio * std::pow (2.0, (semis + profile.pitchSemis) / 12.0);
    // a stylus plays one defect at a time: late-comers queue up behind the event before them
    const double start = std::max (time, busyUntil);
    busyUntil = start + e.length / v->rate;
    v->delay = (int) (start - now);
    v->pcm = l.pcm + e.offset * 2; v->len = (int) e.length; v->pos = 0; v->layer = li;
    const float rot = (l.mono || l.synth ? angle - 45.0f : angle - e.angle) * (float) (kPi / 180.0);
    const float g = amp / e.peak * e.scale * (rng.uni() < 0.5 ? -1.0f : 1.0f);          // random polarity
    const float c = std::cos (rot) * g, s = std::sin (rot) * g;
    v->m00 = c; v->m01 = -s; v->m10 = s; v->m11 = c;
    v->on = true;
}

void NoiseEngine::scheduleLayer (int li, int n) noexcept
{
    auto& l = layers[li];
    const double end = now + n;
    const double rate = l.rate;
    if (rate <= 1e-6) { l.nextBg = l.nextCluster = -1; l.numPending = 0; for (auto& s : l.scratches) s.on = false; l.seeded = false; return; }

    const double lock = juce::jlimit (0.0, 0.95, (double) l.st.lock);
    const double freeRate = rate * (1.0 - lock);
    const double slow = l.st.slow, boost = std::exp (2.0 * slow);
    const double frac = l.st.frac, bgRate = freeRate * (1.0 - frac) * boost, clRate = freeRate * frac / std::max (1.0f, l.st.meanN) * boost;

    // slowly drifting density (cleaner / dirtier stretches of groove)
    const double dt = n / fs;
    l.z += -l.z * dt + std::sqrt (2.0 * dt) * rng.normal();
    const double accept = slow > 0 ? std::exp (slow * l.z - 0.5 * slow * slow) / boost : 1.0;

    auto emit = [&] (double t)
    {
        if (accept < 1.0 && rng.uni() > accept) return;
        startEvent (li, t, drawAmp (l) * (float) dbToGain (1.5 * rng.normal()), 0, 0.0f, (float) rng.bip(), false);
    };

    // background arrivals
    if (bgRate > 1e-6)
    {
        if (l.nextBg < 0) l.nextBg = now + drawGap (l, bgRate) * rng.uni();
        while (l.nextBg < end) { emit (l.nextBg); l.nextBg += drawGap (l, bgRate); }
    }
    // dust clumps: a burst of events with exponentially distributed offsets
    if (clRate > 1e-6)
    {
        if (l.nextCluster < 0) l.nextCluster = now + rng.expo (fs / clRate);
        while (l.nextCluster < end)
        {
            const int k = rng.geometric (l.st.meanN);
            for (int i = 0; i < k && l.numPending < 48; ++i)
                l.pending[l.numPending++] = l.nextCluster + rng.expo (l.st.tau * fs);
            l.nextCluster += rng.expo (fs / clRate);
        }
        for (int i = 0; i < l.numPending;)
        {
            if (l.pending[i] < end) { emit (std::max (l.pending[i], now)); l.pending[i] = l.pending[--l.numPending]; }
            else ++i;
        }
    }

    // rotation-locked defects: a scratch clicks once per revolution for several revolutions
    if (lock > 0.001)
    {
        const double perRev = rate * lock * revPeriod / fs, surv = juce::jlimit (0.3, 0.97, (double) l.st.surv);
        auto birth = [&] (double base, bool midLife)
        {
            for (auto& s : l.scratches)
            {
                if (s.on) continue;
                double ph = rng.uni();
                if (rng.uni() < 0.65) { ph = l.zones[rng.range (l.numZones)] + 0.04 * rng.normal(); ph -= std::floor (ph); }
                s.on = true; s.next = base + ph * revPeriod;
                s.life = rng.geometric (1.0 / (1.0 - surv)); s.k = midLife ? s.life / 2 : 0;
                s.amp = drawAmp (l); s.idx = pick (l, s.amp); s.angle = drawAngle (l, s.idx);
                s.drift = (float) (0.0004 * fs * rng.normal());
                return;
            }
        };
        if (! l.seeded)        // scratches that were already there when playback started
        {
            l.seeded = true; l.revStart = now;
            for (int i = rng.poisson (perRev); i > 0; --i) birth (now, true);
        }
        while (l.revStart + revPeriod < end)
        {
            l.revStart += revPeriod;
            for (int i = rng.poisson (perRev * (1.0 - surv)); i > 0; --i) birth (l.revStart, false);
        }
        for (auto& s : l.scratches)
        {
            if (! s.on || s.next >= end) continue;
            const float x = (s.k + 0.5f) / (float) s.life;
            const float shape = s.life > 2 ? std::sqrt (std::sin ((float) kPi * juce::jlimit (0.02f, 0.98f, x))) : 1.0f;
            startEvent (li, std::max (s.next, now), s.amp * shape * (float) dbToGain (rng.normal()), s.idx, s.angle, 0.15f * (float) rng.normal(), true);
            s.next += revPeriod + s.drift + 0.0003 * fs * rng.normal();
            if (++s.k >= s.life) s.on = false;
        }
    }
    else
        for (auto& s : l.scratches) s.on = false;
}

//------------------------------------------------------------------------------ hiss
void NoiseEngine::renderHiss (float* l, float* r, int n) noexcept
{
    const int len = set->loopLen, ch = set->loopCh;
    const int16_t* d = set->loop.data();
    if (len < 8) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); return; }

    auto read = [&] (double pos, float& a, float& b)
    {
        int i = (int) pos; const float t = (float) (pos - i);
        const int i0 = (i - 1 + len) % len, i1 = i % len, i2 = (i + 1) % len, i3 = (i + 2) % len;
        if (ch == 2)
        {
            a = hermite (d[i0 * 2], d[i1 * 2], d[i2 * 2], d[i3 * 2], t);
            b = hermite (d[i0 * 2 + 1], d[i1 * 2 + 1], d[i2 * 2 + 1], d[i3 * 2 + 1], t);
        }
        else    // mono source record: add a decorrelated copy as a vertical (side) component
        {
            const float m = hermite (d[i0], d[i1], d[i2], d[i3], t);
            const int h = len / 2;
            const float s = hermite (d[(i0 + h) % len], d[(i1 + h) % len], d[(i2 + h) % len], d[(i3 + h) % len], t);
            a = 0.87f * m + 0.49f * s; b = 0.87f * m - 0.49f * s;
        }
    };
    for (int i = 0; i < n; ++i)
    {
        if (hissFade < 0 && --hissRemain <= 0)      // jump to a new random start point, crossfaded
        {
            hiss[1].pos = rng.uni() * len; hissFade = 0;
            hissRemain = (3.0 + 6.0 * rng.uni()) * fs;
        }
        float a, b; read (hiss[0].pos, a, b);
        hiss[0].pos += ratio; if (hiss[0].pos >= len) hiss[0].pos -= len;
        if (hissFade >= 0)
        {
            float a2, b2; read (hiss[1].pos, a2, b2);
            hiss[1].pos += ratio; if (hiss[1].pos >= len) hiss[1].pos -= len;
            const float t = (float) (hissFade / xfade), go = std::cos (0.5f * (float) kPi * t), gi = std::sin (0.5f * (float) kPi * t);
            a = a * go + a2 * gi; b = b * go + b2 * gi;        // equal-power: the two segments are uncorrelated
            if (++hissFade >= xfade) { hiss[0] = hiss[1]; hissFade = -1; }
        }
        l[i] = a; r[i] = b;
    }
}

//------------------------------------------------------------------------------ process
void NoiseEngine::process (float* l, float* r, int n, const Controls& c) noexcept
{
    jassert (n <= maxChunk);
    if (set == nullptr) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); return; }

    layers[1].rate = c.clickGain > 0 ? c.clickRate : 0.0;
    if (std::abs (c.clickRate - lastClickRate) > 0.01f) { layers[1].nextBg = layers[1].nextCluster = -1; lastClickRate = c.clickRate; }
    scheduleLayer (0, n);
    scheduleLayer (1, n);

    float ev[2][2][maxChunk] {};
    for (auto& v : voices)
    {
        if (! v.on) continue;
        float* el = ev[v.layer][0]; float* er = ev[v.layer][1];
        int i = std::min (v.delay, n);
        v.delay -= i;
        for (; i < n; ++i)
        {
            const int k = (int) v.pos; const float t = (float) (v.pos - k);
            if (k >= v.len) { v.on = false; break; }
            const float a = hermite (frame (v.pcm, v.len, k - 1, 0), frame (v.pcm, v.len, k, 0), frame (v.pcm, v.len, k + 1, 0), frame (v.pcm, v.len, k + 2, 0), t);
            const float b = hermite (frame (v.pcm, v.len, k - 1, 1), frame (v.pcm, v.len, k, 1), frame (v.pcm, v.len, k + 1, 1), frame (v.pcm, v.len, k + 2, 1), t);
            el[i] += v.m00 * a + v.m01 * b;
            er[i] += v.m10 * a + v.m11 * b;
            v.pos += v.rate;
        }
    }

    renderHiss (l, r, n);

    // once-per-revolution level wobble (eccentric / warped record)
    const double ph = 2.0 * kPi * std::fmod (now, revPeriod) / revPeriod;
    const float wob = (float) dbToGain (profile.wobbleDb * (std::sin (ph) + 0.3 * std::sin (2.0 * ph + 1.3)));
    const float tN = c.noiseGain * hissDb * wob, tC = c.crackleGain * crackleLin * c.duck, tK = c.clickGain * clickLin, tR = c.rumbleGain * rumbleLin;

    for (int i = 0; i < n; ++i)
    {
        gNoise += smooth * (tN - gNoise); gCrackle += smooth * (tC - gCrackle);
        gClick += smooth * (tK - gClick); gRumble += smooth * (tR - gRumble);
        double m = rng.bip(), s = rng.bip();
        for (auto& b : rumM) m = b.process (m, 0);
        for (auto& b : rumS) s = b.process (s, 0);
        s *= 0.8;                                         // rumble has a strong vertical component
        l[i] = l[i] * gNoise + ev[0][0][i] * gCrackle + ev[1][0][i] * gClick + (float) (m + s) * gRumble;
        r[i] = r[i] * gNoise + ev[0][1][i] * gCrackle + ev[1][1][i] * gClick + (float) (m - s) * gRumble;
    }
    now += n;
}
} // namespace vs
