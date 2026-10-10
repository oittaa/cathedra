# AGENTS.md

How to build, measure, test and export CATHEDRA when you change it. `DESIGN.md` explains how the
intro works; this file is about the tools around it.

## Rules

1. The release `build/cathedra` stays at or under 65,536 bytes. Only the pinned Docker toolchain
   gives the real number (see *Build*); the host compiler differs.
2. A change that alters the release also updates `SHA256SUMS` and the sha256 in `README.md`, both
   taken from a Docker build.
3. `src/spirv.h` and `src/spirv_dev.h` are generated from `src/shaders` by `tools/build_spirv.py`,
   and `src/font.h` by `tools/bake_font.py`. Never edit them by hand. `src/spirv.h` is committed:
   the dev build and hosts without glslang/spirv-tools compile against it, so commit the header the
   Docker build regenerated.
4. `src/devtools.h` and everything under `#ifdef CATHEDRA_DEV` exist only in the dev build. They cost
   nothing in the 64k.
5. Real time is 60 fps at 1080p with every section at native resolution or above on the reference
   GPU (RTX 4080 SUPER). Keep the worst frame of the 1x sweep well under 16.7 ms (it is 6.4 ms).
   The 4K video export is offline and may take as long as it needs.
6. No flashes or strobing. Light that follows the music changes in smooth pulses, and
   `--lumacheck 0 176` (the whole film, about 1.5 minutes) and `--rtcheck` must pass.
7. The scene must never output NaN or Inf (one bad sample lives on in the TAA history).
   `--nancheck` must pass.

## Build

Reproducible release, the authoritative size and hash:

```
./build_docker.sh                      # cached toolchain layer; checks SHA256SUMS; also build/cathedra_dev
cp build/spirv.docker.h src/spirv.h    # after a shader change: the header to commit
```

The image `cathedra-build:1.0` that `build_docker.sh` makes can also build a copy of the tree
directly. This is the quickest loop, and it gives the dev binary too (same `/build/cathedra` path
and steps as the Dockerfile, so `cathedra` is bit-identical to `./build_docker.sh`):

```
mkdir -p build/docker
docker run --rm --network none -v "$PWD":/src:ro -v "$PWD/build/docker":/out cathedra-build:1.0 sh -c '
  mkdir -p /build/cathedra && cd /build/cathedra && cp -r /src/build_linux.sh /src/tools /src/src . &&
  rm -f src/spirv.h && ./build_linux.sh > /out/build.log 2>&1 &&
  cp build/cathedra build/cathedra_dev src/spirv.h /out/ && chown -R '"$(id -u):$(id -g)"' /out'
tail -3 build/docker/build.log         # "LZMA dropper : N bytes"
```

`build/docker/spirv.h` is the header to commit; `build/docker/cathedra_dev` runs on the host
(Ubuntu 26.04, same glibc as the image).

Without Docker, `./build_linux.sh dev` builds `build/cathedra_dev` from the committed `src/spirv.h`
(needs build-essential, libx11-dev, libasound2-dev). `./build_linux.sh` also packs a release, but
its size only counts when it comes from the image. If the host has glslangValidator and
spirv-tools, `build_linux.sh` regenerates `src/spirv.h` with them; commit only the header the image
made.

## The byte budget

Free bytes are 65,536 minus the size of `build/cathedra`. About 38 KB of the packed file is the
shaders, about 26 KB the x86 code and data, and 156 bytes the shell header. Rough costs after
packing and LZMA:

| Change | Cost |
|---|---|
| one more SPIR-V instruction | 2-3 bytes |
| a new float constant | 2-4 bytes; short decimals are cheapest (`.25` beats `.2473`) |
| a different value for an existing constant | about 0 |
| a 10-20 line shader feature | 100-400 bytes |
| C code | about half its x86 size |

Measure every change. LZMA moves a few bytes either way on unrelated edits, and the order of data
matters: the title glyphs packed 169 bytes smaller column by column than row by row, while sorting
the Vulkan function names made the file 12 bytes bigger.

## Shader pipeline

`tools/build_spirv.py`: glslangValidator, then spirv-opt (a size-oriented pass list), then
`spv_byval.py` (read-only parameters by value), then spirv-opt again, then `spv_sortdecl.py` (the
most used constants first), then `spv_renum.py` (ids in definition order), then spirv-val, then
`spvpack.py` (nine streams, SMOL-V style), and finally `src/spirv.h`. `unspv()` in `src/main.c`
decodes it at startup. All compute passes are one module, `bake.comp`, selected by the
specialization constant `MODE`.

The build asserts that `spvpack.decode_all` reproduces the modules. If you change the packing,
also run the C decoder: compile `unspv()` with the release flags and compare its output with
`decode_all`, module by module.

## Dev binary

`build/cathedra_dev` is the release picture plus the switches in `src/devtools.h`.

