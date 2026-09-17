# Architecture

## Layout

```
vr/
  include/          public headers
    gevr_math.h       vectors, quaternions, matrices, off-axis projection
    gevr_input.h      VR input state and the synthetic N64 pads
    gevr_config.h     tunables, loaded from gevr.ini
    gevr_controls.h   the Goodhead mapper
    gevr_camera.h     stereo view and projection
    gevr_engine.h     GoldenEye angle conventions <-> VR conventions
    gevr_xr.h         OpenXR session
    gevr_gl.h         framebuffers, vignette, mirror
  src/              implementations
  shim/             the seam into the engine; empty without GE_VR
  tools/            ge007vr-calibrate
  tests/            headless assertions, no GPU or headset needed
  config/gevr.ini   documented defaults
```

`gevr_core` (math, controls, camera, config, engine) has no dependency on
OpenXR, SDL or GL. That is deliberate: it is the part with a right answer, so
it is built and tested on its own.

## Division of labour

The rule the whole design follows:

> **The engine owns position. The VR layer owns orientation.**

Movement, collision, stairs, lifts and hitscan stay exactly as Rare wrote them,
driven by the synthetic move pad. The camera orientation comes from the live
headset pose, with a servo keeping the engine's own angles in sync so gameplay
agrees with what you see. `Controls.md` covers why.

## Frame flow

```
gevr_shim_frame_begin()      xrWaitFrame, xrBeginFrame, xrSyncActions,
                             xrLocateViews -> gevr_input_state
gevr_shim_inject_pads()      gevr_controls_update -> two N64 pads,
                             written into g_ContDataPtr->samples[curlast]
  ... the engine ticks unchanged ...
for each eye:
  gevr_shim_begin_eye(e)     acquire swapchain image, build the eye's
                             view/projection, bind the framebuffer
  ... the engine renders ...
  gevr_shim_end_eye(e)       vignette, release image
gevr_shim_frame_end()        xrEndFrame with the projection layer
```

## Hook points in the engine

Two, both inside `#ifdef GE_VR`:

- `src/joy.c`, end of `joyConsumeSamplesWrapper` — inject the pads after the
  real controllers are consumed and before any game code reads them.
- `src/fr.c`, after `guPerspectiveF` — replace the main view's projection with
  the headset's asymmetric frustum for the eye being rendered.

The second is not an adjustment, it is a replacement. `guPerspective` builds a
*symmetric* frustum, and a real HMD's is off-centre — approximating it with a
symmetric matrix shears the world toward the nose. `gevr_projection_from_fov`
takes the runtime's four half-angles directly.

One subtlety worth naming: libultra's float matrices are row-vector convention
(`v * M`), the VR layer's are OpenGL column-vector (`M * v`). They are
transposes. `gevr_shim_eye_projection_n64` does the transpose explicitly rather
than letting a `memcpy` produce a scrambled frustum.

## Keeping the ROM build safe

`vr/shim/gevr_shim.c` preprocesses to zero non-blank lines without `GE_VR`, and
`gevr_shim.h` supplies no-op macros so call sites compile away. The CMake target
`gevr_shim_inert` builds it precisely to prove that stays true. This matters
because `src/joy.c` contains code commented "required for matching" — a stray
statement in the wrong place changes the ROM.

## What is left

