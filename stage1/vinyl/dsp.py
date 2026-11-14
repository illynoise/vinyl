"""Shared DSP helpers for the Vinyl Studio Stage 1 crackle-engine prototype.

Everything here is plain numpy/scipy so it can be ported to C++ 1:1 in Stage 2.
"""
import os
import subprocess
import numpy as np
import scipy.signal as ss
import scipy.ndimage as ndi
import soundfile as sf

SR = 48000  # working rate of the prototype (see README: sources are 44.1/48 kHz lossy)


# ----------------------------------------------------------------------------- I/O
def load_audio(path, sr=SR):
    """Decode any ffmpeg-readable file to float32 stereo at `sr`."""
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "2",
           "-ar", str(sr), "-af", "aresample=resampler=soxr", "-"]
    try:
        raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    except subprocess.CalledProcessError:
        cmd.remove("-af"); cmd.remove("aresample=resampler=soxr")
        raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).astype(np.float64)


def save_wav(path, x, sr=SR, subtype="PCM_24"):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sf.write(path, np.clip(x, -1.0, 1.0), sr, subtype=subtype)


def db(v):
    return 20.0 * np.log10(np.maximum(np.abs(v), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)) + 1e-30))


# ----------------------------------------------------------------------------- filters
def zp_filter(x, fc, kind, sr=SR, order=2):
    """Zero-phase Butterworth (effective order 2*order)."""
    sos = ss.butter(order, fc / (sr / 2), kind, output="sos")
    return ss.sosfiltfilt(sos, x, axis=0)


def riaa_playback_ba(sr=SR):
    """RIAA playback (de-emphasis) curve, 3180/318/75 us, unity gain at 1 kHz.

    Analog prototype H(s) = (1 + s*T2) / ((1 + s*T1)(1 + s*T3)), discretised with
    the bilinear transform (pole/zero frequencies pre-warped individually).
    """
    t1, t2, t3 = 3180e-6, 318e-6, 75e-6

    def warp(t):  # pre-warp a real pole/zero at 1/t rad/s
        w = 1.0 / t
        return 1.0 / (2 * sr * np.tan(w / (2 * sr)))

    t1, t2, t3 = warp(t1), warp(t2), warp(t3)
    b, a = ss.bilinear([t2, 1.0], np.polymul([t1, 1.0], [t3, 1.0]), sr)
    w, h = ss.freqz(b, a, [1000.0], fs=sr)
    return b / abs(h[0]), a


def resonator_ba(f0, q, sr=SR):
    """2-pole resonant low-pass (stylus tip-mass / vinyl-compliance resonance)."""
    w0 = 2 * np.pi * min(f0, 0.45 * sr) / sr
    alpha = np.sin(w0) / (2 * q)
    b = np.array([(1 - np.cos(w0)) / 2, 1 - np.cos(w0), (1 - np.cos(w0)) / 2])
    a = np.array([1 + alpha, -2 * np.cos(w0), 1 - alpha])
    return b / a[0], a / a[0]


# ----------------------------------------------------------------------------- transient detection
def detection_envelope(x, sr=SR, hp=1000.0, smooth_ms=0.3, bg_ms=100.0):
    """Short-time envelope of the high-passed stereo signal and its slowly varying
    background (running median, robust against the clicks themselves)."""
    h = zp_filter(x, hp, "hp", sr, order=2)
    e = h[:, 0] ** 2 + h[:, 1] ** 2
    w = max(3, int(sr * smooth_ms / 1000) | 1)
    env = np.sqrt(np.maximum(ndi.uniform_filter1d(e, w, mode="nearest"), 0.0))
    hop = int(sr / 1000)  # 1 ms blocks
    n = len(env) // hop
    blk = np.median(env[: n * hop].reshape(n, hop), axis=1)
    bg_blk = ndi.median_filter(blk, size=int(bg_ms) | 1, mode="nearest")
    bg_blk = ndi.uniform_filter1d(bg_blk, 51, mode="nearest")
    bg = np.interp(np.arange(len(env)), (np.arange(n) + 0.5) * hop, bg_blk)
    return env, np.maximum(bg, 1e-9)


