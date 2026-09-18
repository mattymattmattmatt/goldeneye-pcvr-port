# Quest 3 over Virtual Desktop

> **Status.** Brought up on a Quest 3 over Virtual Desktop, on Windows. The
> runtime binds, both eyes render in stereo, and the yaw/pitch conventions and
> `world_scale` have been checked against a live headset. Treat the rest as
> working-but-young rather than finished.

The layer is a normal PC OpenXR application. It does not know or care that the
headset is a Quest — it talks to whatever OpenXR runtime is active.

## Chain

Virtual Desktop ships its own OpenXR runtime, VirtualDesktopXR, and the game
binds to it directly:

```
Quest 3  --wifi-->  Virtual Desktop Streamer  -->  VirtualDesktopXR  -->  ge007vr
```

SteamVR also works, and adds a hop:

```
Quest 3  --wifi-->  Virtual Desktop Streamer  -->  SteamVR  -->  ge007vr
```

Prefer VirtualDesktopXR. It is one less process between the headset and the
game, and it is what the bring-up ran on.

## Setup

1. **Virtual Desktop Streamer** on the PC, Virtual Desktop on the headset.
   Connect, and confirm you can see the desktop.
2. In the Streamer's settings, set **OpenXR Runtime** to VirtualDesktopXR.
   (If you would rather go through SteamVR: start SteamVR and use
   Settings → OpenXR → *Set SteamVR as OpenXR Runtime* instead. Only one
   runtime can be active at a time, so pick one.)
3. In Virtual Desktop on the headset, set **VR mode** rather than desktop mode.
4. Run `ge007vr-calibrate` (built alongside the game when VR is enabled). The banner reports the runtime and system it found:

```
runtime        : VirtualDesktopXR
system         : Meta Quest 3
per-eye target : 2688 x 2880  (render_scale 1.00)
```

If the runtime line names something you did not expect, step 2 did not take.

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
| runtime does not expose `XR_KHR_opengl_enable` | No OpenXR runtime is active, or the active one has no OpenGL support |
| `xrCreateInstance` failed with `XrResult(-4)` | The runtime is older than the API version asked for. The layer requests 1.0, which every runtime supports; if this appears, something is pinning it higher |
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

- **Stereo feels flat or overdone.** `ipd_scale`, 1.0 being true separation.
  Below 1 pulls the eyes together and makes the world read as larger, which
  some people prefer at N64 art scale.
- **Leaning moves the view too much, too little, or not at all.**
  `room_scale` (1.0 = your real lean, 0 = no positional tracking at all, i.e.
  3DoF) and `room_limit` (metres the view may travel from where you started
  before it stops following). The default 0.6 m covers leaning out of cover and
  ducking from a chair without letting someone who stands up and walks away
  carry the camera through a wall.
- **The whole picture is upside down.** Set `flip_eyes_y=1` in `gevr.ini`. No
  rebuild needed. OpenGL's framebuffer origin is bottom-left and most PC
  runtimes composite in Direct3D, whose origin is top-left; a runtime with
  native OpenGL support is meant to account for that when it copies the image
  across, and there is no way to ask it whether it does. VDXR is untested here
  either way, so this ships off and is one line to turn on. It costs one blit
  per eye. If only the *mirror window* on the desktop is upside down and the
  headset is fine, that is the opposite case -- report it rather than changing
  this, because the two are wired to the same key on purpose.
- **The view turns the wrong way, or is mirrored left-to-right.** Checked on
  hardware and believed right, but `vr/src/gevr_engine.c` owns both conventions
  if it is not: `GEVR_ENGINE_PITCH_SIGN` and the negation inside
  `gevr_vr_yaw_to_engine`. Flipping one of those is the whole fix.
- **The world is the wrong size** -- everything feels like a doll's house, or
  like you are six inches tall. `world_scale` in `gevr.ini` is GoldenEye units
  per real metre. 100 measured about right on a Quest 3; it is a default, not a
  law.
- **Head tracking lags.** The OpenXR frame runs on the render thread, and the
  game thread re-predicts the head pose for the frame it is building
  (`gevr_xr_relocate_head`). If tracking trails your head, that prediction is
  the thing to question -- not the frame placement, which is fixed by the
  threading and cannot move.
