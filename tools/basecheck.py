#!/usr/bin/env python3
"""Tower-base temporal stability (dev tool, not part of the 64k).
Renders consecutive 60 fps frames with the dev build (--raw, real-time TAA path) and measures
the motion-compensated frame-to-frame luminance change (dw, as in flicker.py) separately in
  bases : cloud pixels within R px of a tower silhouette (the cloud/tower contact zone)
  sea   : cloud pixels farther than 3R from any tower (reference: the open cloud sea)
The masks come from a second render of the same frames with a mask build (--mask exe: world 0
writes red = tower stone in front, green = cloud in front, blue = sky;
how to make one: AGENTS.md), so the metric follows the towers exactly as the camera moves.
ratio = dw(bases)/dw(sea): 1 means the bases are as steady as the open sea.
Also writes <name>_heat.png (mean dw, amplified, bases outlined) and <name>_strip.png.
usage: basecheck.py <cathedra_dev> <out_dir> [--size 640x360] [--tier 1] [--n 48] [--shots name:t0,...]"""
import sys, os, subprocess, json, argparse
import numpy as np, cv2
from PIL import Image
ap = argparse.ArgumentParser()
ap.add_argument("exe"); ap.add_argument("out")
ap.add_argument("--size", default="640x360"); ap.add_argument("--tier", default="1")
ap.add_argument("--lead", type=int, default=6); ap.add_argument("--n", type=int, default=48)
ap.add_argument("--shots", default="open6:6,city22:22,low36:36,low42:42")
ap.add_argument("--mask", required=True); ap.add_argument("--still", action="store_true"); ap.add_argument("--reuse", action="store_true")
a = ap.parse_args()
W, H = map(int, a.size.split("x")); os.makedirs(a.out, exist_ok=True)
R = max(3, W // 64)
res = {}
for sh in a.shots.split(","):
    name, t0 = sh.split(":"); t0 = float(t0)
    nf = a.lead + a.n + 1; ts = t0 - a.lead / 60.
    npy = os.path.join(a.out, name + ".npy")
    cmd = [os.path.abspath(a.exe), "--tier", a.tier, "--size", a.size, "--raw", "60", "%.6f" % ts, "%.6f" % (ts + (nf + .5) / 60.)] + (["--still"] if a.still else [])
    if a.reuse and os.path.exists(npy): fs = np.load(npy)
    else:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=a.out)
        fs = np.frombuffer(p.stdout, np.uint8); n = fs.size // (W * H * 3)
        fs = fs[:n * W * H * 3].reshape(n, H, W, 3)[a.lead:a.lead + a.n + 1].copy(); np.save(npy, fs)
    mnpy = os.path.join(a.out, name + "_mask.npy")
    if os.path.exists(mnpy): ms_ = np.load(mnpy)   # the mask only depends on the camera: rendered once per out dir
    else:
        p = subprocess.run([os.path.abspath(a.mask)] + cmd[1:], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=a.out)
        ms_ = np.frombuffer(p.stdout, np.uint8); n = ms_.size // (W * H * 3)
        ms_ = ms_[:n * W * H * 3].reshape(n, H, W, 3)[a.lead:a.lead + a.n + 1].copy(); np.save(mnpy, ms_)
    M = ms_.astype(np.int16)
    tower = (M[..., 0] > M[..., 1] + 40) & (M[..., 0] > M[..., 2] + 40)
    cloud = (M[..., 1] > M[..., 0] + 40) & (M[..., 1] > M[..., 2] + 40)
    F = fs.astype(np.float32)
    L = F @ np.array([.2126, .7152, .0722], np.float32)
    k1 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * R + 1, 2 * R + 1)); k3 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (6 * R + 1, 6 * R + 1))
    near = np.stack([cv2.dilate(t.astype(np.uint8), k1) > 0 for t in tower]); far = np.stack([cv2.dilate(t.astype(np.uint8), k3) > 0 for t in tower])
    k0 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)); edge = np.stack([cv2.dilate(t.astype(np.uint8), k0) > 0 for t in tower])
    bases = near & cloud & ~edge; sea = cloud & ~far   # (2 px next to the silhouette excluded: edge AA is not cloud flicker)
    dw = []; d2 = []
    for k in range(1, len(L)):
        A, B = L[k - 1], L[k]
        fl = cv2.calcOpticalFlowFarneback(cv2.GaussianBlur(B, (0, 0), 1.5).astype(np.uint8), cv2.GaussianBlur(A, (0, 0), 1.5).astype(np.uint8), None, .5, 4, 21, 3, 7, 1.5, 0)
        gy, gx = np.mgrid[0:H, 0:W].astype(np.float32)
        dw.append(np.abs(B - cv2.remap(A, gx + fl[..., 0], gy + fl[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)))
    dw = np.stack(dw); mb = bases[1:]; ms = sea[1:]
    # slow fading: per-frame mean luma of the base zone relative to the sea (detrended), std over time
    rel = np.array([L[k][bases[k]].mean() - L[k][sea[k]].mean() if bases[k].any() and sea[k].any() else 0 for k in range(len(L))])
    r = {"dw_bases": float(dw[mb].mean()), "dw_sea": float(dw[ms].mean()), "ratio": float(dw[mb].mean() / max(dw[ms].mean(), 1e-6)),
         "p99_bases": float(np.percentile(dw[mb], 99)), "frac_gt8_bases": float((dw[mb] > 8).mean()),
         "rel_luma_std": float(np.std(rel - np.polyval(np.polyfit(np.arange(len(rel)), rel, 1), np.arange(len(rel))))),
         "base_px": int(mb.sum() / len(mb))}
    res[name] = r
    print("%-8s t=%6.2f  dw bases %6.3f  sea %6.3f  ratio %5.2f | bases p99 %6.2f >8 %.4f | base-vs-sea luma wobble %.3f | %d px" % (name, t0, r["dw_bases"], r["dw_sea"], r["ratio"], r["p99_bases"], r["frac_gt8_bases"], r["rel_luma_std"], r["base_px"]), flush=True)
    hm = np.clip(dw.mean(0) * 10, 0, 255).astype(np.uint8)
    hc = cv2.applyColorMap(hm, cv2.COLORMAP_INFERNO)[..., ::-1].copy()
    cnt, _ = cv2.findContours(bases[len(bases) // 2].astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    cv2.drawContours(hc, cnt, -1, (60, 200, 255), 1)
    Image.fromarray(hc).save(os.path.join(a.out, name + "_heat.png"))
    Image.fromarray(np.concatenate([fs[0], fs[len(fs) // 2], fs[-1]], 1)).save(os.path.join(a.out, name + "_strip.png"))
json.dump(res, open(os.path.join(a.out, "basecheck.json"), "w"), indent=1)
