// Platter stop / start: variable-speed reader over a circular buffer.
// Pitch and level fall together; spin-up follows a motor (exponential) curve.
#pragma once
#include "Filters.h"
#include <vector>
#include <atomic>

namespace vs
{
class SlowDown
{
public:
    enum State { live, slowing, stopped, spinning, recover };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = (int) (fs * 31.0) + 4096;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; state = live; speed = 1.0; lag = 0; xf = 0; reached = false;
        speedOut.store (1.0f);
    }

    /** stop: Stop button state.  length: seconds for the slow-down.  spinTau: motor time constant. */
    void process (float* l, float* r, int n, bool stop, double length, double spinTau) noexcept
    {
        length = std::clamp (length, 0.001, 30.0);
        if (stop && (state == live || state == spinning || state == recover))
        {
            if (state == live) lag = 0;
            state = slowing; startSpeed = speed; elapsed = 0; reached = false;
        }
        else if (! stop && (state == slowing || state == stopped))
        {
            if (state == stopped) lag = 0;              // start again from "now"
            state = spinning;
        }
        const double spinA = 1.0 - std::exp (-1.0 / (std::max (0.01, spinTau) * fs));
        const int xfLen = std::max (16, (int) (0.04 * fs));

        for (int i = 0; i < n; ++i)
        {
            buf[0][(size_t) w] = l[i]; buf[1][(size_t) w] = r[i];
            if (state == live) { if (++w >= size) w = 0; continue; }

            if (state == slowing)
            {
                elapsed += 1.0 / (length * fs);
                // friction + brake: nearly linear, with a soft landing
                const double x = std::min (1.0, elapsed);
                speed = startSpeed * (1.0 - x) * (1.0 - 0.25 * x * (1.0 - x));
                if (elapsed >= 1.0) { speed = 0; state = stopped; reached = true; }
            }
            else if (state == spinning)
            {
                speed += spinA * (1.02 - speed);       // motor torque falls as it approaches speed
                if (speed >= 1.0) { speed = 1.0; state = recover; xf = 0; }
            }

            float a = 0, b = 0;
            if (state != stopped)
            {
                lag += 1.0 - speed;
                lag = std::clamp (lag, 0.0, (double) (size - 8));
                const double rp = (double) w - lag;
                const double fl = std::floor (rp); const float t = (float) (rp - fl);
                const int k = (int) fl;
                auto at = [&] (int ch, int idx) { idx %= size; if (idx < 0) idx += size; return buf[ch][(size_t) idx]; };
                // samples newer than the write head do not exist yet: clamp to the head
                auto rd = [&] (int ch, int idx) { return at (ch, lag < 2.0 ? std::min (idx, w) : idx); };
                a = hermite (rd (0, k - 1), rd (0, k), rd (0, k + 1), rd (0, k + 2), t);
                b = hermite (rd (1, k - 1), rd (1, k), rd (1, k + 1), rd (1, k + 2), t);
                const float g = (float) std::sqrt (std::clamp (speed, 0.0, 1.0)) * (float) std::min (1.0, speed * 25.0);
                a *= g; b *= g;
            }
            if (state == recover)      // catch up with the live signal (short crossfade)
            {
                const float t = (float) xf / (float) xfLen, gi = std::sin (0.5f * (float) kPi * t), go = std::cos (0.5f * (float) kPi * t);
                const float mix = lag < 1.0 ? 1.0f : 0.0f;       // no lag: already live
                a = mix > 0 ? l[i] : a * go + l[i] * gi;
                b = mix > 0 ? r[i] : b * go + r[i] * gi;
                if (++xf >= xfLen) { state = live; lag = 0; speed = 1.0; }
            }
            l[i] = a; r[i] = b;
            if (++w >= size) w = 0;
        }
        speedOut.store ((float) (state == live ? 1.0 : speed), std::memory_order_relaxed);
    }

    bool isStopped() const noexcept { return state == stopped; }
    /** True once after the platter has come to rest (used for Auto Resume). */
    bool consumeReached() noexcept { const bool r = reached; reached = false; return r; }
    std::atomic<float> speedOut { 1.0f };

private:
    double fs = 48000.0, speed = 1.0, startSpeed = 1.0, elapsed = 0, lag = 0;
    std::vector<float> buf[2];
    int size = 0, w = 0, xf = 0;
    State state = live;
    bool reached = false;
};

/** VU ballistics: rectified average through two cascaded poles, 99 % in 300 ms. */
struct VuBallistics
{
    double a = 0, z1 = 0, z2 = 0;
    void prepare (double fs) noexcept { a = std::exp (-1.0 / (fs * 0.0452)); z1 = z2 = 0; }
    inline void push (float x) noexcept { z1 = std::abs (x) + a * (z1 - std::abs (x)); z2 = z1 + a * (z2 - z1); }
    /** Level as the peak amplitude of the equivalent sine wave. */
    float value() const noexcept { return (float) (z2 * (kPi / 2.0)); }
};
} // namespace vs
