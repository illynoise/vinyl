#!/usr/bin/env python3
"""Stage 1 analysis: recordings of silent grooves -> click library, declicked hiss
loops, rumble spectrum and a statistical noise profile per recording.

    python3 analyze.py                 # all recordings listed in recordings.json
    python3 analyze.py --only noise02

Output: library/<id>/{clicks.wav,grains.wav,events.json,hiss_loop_N.wav,
                      rumble_psd.npy,profile.json}
"""
import argparse, json, os
import numpy as np
import scipy.signal as ss
from vinyl.dsp import (SR, load_audio, save_wav, db, rms, zp_filter, detection_envelope,
                       detect_events, pca_angle)
from vinyl.timing import fit_clustering, rotation_lock, FANO_WINDOWS

CLICK_SNR_DB = 28.0     # events this far above the local hiss envelope are "clicks", the rest "crackle grains"
BIG_SNR_DB = 22.0       # events that get a wider window so their low-frequency thump is captured
XF = 16                 # edge fade inside the event window (samples)
LF_SPLIT = 150.0        # Hz, bed low band is interpolated (not texture-filled) across gaps
RUMBLE_FC = 120.0       # Hz, below this the bed is modelled by the synthetic rumble layer
RUMBLE_SR = 600
LOOP_SECONDS = 32.4     # 18 revolutions at 33 1/3 rpm
MAX_GRAINS, MAX_CLICKS = 1500, 500


def fit_trunc_lognormal(pk, lo, hi):
    """MLE of a log-normal truncated to [lo, hi] (the detector hides everything
    below its threshold, so a plain mean/std fit would be biased)."""
    from scipy.optimize import minimize
    from scipy.stats import truncnorm
    y, a, b = np.log(pk), np.log(lo), np.log(hi)

    def nll(p):
        mu, sg = p
        return -np.sum(truncnorm.logpdf(y, (a - mu) / sg, (b - mu) / sg, loc=mu, scale=sg))
    best = None
    for mu0 in (y.mean(), a, a - 1.0):
        r = minimize(nll, [mu0, y.std() + 0.2], bounds=[(a - 3.0, b), (0.2, 2.5)], method="L-BFGS-B")
        if best is None or r.fun < best.fun:
            best = r
    return float(best.x[0]), float(best.x[1])


def fit_trunc_pareto(pk, lo, hi):
    """MLE of the power-law exponent of a Pareto truncated to [lo, hi]."""
    from scipy.optimize import minimize_scalar
    y = np.log(pk / lo)
    r = np.log(hi / lo)

    def nll(al):
        return -(len(y) * (np.log(al) - np.log(1 - np.exp(-al * r))) - al * y.sum())
    return float(minimize_scalar(nll, bounds=(0.05, 6.0), method="bounded").x)


def trim(x, start=None, end=None):
    """Drop digital silence at both ends plus optional manual trims (seconds)."""
    fr = int(0.05 * SR)
    n = len(x) // fr
    lvl = db(np.sqrt(np.mean(x[: n * fr].reshape(n, fr, 2) ** 2, axis=(1, 2))))
    live = np.nonzero(lvl > -80)[0]
    a, b = live[0] * fr, (live[-1] + 1) * fr
    a += int((0.3 if start is None else start) * SR)
    b -= int((0.3 if end is None else end) * SR)
    return x[a:b], a / SR


