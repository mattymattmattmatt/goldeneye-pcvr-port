# Controls

## Why Goodhead

GoldenEye ships eight control styles. Four of them (2.1 Plenty, 2.2 Galore,
2.3 Domino, 2.4 Goodhead) read **two** N64 controllers, splitting aiming and
movement across two sticks. That is twin-stick, in 1997.

Tracing the routing in `src/game/bondview2.c` for the 2.x branch:

```
pad 0  stick X -> moveData.analogTurn      pad 1  stick X -> moveData.analogStrafe
pad 0  stick Y -> moveData.analogPitch     pad 1  stick Y -> moveData.analogWalk
pad 0  Z       -> insightaimmode           pad 1  Z       -> fire
```

2.4 Goodhead is the variant where pad 1 supplies strafe *and* walk, leaving
pad 0 as a pure aim stick. That is exactly the shape a VR port wants, so the
layer synthesises two pads and lets the engine's own code consume them. No
player movement code is modified.

`gevr_shim_get_pads()` forces the style to Goodhead every frame, so a stray
change in the options menu cannot quietly break the mapping mid-level. The shim
hands the pads over and `joy.c` writes them into its own ring buffer -- `struct
contdata` is private to that file and stays that way.

## Aim is not a stick

Pad 0 is described above as the aim stick, and for controller aim it is. For
head aim -- the default, and the R.E.A.L.-style behaviour -- it is not used for
look at all, because it cannot be.

The engine does not let a stick write the look angles. `MoveBond` integrates
*rates* into them:

```c
vv_theta = vv_theta + speedtheta * g_GlobalTimerDelta * 3.5f;
```

Anything arriving through a synthetic pad is therefore rate control, and can
only ever chase the headset: the output is proportional to the error, so a
standing error is what makes it move at all. Hold still and it converges; turn
your head and the view trails by an amount that grows with how fast you turned.
In a flat game that is mushy. In a headset it is the specific thing that makes
people ill.

So head aim writes `vv_theta` and `vv_verta` directly, from
`gevr_shim_head_aim()`, inside `MoveBond` and immediately *before*
`bondviewApplyVertaTheta()`. That ordering is load-bearing: that call derives
`vv_costheta`/`vv_sintheta`/`vv_cosverta`/`vv_sinverta`, and the view matrix,
gun direction, hitscan and radar all read the derived values rather than the
angles. Writing before it means the whole engine agrees with the headset within
the frame.

Yaw splits in two: the headset contributes an absolute offset over the range a
neck reaches, and the stick still turns the body underneath it, so you can face
behind you in a chair. The engine keeps movement, collision and hitscan.

## The mapping

| Input | Action | N64 |
|---|---|---|
| Left stick | Move — strafe and walk | pad 1 stick |
| Right stick | Turn — snap by default | (VR only) |
| Head | Look. Pitch and yaw both come from the headset | writes `vv_theta`/`vv_verta` |
| Right trigger | Fire | pad 1 Z |
| Right grip | ADS / aim mode | pad 0 Z |
| **B** | **Reload, and use doors / switches / objectives** | B |
| **A** | **Cycle weapon** | A |
| Menu button | Pause / watch menu | Start |
| Left grip | Crouch (engine-dependent, see below) | R |
| **Both sticks clicked** | Recentre | — |

Rebind any of these in `gevr.ini`. An action keeps its N64 pad and button
whatever you bind it to, because the pad split is what makes twin-stick work —
moving `fire` to a different button still sends pad 1's Z.

### A and B were the wrong way round until they were read out of the engine

Worth recording, because a hardware test did not catch it and could not have:
both buttons did something, they just did each other's job. `bondview2.c` is
unambiguous in both the one-pad and the Goodhead branches:

```c
moveData.weaponBackOffset    = (buttons & A_BUTTON) ...     /* cycle weapon */
moveData.weaponForwardOffset = ((buttons & ~oldbuttons) & A_BUTTON) ...
moveData.btap                = ((buttons & ~oldbuttons) & B_BUTTON) != 0;
```

