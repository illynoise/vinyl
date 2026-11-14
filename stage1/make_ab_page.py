#!/usr/bin/env python3
"""Build the browser version of the blind test: ab_page/index.html + audio/.

Clips get anonymous names; real and generated clips go through the identical
MP3 encode so the delivery codec cannot give them away.
"""
import base64, json, os, random, subprocess, tempfile
from vinyl.dsp import save_wav
from ab_test import ClipSource
from render_tests import TESTS

N, SECONDS, SEED = 20, 6.0, 20261114


def mp3(src_wav, dst, kbps):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src_wav, "-c:a", "libmp3lame", "-b:a", f"{kbps}k", dst], check=True)


if __name__ == "__main__":
    rng = random.Random(SEED)
    os.makedirs("ab_page/audio", exist_ok=True)
    ents = json.load(open("recordings.json"))["recordings"]
    src = {e["id"]: ClipSource(e, "library", SECONDS) for e in ents}
    plan = [(pid, kind) for pid, n in (("needle", 3), ("noise02", 3), ("asmr", 2), ("burgh", 2)) for kind in ("real", "generated") for _ in range(n)]
    rng.shuffle(plan)
    key = []
    with tempfile.TemporaryDirectory() as d:
        for i, (pid, kind) in enumerate(plan):
            clip = src[pid].real_clip(rng) if kind == "real" else src[pid].generated_clip(rng)
            save_wav(f"{d}/c.wav", clip)
            mp3(f"{d}/c.wav", f"ab_page/audio/clip{i + 1:02d}.mp3", 256)
            key.append([pid, 1 if kind == "real" else 0])
    renders = []
    for name, prof, kw, desc in TESTS:
        mp3(f"renders/{name}.wav", f"ab_page/audio/{name}.mp3", 192)
        renders.append([name, desc])
    data = dict(k=base64.b64encode(json.dumps(key).encode()).decode(), renders=renders)
    html = open("ab_page/template.html").read().replace("/*DATA*/", "const DATA = " + json.dumps(data) + ";")
    open("ab_page/index.html", "w").write(html)
    json.dump({f"audio/{f}": f"audio/{f}" for f in sorted(os.listdir("ab_page/audio"))}, open("ab_page/files.json", "w"))
    print("clips:", len(key), "renders:", len(renders))
