"""Vinyl surface-noise generator (Stage 1 prototype of the plugin's noise engine).

Four independent layers, each returned separately and summed:

    noise    sampled hiss loops, random start points + crossfades, once-per-rev wobble
    crackle  dense fine grains from the library (smallest events)
    clicks   larger stylus-interference bursts from the library
    rumble   low-frequency turntable rumble shaped by the measured LF spectrum

The structure (block-free event scheduler + sample player) is deliberately simple
so it ports directly to a real-time C++ voice pool.
"""
import json, os
from dataclasses import dataclass, field
import numpy as np
import scipy.signal as ss
import soundfile as sf
from scipy.stats import truncnorm
from .dsp import SR, riaa_playback_ba, resonator_ba, rotate_lr
from .timing import clustered_times

OS = 4  # library events are stored 4x oversampled so pitch variation is clean


def db2lin(d):
    return 10.0 ** (d / 20.0)


def density_to_rate(density):
    """Clicks Density 0..100 -> clicks per second.
    0 = about one click every 7.5 s (5-10 s), 100 = 2 clicks per second."""
    lo, hi = 1.0 / 7.5, 2.0
    return lo * (hi / lo) ** (np.clip(density, 0, 100) / 100.0)


def clicks_level_to_gain(level):
    """Clicks Level Off..100: 0 = off, 50 = recorded level, 100 = +18 dB."""
    return 0.0 if level <= 0 else db2lin((level - 50.0) * 0.36)


@dataclass
class Settings:
    noise_db: float = 0.0            # hiss level trim
    crackle_db: float = 0.0          # crackle level trim
    clicks_level: float = 50.0       # Off(0)..100
    clicks_density: float = None     # 0..100, None = use the profile's measured click rate
    clicks_gain_db: float = 0.0
    crackle_density: float = 1.0     # multiplier on the profile's grain rate
    rumble_db: float = 0.0
    rpm: float = None                # 33.333 / 45, None = profile (or 33 1/3)
    eccentricity_db: float = 0.6     # once-per-revolution hiss level swing
    pitch_var_semitones: float = 1.0
    gain_var_db: float = 1.5
    angle_jitter_deg: float = 6.0    # extra random M/S angle on top of the profile's distribution
    scratches: float = None          # rotation-locked share of events 0..1, None = profile
    extra_scratches: int = 0         # additional loud rotation-locked scratches (clicks layer)
    amp_law: str = "lognormal"      # heavy-tailed amplitude law: 'lognormal' or 'pareto' (power law)
    synthetic_mix: float = 0.0       # 0 = library clicks, 1 = synthetic fallback clicks only
    level_jitter_db: float = 0.0     # random overall hiss level offset (blind-test pool)
    seed: int = 0


class Layer:
    """One class of events (grains or clicks) of a profile."""

    def __init__(self, audio, rows, stats, mono=False):
        self.stats = stats
        self.rows = rows
        self.segs = []
        for r in rows:
            seg = audio[r["offset"]:r["offset"] + r["length"]]
            pad = np.pad(seg, ((8, 8), (0, 0)))
            self.segs.append(ss.resample_poly(pad, OS, 1, axis=0)[8 * OS:-8 * OS])
        self.peak = np.array([r["peak"] for r in rows])
        self.angle = np.array([r["angle_deg"] for r in rows])
        self.mono = mono      # mono source recording: never rotate, keep L = R
        self.order = np.argsort(self.peak)
        self.sorted_log = np.log(self.peak[self.order]) if len(rows) else np.zeros(0)

    def __len__(self):
        return len(self.rows)

    def draw_amplitude(self, rng, n, law):
        """Heavy-tailed peak amplitudes, drawn by inverse CDF: truncated
        log-normal (crackle) or truncated power-law / Pareto (clicks)."""
        s = self.stats
        u = rng.uniform(0, 1, n)
        lo, hi = s["peak_min"], s["peak_max"]
        if law == "pareto":
            al = max(s["pareto_alpha"], 0.05)
            return lo * (1 - u * (1 - (lo / hi) ** al)) ** (-1.0 / al)
        mu, sg = s["lognorm_mu"], s["lognorm_sigma"]
        y = truncnorm.ppf(u, (np.log(lo) - mu) / sg, (np.log(hi) - mu) / sg, loc=mu, scale=sg)
        return np.clip(np.exp(y), lo, hi)

    def pick(self, rng, amp):
        """Random library sample among those recorded at a similar level, so big
        pops keep their thump/ring and small ticks stay small ticks."""
        k = int(np.searchsorted(self.sorted_log, np.log(amp)))
        lo, hi = max(0, k - 8), min(len(self.order), k + 8)
        return int(self.order[rng.integers(lo, hi)])