| Switch | Use |
|---|---|
| `-w`, `--size WxH`, `-s scale`, `--tier 0-3` | window, resolution, fixed render scale, quality tier |
| `--section n`, `--from s --to s`, `--loop` | play one section or a time range, optionally looping |
| `--selftest`, `--shot t`, `--frames n`, `--taa n` | stills (`shot_t*.png`) and a timing log (`cathedra_selftest.log`) in the working directory; `--frames 0` skips the per-section timing |
| `--raw fps t0 t1`, `--still` | raw RGB24 frames to stdout, deterministic (`--still` freezes the instant) |
| `--sweep` | per-second GPU time at 1x over the film |
| `--nancheck t0 t1` | every frame: NaN/Inf counts on the GPU and black tiles (exit 3) |
| `--lumacheck t0 t1` | every frame: luma spikes and exposure pulses (exit 4) |
| `--rtcheck T`, `--rtfrom t0` | real-time playback with the release's scaler, per-frame `rtcheck.csv` (exit 5) |
| `--export dir` and its options | see *Video export* |

`CATHEDRA_NOAUDIO=1` plays without sound. `CATH_OLDSCALE` selects the older mid-shot scaler.

## Tests

`./test_linux.sh` runs `build/cathedra_dev` into `test_out/`: the self-test log and stills at 14
instants. `-sweep`, `-nancheck` (140-175 s), `-lumacheck` (0-176 s) and `-rtcheck` (real-time start,
0-4 s) add the checks; `-all` runs all four, `-export N` adds an N-second export smoke test and
`-rebuild` builds first. The exit code is that of the first failing check.

The timing checks (`--sweep`, `--rtcheck`, the `ms` in the log) react to anything else using the
GPU: a video player, a browser, a running export. Rerun before you trust a failure. The section
cuts where the render scale changes (most of them at 1080p on the 4080) reallocate the render
targets, which costs one or two long frames there (20-45 ms). That has always been so. Sections whose
probe time sits near a threshold of `sqrt((12 ms - BVH) / worst rest)` can get either scale from run to run.

To check a change elsewhere did not alter the picture, compare frames, not `--shot` stills: a
self-test still warms up for 0.4 s of wall clock, so its TAA jitter phase differs from run to run.
`--raw 60 t0 t1` is deterministic. Render the same instants with both builds and diff the frames:
`tools/framediff.py A B t...` does that.
The city sections (0-2 and 6) still differ in a few hundred pixels between two runs of the same
build.

## Video export

```
./build/cathedra_dev --export out                                 # 4K60, the whole film
./build/cathedra_dev --export out --section 7                     # one section
./build/cathedra_dev --export out --first 6720 --last 7679        # frames 6720-7679
./build/cathedra_dev --export out --section 8 --redo --nowav      # render again, no WAV
./build/cathedra_dev --export - | ffmpeg ...                      # raw frames to a pipe
```

Frames are numbered as in the whole film (frame N is time N/fps), so a section or range drops
straight into a full export. Frames already there are skipped (rerun to resume) unless `--redo`.
`--nowav` leaves out `cathedra_48k.wav`; the synth still runs, because the picture syncs to it.
Each frame averages `--spp` jittered renders (32 by default), so on an RTX 4080 SUPER a 4K frame
takes 0.4-1.2 s and the whole film about two hours. Every run rewrites `export_info.txt` for its
own range.

## Other tools

| Tool | Use |
|---|---|
| `tools/framediff.py` | two dev builds at the same instants: deterministic frames diffed (max, mean, pixels over 2/255), `--png` adds side-by-side heat maps; numpy, Pillow |
| `tools/noisescan.py` | residual noise of frozen instants (`--still`, only the TAA jitter moves): a cheaper march that leaves it unchanged adds no shimmer; numpy, Pillow |
| `tools/flicker.py` | temporal stability of named regions over a few frames (`--raw`); numpy, opencv, Pillow |
| `tools/fade.py` | slow fading at the tower bases over long windows; numpy |
| `tools/basecheck.py` | tower-base stability, from a second render of the same frames by a mask build (`--mask`, see below) |
| `tools/synth_test.c` | `cc -O2 -o build/synth_test tools/synth_test.c -lm -lpthread`: renders the soundtrack to a WAV, prints timing, peak and RMS |
| `tools/bake_font.py` | regenerates `src/font.h` (the title lines and their glyphs) from the Cinzel in `tools/fonts` (Pillow); with this font it reproduces the committed header byte for byte |

A mask build is a dev build whose world 0 draws a flat segmentation instead of the picture: red =
tower stone in front, green = cloud in front, blue = sky. It is not a compile switch. In a copy of
the tree, replace the final `return col;` of `world0()` in `src/shaders/scene.frag` with

```
return vec3(t<1e4&&cl.a>.3?9.:0.,cl.a<=.3?9.:0.,t>=1e4&&cl.a>.3?9.:0.);
```

and build that copy's `cathedra_dev` in the image. `basecheck.py` uses the masks to measure
flicker and fading only where the cloud meets the tower bases, against the open sea.

## Comparing pictures

For a visual change, render the same instants with the dev builds of both commits at the
resolution that matters (crops of 4K stills show what a 4K export will), and look at motion with a
strip of consecutive `--raw` frames. Light that flickers needs `--lumacheck` and `--rtcheck` over
its range.
