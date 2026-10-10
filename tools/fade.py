#!/usr/bin/env python3
"""Slow fading at the tower bases over a long moving-camera window, numpy only (runs on the RTX box).
Per frame: mean luma of cloud pixels within R px of tower stone (bases) minus mean luma of open sea
(cloud farther than 3R). Reports the std of that difference after removing a 1 s moving average
(= fading on 0.1-1 s time scales) and the mean absolute frame-to-frame change of it."""
import sys, numpy as np
W, H = 960, 540
def frames(f):
    d = np.fromfile(f, np.uint8); n = d.size // (W * H * 3); return d[:n * W * H * 3].reshape(n, H, W, 3)
def dil(m, r):
    c = np.pad(m.astype(np.int32), r).cumsum(0).cumsum(1); k = 2 * r + 1
    c = np.pad(c, ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) > 0
img, msk = frames(sys.argv[1]), frames(sys.argv[2]); R = 15
n = min(len(img), len(msk)); out = []
for k in range(8, n):
    M = msk[k].astype(np.int16); L = img[k].astype(np.float32) @ np.array([.2126, .7152, .0722], np.float32)
    tw = (M[..., 0] > M[..., 1] + 40) & (M[..., 0] > M[..., 2] + 40); cl = (M[..., 1] > M[..., 0] + 40) & (M[..., 1] > M[..., 2] + 40)
    b = dil(tw, R) & cl & ~dil(tw, 2); s = cl & ~dil(tw, 3 * R)
    out.append(L[b].mean() - L[s].mean() if b.any() and s.any() else np.nan)
x = np.array(out); x = x[~np.isnan(x)]
ma = np.convolve(x, np.ones(60) / 60, 'valid'); hp = x[30:30 + len(ma)] - ma
print("%s frames %d  base-sea mean %.2f  fade std (0.1-1 s) %.3f  |d/dframe| %.3f" % (sys.argv[3], len(x), x.mean(), hp.std(), np.abs(np.diff(x)).mean()))
