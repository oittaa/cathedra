#!/usr/bin/env python3
"""Temporal stability metric (dev tool, not part of the 64k).
Renders consecutive 60 fps frames with the dev build (--raw, real-time TAA path),
drops the TAA lead-in, and reports per-pixel frame-to-frame luminance change in
named regions:
  d1  = |L[k]-L[k-1]|                     (all change incl. camera motion)
  d2  = |L[k]-(L[k-1]+L[k+1])/2|          (temporal 2nd derivative: steady motion
                                            cancels, blinking / popping does not)
  dw  = |L[k]-warp(L[k-1])|               (optical-flow motion compensated change: the
                                            flicker metric; needs opencv-python-headless)
Luminance in 0..255 (Rec.709 on the 8-bit output), on 2x2 box-averaged pixels
(suppresses the per-frame film grain of the post pass).
usage: flicker.py <cathedra_dev> <out_dir> [--size 960x540] [--tier 1] [--lead 4] [--n 8]
       [--shots name:t0[:x0,y0,x1,y1;...]] ...   rects are normalised [0..1] image coordinates
"""
import sys, os, subprocess, json, argparse
import numpy as np, cv2
from PIL import Image
SHOTS = {   # name: (start time, frozen instant?, regions as normalised x0,y0,x1,y1)
  # moving camera, real-time TAA path
  "open6":   (6.0,   0, {"sea": (0., .55, 1., .87), "spire_bases": (.45, .38, 1., .56)}),   # opening sea
  "open":    (8.0,   0, {"sea": (0., .55, 1., .87), "spire_bases": (.45, .38, 1., .56)}),
  "city":    (20.0,  0, {"sea": (0., .66, 1., .87), "tower_bases": (0., .45, 1., .66)}),
  "bridges": (44.0,  0, {"sea": (0., .76, 1., .87), "tower_bases": (0., .50, 1., .76)}),
  "window":  (104.0, 0, {"windows": (.66, .37, .96, .81)}),
  # frozen instant (--still): camera and scene fixed, only TAA sub-pixel jitter advances ->
  # any change is aliasing / z-fighting / unresolved noise, i.e. pure flicker
  "open6_still":   (6.0,   1, {"sea": (0., .55, 1., .87), "spire_bases": (.45, .38, 1., .56)}),
  "open_still":    (8.0,   1, {"sea": (0., .55, 1., .87), "spire_bases": (.45, .38, 1., .56)}),
  "city_still":    (20.0,  1, {"sea": (0., .66, 1., .87), "tower_bases": (0., .45, 1., .66)}),
  "bridges_still": (44.0,  1, {"sea": (0., .76, 1., .87), "tower_bases": (0., .50, 1., .76)}),
  "window_still":  (104.0, 1, {"windows": (.66, .37, .96, .81)}),
}
ap = argparse.ArgumentParser()
ap.add_argument("exe"); ap.add_argument("out")
ap.add_argument("--size", default="960x540"); ap.add_argument("--tier", default="1")
ap.add_argument("--lead", type=int, default=4); ap.add_argument("--n", type=int, default=8)
ap.add_argument("--only", default="")
ap.add_argument("--reuse", action="store_true", help="analyse the PNGs already in <out_dir>")
a = ap.parse_args()
W, H = map(int, a.size.split("x")); os.makedirs(a.out, exist_ok=True)
res = {}
for name, (t0, still, regs) in SHOTS.items():
    if a.only and name not in a.only.split(","): continue
    nf = a.lead + a.n + 1
    ts = t0 if still else t0 - a.lead / 60.
    cmd = [os.path.abspath(a.exe), "--tier", a.tier, "--size", a.size, "--raw", "60", "%.6f" % ts, "%.6f" % (ts + (nf + .5) / 60.)] + (["--still"] if still else [])
    if a.reuse:
        fs = np.stack([np.asarray(Image.open(os.path.join(a.out, "%s_%02d.png" % (name, k))).convert("RGB")) for k in range(a.n + 1)])
    else:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=a.out)
        fs = np.frombuffer(p.stdout, np.uint8)
        n = fs.size // (W * H * 3); fs = fs[:n * W * H * 3].reshape(n, H, W, 3)[a.lead:a.lead + a.n + 1]
        for k, f in enumerate(fs): Image.fromarray(f).save(os.path.join(a.out, "%s_%02d.png" % (name, k)))
    L = fs.astype(np.float32) @ np.array([.2126, .7152, .0722], np.float32)
    L = L[:, :H // 2 * 2, :W // 2 * 2].reshape(len(L), H // 2, 2, W // 2, 2).mean((2, 4))
    d1 = np.abs(np.diff(L, axis=0))[1:]
    d2 = np.abs(L[1:-1] - .5 * (L[:-2] + L[2:]))
    # dw: motion-compensated change. Dense optical flow (Farneback, wide window, on a
    # blurred copy so the flow follows the camera, not the shimmer) warps frame k-1 onto k.
    dw = []
    for k in range(2, len(L)):
        A, B = L[k - 1], L[k]
        fl = cv2.calcOpticalFlowFarneback(cv2.GaussianBlur(B, (0, 0), 1.5).astype(np.uint8), cv2.GaussianBlur(A, (0, 0), 1.5).astype(np.uint8),
                                          None, .5, 4, 21, 3, 7, 1.5, 0)
        gy, gx = np.mgrid[0:A.shape[0], 0:A.shape[1]].astype(np.float32)
        Aw = cv2.remap(A, gx + fl[..., 0], gy + fl[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        dw.append(np.abs(B - Aw))
    dw = np.stack(dw)
    res[name] = {}
    for rn, (x0, y0, x1, y1) in regs.items():
        sl = (slice(None), slice(int(y0 * H / 2), int(y1 * H / 2)), slice(int(x0 * W / 2), int(x1 * W / 2)))
        r = {"d1_mean": float(d1[sl].mean()), "d1_p99": float(np.percentile(d1[sl], 99)),
             "d2_mean": float(d2[sl].mean()), "d2_p99": float(np.percentile(d2[sl], 99)),
             "frac_d2_gt8": float((d2[sl] > 8).mean()),
             "dw_mean": float(dw[sl].mean()), "dw_p99": float(np.percentile(dw[sl], 99)), "frac_dw_gt8": float((dw[sl] > 8).mean())}
        res[name][rn] = r
        print("%-8s %-11s t=%6.2f  d1 mean %6.3f p99 %6.2f | d2 mean %6.3f p99 %6.2f >8 %.4f | dw mean %6.3f p99 %6.2f >8 %.4f" % (name, rn, t0, r["d1_mean"], r["d1_p99"], r["d2_mean"], r["d2_p99"], r["frac_d2_gt8"], r["dw_mean"], r["dw_p99"], r["frac_dw_gt8"]), flush=True)
    # visual: max |d2| over the run, amplified
    hm = np.clip(dw.max(0) * 8, 0, 255).astype(np.uint8)
    Image.fromarray(hm).resize((W, H)).save(os.path.join(a.out, "%s_flickermap.png" % name))
json.dump(res, open(os.path.join(a.out, "flicker.json"), "w"), indent=1)
