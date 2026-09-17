/*
 * gevr_engine.h - conversions between GoldenEye's angle conventions and the
 * VR layer's.
 *
 * The engine stores camera angles as f32 DEGREES wrapped into [0, 360):
 *   g_CurrentPlayer->vv_theta  - yaw
 *   g_CurrentPlayer->vv_verta  - pitch
 *
 * The yaw sign is the opposite of the VR layer's. bondview2.c builds the view
 * rotation as a rotation about Y of (360 - vv_theta) degrees, which means
 * vv_theta grows clockwise seen from above, i.e. turning RIGHT. The VR layer
 * follows the usual right-handed Y-up convention where positive yaw turns
 * LEFT, so the two are negatives of one another.
 *
 * Keeping this in its own translation unit means the sign conventions are
 * covered by the headless tests rather than only discovered in a headset.
 */
#ifndef GEVR_ENGINE_H
#define GEVR_ENGINE_H

#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine yaw (degrees, [0,360), clockwise) -> VR yaw (radians, (-pi,pi], CCW) */
float gevr_engine_yaw_to_vr(float vv_theta_deg);

/* VR yaw -> engine yaw. Always returns a value in [0, 360). */
float gevr_vr_yaw_to_engine(float vr_yaw_rad);

/* Engine pitch (degrees, [0,360) with values above 180 meaning negative)
 * -> VR pitch in radians, signed, in (-pi, pi].
 *
 * GEVR_ENGINE_PITCH_SIGN selects which way the engine counts pitch. The
 * decomp does not state it unambiguously and it interacts with the player's
 * in-game "look up/down" option, so it is a single constant here and the
 * calibrate tool reports the resulting sign against the live headset. */
float gevr_engine_pitch_to_vr(float vv_verta_deg);
float gevr_vr_pitch_to_engine(float vr_pitch_rad);

/* Folds an arbitrary degree value into [0, 360), matching what the engine's
 * own wrap loops in bondview2.c produce. */
float gevr_wrap_360(float deg);

/* Signed form: folds into (-180, 180]. */
float gevr_wrap_180(float deg);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_ENGINE_H */
