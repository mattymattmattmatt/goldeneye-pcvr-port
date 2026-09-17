# Quest 3 over Virtual Desktop

> **Status.** The VR layer has been built and run on Linux/x86-64 only, and
> without a headset -- it comes up, finds no runtime, reports why and runs the
> game flat. Nothing below the "Setup" heading has been executed against real
> hardware. Treat the first run as a bring-up, not an install.

The layer is a normal PC OpenXR application. It does not know or care that the
headset is a Quest — it talks to whatever OpenXR runtime is active, and Virtual
Desktop presents the Quest to the PC as a SteamVR headset.

## Chain

```
Quest 3  --wifi-->  Virtual Desktop Streamer  -->  SteamVR  -->  OpenXR  -->  ge007vr
```

## Setup

1. **Virtual Desktop Streamer** on the PC, Virtual Desktop on the headset.
   Connect, and confirm you can see the desktop.
2. **SteamVR** running, and set as the active OpenXR runtime — SteamVR
   Settings → OpenXR → *Set SteamVR as OpenXR Runtime*. This is the step that
   is most often missed: if the Oculus runtime is active instead, the app will
   start but may not see Virtual Desktop's headset.
3. In Virtual Desktop on the headset, set **VR mode** rather than desktop mode.
4. Run `ge007vr-calibrate` (built alongside the game when VR is enabled). The banner reports the runtime and system it found:

```
runtime        : SteamVR/OpenXR
system         : Quest 3
per-eye target : 2064 x 2208  (render_scale 1.00)
```

If the runtime line does not say SteamVR, step 2 did not take.

## Controls check

The calibrate tool prints a live line while running. `L[ok] R[ok]` means both
controllers are tracking. Push each stick and watch `pad0` and `pad1` respond:

- left stick moves `pad1`
- right stick and your head move `pad0`

If a stick does nothing, check which interaction profile bound — the layer
suggests bindings for Touch, Index, Vive wands, WMR and the Khronos simple
controller, and SteamVR only advertises what is connected.

## Performance

Wireless streaming adds latency on top of render time, so headroom matters
more than it does wired.

- `render_scale` below 1.0 is the first lever. 0.8 is usually invisible at
  N64 art scale and buys a lot.
- Set Virtual Desktop's own bitrate and refresh to something your network
  actually sustains. 90 Hz on a shaky 5 GHz link is worse than a solid 72 Hz.
- Prefer a wired 5 GHz or 6 GHz access point to the PC. Streaming over the same
  radio the PC is using is the usual cause of stutter that looks like a
  framerate problem but is not.

## Comfort

Start with the defaults — snap turn at 30° and the vignette on. If snap turning
feels restrictive once you have your VR legs, `turn_mode = smooth` with
`smooth_turn_dps` around 90–120 is the usual next step. Turn the vignette down
before you turn it off.

Facility's lifts and Surface's stairs are the places where locomotion in a
1997 engine will feel least like a modern VR title. That is the engine, not the
mapping.

## When it does not start

The tool prints the actual OpenXR failure rather than a generic message. The
common ones:

| Message | Cause |
|---|---|
| runtime does not expose `XR_KHR_opengl_enable` | SteamVR is not the active OpenXR runtime, or nothing is running |
| `xrGetSystem` failed | No headset connected; Virtual Desktop is not streaming |
| needs an X11 window for the GLX binding | Linux on Wayland — run with `SDL_VIDEODRIVER=x11` |


## Building with VR

VR is off by default; the game builds and runs flat without it.

```sh
cmake -S . -B build-pc -DCMAKE_BUILD_TYPE=Release -DGE_VR=ON
cmake --build build-pc -j
```

Configure prints which way it went:

```
-- VR: enabled (OpenXR runtime found)
```

If it says the runtime was not found, the build carries on flat -- the OpenXR
*SDK* is a build-time dependency (MSYS2: `mingw-w64-x86_64-openxr-sdk`,
Debian/Ubuntu: `libopenxr-dev`), separate from the OpenXR *runtime* SteamVR
provides at run time.

## If it starts flat when you expected VR

The shim says why on startup, and the message is the first thing to read. The
common one is:

```
[gevr] VR unavailable: the OpenXR runtime does not expose XR_KHR_opengl_enable.
       With SteamVR, make sure it is set as the active OpenXR runtime.
```

That is step 2 above not having taken, or the Oculus runtime being active
instead of SteamVR.

## Things most likely to be wrong on the first run

These are known-unverified rather than known-broken, and each is a small fix in
one place:

- **The view turns the wrong way, or is upside down.** The engine's yaw and
  pitch sign conventions were inferred from the decompiled source, not measured
  against a running game. `vr/src/gevr_engine.c` owns both:
  `GEVR_ENGINE_PITCH_SIGN` and the negation inside `gevr_vr_yaw_to_engine`.
  Flipping one of those is the whole fix.
- **The world is the wrong size** -- everything feels like a doll's house, or
  like you are six inches tall. `world_scale` in `gevr.ini` is GoldenEye units
  per real metre; ~100 is a starting point, not a measured value.
- **Stereo feels flat or overdone.** `ipd_scale`, 1.0 being true separation.
- **Head tracking lags.** The XR frame is opened by whichever part of the game
  reaches the VR layer first in a frame, so that poses are fresh when the game
  reads them rather than when the renderer does. If tracking still trails your
  head, that placement is the thing to question.
