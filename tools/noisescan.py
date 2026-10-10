#!/usr/bin/env python3
"""Residual temporal noise per instant (dev tool, not part of the 64k). Freezes the instant (--still:
only the TAA sub-pixel jitter advances), renders 24 frames and, over the last 12, takes the per-pixel
std of the luma (Rec.709 on the 8-bit output, 2x2 box-averaged): mean, p99 and p99.9. What survives
the TAA here is aliasing or unconverged stochastic sampling (jittered marches, AO rays, the jittered
sun disc), the shimmer a viewer sees in real time. Compare builds at the same instants: a cheaper
march that leaves these numbers unchanged leaves no visible noise. With --png DIR it writes the last
frame next to a heat map of the std (x40). numpy; Pillow for --png.
usage: noisescan.py <cathedra_dev> t... [--size 1920x1080] [--tier 3] [--png DIR]"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
ap = argparse.ArgumentParser()
ap.add_argument("exe"); ap.add_argument("t", nargs="+", type=float)
ap.add_argument("--size", default="1920x1080"); ap.add_argument("--tier", default="3"); ap.add_argument("--png", default="")
a = ap.parse_args()
W, H = map(int, a.size.split("x")); cwd = tempfile.mkdtemp()   # the dev build writes its log into the working directory
if a.png:
    from PIL import Image
    os.makedirs(a.png, exist_ok=True)
for t in a.t:
    r = subprocess.run([os.path.abspath(a.exe), "--tier", a.tier, "--size", a.size, "--still", "--raw", "60", "%.4f" % t, "%.4f" % (t + 24.5 / 60)],
                       capture_output=True, cwd=cwd)
    b = np.frombuffer(r.stdout, np.uint8); n = len(b) // (W * H * 3)
    if n < 20: sys.exit("%s rendered %d frames at t=%g" % (a.exe, n, t))
    F = b[:n * W * H * 3].reshape(n, H, W, 3)[n - 12:].astype(np.float32)
    L = F @ np.array([.2126, .7152, .0722], np.float32)
    L = L[:, :H // 2 * 2, :W // 2 * 2].reshape(12, H // 2, 2, W // 2, 2).mean((2, 4))
    s = L.std(0)
    print("t=%7.3f  mean std %.3f  p99 %.2f  p99.9 %.2f" % (t, s.mean(), np.percentile(s, 99), np.percentile(s, 99.9)))
    if a.png:
        hm = np.repeat(np.clip(s * 40, 0, 255).astype(np.uint8)[..., None], 3, 2)
        Image.fromarray(np.concatenate([F[-1].astype(np.uint8)[:H // 2 * 2:2, :W // 2 * 2:2], hm], 1)).save(os.path.join(a.png, "noise_t%07.3f.png" % t))
