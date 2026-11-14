#!/usr/bin/env python3
"""Pack the Stage 1 library into compact assets for the plugin (embedded as
JUCE BinaryData):  Assets/noise_<id>.bin (stats + click/grain samples, int16)
and Assets/hiss_<id>.flac (declicked hiss loop, 48 kHz, 16 bit).

    python3 tools/export_library.py            # run from the repo root
"""
import json, os, struct, sys
import numpy as np, soundfile as sf

SRC, DST = "stage1/library", "Assets"
MAX = {"grains": 500, "clicks": 250}
STAT_KEYS = ("rate", "lognorm_mu", "lognorm_sigma", "peak_min", "peak_max")


def stats(c):
    cl, lk = c.get("cluster", {}), c.get("lock", {})
    v = [c.get(k, 0.0) for k in STAT_KEYS]
    v += [cl.get("frac", 0.0), cl.get("mean_n", 1.0), cl.get("tau", 0.05), cl.get("shape", 1.0), cl.get("slow", 0.0)]
    v += [lk.get("locked_frac", 0.0), lk.get("survival", 0.0) or 0.85]
    return struct.pack("<12f", *v)


def export(pid, rng):
    d = os.path.join(SRC, pid)
    prof = json.load(open(f"{d}/profile.json")); ev = json.load(open(f"{d}/events.json"))
    mono = prof["bed_lr_corr"] > 0.999
    loop, sr = sf.read(f"{d}/hiss_loop_0.wav", always_2d=True)
    scale = 10 ** (-30 / 20) / np.sqrt(np.mean(loop ** 2))            # store at -30 dBFS rms for 16-bit resolution
    q = np.clip(np.round(loop * scale * 32767), -32768, 32767).astype(np.int16)
    sf.write(f"{DST}/hiss_{pid}.flac", q[:, :1] if mono else q, sr, subtype="PCM_16")
    out = [b"VSNL", struct.pack("<IIIf f", 1, sr, int(mono), prof["rotation"].get("period") or 1.8, 1.0 / scale)]
    out += [stats(prof["grain"]), stats(prof["click"])]
    tables, pcm, off = [], [], 0
    for cls in ("grains", "clicks"):
        audio, _ = sf.read(f"{d}/{cls}.wav", always_2d=True)
        rows = ev[cls]
        if len(rows) > MAX[cls]:
            rows = [rows[i] for i in sorted(rng.choice(len(rows), MAX[cls], replace=False))]
        rows = sorted(rows, key=lambda r: r["peak"])                   # sorted by loudness for the picker
        tab = [struct.pack("<I", len(rows))]
        for r in rows:
            seg = audio[r["offset"]:r["offset"] + r["length"]]
            pk = np.abs(seg).max() + 1e-12
            pcm.append(np.round(seg / pk * 32000).astype(np.int16))
            tab.append(struct.pack("<IIfff", off, len(seg), r["peak"], r["angle_deg"], pk / 32000.0))
            off += len(seg)
        tables.append(b"".join(tab))
    blob = b"".join(out + tables) + np.concatenate(pcm).tobytes()
    open(f"{DST}/noise_{pid}.bin", "wb").write(blob)
    print(f"{pid}: bin {len(blob)/1e6:.2f} MB, flac {os.path.getsize(f'{DST}/hiss_{pid}.flac')/1e6:.2f} MB")


if __name__ == "__main__":
    rng = np.random.default_rng(1)
    for pid in ("burgh", "needle", "noise02", "asmr"):
        export(pid, rng)
