# GoldenEye 007 — VR layer

An OpenXR VR layer for this GoldenEye 007 decompilation, aimed at SteamVR on
the desktop (which is how a Quest 3 connects through Virtual Desktop or Link).

The controls are built on the game's own **2.4 Goodhead** control style — the
stock dual-controller layout where one stick aims and the other moves. Feeding
it two synthetic N64 pads gives true twin-stick without rewriting any of the
player movement code.

## Status, honestly

The VR layer is written, builds warning-clean, and is covered by 370 headless
assertions. **The game is not yet playable in VR**, because this repository
builds an N64 ROM and has no PC target — nothing in it draws a pixel on a
desktop GPU.

But the remaining path is now well understood, because somebody has already
walked it for the sister game.
[Alex-LeTux/perfect_dark_vr](https://github.com/Alex-LeTux/perfect_dark_vr) is
a working VR port of Perfect Dark, and it got there in three layers: the
decomp, a native PC port (`port/`), then an OpenXR fork (`port/vr/`).

GoldenEye has layer 1. This repository is layer 3. Layer 2 is the gap.

| Piece | State |
|---|---|
| OpenXR session, swapchains, stereo frame loop | **done** |
| Controller bindings (Touch, Index, Vive, WMR, simple) | **done** |
| Goodhead twin-stick mapping + head servo | **done, tested** |
| Stereo camera, off-axis projection, roomscale | **done, tested** |
| Comfort: snap turn, vignette, recentre, haptics | **done** |
| Game-side hooks, inert without `GE_VR` | **done** |
| Calibration harness you can run in the headset | **done** |
| PC platform layer (libultra shim, scheduler, ROM assets) | **not started — scoped** |
| Graphics backend (Fast3D over the display lists) | **not started — scoped** |
| Audio backend | **not started** |

"Scoped" rather than "unknown": Perfect Dark's entire libultra shim is 487
lines and already covers 38 of the 88 libultra functions GoldenEye calls, and
its Fast3D renderer is ~7.5k lines that GoldenEye's stock-GBI display lists
should feed directly. [Architecture.md](Architecture.md) has the measured
breakdown and the work plan.

One correction worth flagging, since an earlier version of these docs said
otherwise: GoldenEye's custom RSP microcode is **not** a blocker. A Fast3D port
intercepts the display list and never runs the microcode at all — Perfect Dark
has custom microcode too, and its port simply bypasses it.

## What you can run today

`ge007vr-calibrate` is a real OpenXR application. It brings up a session, binds
the real action set, runs the real control mapper, and drives a stand-in for
the engine's movement integrator. You put the headset on and walk around a
one-metre grid using exactly the scheme the game will use.

That makes it useful for three things before the renderer exists:

- confirming SteamVR sees your headset and both Touch controllers
- feeling the twin-stick mapping and tuning snap angle, deadzones and servo gain
- measuring `world_scale` — walk a known number of grid squares and compare

```sh
cmake -S vr -B build/vr -DCMAKE_BUILD_TYPE=Release
cmake --build build/vr -j
cp vr/config/gevr.ini build/vr/
./build/vr/ge007vr-calibrate
```

Tests, which need neither a headset nor a GPU:

```sh
ctest --test-dir build/vr --output-on-failure
```

## Assets

Same rule as the rest of the repository: it does not contain the game's assets,
and you need your own copy of GoldenEye 007 to extract them. Nothing in the VR
layer changes that.

## The ROM build is unaffected

Every hook in `src/` sits inside `#ifdef GE_VR`, which the ROM build never
defines. `src/joy.c` in particular carries code commented "required for
matching", so this is enforced rather than assumed: `vr/shim/gevr_shim.c`
preprocesses to zero non-blank lines without `GE_VR`, and the CMake target
`gevr_shim_inert` exists to keep it that way.

## Documentation

- [Controls.md](Controls.md) — the control scheme, and why it is built this way
- [QuestSetup.md](QuestSetup.md) — Quest 3, Virtual Desktop and SteamVR setup
- [Architecture.md](Architecture.md) — how the layer works, and what is left
