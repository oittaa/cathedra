#!/usr/bin/env python3
"""Compare two dev builds frame by frame (dev tool, not part of the 64k). For each instant t, each
build renders the 12 consecutive 60 fps frames that end at t (--raw: deterministic, the real-time TAA
path) and the last ones are diffed: max and mean |difference| over RGB (0..255), and the pixels that
differ by more than 2. With --png DIR it also writes each pair side by side with a heat map of the
difference (x20). Two runs of the same build match within a few levels in a few dozen pixels; the
city sections (0-2 and 6) can differ in a few hundred. numpy; Pillow for --png.
usage: framediff.py <cathedra_dev A> <cathedra_dev B> t... [--size 1920x1080] [--tier 3] [--png DIR]"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
ap = argparse.ArgumentParser()
ap.add_argument("a"); ap.add_argument("b"); ap.add_argument("t", nargs="+", type=float)
ap.add_argument("--size", default="1920x1080"); ap.add_argument("--tier", default="3"); ap.add_argument("--png", default="")
a = ap.parse_args()
W, H = map(int, a.size.split("x")); cwd = tempfile.mkdtemp()   # the dev build writes its log into the working directory
def frame(exe, t):
    r = subprocess.run([os.path.abspath(exe), "--tier", a.tier, "--size", a.size, "--raw", "60", "%.5f" % (t - 11 / 60.), "%.5f" % (t + .5 / 60.)],
                       capture_output=True, cwd=cwd)
    b = np.frombuffer(r.stdout, np.uint8); n = len(b) // (W * H * 3)
    if not n: sys.exit("%s rendered no frame at t=%g" % (exe, t))
    return b[(n - 1) * W * H * 3:n * W * H * 3].reshape(H, W, 3)
if a.png:
    from PIL import Image
    os.makedirs(a.png, exist_ok=True)
for t in a.t:
    A, B = frame(a.a, t), frame(a.b, t)
    d = np.abs(A.astype(int) - B.astype(int))
    print("t=%7.3f  max %3d  mean %.4f  px>2: %d" % (t, d.max(), d.mean(), int((d.max(2) > 2).sum())))
    if a.png:
        hm = np.repeat(np.clip(d.max(2) * 20, 0, 255).astype(np.uint8)[..., None], 3, 2)
        Image.fromarray(np.concatenate([A, B, hm], 1)).save(os.path.join(a.png, "diff_t%07.3f.png" % t))