and `btap` sets `field_D0`, which `lv.c` reads to call
`attempt_reload_item_in_hand()` and `bond_interact_object()`. So **B is one
button doing both reload and interact, and A steps the weapon.** GEVR's shipped
control sheet lands on the same pair, arrived at independently.

`vr/tests/test_controls.c` now asserts each face button reaches only its own
bit, so the pair cannot quietly swap again.

Buttons may sit on either pad: the Goodhead branch ORs A and B across both
(`bondview2.c` ~5026 and ~5090). Only the sticks and the two Z bits care which
pad they arrive on.

### Recentre is a chord

Both thumbsticks clicked together, not a face button. A single stick click is
very easy to brush while moving, and a stray recentre in the middle of a
firefight is worse than having no recentre at all. GEVR ships the same gesture
as its only recentre, which is some evidence it is the right one.

## Head control, and why there is a servo

The engine owns the camera angles (`vv_theta`, `vv_verta`) and uses them for
hitscan. If VR simply drew from the headset pose and left those angles alone,
you would look at a guard and shoot the wall.

So the layer runs a proportional controller. Each frame it takes the error
between where the headset is pointing and where the engine thinks the camera
is pointing, and emits an aim-stick deflection proportional to it. Because the
engine treats that stick as a turn *rate*, a P term on angle error is the
natural pairing: it converges without oscillating. Defaults settle a 30° snap
in about four frames at 90 Hz.

Two details this gets right, both covered by tests:

- **The view does not wait for the servo.** The camera is rendered from the
  live headset pose, not the engine's angles. A one-frame lag in the engine is
  invisible; a one-frame lag in the horizon is nauseating.
- **The error wraps.** Facing 175° with the engine at −175° is a 10° error, not
  350°. Without that, the player spins on their heel at the seam.

`invert_pitch` flips the emitted stick, not the angle the servo aims for.
Inverting the target instead makes the camera converge on the mirror image of
where you are looking and then run away to the pitch limit — this was a real
bug during development, and there is now a regression test for it.

## Aiming with the controller

With `aim_mode = controller`, the right hand aims the gun independently of
your view — but only in aim mode, because that is the only place the engine
supports it. Holding aim mode freezes natural turning and pans the crosshair
instead, driven by the aim stick past ±60. The layer steers that band from how
far your right hand points away from your view.

Outside aim mode the gun follows the view, which is how the game works flat.

## Deadzones, and the notch

The engine treats `|stick| <= 5` as dead and subtracts 5 from what is left, so
the usable range is 5..80, not 0..80. Everything the layer emits adds that
notch back on — otherwise the first 6% of every stick throw is silently eaten
and the controls feel numb around centre.

Stick deadzones are radial rather than per-axis. A per-axis deadzone carves a
square hole out of the stick and makes diagonal movement snag on the way in.

## Comfort

- **Snap turn** by default, 30°, with hysteresis so holding the stick over
  turns once rather than once per frame.
- **Vignette** that narrows the field of view while you move, scaled by speed.
  The falloff is deliberately soft; a hard edge reads as a black ring.
- **Recentre** on X, which rewrites the play-space origin. It keeps only yaw —
  carrying pitch or roll into the origin would tilt the horizon.
- **Physical crouch** works through head height, no button needed. The
  `crouch` binding is separate and marked engine-dependent because GoldenEye's
  own ducking is tied to aim mode rather than a dedicated button; treat that
  binding as experimental.

## Tuning

Run `ge007vr-calibrate` and edit `gevr.ini`, or pass `--set=key=value` to try a
value without editing anything:

```sh
./ge007vr-calibrate --set=snap_degrees=45 --set=turn_mode=smooth
```

Start with `snap_degrees`, `move_deadzone` and `servo_yaw_gain`. If the view
feels like it drags behind your head, raise the servo gain; if it wobbles when
you stop moving, lower it.