def declick(x, starts, ends, bg, rng):
    """Return bed (x with every event region replaced by plausible hiss).

    Low band: interpolated across the gap.  High band: texture copied from the
    already-cleaned signal a few ms earlier (keeps stereo correlation and hiss
    spectrum).  `x - bed` is therefore non-zero only inside event regions.
    """
    rough = x.copy()
    for s, e in zip(starts, ends):
        a = x[max(0, s - 48):s].mean(axis=0) if s > 0 else x[e:e + 48].mean(axis=0)
        b = x[e:e + 48].mean(axis=0) if e < len(x) else a
        rough[s:e] = a + (b - a) * np.linspace(0, 1, e - s)[:, None]
    low = zp_filter(rough, LF_SPLIT, "lp")
    hf = x - low                      # becomes the cleaned high band in place
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(XF) + 0.5) / XF)
    for s, e in zip(starts, ends):
        n = e - s
        d = int(rng.integers(240, 3840))
        src = s - d - n
        if src < 0:
            src = min(e + d, len(x) - n)
        fill = hf[src:src + n] * np.clip(bg[(s + e) // 2] / bg[src + n // 2], 0.6, 1.6)
        w = np.ones(n)
        k = min(XF, n // 2)
        w[:k], w[n - k:] = ramp[:k], ramp[:k][::-1]
        hf[s:e] = hf[s:e] * (1 - w[:, None]) + fill * w[:, None]
    return low + hf


def event_meta(seg, t, snr_db):
    m, s = (seg[:, 0] + seg[:, 1]) / 2, (seg[:, 0] - seg[:, 1]) / 2
    em, es = float(np.sum(m * m)), float(np.sum(s * s))
    ang = pca_angle(seg)
    pc = seg[:, 0] * np.cos(np.radians(ang)) + seg[:, 1] * np.sin(np.radians(ang))
    sp = np.abs(np.fft.rfft(pc * np.hanning(len(pc)), 2048)) ** 2
    fr = np.fft.rfftfreq(2048, 1 / SR)
    env = np.abs(seg).max(axis=1)
    pk = float(env.max())
    above = np.nonzero(env > pk * 10 ** (-20 / 20))[0]
    return dict(t=round(float(t), 5), length=int(len(seg)), peak=pk, peak_db=round(float(db(pk)), 2),
                snr_db=round(float(snr_db), 2),
                dur_ms=round(float((above[-1] - above[0] + 1) / SR * 1000), 3),   # -20 dB width
                centroid_hz=round(float((fr * sp).sum() / (sp.sum() + 1e-30)), 1),
                ms_balance=round((em - es) / (em + es + 1e-30), 4),               # +1 mid (lateral) .. -1 side (vertical)
                angle_deg=round(ang, 2),
                polarity=int(np.sign(pc[np.argmax(np.abs(pc))])))


def make_loops(hiss, rng, n_target):
    """Seamless loops of the declicked hiss bed.  Long sources are sliced; short
    sources are quilted from random 1.2-3.5 s pieces with equal-power crossfades."""
    L, xf = int(LOOP_SECONDS * SR), int(0.25 * SR)
    fi = np.sin(0.5 * np.pi * (np.arange(xf) + 0.5) / xf)[:, None]
    fo = fi[::-1]
    loops = []
    if len(hiss) >= L + xf:
        n = max(1, min(n_target, len(hiss) // (L + xf)))
        for a in np.linspace(0, len(hiss) - (L + xf), n).astype(int):
            loops.append(hiss[a:a + L + xf].copy())
        mode = "sliced"
    else:
        px = int(0.15 * SR)
        pi_, po = np.sin(0.5 * np.pi * (np.arange(px) + 0.5) / px)[:, None], None
        po = pi_[::-1]
        for _ in range(n_target):
            out = np.zeros((L + xf + px, 2)); pos, last = 0, -10 ** 9
            while pos < L + xf:
                ln = int(rng.uniform(1.2, min(3.5, len(hiss) / SR * 0.45)) * SR)
                for _try in range(20):
                    a = int(rng.integers(0, len(hiss) - ln))
                    if abs(a - last) > SR:
                        break
                last = a + ln
                pc = hiss[a:a + ln].copy()
                pc[:px] *= pi_; pc[-px:] *= po
                ln = min(ln, len(out) - pos)
                out[pos:pos + ln] += pc[:ln]
                pos += ln - px
            loops.append(out[px:px + L + xf])
        mode = "quilted"
    res = []
    for lp in loops:                      # close the loop: fade the tail into the head
        body = lp[:L].copy()
        body[:xf] = body[:xf] * fi + lp[L:L + xf] * fo
        res.append(body)
    return res, mode


def analyze(entry, out_root, seed=1):
    rng = np.random.default_rng(seed)
    pid = entry["id"]
    x, t0 = trim(load_audio(entry["file"]), entry.get("trim_start"), entry.get("trim_end"))
    dur = len(x) / SR
    out = os.path.join(out_root, pid)
    os.makedirs(out, exist_ok=True)

    env, bg = detection_envelope(x)
    starts, ends, snr = detect_events(x, env_bg=(env, bg))
    snr_db = db(snr)
    big = snr_db > BIG_SNR_DB           # widen: room for the LF thump / ring-out
    starts = np.where(big, np.maximum(starts - int(.003 * SR), 0), starts)
    ends = np.where(big, np.minimum(ends + int(.006 * SR), len(x)), ends)
    keep = np.ones(len(starts), bool)   # re-merge overlaps created by widening
    for i in range(1, len(starts)):
        j = i - 1
        while not keep[j]: j -= 1
        if starts[i] <= ends[j]:
            ends[j] = max(ends[j], ends[i]); snr_db[j] = max(snr_db[j], snr_db[i]); keep[i] = False
    starts, ends, snr_db = starts[keep], ends[keep], snr_db[keep]

    bed = declick(x, starts, ends, bg, rng)
    resid = x - bed
    metas = []
    for s, e, q in zip(starts, ends, snr_db):
        md = event_meta(resid[s:e], s / SR, q)
        md["cls"] = "click" if q >= CLICK_SNR_DB else "grain"
        md["_s"] = int(s)
        metas.append(md)

    # ---- library (random subset if huge; clicks are all kept up to the cap)
    lib_meta = {}
    for cls, cap, fname in (("grain", MAX_GRAINS, "grains.wav"), ("click", MAX_CLICKS, "clicks.wav")):
        ev = [m for m in metas if m["cls"] == cls]
        if len(ev) > cap:
            ev = [ev[i] for i in sorted(rng.choice(len(ev), cap, replace=False))]
        off, chunks, rows = 0, [], []
        for m in ev:
            seg = resid[m["_s"]:m["_s"] + m["length"]]
            chunks.append(seg)
            r = {k: v for k, v in m.items() if not k.startswith("_")}
            r["offset"] = off; off += len(seg); rows.append(r)
        import soundfile as sf
        sf.write(os.path.join(out, fname), np.concatenate(chunks) if chunks else np.zeros((1, 2)), SR, subtype="FLOAT")
        lib_meta[cls] = rows
    json.dump(dict(sr=SR, grains=lib_meta["grain"], clicks=lib_meta["click"]),
              open(os.path.join(out, "events.json"), "w"))

    # ---- hiss bed -> rumble spectrum + level-flattened loops
    low = zp_filter(bed, RUMBLE_FC, "lp")
    hiss = bed - low
    _, bg2 = detection_envelope(hiss, bg_ms=400)
    slow = np.convolve(bg2, np.ones(SR) / SR, "same")
    slow[:SR // 2], slow[-SR // 2:] = slow[SR // 2], slow[-SR // 2 - 1]
    drift_db = float(db(slow.max() / slow.min()))
    hiss_flat = hiss * (np.sqrt(np.mean(slow ** 2)) / slow)[:, None]   # flatten level drift, keep mean power
    loops, mode = make_loops(hiss_flat, rng, 3 if dur > 3 * LOOP_SECONDS else 2)
    for i, lp in enumerate(loops):
        save_wav(os.path.join(out, f"hiss_loop_{i}.wav"), lp, subtype="FLOAT")

    d = ss.resample_poly(low, 1, SR // RUMBLE_SR, axis=0)
    ms = np.stack([(d[:, 0] + d[:, 1]) / 2, (d[:, 0] - d[:, 1]) / 2], 1)
    f, P = ss.welch(ms, RUMBLE_SR, nperseg=1024, axis=0)
    np.save(os.path.join(out, "rumble_psd.npy"), np.column_stack([f, P]))

    # ---- statistics -> profile
    prof = dict(id=pid, source=os.path.basename(entry["file"]), sr=SR, duration=round(dur, 2),
                trim_start=round(t0, 2), n_events=len(metas), loop_mode=mode, n_loops=len(loops),
                loop_seconds=LOOP_SECONDS, hiss_rms_db=round(float(db(rms(hiss_flat))), 2),
                hiss_level_drift_db=round(drift_db, 2), rumble_rms_db=round(float(db(rms(low))), 2),
                bed_lr_corr=round(float(np.corrcoef(hiss[:, 0], hiss[:, 1])[0, 1]), 3),
                duty=round(float((ends - starts).sum() / len(x)), 4), fano_windows=FANO_WINDOWS)
    all_t = np.array([m["t"] for m in metas])
    lock_all = rotation_lock(all_t[np.array([m["snr_db"] for m in metas]) > 14], dur)
    prof["rotation"] = lock_all
    for cls in ("grain", "click"):
        ev = [m for m in metas if m["cls"] == cls]
        t = np.array([m["t"] for m in ev]); pk = np.array([m["peak"] for m in ev])
        c = dict(rate=round(len(ev) / dur, 4), count=len(ev))
        if len(ev) >= 8:
            lo, hi = float(np.percentile(pk, 2)), float(np.percentile(pk, 99.7))
            pkc = np.clip(pk, lo, hi)
            mu, sg = fit_trunc_lognormal(pkc, lo, hi)
            c.update(lognorm_mu=mu, lognorm_sigma=sg, peak_min=lo, peak_max=hi,
                     pareto_xmin=lo, pareto_alpha=fit_trunc_pareto(pkc, lo, hi),
                     dur_ms_median=float(np.median([m["dur_ms"] for m in ev])),
                     centroid_hz_median=float(np.median([m["centroid_hz"] for m in ev])),
                     ms_balance_mean=float(np.mean([m["ms_balance"] for m in ev])))
            lock = rotation_lock(t, dur)
            if lock_all["period"] and not lock["period"]:
                lock = dict(lock, period=lock_all["period"], rpm=lock_all["rpm"])
            c["lock"] = lock
            cl, fano = fit_clustering(t, dur, dead=float(np.mean([m["length"] for m in ev])) / SR)
            c["cluster"] = cl
            c["fano"] = [None if not np.isfinite(v) else round(float(v), 3) for v in fano]
        prof[cls] = c
    json.dump(prof, open(os.path.join(out, "profile.json"), "w"), indent=1)

    lf = ss.resample_poly(low, 1, 80, axis=0)[:, 0]
    print(f"[{pid}] {dur:.1f}s  events {len(metas)} ({len(metas)/dur:.1f}/s, duty {prof['duty']:.2f})  "
          f"grains {prof['grain']['rate']:.1f}/s  clicks {prof['click']['rate']:.2f}/s  "
          f"hiss {prof['hiss_rms_db']:.1f} dB (drift {drift_db:.1f})  rumble {prof['rumble_rms_db']:.1f} dB "
          f"(kurt {np.mean(lf**4)/np.mean(lf**2)**2:.1f})  loops {len(loops)}x{LOOP_SECONDS}s {mode}")
    for cls in ("grain", "click"):
        c = prof[cls]
        if "cluster" in c:
            print(f"    {cls:5s} lognormal mu {c['lognorm_mu']:.2f} sigma {c['lognorm_sigma']:.2f}  pareto a {c['pareto_alpha']:.2f}  "
                  f"cluster {c['cluster']}  lock {c['lock']['locked_frac']:.2f}@{c['lock']['period']:.3f}s  fano {c['fano']}")
    return prof


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="recordings.json")
    ap.add_argument("--out", default="library")
    ap.add_argument("--only")
    a = ap.parse_args()
    for ent in json.load(open(a.config))["recordings"]:
        if a.only in (None, ent["id"]):
            analyze(ent, a.out)
