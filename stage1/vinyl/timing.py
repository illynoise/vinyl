"""Event timing models: clustered (Neyman-Scott) Poisson arrivals and their fit."""
import numpy as np
from scipy.ndimage import gaussian_filter1d
from .dsp import fano_curve

FANO_WINDOWS = [0.02, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0]


def clustered_times(rng, rate, duration, frac=0.5, mean_n=4.0, tau=0.05, shape=1.0, slow=0.0):
    """Non-uniform 'dust clump' arrivals.

    A fraction (1-frac) of events is plain Poisson background.  The rest arrive in
    clumps: clump centres are Poisson, each clump holds a geometric number of
    events (mean `mean_n`) spread after the centre with exponential offsets (`tau`
    seconds).  The long-run average rate is `rate` events/s.

    `shape` > 1 makes the background more regular than Poisson (gamma-renewal
    inter-arrival times) - some worn records tick almost evenly.

    `slow` > 0 adds a slowly drifting density (log-normal rate modulation with
    ~1 s correlation): dirtier and cleaner stretches of groove.
    """
    if rate <= 0 or duration <= 0:
        return np.zeros(0)
    if slow > 0:                                   # thinning of a denser process
        boost = float(np.exp(2.0 * slow))
        t = clustered_times(rng, rate * boost, duration, frac, mean_n, tau, shape)
        step = 0.05
        z = gaussian_filter1d(rng.normal(size=int(duration / step) + 80), 1.0 / step, mode="wrap")
        z /= z.std() + 1e-12
        lam = np.exp(slow * np.interp(t, np.arange(len(z)) * step, z) - 0.5 * slow ** 2) / boost
        return t[rng.uniform(size=len(t)) < lam]
    rb = rate * (1 - frac)
    if shape > 1.01 and rb > 0:
        n = int(rb * duration * 1.5 + 20)
        gaps = rng.gamma(shape, 1.0 / (shape * rb), n)
        t = [np.cumsum(gaps) - rng.uniform(0, 1.0 / rb)]
    else:
        t = [rng.uniform(0, duration, rng.poisson(rb * duration))]
    if frac > 0:
        lead = 6 * tau
        nc = rng.poisson(rate * frac / mean_n * (duration + lead))
        centres = rng.uniform(-lead, duration, nc)
        sizes = rng.geometric(1.0 / mean_n, nc)
        t.append(np.repeat(centres, sizes) + rng.exponential(tau, int(sizes.sum())))
    t = np.concatenate(t)
    return np.sort(t[(t >= 0) & (t < duration)])


def hard_core(times, dead):
    """Push events so none starts within `dead` seconds of the previous one
    (same rule the generator applies with the real event lengths)."""
    t = np.sort(times).copy()
    for i in range(1, len(t)):
        if t[i] < t[i - 1] + dead:
            t[i] = t[i - 1] + dead
    return t


def fit_clustering(times, duration, dead=0.0, seed=0):
    """Grid-search arrival-model parameters whose Fano-factor curve matches the
    measured one (log-domain least squares).  `dead` = typical event length:
    detected events cannot overlap, the simulation gets the same constraint."""
    rate = len(times) / duration
    target = fano_curve(times, duration, FANO_WINDOWS)
    ok = np.isfinite(target)
    best = (np.inf, dict(frac=0.0, mean_n=1.0, tau=0.05, shape=1.0, slow=0.0))
    if rate <= 0 or ok.sum() < 3:
        return best[1], target
    rng = np.random.default_rng(seed)
    sim_dur = float(np.clip(15000.0 / rate, 200.0, 1500.0))
    grid = [(0.0, 1.0, 0.05, sh, 0.0) for sh in (1.4, 2.0, 3.0)]
    grid += [(0.0, 1.0, 0.05, 1.0, sl) for sl in (0.0, 0.3, 0.6, 0.9)]
    grid += [(fr, mn, tau, 1.0, sl) for fr in (0.2, 0.4, 0.6, 0.8) for mn in (2, 3, 5, 8)
             for tau in (0.004, 0.012, 0.03, 0.08, 0.25) for sl in (0.0, 0.3, 0.6, 0.9)]
    for frac, mean_n, tau, shape, slow in grid:
        t = hard_core(clustered_times(rng, rate, sim_dur, frac, mean_n, tau, shape, slow), dead)
        f = fano_curve(t, sim_dur, FANO_WINDOWS)
        err = float(np.mean((np.log(np.maximum(f[ok], .05)) - np.log(np.maximum(target[ok], .05))) ** 2))
        if err < best[0]:
            best = (err, dict(frac=frac, mean_n=float(mean_n), tau=tau, shape=shape, slow=slow))
    return best[1], target


def rotation_lock(times, duration, tol=0.003):
    """Detect rotation-locked defects: events that recur one revolution later.

    Returns dict(period, rpm, p1, p2, locked_frac, survival).  p1/p2 = chance-
    corrected probability that an event has a partner 1 / 2 revolutions later.
    """
    times = np.sort(np.asarray(times))
    res = dict(period=0.0, rpm=0.0, p1=0.0, p2=0.0, locked_frac=0.0, survival=0.0)
    rate = len(times) / max(duration, 1e-9)
    if len(times) < 12:
        return res
    chance = 1 - np.exp(-2 * tol * rate)

    def partner_frac(T):
        ok = times < duration - T
        if ok.sum() < 6:
            return 0.0
        tt = times[ok] + T
        i = np.searchsorted(times, tt)
        d = np.minimum(np.abs(times[np.clip(i, 0, len(times) - 1)] - tt),
                       np.abs(times[np.clip(i - 1, 0, len(times) - 1)] - tt))
        return (float(np.mean(d < tol)) - chance) / max(1 - chance, 1e-6)

    best = (0.0, 0.0, 0.0)
    for rpm in (100 / 3, 45.0, 78.0):
        for dev in np.arange(-0.03, 0.0301, 0.0005):   # +-3 % speed error
            T = 60.0 / rpm / (1 + dev)
            if 2.2 * T > duration:
                continue
            p = partner_frac(T)
            if p > best[0]:
                best = (p, T, rpm)
    p1, T, rpm = best
    # significance: need clearly more partners than chance
    n = int((times < duration - T).sum()) if T else 0
    sigma = np.sqrt(max(chance * (1 - chance), 1e-9) / max(n, 1)) / max(1 - chance, 1e-6)
    if T == 0 or p1 < max(0.15, 5 * sigma):
        return res
    p2 = max(partner_frac(2 * T), 0.0)
    q = float(np.clip(p2 / p1 if p1 > 0 else 0.5, 0.5, 0.97))
    return dict(period=float(T), rpm=float(rpm), p1=float(p1), p2=float(p2),
                locked_frac=float(np.clip(p1 / q, 0, 0.95)), survival=q)
