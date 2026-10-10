# CATHEDRA 1.0

A 64k intro for Linux. One fullscreen Vulkan 1.3 pass with hardware ray queries, a procedural soundtrack, and a GPU precalc that fills the card before the film starts. The packed entry is `build/cathedra`, at most 65,536 bytes.

## Release and dev

`cathedra` has no command line. It opens fullscreen at the desktop resolution, `Esc` quits, and `Space` pauses. Failure is one line on stderr.

`cathedra_dev` (`-DCATHEDRA_DEV`) is the same picture plus the switches in `src/devtools.h`: windowed playback, a time range, self-test, stills, and frame export. Both builds embed the same `src/spirv.h`.

The sources also contain a Win32 window, `waveOut` playback, and a MinGW `TINY_CRT` entry. `CMakeLists.txt` can build that unpacked executable. The 65,536-byte limit is the Linux shell dropper from `build_linux.sh`, not a Windows pack.

## The film

Eleven sections of 16 seconds. The camera is one function of time (`camera()` in `src/shaders/common.glsl`), shared by the scene, the swarm, and the temporal resolve. Titles are a signed-distance field baked from Cinzel, read through a small [1 2 1] filter so the glyph bitmap's pixel steps do not show when a title is magnified (the opening title is 8x at 4K). The opening title appears letter by letter from left to right, and later a slow warm glint crosses it once.

| Sections | World | Title |
|---|---|---|
| 0–2 and 6 | Gothic city in a cloud sea. 6 is the climb through the deck. | I ASCENSIO |
| 3–4 | Infinite nave, rose windows, wet floor. | II NAVIS |
| 5 | Fractal core. | III COR |
| 7–8 | Ringed planet. 7 carries the ring station; 8 is the eclipse. | IV ORBIS |
| 9–10 | The star, habitat rings, swarm, and the planet taken apart. | V SOL |

## A frame

Push constants are time, resolution, render scale, eight music envelopes, the previous time, sub-pixel jitter, and a history flag. One descriptor set holds the noise volumes, the title atlas, the HDR scene, the TAA history, the cloud, city-light, and planet volumes, the planet map, the meshes, the swarm, and the two top-level acceleration structures.

