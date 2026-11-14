#!/usr/bin/env python3
"""Render vinyl surface noise from an analysed profile.

    python3 generate.py --profile needle --seconds 30 --out out.wav
    python3 generate.py --profile noise02 --density 80 --clicks-level 70 --rpm 45 --stems
"""
import argparse, os
from vinyl.dsp import save_wav, SR
from vinyl.engine import VinylNoise, Settings

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", required=True, help="profile id (folder in library/)")
    ap.add_argument("--library", default="library")
    ap.add_argument("--out", default="out.wav")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--noise-db", type=float, default=0.0)
    ap.add_argument("--crackle-db", type=float, default=0.0)
    ap.add_argument("--clicks-level", type=float, default=50.0, help="0 (off) .. 100, 50 = recorded level")
    ap.add_argument("--density", type=float, default=None, help="clicks density 0..100 (default: profile's measured rate)")
    ap.add_argument("--clicks-gain-db", type=float, default=0.0)
    ap.add_argument("--crackle-density", type=float, default=1.0)
    ap.add_argument("--rumble-db", type=float, default=0.0)
    ap.add_argument("--rpm", type=float, default=None, help="33.333 or 45 (default: detected, else 33 1/3)")
    ap.add_argument("--scratches", type=float, default=None, help="rotation-locked share of events 0..1")
    ap.add_argument("--extra-scratches", type=int, default=0, help="add N loud once-per-revolution scratches")
    ap.add_argument("--synthetic", type=float, default=0.0, help="0..1 share of synthetic fallback clicks")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--stems", action="store_true", help="also write each layer separately")
    a = ap.parse_args()
    s = Settings(noise_db=a.noise_db, crackle_db=a.crackle_db, clicks_level=a.clicks_level,
                 clicks_density=a.density, clicks_gain_db=a.clicks_gain_db, crackle_density=a.crackle_density,
                 rumble_db=a.rumble_db, rpm=a.rpm, scratches=a.scratches, extra_scratches=a.extra_scratches,
                 synthetic_mix=a.synthetic, seed=a.seed)
    layers = VinylNoise(os.path.join(a.library, a.profile), s).render(a.seconds)
    save_wav(a.out, layers["mix"])
    if a.stems:
        for k in ("noise", "crackle", "clicks", "rumble"):
            save_wav(a.out.replace(".wav", f"_{k}.wav"), layers[k])
    print("wrote", a.out, f"({a.seconds:.0f}s, {SR} Hz)")
