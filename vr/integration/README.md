# Grafting the VR layer onto another GoldenEye port

This directory exists because the VR layer was always meant to sit on top of
*a* PC port, not specifically the one in `port/`. Native ports of the
`n64decomp/007` decompilation now exist that are further along than `port/`
ever got — in particular they have actually been run, which `port/` has not.
Moving `vr/` onto one of them is a better trade than finishing `port/`.

What follows is the whole integration surface. It is small on purpose.

## What moves, and what it costs

| Piece | Lines | Coupling | Work to move |
|---|---|---|---|
| `vr/src/`, `vr/include/` | ~1,700 | none to the game; SDL2 + OpenXR only | drop in unchanged |
| `vr/shim/` | ~456 | the only VR code that includes game headers | unchanged if the host uses the same decomp |
| `src/joy.c`, `src/fr.c`, `src/game/bondview2.c` hooks | 66 | 6 sites, all `#ifdef GE_VR` | `git apply` the patch here |
| The host's frame loop | — | must render twice per frame | **the real work** |

Verified before writing this: `cmake -S vr -B build/vr` configures, builds and
passes its 370 assertions with no reference to `port/` anywhere. The core is
detachable in fact, not just in intent.

## The game-side patch

`0001-ge-vr-game-hooks.patch` is 66 lines of pure insertion against the
pristine decomp — no deletions, no moved code — and is verified to apply
cleanly to a checkout of the decomp's base commit. Every hunk is inside
`#ifdef GE_VR`, so a host that does not define it sees unchanged call sites.
That matters beyond tidiness: `joy.c` contains code commented "required for
matching", so the ROM build must see the file untouched.

Three hooks:

**`src/joy.c`** — after the real controllers are consumed and before any game
code reads them, pads 0 and 1 are overwritten with the VR-derived state. The
sample written is `samples[curlast]`, because that is the one `joyGetStickX`
and its neighbours read; anything written elsewhere in the ring is ignored.
The shim hands over pads and the caller stores them — `struct contdata` is
private to `joy.c` and should stay that way.

**`src/fr.c`** — after `guPerspectiveF`, the symmetric frustum is replaced with
the headset's asymmetric one for the eye being rendered. `guPerspective` cannot
express an off-centre frustum, and approximating one with a symmetric matrix
shears the world toward the nose. `g_viPerspNorm` is deliberately left alone:
it drives the RSP's perspective correction, which a PC renderer backend does
not use.

**`src/game/bondview2.c`** — absolute head aim, inside `MoveBond`, immediately
before `bondviewApplyVertaTheta()`. The engine's analog stick writes *rates*
(`speedtheta`/`speedverta`) which `MoveBond` integrates into the angles, so
anything reaching the engine through a synthetic stick is rate control and can
only ever chase the headset. This overwrites the integrated angles with
absolute ones. The position in the function is load-bearing:
`bondviewApplyVertaTheta()` derives `vv_costheta`/`vv_sintheta`/`vv_cosverta`/
`vv_sinverta`, and the view matrix, gun direction, hitscan and radar all read
those derived values — so writing before it makes the whole engine agree with
the headset in the same frame, and writing after it leaves every one of them a
frame stale.

A Fast3D-based host takes the projection matrix out of the display list the
same way the RSP would, so the `fr.c` hook should behave identically there.

## The part that is actually work

The frame loop. One VR frame is:

```
gevr_shim_frame_begin()      once, before the game ticks
gevr_shim_get_pads()         after joyConsumeSamplesWrapper()
... the game ticks as normal ...
for each eye:
    gevr_shim_begin_eye(e)
    ... the game renders its display list ...
    gevr_shim_end_eye(e)
gevr_shim_frame_end()
```

A flat port has no reason to render the scene twice per frame, so its frame
loop will need opening up: the tick and the render have to be separable, and
the render has to be callable once per eye with the swapchain image for that
eye bound. This is the one piece that cannot be prepared in advance, because
it depends entirely on how the host has structured its main loop.

Everything else on this page is a copy or a `git apply`.

## Order of work

1. Fork the host port, get it building and running flat, with your own ROM.
2. Copy `vr/` in wholesale and build it standalone — it has no dependency on
   the host, so this should succeed before any integration at all.
3. `git apply` the game-side patch. Still build flat: without `GE_VR` defined
   nothing changes, which is the point.
4. Open up the host's frame loop for the eye pass. This is the milestone.
5. Define `GE_VR`, link `gevr_runtime`, and bring up the headset.

Do not start at step 5.

## What the engine keeps

The VR layer supplies input and per-eye view and projection. Movement,
collision and hitscan stay with the engine. The control mapping is the game's
own 2.4 Goodhead dual-pad style — pad 0 aims, pad 1 moves — which is why the
shim forces that control style when it takes over: the whole mapping depends
on that routing.
