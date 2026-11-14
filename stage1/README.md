# Vinyl Studio, Stage 1: crackle engine prototype

Base Jase Plugins. Python (numpy/scipy) prototype of the surface-noise engine
that Stage 2 ports to C++.

Requirements: Python 3.10+, `pip install numpy scipy soundfile`, and `ffmpeg` on the PATH.

## Commands

    python3 analyze.py          # recordings -> library/<id>/ (clicks, grains, hiss loops, profile)
    python3 generate.py --profile needle --seconds 30 --out out.wav --stems
    python3 render_tests.py     # twelve 30 s files at different settings -> renders/
    python3 verify_stats.py     # real vs generated, measured with the same detector
    python3 ab_test.py          # blind A/B test in the terminal (plays audio, scores guesses)
    python3 make_ab_page.py     # browser version of the blind test -> ab_page/

Add a recording: drop it in `recordings/`, add an entry to `recordings.json`, run `analyze.py`.

## What is where

| File | Purpose |
| --- | --- |
| `vinyl/dsp.py` | I/O, filters, RIAA curve, transient detector, M/S angle helpers |
| `vinyl/timing.py` | clustered arrival model, its fit, rotation-lock detection |
| `vinyl/engine.py` | the generator (noise, crackle, clicks, rumble layers) |
| `analyze.py` | click extraction, declicked hiss loops, per-recording profile |
| `library/<id>/` | `clicks.wav`, `grains.wav`, `events.json` (metadata), `hiss_loop_N.wav`, `rumble_psd.npy`, `profile.json` |

## How it works

**Analysis.** Transients are found on the high-passed stereo envelope against a
running-median background (hysteresis 3.0x / 1.6x). Each event is cut out as
`recording - declicked bed`, where the bed is rebuilt by interpolating the low
band and copying hiss texture from a few ms earlier. Events 28 dB or more above
the local hiss are "clicks", the rest are "crackle grains". Metadata per event:
peak level, duration, spectral centroid, mid/side balance, L/R-plane angle.
The declicked bed above 120 Hz becomes 32.4 s loops (18 revolutions); short
recordings are quilted from random pieces. Below 120 Hz only the spectrum is kept.

**Generator.**
- Noise: loops played from random start points in 3-9 s segments with 250 ms
  crossfades, plus a once-per-revolution level wobble.
- Crackle and clicks: clustered arrivals (Poisson background + dust clumps +
  slow density drift), truncated log-normal amplitudes (Pareto optional), a
  library sample chosen near the drawn loudness, random polarity, pitch
  (+-1 semitone), gain (+-1.5 dB) and M/S angle. Events never overlap.
- Rotation-locked scratches repeat every revolution (1.8 s at 33 1/3, 1.333 s
  at 45) for a random number of revolutions, bunched into damaged sectors.
- Synthetic fallback click: impulse -> 15-20 kHz stylus resonance -> RIAA de-emphasis.
- Rumble: noise shaped by the measured sub-120 Hz spectrum.
- Clicks Density 0..100 maps to one click per 7.5 s .. 2 clicks per second.

## Known limits

- The source files are 128 kbit/s MP3/AAC at 44.1/48 kHz, about 3 minutes in
  total, not 96 kHz masters. The prototype therefore runs at 48 kHz and nothing
  above 16 kHz exists in the library.
- Two recordings are under 20 s, so their 32 s hiss loops reuse material.
- Long-range clustering (density changes over seconds) is weaker in the
  generated output than in the recordings; see the Fano rows of `verify_stats.py`.
