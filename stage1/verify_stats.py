#!/usr/bin/env python3
"""Objective check: measure real recordings and generated noise with the same
detector and print them side by side (level, spectrum, event rates, amplitude
distribution, clustering, stereo image).

    python3 verify_stats.py            # all profiles
"""
import argparse, json, os
import numpy as np
import scipy.signal as ss
from vinyl.dsp import SR, load_audio, db, rms, zp_filter, detection_envelope, detect_events, fano_curve
from vinyl.engine import VinylNoise, Settings
from analyze import trim

BANDS = [(20, 120), (120, 500), (500, 2000), (2000, 5000), (5000, 10000), (10000, 16000)]


def measure(x):
    dur = len(x) / SR
    m, s = (x[:, 0] + x[:, 1]) / 2, (x[:, 0] - x[:, 1]) / 2
    r = {"rms M dB": db(rms(m)), "rms S dB": db(rms(s))}
    f, P = ss.welch(m + 0.5 * s, SR, nperseg=32768)
    for lo, hi in BANDS:
        k = (f >= lo) & (f < hi)
        r[f"{lo}-{hi} Hz dB"] = 10 * np.log10(P[k].sum() * (f[1] - f[0]) + 1e-30)
    h = zp_filter(x, 1000, "hp")
    r["kurtosis >1k"] = np.mean(h ** 4) / np.mean(h ** 2) ** 2
    r["L/R corr >120"] = np.corrcoef(*zp_filter(x, 120, "hp").T)[0, 1] if rms(s) > 1e-6 else 1.0
    env, bg = detection_envelope(x)
    st, en, snr = detect_events(x, env_bg=(env, bg))
    q = db(snr)
    pk = np.array([np.abs(x[a:b]).max() for a, b in zip(st, en)]) if len(st) else np.zeros(1)
    r["hiss env dB"] = db(np.median(bg))
    for th in (10, 14, 20, 28):
        r[f"events/s >{th} dB"] = np.sum(q > th) / dur
    for p in (50, 90, 99):
        r[f"peak p{p} dB"] = db(np.percentile(pk, p))
    r["peak max dB"] = db(pk.max())
    fc = fano_curve(st / SR, dur, [0.1, 0.5, 2.0])
    r["Fano 0.1s"], r["Fano 0.5s"], r["Fano 2s"] = fc
    return r


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=90)
    ap.add_argument("--only")
    a = ap.parse_args()
    for ent in json.load(open("recordings.json"))["recordings"]:
        if a.only not in (None, ent["id"]):
            continue
        real, _ = trim(load_audio(ent["file"]), ent.get("trim_start"), ent.get("trim_end"))
        gen = VinylNoise(os.path.join("library", ent["id"]), Settings(seed=7)).render(a.seconds)["mix"]
        R, G = measure(real), measure(gen)
        print(f"\n=== {ent['id']}  (real {len(real)/SR:.0f}s vs generated {a.seconds:.0f}s) ===")
        print(f"{'':22s}{'real':>9s}{'generated':>11s}{'diff':>8s}")
        for k in R:
            print(f"{k:22s}{R[k]:9.2f}{G[k]:11.2f}{G[k]-R[k]:8.2f}")
