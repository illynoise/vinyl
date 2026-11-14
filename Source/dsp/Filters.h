// Small real-time filter kit (no allocation, double-precision state).
#pragma once
#include <cmath>
#include <algorithm>

namespace vs
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    inline double process (double x, int ch) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void bypass() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    static double clampF (double fs, double f) noexcept { return std::clamp (f, 1.0, fs * 0.47); }

    void lowpass (double fs, double f, double q) noexcept
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double fs, double f, double q) noexcept
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double fs, double f, double q) noexcept
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double fs, double f, double dB, double q = 0.7071) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
             (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    void highShelf (double fs, double f, double dB, double q = 0.7071) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
             (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }

    /** RIAA playback de-emphasis (3180 / 318 / 75 us), unity at 1 kHz.  Matched-z
        design with a single zero so that the inverse (riaaRecord) is stable and
        cutting followed by playback is exactly transparent. */
    void riaaPlayback (double fs) noexcept
    {
        const double p1 = std::exp (-1.0 / (fs * 3180e-6)), p2 = std::exp (-1.0 / (fs * 75e-6)), z = std::exp (-1.0 / (fs * 318e-6));
        b0 = 1; b1 = -z; b2 = 0; a1 = -(p1 + p2); a2 = p1 * p2;
        const double g = 1.0 / magnitude (fs, 1000.0);
        b0 *= g; b1 *= g;
    }
    /** RIAA recording pre-emphasis: exact inverse of riaaPlayback. */
    void riaaRecord (double fs) noexcept
    {
        Biquad p; p.riaaPlayback (fs);
        b0 = 1.0 / p.b0; b1 = p.a1 / p.b0; b2 = p.a2 / p.b0; a1 = p.b1 / p.b0; a2 = 0;
    }

    double magnitude (double fs, double f) const noexcept
    {
        const double w = 2 * kPi * f / fs, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

struct OnePole
{
    double a = 0, z[2] {};
    void setTime (double fs, double seconds) noexcept { a = seconds <= 0 ? 0 : std::exp (-1.0 / (fs * seconds)); }
    void setCutoff (double fs, double f) noexcept { a = std::exp (-2 * kPi * f / fs); }
    inline double lp (double x, int ch) noexcept { z[ch] = x + a * (z[ch] - x); return z[ch]; }
    inline double hp (double x, int ch) noexcept { return x - lp (x, ch); }
    void reset (double v = 0) noexcept { z[0] = z[1] = v; }
};

/** Tiny fast RNG for the audio thread. */
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    void seed (uint64_t v) noexcept { s = v * 0x9E3779B97F4A7C15ull + 0x1234567ull; next(); next(); }
    inline uint64_t next() noexcept { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    inline double uni() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }       // [0,1)
    inline double bip() noexcept { return uni() * 2.0 - 1.0; }
    inline double normal() noexcept { const double u = std::max (uni(), 1e-12), v = uni(); return std::sqrt (-2.0 * std::log (u)) * std::cos (2 * kPi * v); }
    inline double expo (double mean) noexcept { return -mean * std::log (std::max (uni(), 1e-12)); }
    inline int range (int n) noexcept { return n <= 0 ? 0 : (int) (next() % (uint64_t) n); }
    int poisson (double mean) noexcept
    {
        if (mean <= 0) return 0;
        if (mean > 30) return std::max (0, (int) std::lround (mean + std::sqrt (mean) * normal()));
        const double L = std::exp (-mean); double p = 1; int k = 0;
        do { ++k; p *= uni(); } while (p > L && k < 200);
        return k - 1;
    }
    int geometric (double mean) noexcept      // >= 1
    {
        if (mean <= 1.0) return 1;
        return 1 + (int) std::floor (std::log (std::max (uni(), 1e-12)) / std::log (1.0 - 1.0 / mean));
    }
};

inline float hermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}
inline double dbToGain (double d) noexcept { return std::pow (10.0, d / 20.0); }
} // namespace vs