Updated after studying [Alex-LeTux/perfect_dark_vr](https://github.com/Alex-LeTux/perfect_dark_vr),
a working VR port of Perfect Dark. It is the proof that this whole approach
works, and it corrects an assumption made earlier in this document.

### The proven recipe

Perfect Dark got to VR in three layers:

1. **The decomp** — `n64decomp` Perfect Dark, 100% complete.
2. **A native PC port** — a `port/` directory holding the platform layer
   (`port/src/libultra.c`, `pdsched.c`, `audio.c`, `video.c`, `romdata.c`) and
   a Fast3D renderer (`port/fast3d/`, ~7.5k lines) that turns N64 display
   lists into GPU draw calls. Assets are read at runtime from the player's own
   ROM.
3. **A VR fork** — `port/vr/`, ~5.5k lines of OpenXR, targeting both PCVR and
   Quest standalone. MIT licensed, as is the port it builds on.

GoldenEye already has layer 1, and this repository now has layer 3 (`vr/`,
~6.2k lines). **Layer 2 is the entire gap.**

### Correction: the microcode is not a blocker

An earlier revision of this document said GoldenEye's custom RSP microcode
meant "an off-the-shelf translator will not drop straight in". That conclusion
was wrong, and the Perfect Dark port is the counter-example.

Perfect Dark has custom microcode too — `src/rsp/gsp.s`, `asp.s`, `rspboot.s`,
the same situation as this repository's `rsp/graphics/gmain.s`. Its PC port
does not run any of it. `port/src/video.c` intercepts the finished display
list and hands it to `gfx_run()`; the RSP is bypassed entirely rather than
emulated. What matters is the *command format* of the display list, not the
microcode that would have executed it — and GoldenEye builds its lists with
the stock GBI macros (`gSPDisplayList`, `gSPVertex` in `src/game/model.c`),
which is exactly what Fast3D already consumes.

So the renderer is a porting job, not a research problem.

### Measured size of the gap

GoldenEye calls **88** distinct libultra functions. Perfect Dark's PC shim is
**487 lines** and already covers **38** of them by name. Of the 50 remaining:

- **12** are boot, TLB, logging or dev-hardware entry points
  (`osInitialize`, `osMapTLBRdb`, `osLeoDiskInit`, `osReadHost`) that a PC
  build stubs rather than implements.
- A further group — `osSpTaskLoad`, `osSpTaskStartGo`, `osDpSetNextBuffer`,
  `osViGetCurrentFramebuffer` — are precisely the ones a Fast3D port
  *replaces* wholesale, because the display list is intercepted before the
  RSP/RDP would ever see it. Perfect Dark does not implement them either.
- What genuinely needs writing is the scheduler (`osCreateScheduler`,
  `osSc*`), PI DMA for ROM reads (`osPiRaw*`, `osEPiRaw*`), timers, and the
  controller-pak and rumble paths (`osPfs*`, `osMotor*`).

That the whole shim fits in 487 lines for Perfect Dark is the useful signal:
the port replaces subsystems rather than emulating hardware.

### The renderer gap, measured

The compatibility question has now been answered by measurement rather than
assumption, and the answer is good.

GoldenEye builds the **F3DEX (GBI 1)** branch of `PR/gbi.h` — no `F3DEX_GBI_2`
define exists anywhere in the build.

Rare's custom **`G_TRI4`** packed four-triangle command, added in
`include/gbi_extension.h` and used to redefine `gSP2Triangles`, sits at the
*same opcode Perfect Dark uses*, and Perfect Dark's Fast3D already decodes it.
This was the main risk: a stock decoder knowing only `G_TRI1`/`G_TRI2` would
render almost no geometry at all.

Comparing the 66 distinct GBI macros GoldenEye emits against the opcodes that
renderer handles leaves exactly **three** unhandled: `G_MODIFYVTX` (whose only
use in the source is commented out), and `G_SETBLENDCOLOR` and
`G_SETPRIMDEPTH`, which occur as one adjacent pair in `src/boss.c`.

`port/src/gbi_walk.c` decodes and validates all of this with 61 assertions.
`port/README.md` has the full breakdown, including the `G_SETTEX`/`G_NOOP`
opcode collision to watch for.

### Work plan for layer 2

1. ~~`port/src/libultra.c` — threads, message queues and timers over real OS
   primitives; stub the boot and TLB entry points.~~ **Done**, 236 assertions.
2. ~~`port/src/romdata.c` — load assets from the player's own GoldenEye ROM at
   runtime.~~ **Done**, 53 assertions. PI DMA now reads from the player's ROM,
   so the game's existing asset pipeline works unchanged. No assets are
   redistributed.
3. `port/fast3d/` — bring in Fast3D and drive it from a `video.c` that
   intercepts the display list where `src/fr.c` builds it. The decoding half
   is done (`gbi_walk.c`, 61 assertions); what remains is the GPU backend.
4. `port/src/audio.c` — the sequence and sample playback in
   `src/libultra/audio` over SDL audio. Independent of VR and can come last.
5. Move `vr/` to `port/vr/` once the port layer exists, matching the layout
   that Perfect Dark's VR fork already proves out.

Both Perfect Dark's port and its VR fork are MIT licensed, so adapting the
platform layer is permitted with attribution. The engine-specific parts
(`romdata`, asset formats, the `fr.c` interception point) are GoldenEye's own
and have to be written against this codebase.

### Still worth doing once it runs

- **HUD.** The 2D HUD draws in screen space, which is painful in a headset.
  `hud_distance` and `hud_scale` exist for projecting it onto a floating panel.
- **World scale.** `world_scale = 100` is an estimate from `eyeheight`
  magnitudes. Measure it with the calibrate tool and correct the default.
- **Pitch sign.** `GEVR_ENGINE_PITCH_SIGN` in `gevr_engine.c` encodes which way
  the engine counts pitch. Confirm against real hardware.
- **Quest standalone.** Perfect Dark's VR fork runs on-headset as well as over
  PCVR. Nothing in `vr/` prevents it, but it needs an Android build and a GLES
  path in the renderer.

## Testing

`vr/tests/test_controls.c` — 370 assertions, no GPU or headset:

axis encoding and the deadzone notch, radial stick shaping, angle wrapping at
the ±180° seam, quaternion yaw including gimbal lock, off-axis projection
against the symmetric case, Goodhead pad routing, snap-turn latching, servo
convergence and settling (including across the seam), `invert_pitch` polarity,
stereo eye separation and IPD scaling, view-matrix inversion, roomscale and
crouch offsets, config clamping, and the engine angle round-trips.

The servo tests model the engine's turn integrator rather than calling it, so
they run headless. The model is simple, but it is enough to catch divergence,
overshoot and sign errors — all three of which it did catch during development.
