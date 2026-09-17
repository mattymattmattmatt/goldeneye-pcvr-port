# Integration notes for this host

Written after reading the host's own code, replacing the guesses in
`README.md` that were made before it was available.

## The frame loop is three lines

The whole of stereo hangs on one place — `osSpTaskStartGo()` in
`port/src/libultra.c`, the graphics-task branch:

```c
videoStartFrame();
gfx_run((Gfx *)t->t.data_ptr);
videoEndFrame();
```

`gfx_run` takes the *finished* display list. Nothing else in the host submits
geometry. That is a much better position than the plan assumed.

## The game does not have to render twice

The earlier plan assumed the engine's render path would need to run once per
eye, which would have meant making it re-entrant. It does not, because of how
the projection reaches the renderer: `fr.c` emits

```c
gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(g_viProjectionMatrix), ... G_MTX_PROJECTION);
```

The display list references the matrix *by address*. So the contents of
`g_viProjectionMatrix` can be rewritten between two `gfx_run` calls over the
same list, and the second pass draws the same geometry through a different
frustum. One list, built once by the game, executed twice.

**Correction to the first reading of this.** `gfx_run` is not a list executor,
it is a whole-frame function: it calls `gfx_wapi->start_frame()` and
`gfx_rapi->start_frame()`, **clears the framebuffer**, walks the list, appends
the options overlay, flushes, and runs the present/resolve path. Calling it
twice in a frame would have the second call clear what the first drew. So the
eye loop cannot sit around `gfx_run`; it has to go *inside* it, around the
inner `gfx_run_dl(commands)` — either by modifying `gfx_run` or by adding a
`gfx_run_stereo` beside it. The fork can do either.

The shape is then:

```c
gfx_rapi->start_frame();
for (eye = first; eye != done; eye = next(eye)) {
    gfx_sp_reset();
    gfx_rapi->start_draw_to_framebuffer(eyeFb[eye], scale);
    gfx_rapi->clear_framebuffer(true, false);
    gevrPatchProjection(eye);   /* rewrite g_viProjectionMatrix in place */
    gfx_run_dl(commands);       /* same list, once per eye */
    gfx_flush();
}
```

Both eyes per frame gives full stereo. One eye per frame, alternating, is
alternate-eye rendering: half the render cost, each eye's image left standing
in the compositor until its turn comes round again. Only `next()` differs, so
both can be a config switch rather than two implementations.

## The replay assumption was tested, and holds

The plan rested on `gfx_run_dl` being replayable — walking the list without
consuming it, and not being disturbed by state the previous walk left behind.
Reading the source got close: the walker never writes to the list, but
`gfx_sp_reset()` resets only three fields (matrix stack depth, light count,
lights-changed), so a second walk inherits everything else.

`port/tests/test_dl_replay.cpp` settles it by running it. A recording backend
captures every triangle the interpreter emits, the same list is walked twice,
and the recordings are compared bit for bit — both across separate frames and,
the case that actually matters, twice inside one frame. Both come back
identical. Run it with `port/tests/run_dl_replay.sh`; it needs no ROM and no
GPU.

**True stereo by re-walking the game's own display list is viable.** The engine
does not need to render twice.

What that does *not* cover: the probe uses a stub backend, so it says nothing
about the real GL backend's per-frame state, nor about the framebuffer clear
described above. Those are answered by running the real thing.

Three of the probe's own bugs are worth recording, because each produced a
confident wrong answer first:

- Comparing floats with `!=` reported a difference between bit-identical
  buffers, because `NaN != NaN`. It now uses `memcmp`.
- The NaNs came from the probe's list having no projection and no viewport,
  and from packing the fixed-point `Mtx` wrongly — fast3d puts two matrix
  elements in each `int32`, the even one in the high half. The probe now
  writes the exact inverse of `gfx_sp_matrix`'s unpacking.
- The last NaN came from calling `gfx_run` without `gfx_start_frame()`, which
  leaves `gfx_current_dimensions.aspect_ratio` at zero;
  `gfx_adjust_x_for_aspect_ratio` divides X by it. The host always brackets
  the run, and now so does the probe.

The probe refuses to report a result when any emitted float is NaN, or when
nothing was emitted, rather than comparing noise against noise.

## The host already hooks the projection, and knows what else depends on it

`fr.c` here is not the pristine decomp. Under `#ifdef PORT` it already
intercepts `guPerspectiveF` at the same call site the VR projection hook
targets, to widen the FOV — and, importantly, its D222 note records that
`currentPlayerSetPerspective` feeds `c_perspfovy`, from which
`currentPlayerSetCameraScale()` derives the frustum-cull plane normals
(`c_cameratopnorm`, `c_cameraleftnorm`) and the fog/LOD distance scale.

That matters for stereo and was not on the plan. Replacing the projection
matrix alone would leave the cull planes calibrated to the *nominal* symmetric
frustum, so geometry visible at the outer edge of one eye's asymmetric frustum
could be culled before it is drawn. The fix is to keep the cull planes matched
to the union of both eyes' frusta rather than to either one. The host has
already built the plumbing for exactly this kind of adjustment; the VR path
should go through it rather than around it.

## Patch status against this tree

| File | State | Action |
|---|---|---|
| `src/joy.c` | byte-identical to the decomp base | applied |
| `src/fr.c` | host-modified under `#ifdef PORT` | re-target by hand |
| `src/game/bondview2.c` | host-modified under `#ifdef PORT` | re-target by hand |

The head-aim hook's anchor survives in this tree: the rate integration is at
`bondview2.c:6837` and `bondviewApplyVertaTheta()` at `6851`, so the hook still
goes immediately before that call, just at a different line number than in the
original patch.

`vr/` itself needed no changes: it configures, builds and passes its 2 test
suites inside this tree exactly as it did standalone.
