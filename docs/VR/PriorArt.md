# Prior art

What this layer owes to other people's work, and what it deliberately does not
take.

## GEVR — https://github.com/no6969el/GEVR

A shipping OpenXR VR build of GoldenEye 007, MIT, by no6969el. It got to a
playable public Beta (vr441) well before this branch was playable at all, on a
different host: the GETV / goldeneye-native port by Evan King, with D3D12
earlier in its history, where this branch sits on jkdansereau's PC port with
fast3d over OpenGL.

**No GEVR code is in this tree.** Their playable VR work ships in the release
zip rather than the public repository, so there was nothing to copy even had
that been wanted — what the repository publishes is an engineering log of
around 400 documents, and that is the part worth reading.

Taken from it, as designs rather than code:

- **The recentre chord.** Both thumbsticks together, never a face button.
- **The control sheet** — ADS on the squeeze, the menu button for pause — which
  independently agreed with what `bondview2.c` says A and B do, and so caught a
  swap in this layer that a hardware test had not.
- **`docs/211`, the aim solver.** The crosshair belongs where the bullet goes,
  not where the eye looks: the muzzle sits measurably below the camera, two
  parallel rays from origins that far apart never meet, and the honest fix is a
  chosen zero distance like a rifle's rather than a live trace. Not implemented
  here yet; recorded because it is the correct shape of the answer and this
  layer will need it the moment the gun leaves the view centre.
- **The confirmation that `world_scale` ≈ 100 game units per metre** is the
  right starting point, from their `-UnitsPerMetre` default.

Also worth recording: their known-issues list includes *"glass bullet holes can
show in one eye in places."* This branch has the same class of fault, which is
useful evidence that per-eye divergence when replaying one N64 display list
twice is a property of the approach rather than a mistake in either
implementation.

## jkdansereau's PC port

The host this VR layer is built on. Everything outside `vr/` and the marked
hooks is theirs.

## n64decomp/007

The decompilation that makes a from-source port possible at all.

## Luke Ross's R.E.A.L. mods

Not code — the shape of the thing. True stereo with the engine's own camera
kept authoritative, rather than a flat image on a virtual screen.