1. A compute pass writes this frame’s camera and the previous one into the last slots of the swarm buffer.
2. From t = 144 s the finale BVH work runs every frame: debris transforms, the swarm vertex pass, and a rebuild of the finale top-level structure. All three are functions of time alone, so they need no warm-up.
3. The scene is a fullscreen triangle at the render scale. Meshes are ray queries. Volumes and SDFs are marched. The swarm is instanced on top from t = 144 s.
4. Temporal resolve: Halton(2,3) jitter, analytic reprojection from both cameras, YCoCg variance clip, into a ping-pong history with a mip chain. History restarts on a section cut, and the first frames after it average evenly, so the first raw frame does not linger.
5. Post: background-only depth of field (none in the cloud sections), mip bloom, ACES, a per-world grade, grain, a smooth vignette, letterbox, and the titles. The end card is bevelled gold from the glyph distance field (no dark plate behind it: its bytes went to the opening's mist).

The target is 60 fps in real time, at native resolution or above. On the reference RTX 4080 SUPER at 1080p the slowest frame at 1× takes 6.4 ms (the core), so every section renders above 1× (1.375× to 2×). Before audio starts, `probe()` times four instants of each section at 1× and sets that section’s render scale to `sqrt((12 ms - BVH) / worst rest)` (the finale’s per-frame BVH work does not grow with the resolution), in steps of 1/8, clamped to 0.5–2. The scale is applied only at the section cut. Inside a shot the only change is an emergency step down when 30 frames average over 15 ms. `CATH_OLDSCALE` in the dev build selects the older mid-shot scaler instead. A scale change at a section cut reallocates the render targets, which costs one or two long frames there. The video export in the dev build is offline: 32 jittered renders per frame, no time limit.

## Precalc

The loading bar advances per stage. The big volumes are allocated softly: if one fails, the set is freed and the next lower tier is used. Tier 0 is a CPU device. Otherwise the tier follows the largest device-local heap: above 12 GB is 3, above 7 GB is 2, above 3 GB is 1.

Columns are cloud volume (x, y, z), planet volume, planet map width, tower variants, swarm elements, debris chunks.

| Tier | Cloud | Planet | Map | Variants | Swarm | Debris |
|---|---|---|---|---|---|---|
| 0 | 256×24×256 | 160³ | 1024 | 6 | 128 K | 1,024 |
| 1 | 768×48×768 | 320³ | 2048 | 16 | 256 K | 4,096 |
| 2 | 1536×96×1536 | 512³ | 4096 | 32 | 1 M | 16,384 |
| 3 | 2048×128×2048 | 640³ | 8192 | 48 | 4 M | 32,768 |

Also baked, at every tier: 64³ value noise, a 128³ Perlin-Worley volume with 8 mips, and the title atlas. The city-light volume is a coarser grid over the same city (a quarter of the cloud width, half the height plus a margin). Cloud voxels get a handful of ray-traced tower-shadow rays (96 at tier 3). City-light voxels get many more rays over the sphere plus one sun-shadow ray (3,072 at tier 3).

Music renders on the CPU at the same time, one thread per section, into a 176 s stereo buffer at 48 kHz (the rate of most sound servers and of video, so nothing resamples it) with per-instrument envelopes at 100 Hz.

## The city

`src/arch.h` writes compact parts: frusta, buttress struts, pointed-arch frames, glass, tracery, rocks. `mesh_gen.comp` expands them into triangles. Each variant stacks tiers on a buttressed octagonal plinth, with lancets, gables, pinnacles, flying buttresses, and a spire. Some variants are ruins (the spire broken off). Heroes are never ruins. A unit sky-bridge span is instanced between row neighbours, below every body top. The city reaches 6 km deep, and beyond 3.6 km towers close the avenue, so no view ends at an empty horizon.

Shading is ray-queried. Stone is an ashlar height field shared by the albedo and a close-range relief march, so colour and joints agree: dressed blocks of varying length in courses of varying height, flat faces with crisp arrises, thin lime-mortar joints recessed up to 3 cm. Detail is band-limited by the pixel footprint. Gold leaf follows the slow waves of the beaten metal, so a flat facet does not mirror one flat sky. A quarter of the windows are lit from inside, except down at the cloud line. Glass is leaded pot-metal (cobalt and ruby first, then amber, green and violet) that mirrors the sky from outside and glows softly against the light; far panes fade to a dark average. Fog is denser low and thinner aloft. Rays reach 4 km and dissolve into the sky colour before they end. A flock of 16 birds crosses the opening in world space, covered by the cloud transmittance in front of them: each its own size, gull wings bent up to the wrist and down to the tip, their edges glowing warm when the birds pass near the low sun. In the climb a second flock circles the tower 90-135 m out, at 90-210 m: the camera looks up at it, rises past it, and at the end looks down on it over the deck.

The cloud sea is a baked density plus sun and sky optical depth plus tower visibility. Live erosion fades with distance. Above the deck lies a faint morning mist (50-85 m): soft banks drifting between the towers, thinning toward the layer's top and bottom, lit like the sky just above the horizon in each direction. The closed undercast below the deck rolls in heaps too, so the climb tower's hollow has no flat floor. In the last 16 m before a tower the march takes no long empty strides, so the thin cloud hugging a tower's base is caught in every frame (with long strides it flickered in the fast low flight), and within 2.5 m of a wall the cloud is lit from just outside it (the bake blends in the tower's lit inside). Two short taps toward the sun shade the billows; they read the baked density alone, without the erosion. A stride that would jump past a tower ends in front of it, so the thin cloud around each base is always integrated. Voxels inside a tower start their sun rays outside that tower’s own walls. From the climb, an open ray keeps an analytic far deck out to the horizon, mip-filtered, and the climb itself scales cloud opacity with distance so the sunlit deck top does not white out the frame. Seen from up there, the analytic deck beyond the march is lit a little dimmer, so it meets the marched sea at the horizon instead of a white strip. It also carries 200 m cells like the marched heaps, so the texture runs on past the end of the march, and from high up it fades into the horizon haze. In the opening act the deck answers the music: lightning glows deep in the clouds ahead on some beats, more often in the low flight, and always on the impact that opens the flight. Whether a beat flashes is decided once per beat, so a flash never cuts off or pops on halfway. Each flash is one soft pulse (a 60 ms rise, gone before the next beat), not a strobe, so `--lumacheck` and `--rtcheck` pass.

## The nave and the core

The nave is a ray-marched gothic hall. The air of the vault glows with the neon of each bay, in section 3 pulsing with the snare and the travelling wave like the tubes. In section 4 soft swells of neon run down the hall and reach the core on every chord change, then on every bar once the riser starts and on every beat in the last four seconds, brighter toward the core (which swells as each one arrives) and heating up to its orange as the riser builds. The oculi are rose windows: coloured panes, lead, and a sun jittered across its disc so the pools of light blur with distance. Water stains run down the piers from the capitals, fading as they go, and gather on the vault webs toward the springing, pale mineral streaks run between them, and the pier bases are dark with rising damp below a ragged tide line edged with salts; all of it fades out where it would get finer than the pixel footprint. The floor runs from dry slab to damp film to standing puddles, and the reflected ray picks up the shafts. Dust drifts through the window light: one mote per half-metre cell, scattered at random, each wandering on its own while the air slowly settles; most are faint and a few glint, and they show only where a beam crosses them.

