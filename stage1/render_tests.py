#!/usr/bin/env python3
"""Render the Stage 1 listening set: 30 s files at several settings -> renders/

    python3 render_tests.py
"""
import os
from vinyl.dsp import save_wav
from vinyl.engine import VinylNoise, Settings

SECONDS = 30.0
TESTS = [  # (file name, profile, settings, description)
    ("01_needle_matched",        "needle",  dict(), "Worn pressing (mono source), all layers at the recorded balance"),
    ("02_noise02_matched",       "noise02", dict(), "Hissy 33 1/3 groove with rotation-locked ticks, recorded balance"),
    ("03_asmr_matched",          "asmr",    dict(), "Heavy crackle and pops with strong rumble, recorded balance"),
    ("04_burgh_matched",         "burgh",   dict(), "Quiet surface with sparse loud pops, recorded balance"),
    ("05_needle_density_min",    "needle",  dict(clicks_density=0, clicks_level=60), "Clicks Density 0: about one click every 5-10 s"),
    ("06_needle_density_max",    "needle",  dict(clicks_density=100, clicks_level=60), "Clicks Density 100: about 2 clicks per second"),
    ("07_noise02_45rpm_scratch", "noise02", dict(rpm=45.0, extra_scratches=2, clicks_level=55, clicks_density=20), "45 rpm with two scratches repeating every 1.333 s"),
    ("08_needle_33rpm_scratch",  "needle",  dict(rpm=100 / 3, extra_scratches=2, clicks_density=10), "33 1/3 rpm with two scratches repeating every 1.8 s"),
    ("09_asmr_crackle_only",     "asmr",    dict(noise_db=-80, rumble_db=-80, clicks_level=0), "Crackle layer alone (noise, rumble and clicks off)"),
    ("10_noise02_hiss_only",     "noise02", dict(crackle_db=-80, clicks_level=0, rumble_db=-80), "Noise (hiss) layer alone, with once-per-revolution wobble"),
    ("11_needle_synthetic",      "needle",  dict(synthetic_mix=1.0, clicks_density=70, crackle_db=-80), "Fallback synthetic clicks only (impulse > stylus resonance > RIAA)"),
    ("12_burgh_dusty",           "burgh",   dict(crackle_density=2.5, crackle_db=4, clicks_density=85, clicks_level=65), "Crackle density x2.5, heavy clicks: a dusty record"),
]

if __name__ == "__main__":
    os.makedirs("renders", exist_ok=True)
    with open("renders/README.txt", "w") as f:
        for i, (name, prof, kw, desc) in enumerate(TESTS):
            mix = VinylNoise(os.path.join("library", prof), Settings(seed=100 + i, **kw)).render(SECONDS)["mix"]
            save_wav(f"renders/{name}.wav", mix)
            f.write(f"{name}.wav\n    {desc}\n    profile={prof} settings={kw}\n")
            print(f"{name}.wav  peak {abs(mix).max():.2f}  {desc}")
