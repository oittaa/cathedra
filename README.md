# CATHEDRA 1.0

A 64k intro for Linux. Vulkan 1.3 with hardware ray tracing, 65,536 bytes.

## Requirements

- Linux with X11 or XWayland (tested on Ubuntu 26.04)
- A GPU with Vulkan 1.3, `VK_KHR_acceleration_structure` and `VK_KHR_ray_query` (Nvidia RTX, AMD RX 6000+, Intel Arc)
- An x86-64 CPU with SSE4.1 (any from 2008 on)
- `libvulkan1` and your GPU driver, ALSA or PipeWire for sound

## Run

```
./build/cathedra
```

Loading takes about 10-15 seconds, then the show runs fullscreen at the desktop resolution. `Esc` quits, `Space` pauses.

It runs in real time. On an Nvidia RTX 4080 SUPER at 1080p it stays above 60 fps from the first frame to the last, at native resolution or above: rendered at 1×, the slowest frame of the film takes 6.4 ms of GPU time, against the 16.7 ms of a 60 fps frame. Before the show starts, the intro times every section and spends the headroom on supersampling (1.375× to 2× per section on the 4080 at 1080p). A full real-time run then averages 7.9 ms of GPU time per frame, 11.6 ms at most. A slower GPU gets a lower render scale, down to 0.5×, not a lower frame rate. The exception is the section cuts where the render scale changes, with one or two longer frames each.

## Build

Reproducible build with Docker. It produces a bit-identical binary:

```
./build_docker.sh            # uses the Docker cache
./build_docker.sh --clean    # from scratch
```

Expected sha256 of `build/cathedra`:

```
ce7adc8d7087ba6c82f7bb8b04a8575cf2a862b833163b76df4992751d5fce7c
```

## Dev build

`build/cathedra_dev` has no size limit and adds tools:

```
./build/cathedra_dev -w                      # windowed
./build/cathedra_dev --section 7 --loop      # loop one section (0-10)
./build/cathedra_dev -w --from 100 --to 140  # play a time range
./build/cathedra_dev --selftest              # timing log and screenshots
./test_linux.sh -all                         # self-test, timing sweep, NaN, flash and real-time checks
```

`AGENTS.md` describes the tools: building in the pinned Docker image, the byte budget, the checks, and comparing builds.

## Video export

```
./build/cathedra_dev --export out --size 3840x2160 --fps 60
./build/cathedra_dev --export out --section 7                 # one section
./build/cathedra_dev --export out --first 6720 --last 7679    # a frame range
./build/cathedra_dev --export out --section 8 --redo --nowav  # render again, no WAV
```

Writes 16-bit PNG frames (`out/frame_000000.png` and on) and `out/cathedra_48k.wav`. The audio starts at frame 0. Frame numbers are those of the whole film, so a section or a range drops into a full export. Rerunning the command resumes where it stopped. `--redo` renders frames that are already there again, and `--nowav` leaves out the WAV.

The export is offline, not real time. Every frame averages 32 jittered renders (`--spp`), so on an RTX 4080 SUPER a 4K frame takes 0.4 to 1.2 s and the whole film about two hours.