The core is a ray-marched fractal, nine iterations, with a kick haze that does not cover the surface. Its heart is molten: the depths of the bulb glow orange through its openings, swelling with the kick and the bass, and pulses of light run outward along the veins. Its light streams out into the haze as rays: a sample in the air is lit where the shell is open in its direction from the centre (one extra fractal evaluation per march step, four iterations deep, skipped right at the surface). On the drop and at every chord change (every 4 s) a shockwave bursts out of the heart: a ragged shell of light that sweeps out through the surface, lighting it as it passes, and fades before it can reach the camera. It glows mostly at its rim, and its brightness follows the camera distance so it covers the screen the same at every wave and stays under the flash limit.

## Orbit

The planet map is equirectangular: elevation, city lights, cloud cover, moisture. Cover includes fronts, convective clusters, trade cumulus, west-coast decks, and three log-spiral cyclones with a clear eye and a cirrus outflow. The cloud layer is a shell of jittered steps. Oceans carry depth colour, shelves, blooms, wind-roughened sunglint (a Beckmann lobe, broken up by streaky calm slicks), and on the night side city lights (clusters of separate towns and districts, not soft blobs), fishing fleets, moon glint, and ring-shine. Sunlight reddens toward the terminator. The rings carry sharp ringlets of random opacity, faded to their mean below the pixel footprint. Looking straight down, the haze is thinner than at the limb.

Section 7 uses a sun that shows a growing crescent. The home star already wears a Dyson swarm: eight hair-thin rings of countless collectors, nested and tilted, each with a soft glow, glints running along them, the far halves (their sunlit faces) brighter, some still being built. A ring station co-orbits with the camera and drifts across the limb with the sun behind it. Its torus, six spokes, and hub spin; the despun mast and its four solar wings stay put. The wings are dark cells with the sun showing through the gaps between them. The ring carries a row of lit windows on each face and red and green lights on its rim; white strobes on the mast ends flash with the kick. Three seconds in, a shuttle leaves the hub and burns away retrograde, dropping toward the planet: a blue-white engine light with a trail as long as its last second of flight. Earthshine lights the side that faces the planet, and the edges that graze the sun get a warm rim. It is sphere-traced inside a bounding sphere, with a tetrahedron normal, to a hit tolerance below a pixel at 4K, and its edges are left to the temporal jitter. Section 8 keeps the eclipse sun. Around the planet, in the plane of its rings, runs an orbital ring habitat built like the finale's rings (plated hull, rows of windows, lamp rows, the running blue strip, shaded as hull all round), lit by the sun past the planet and tethered by six elevators. Past the limb, the star's corona runs out in streamers, brightest on the side where the hidden sun sits nearest the limb, around a thin red ring: the sunsets all round the planet seen through its air. As the riser builds to the cut, the edge of the sun breaks out at the limb point nearest it: a diamond ring, its glare swelling with the music.

## The finale

Habitat rings are solid bands: an outer hull with rows of lit windows, an inner inhabited cylinder, and two side walls, so the band is opaque from every side. The inhabited side is coloured like the Earth seen from orbit (muted farmland and forest, deserts, bare rock and snow on the heights, beaches, turquoise shallows, clouds grey where thin), shaded by the slope of its terrain,, seen through the band’s own air, which thickens the more obliquely we look. The star’s surface shows its granulation, spots, faculae and limb darkening (bright enough to blaze, dim enough not to clip). Its halo is drawn only when nothing sits in front of it. The corona is a short volumetric march: helmet streamers, polar plumes, and a mass ejection reseeded every 9 seconds. Swarm shadows are two layers of collectors on the direction from the star, each latitude rotating at its own rate.

The planet being taken apart is a baked signed-distance, heat, and strata volume: an open rift, terraces, canyons, and magma. A mass-driver ring circles it: a segmented steel tube with a row of small lights, payloads running along it as cyan pulses. Rim scaffolds and debris chunks are instances on the finale top-level structure. The swarm is collector panels, foil, mirrors, and dust, culled and shaded per frame. Panels big on screen are satellites: a body and two cell-gridded wings on a boom, their edges glowing when the star is behind them; every panel has its own brightness, and only huge ones right at the camera fade out.

## Size

The shaders are one set of SPIR-V modules. All compute passes are specialisations of a single module (a mode constant and a workgroup size), so the helpers exist once. The camera path exists once, in the camera pass. `tools/spv_byval.py`, `tools/spv_sortdecl.py` (the most used constants first, so they get one-byte ids) and `tools/spv_renum.py` run after `spirv-opt`. `tools/spvpack.py` re-encodes the modules (varints, absolute or result-relative ids, remapped opcodes, constants as short decimals when one gives the exact float); `unspv()` in `src/main.c` decodes them at startup, and the build checks a bit-exact round trip.

The release compile is `-Oz` with the post-RA scheduler and peephole2 off, plus the extra packing switches listed in `build_linux.sh`. The entry is a two-line shell header plus a raw LZMA1 stream with the x86 filter (`lc=1`, `pb=0`). The encoder’s match finder, nice length, and depth are searched; the decoder does not need them.
