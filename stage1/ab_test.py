#!/usr/bin/env python3
"""Blind A/B test: real vinyl recordings vs. the Stage 1 generator.

Each trial plays ONE clip - either a random excerpt of a real recording or a
freshly generated clip using that recording's profile - and you answer whether
it was real or generated.  At the end you get your score and the probability of
doing that well by pure guessing.  ~50 % correct = you cannot tell them apart.

    python3 ab_test.py                      # 20 trials, 6 s clips, all profiles
    python3 ab_test.py --trials 30 --seconds 8 --profile needle

Keys:  r = real   g = generated   p = play again   q = quit
"""
import argparse, json, math, os, random, shutil, subprocess, sys, tempfile, time
import numpy as np
from vinyl.dsp import SR, load_audio, save_wav
from vinyl.engine import VinylNoise, Settings, Profile
from analyze import trim


def player_cmd(path):
    if sys.platform == "darwin" and shutil.which("afplay"):
        return ["afplay", path]
    if shutil.which("ffplay"):
        return ["ffplay", "-v", "quiet", "-nodisp", "-autoexit", path]
    if shutil.which("aplay"):
        return ["aplay", "-q", path]
    if sys.platform.startswith("win"):
        return ["powershell", "-c", f"(New-Object Media.SoundPlayer '{path}').PlaySync()"]
    return None


def fade(x, ms=30):
    n = int(SR * ms / 1000)
    w = np.sin(0.5 * np.pi * np.arange(n) / n) ** 2
    x = x.copy(); x[:n] *= w[:, None]; x[-n:] *= w[::-1, None]
    return x


class ClipSource:
    """Hands out real excerpts and matching generated clips for one profile."""

    def __init__(self, entry, library, seconds):
        self.id = entry["id"]
        self.real, _ = trim(load_audio(entry["file"]), entry.get("trim_start"), entry.get("trim_end"))
        self.profile = Profile(os.path.join(library, self.id))
        self.n = int(min(seconds, len(self.real) / SR - 0.2) * SR)

    def real_clip(self, rng):
        a = rng.randrange(0, len(self.real) - self.n)
        return fade(self.real[a:a + self.n])

    def generated_clip(self, rng):
        # the real recordings drift in hiss level over time; give generated clips the same spread
        jit = self.profile.meta.get("hiss_level_drift_db", 0) / 3.0
        s = Settings(seed=rng.randrange(1 << 30), level_jitter_db=jit)
        mix = VinylNoise(self.profile, s).render(self.n / SR + 1.0)["mix"]
        return fade(mix[SR // 2: SR // 2 + self.n])


def p_value(correct, n):
    """Two-sided binomial test against guessing (p = 0.5)."""
    pmf = [math.comb(n, k) * 0.5 ** n for k in range(n + 1)]
    return min(1.0, sum(p for p in pmf if p <= pmf[correct] + 1e-12))


def verdict(correct, n):
    p = p_value(correct, n)
    if p < 0.05 and correct > n / 2:
        return p, "You can reliably tell generated from real -> Stage 1 needs another iteration."
    if p < 0.05:
        return p, "Reliably WRONG: you hear a difference but take the generated clips for the real ones."
    return p, "Not distinguishable from guessing at this sample size -> generator passes."


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--trials", type=int, default=20)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--profile", help="only this profile id")
    ap.add_argument("--library", default="library")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--no-play", action="store_true", help="do not play audio (for testing the script)")
    ap.add_argument("--log", default="ab_results.json")
    a = ap.parse_args()

    rng = random.Random(a.seed)
    ents = [e for e in json.load(open("recordings.json"))["recordings"] if a.profile in (None, e["id"])]
    print("loading recordings and library ...")
    sources = [ClipSource(e, a.library, a.seconds) for e in ents]
    tmp = tempfile.mkdtemp(prefix="vinyl_ab_")
    clip = os.path.join(tmp, "clip.wav")
    if not a.no_play and player_cmd(clip) is None:
        sys.exit("no audio player found (need afplay, ffplay or aplay)")

    # balanced: half real, half generated, shuffled
    kinds = (["real", "generated"] * a.trials)[: a.trials]
    rng.shuffle(kinds)
    log, correct = [], 0
    print(f"\n{a.trials} trials. Answer r (real) / g (generated), p = play again, q = quit.\n")
    for i, kind in enumerate(kinds):
        src = rng.choice(sources)
        save_wav(clip, src.real_clip(rng) if kind == "real" else src.generated_clip(rng))
        while True:
            if not a.no_play:
                subprocess.run(player_cmd(clip))
            ans = input(f"[{i + 1}/{a.trials}] real or generated? (r/g/p/q) ").strip().lower()[:1]
            if ans in ("r", "g", "q"):
                break
        if ans == "q":
            break
        ok = (ans == "r") == (kind == "real")
        correct += ok
        log.append(dict(trial=i + 1, profile=src.id, truth=kind, answer="real" if ans == "r" else "generated", correct=bool(ok)))
    shutil.rmtree(tmp, ignore_errors=True)

    n = len(log)
    if n:
        p, text = verdict(correct, n)
        print(f"\nScore: {correct}/{n} correct ({100 * correct / n:.0f} %).  Chance of this by guessing: p = {p:.3f}")
        for pid in sorted({r["profile"] for r in log}):
            rows = [r for r in log if r["profile"] == pid]
            print(f"   {pid:8s} {sum(r['correct'] for r in rows)}/{len(rows)}")
        fa = sum(1 for r in log if r["truth"] == "generated" and r["answer"] == "real")
        print(f"   generated clips you took for real: {fa}/{sum(1 for r in log if r['truth'] == 'generated')}")
        print(text)
        json.dump(dict(time=time.strftime("%Y-%m-%d %H:%M:%S"), correct=correct, trials=n, p_value=p, log=log),
                  open(a.log, "w"), indent=1)
        print("details saved to", a.log)