def detect_events(x, sr=SR, thr_on=3.0, thr_off=1.6, pre_ms=0.5, post_ms=0.6,
                  max_ms=60.0, env_bg=None):
    """Return (starts, ends, peak_snr) of transient regions.

    A region is a run where env > thr_off*bg that contains at least one sample
    above thr_on*bg (hysteresis); runs closer than 0.4 ms are merged.
    """
    env, bg = env_bg if env_bg is not None else detection_envelope(x, sr)
    ratio = env / bg
    low = ndi.binary_closing(ratio > thr_off, np.ones(int(sr * 0.0004) | 1, bool))
    lab, n = ndi.label(low)
    if n == 0:
        z = np.zeros(0, int)
        return z, z, np.zeros(0)
    mx = ndi.maximum(ratio, lab, np.arange(1, n + 1))
    keep = np.nonzero(mx > thr_on)[0]
    sl = ndi.find_objects(lab)
    pre, post, mxlen = int(sr * pre_ms / 1000), int(sr * post_ms / 1000), int(sr * max_ms / 1000)
    starts, ends, snr = [], [], []
    for k in keep:
        s, e = sl[k][0].start, sl[k][0].stop
        e = min(e, s + mxlen)
        s, e = max(0, s - pre), min(len(x), e + post)
        if starts and s <= ends[-1]:  # merge overlapping
            ends[-1] = max(ends[-1], e)
            snr[-1] = max(snr[-1], mx[k])
        else:
            starts.append(s); ends.append(e); snr.append(mx[k])
    return np.array(starts), np.array(ends), np.array(snr)


# ----------------------------------------------------------------------------- statistics
def fano_curve(times, duration, windows):
    """Fano factor (var/mean of counts) per window size; 1.0 = plain Poisson,
    > 1 = clustered arrivals."""
    out = []
    for w in windows:
        nb = int(duration / w)
        if nb < 8 or len(times) < 8:
            out.append(np.nan); continue
        c, _ = np.histogram(times, bins=nb, range=(0, nb * w))
        out.append(float(np.var(c) / max(np.mean(c), 1e-9)))
    return np.array(out)


def pca_angle(seg):
    """Principal axis of a stereo segment in the L/R plane, degrees in (-90, 90].
    +45 = lateral (L=R, pure Mid), -45 = vertical (L=-R, pure Side), 0 / 90 = one
    groove wall only (L-only / R-only)."""
    c = seg.T @ seg
    w, v = np.linalg.eigh(c)
    a = np.degrees(np.arctan2(v[1, 1], v[0, 1]))
    if a <= -90: a += 180
    if a > 90: a -= 180
    return float(a)


def rotate_lr(seg, deg):
    """Rotate a stereo segment in the L/R plane."""
    t = np.radians(deg)
    c, s = np.cos(t), np.sin(t)
    return np.stack([c * seg[:, 0] - s * seg[:, 1], s * seg[:, 0] + c * seg[:, 1]], axis=1)


def codec_roundtrip(x, sr=SR, kbps=128, lowpass=None):
    """Encode to MP3 and decode again.  The source recordings are 128 kbit/s lossy
    files, so generated audio gets the same treatment before any real-vs-generated
    comparison - otherwise the codec's band limit alone would give the game away."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        a, b = os.path.join(d, "a.wav"), os.path.join(d, "b.mp3")
        sf.write(a, np.clip(x, -1, 1), sr, subtype="FLOAT")
        cmd = ["ffmpeg", "-v", "error", "-y", "-i", a, "-c:a", "libmp3lame", "-b:a", f"{kbps}k"]
        if lowpass:
            cmd += ["-af", f"lowpass=f={lowpass}"]
        subprocess.run(cmd + [b], check=True)
        y = load_audio(b, sr)
    return y[:len(x)] if len(y) >= len(x) else np.pad(y, ((0, len(x) - len(y)), (0, 0)))