class Profile:
    def __init__(self, path):
        self.path = path
        self.meta = json.load(open(os.path.join(path, "profile.json")))
        ev = json.load(open(os.path.join(path, "events.json")))
        g, _ = sf.read(os.path.join(path, "grains.wav"), always_2d=True)
        c, _ = sf.read(os.path.join(path, "clicks.wav"), always_2d=True)
        mono = self.meta.get("bed_lr_corr", 0) > 0.999
        self.grains = Layer(g, ev["grains"], self.meta["grain"], mono)
        self.clicks = Layer(c, ev["clicks"], self.meta["click"], mono)
        self.loops = [sf.read(os.path.join(path, f"hiss_loop_{i}.wav"), always_2d=True)[0]
                      for i in range(self.meta["n_loops"])]
        self.rumble_psd = np.load(os.path.join(path, "rumble_psd.npy"))


def synthetic_click_bank(rng, n=64, sr=SR):
    """Fallback click model: impulse -> stylus/cartridge resonance (15-20 kHz)
    -> RIAA playback de-emphasis.  Returned OS-times oversampled, unit peak, mono."""
    b_r, a_r = riaa_playback_ba(sr * OS)
    bank = []
    for _ in range(n):
        ln = int(0.012 * sr * OS)
        x = np.zeros(ln)
        x[8] = 1.0
        if rng.uniform() < 0.4:                       # scratch edge: bipolar doublet
            x[8 + int(rng.integers(2, 10)) * OS // 2] = -rng.uniform(0.4, 1.0)
        b, a = resonator_ba(rng.uniform(15000, 20000), rng.uniform(2.0, 6.0), sr * OS)
        y = ss.lfilter(b_r, a_r, ss.lfilter(b, a, x))
        y *= np.minimum(1.0, (ln - np.arange(ln)) / (0.004 * sr * OS))   # fade the de-emphasis tail
        bank.append(np.sqrt(2) * y / np.abs(y).max())
    return bank


class VinylNoise:
    def __init__(self, profile, settings=None):
        self.p = profile if isinstance(profile, Profile) else Profile(profile)
        self.s = settings or Settings()
        self.rng = np.random.default_rng(self.s.seed)
        rot = self.p.meta.get("rotation", {})
        self.rev = 60.0 / self.s.rpm if self.s.rpm else (rot.get("period") or 1.8)
        self.synth = synthetic_click_bank(self.rng)

    # ------------------------------------------------------------------ noise (hiss)
    def render_noise(self, n):
        rng, loops = self.rng, self.p.loops
        xf = int(0.25 * SR)
        fi = np.sin(0.5 * np.pi * (np.arange(xf) + 0.5) / xf)[:, None]
        out = np.zeros((n + xf, 2))
        pos = 0
        while pos < n:
            lp = loops[rng.integers(len(loops))]
            ln = min(int(rng.uniform(3.0, 9.0) * SR), n + xf - pos)
            idx = (int(rng.integers(len(lp))) + np.arange(ln)) % len(lp)   # random start, wraps the seamless loop
            seg = lp[idx].copy()
            k = min(xf, ln)
            if pos > 0:
                seg[:k] *= fi[:k]
            if pos + ln < n + xf:
                seg[-k:] *= fi[:k][::-1]
            out[pos:pos + ln] += seg
            pos += ln - xf
        out = out[:n]
        t = np.arange(n) / SR
        ph = rng.uniform(0, 2 * np.pi, 2)
        wob = self.s.eccentricity_db * (np.sin(2 * np.pi * t / self.rev + ph[0])
                                        + 0.3 * np.sin(4 * np.pi * t / self.rev + ph[1]))
        return out * db2lin(wob + rng.uniform(-1, 1) * self.s.level_jitter_db)[:, None]

    # ------------------------------------------------------------------ rumble
    def render_rumble(self, n):
        f, pm, ps = self.p.rumble_psd.T
        fs = 2 * f[-1]
        dec = int(round(SR / fs))
        m = n // dec + 64
        fr = np.fft.rfftfreq(m, 1 / fs)
        chans = []
        for psd in (pm, ps):
            mag = np.sqrt(np.interp(fr, f, psd) * fs * m / 2)
            spec = mag * (self.rng.normal(size=len(fr)) + 1j * self.rng.normal(size=len(fr))) / np.sqrt(2)
            spec[0] = 0
            chans.append(np.fft.irfft(spec, m))
        mid, side = chans
        lr = np.stack([mid + side, mid - side], 1)
        return ss.resample_poly(lr, dec, 1, axis=0)[16 * dec:16 * dec + n]

    # ------------------------------------------------------------------ events
    def _schedule(self, layer, rate, dur, locked_frac, survival, extra=0):
        """Decide every event of one layer: time, peak amplitude, library sample,
        M/S angle, pitch offset.  Returns a list of dicts."""
        rng, s, T = self.rng, self.s, self.rev
        cl = layer.stats.get("cluster", dict(frac=0.3, mean_n=3.0, tau=0.08, shape=1.0))
        law = s.amp_law
        ev = []

        def make(t, amp, idx=None, ang="draw", semis=None):
            synth = (not len(layer)) or rng.uniform() < s.synthetic_mix
            if synth:
                idx = int(rng.integers(len(self.synth)))
                ang = self._angle(layer, None) if ang == "draw" else ang
                ln = len(self.synth[idx])
            else:
                idx = layer.pick(rng, amp) if idx is None else idx
                ang = self._angle(layer, idx) if ang == "draw" else ang
                ln = len(layer.segs[idx])
            semis = rng.uniform(-1, 1) * s.pitch_var_semitones if semis is None else semis
            return dict(t=t, amp=amp, idx=idx, ang=ang, semis=semis, synth=synth, layer=layer,
                        dur=ln / (OS * 2.0 ** (semis / 12.0)) / SR)

        # free-running defects: clustered, non-uniform Poisson arrivals (dust clumps)
        free = clustered_times(rng, rate * (1 - locked_frac), dur, cl["frac"], cl["mean_n"], cl["tau"],
                               cl.get("shape", 1.0), cl.get("slow", 0.0))
        amps = layer.draw_amplitude(rng, len(free), law) * db2lin(rng.normal(0, s.gain_var_db, len(free)))
        ev += [make(t, a) for t, a in zip(free, amps)]

        # rotation-locked defects: a scratch crosses several neighbouring grooves, so
        # it clicks once per revolution for a number of revolutions, then fades out.
        per_rev = rate * locked_frac * T                    # scratches alive at any moment
        n0 = rng.poisson(per_rev)                           # already there when we start
        nb = rng.poisson(per_rev * (1 - survival) * dur / T)  # appear during the render
        # scuffs and fingerprints damage a sector of the disc, so scratch angles bunch up
        zones = rng.uniform(0, T, int(rng.integers(2, 5)))

        def phase():
            if rng.uniform() < 0.65:
                return (rng.choice(zones) + rng.normal(0, 0.04 * T)) % T
            return rng.uniform(0, T)
        scr = [(phase() - T, int(rng.geometric(1 - survival)), 0.5) for _ in range(n0)]
        scr += [(np.floor(rng.uniform(0, dur) / T) * T + phase(), int(rng.geometric(1 - survival)), 0.0)
                for _ in range(nb)]
        scr = [(t0, life, ph, layer.draw_amplitude(rng, 1, law)[0]) for t0, life, ph in scr]
        for _ in range(extra):                              # user-requested obvious scratches
            hi = layer.stats.get("peak_max", 0.5)
            scr.append((rng.uniform(0, T), int(dur / T) + 2, 0.5, hi * rng.uniform(0.35, 0.8)))
        for t0, life, ph, a0 in scr:
            proto = make(t0, a0, semis=0.0)
            drift = rng.normal(0, 0.0004)                   # non-radial scratch: phase creeps each rev
            for k in range(life):
                t = t0 + k * T + k * drift + rng.normal(0, 0.0003)
                if 0 <= t < dur:
                    # fade in/out over the scratch's life (already mid-life if it predates the render)
                    x = (k + 0.5) / life * (1 - ph) + ph
                    shape = np.sin(np.pi * x) ** 0.5 if life > 2 else 1.0
                    ev.append(dict(proto, t=t, amp=a0 * shape * db2lin(rng.normal(0, 1.0)),
                                   semis=rng.normal(0, 0.15)))
        return ev

    def _angle(self, layer, idx):
        """M/S angle of a new event (lateral defect = in-phase, vertical defect =
        out-of-phase, single groove wall = one channel only).  Drawn from the
        profile's own defect angles at a similar loudness, plus jitter; `None`
        means 'leave the sample as recorded' (mono sources)."""
        if idx is None or not len(layer):
            return float(self.rng.choice([45.0, 45.0, -45.0, 0.0, 90.0]) + self.rng.normal(0, 8))
        if layer.mono:
            return None
        other = layer.pick(self.rng, layer.peak[idx])
        return float(layer.angle[other] + self.rng.normal(0, self.s.angle_jitter_deg))

    @staticmethod
    def _separate(events):
        """A stylus plays one defect at a time: events never overlap (exactly how
        the library was cut out of the recordings).  Late-comers are pushed to
        the end of the event before them."""
        events.sort(key=lambda e: e["t"])
        end = -1.0
        for e in events:
            if e["t"] < end:
                e["t"] = end
            end = e["t"] + e["dur"]
        return events

    def _render_events(self, events, n, gain):
        rng = self.rng
        out = np.zeros((n + SR, 2))
        for e in events:
            if e["synth"]:
                src = self.synth[e["idx"]]
                a = np.radians(e["ang"] if e["ang"] is not None else 45.0)
                seg = np.stack([src * np.cos(a), src * np.sin(a)], 1) * e["amp"]
            else:
                layer, idx = e["layer"], e["idx"]
                seg = layer.segs[idx] if e["ang"] is None else rotate_lr(layer.segs[idx], e["ang"] - layer.angle[idx])
                seg = seg * (e["amp"] / layer.peak[idx])
            pos = np.arange(0, len(seg) - 1, OS * 2.0 ** (e["semis"] / 12.0))   # resample from the 4x store
            i0 = pos.astype(int); fr = (pos - i0)[:, None]
            y = seg[i0] * (1 - fr) + seg[i0 + 1] * fr
            if rng.uniform() < 0.5:
                y = -y                                            # random polarity
            a0 = int(e["t"] * SR)
            if a0 < n:
                out[a0:a0 + len(y)] += y[: len(out) - a0] * gain
        return out[:n]

    # ------------------------------------------------------------------ top level
    def render(self, seconds):
        s, m, n = self.s, self.p.meta, int(seconds * SR)
        layers = {}
        layers["noise"] = self.render_noise(n) * db2lin(s.noise_db)
        layers["rumble"] = self.render_rumble(n) * db2lin(s.rumble_db)

        def lock(cls):
            lk = m[cls].get("lock", {})
            f = lk.get("locked_frac", 0.0) if s.scratches is None else s.scratches
            return f, (lk.get("survival") or 0.85)

        f, q = lock("grain")
        grains = self._schedule(self.p.grains, m["grain"]["rate"] * s.crackle_density, seconds, f, q)

        rate = m["click"]["rate"] if s.clicks_density is None else density_to_rate(s.clicks_density)
        g = clicks_level_to_gain(s.clicks_level) * db2lin(s.clicks_gain_db)
        f, q = lock("click")
        click_layer = self.p.clicks if len(self.p.clicks) >= 8 else Layer(np.zeros((1, 2)), [], self._fallback_stats())
        clicks = self._schedule(click_layer, rate, seconds, f, q, s.extra_scratches) if g > 0 else []

        self._separate(grains + clicks)
        layers["crackle"] = self._render_events(grains, n, db2lin(s.crackle_db))
        layers["clicks"] = self._render_events(clicks, n, g)
        self.events = dict(crackle=grains, clicks=clicks)
        layers["mix"] = layers["noise"] + layers["rumble"] + layers["crackle"] + layers["clicks"]
        return layers

    def _fallback_stats(self):
        """Click statistics for profiles whose recording contains no large clicks:
        scale relative to the crackle grains (clicks start ~ where grains end)."""
        g = self.p.meta["grain"]
        x0 = g.get("peak_max", 0.05) * 0.7
        hi = min(x0 * 8, 0.9)
        return dict(pareto_xmin=x0, pareto_alpha=1.2, peak_max=hi, peak_min=x0,
                    lognorm_mu=float(np.log(x0)), lognorm_sigma=0.8,
                    cluster=dict(frac=0.3, mean_n=2.0, tau=0.08, shape=1.0))
