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

That makes both stereo modes cheap to express at the same seam:

```c
videoStartFrame();
for (eye = first; eye != done; eye = next(eye)) {
    gevrBeginEye(eye);          /* bind that eye's swapchain image */
    gevrPatchProjection(eye);   /* rewrite g_viProjectionMatrix in place */
    gfx_run((Gfx *)t->t.data_ptr);
    gevrEndEye(eye);
}
videoEndFrame();
```

Both eyes per frame gives full stereo. One eye per frame, alternating, is
alternate-eye rendering: half the render cost, each eye's image left standing
in the compositor until its turn comes round again. The loop is the same code;
only what `next()` returns differs, so both can be a config switch rather than
two implementations.

**Unverified assumption.** This rests on `gfx_run` being replayable — that it
walks the list without consuming or mutating it, and that whatever state it
carries (segment table, matrix stack, combiner) either resets per run or does
not leak between two runs inside one `gfx_start_frame`/`gfx_end_frame` pair.
That has not been tested. It is the first thing to check, and if it does not
hold, alternate-eye rendering still works, because that runs the list exactly
once per frame as the host already does.

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
