/*
 * gevr_headaim.h - absolute head aim, in the style of the R.E.A.L. VR mods.
 *
 * This replaces the proportional servo in gevr_controls.c for GEVR_AIM_HEAD,
 * and the reason is worth stating plainly, because the servo looks like it
 * ought to work.
 *
 * The engine stores the player's look direction as two absolute angles,
 * vv_theta (yaw) and vv_verta (pitch), and the analog stick does not write
 * them. It writes *rates* -- speedtheta and speedverta -- which MoveBond
 * integrates:
 *
 *     vv_theta = vv_theta + speedtheta * g_GlobalTimerDelta * 3.5f
 *
 * So anything that reaches the engine through a synthetic stick is rate
 * control. A servo aimed at the headset's angle therefore *chases* it and can
 * never match it: the output is proportional to the error, so a steady error
 * is what makes it move at all. Hold your head still and it converges; turn
 * your head and the view trails behind by an angle that grows with how fast
 * you turned. In flat games that is merely mushy. In a headset it is the
 * specific thing that makes people ill, because the world no longer moves with
 * the head.
 *
 * Head aim has to be positional, not rate-based. So this computes the absolute
 * angles the engine should hold this frame, and the shim writes them straight
 * into vv_theta/vv_verta inside MoveBond, immediately before the engine calls
 * bondviewApplyVertaTheta(). That ordering matters and is not incidental:
 * bondviewApplyVertaTheta() is what derives vv_costheta, vv_sintheta,
 * vv_cosverta and vv_sinverta from the angles, and everything downstream --
 * the view matrix, the gun's direction, hitscan, the radar -- reads those
 * derived values rather than the angles. Writing before that call means the
 * whole engine agrees with the headset within the same frame. Writing after it
 * would leave every derived value one frame stale, which is the servo's lag
 * back again by another route.
 *
 * What the engine keeps: movement, collision and hitscan are untouched. This
 * only decides where the player is looking, which is the one thing the headset
 * genuinely knows better.
 */
#ifndef GEVR_HEADAIM_H
#define GEVR_HEADAIM_H

#include "gevr_engine.h"
#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Yaw has two contributions and they are not interchangeable.
 *
 * The headset supplies an absolute offset, but only over the range a neck
 * actually reaches -- roughly +/- 80 degrees seated, and less than that
 * comfortably. A game that asked the player to turn 180 degrees by neck alone
 * would be unplayable seated, so the stick still turns the body, and the head
 * offset is applied on top of wherever the body currently faces.
 *
 * base_yaw is therefore accumulated from the stick and persists across frames;
 * head yaw is absolute and is added fresh each frame. Snap turning moves the
 * base in steps, which is the same mechanism with a different input curve.
 */
typedef struct gevr_headaim_state {
    float base_yaw;        /* radians, accumulated body yaw from the stick */
    int   have_recentre;   /* set once the first pose has been seen        */
    float recentre_yaw;    /* head yaw treated as "straight ahead"         */
} gevr_headaim_state;

/*
 * No sign flags here on purpose. gevr_engine.c already owns the conversion
 * between the VR layer's convention and the engine's -- gevr_vr_yaw_to_engine
 * negates yaw, and GEVR_ENGINE_PITCH_SIGN carries pitch -- and a second copy
 * of that decision in this struct is a second place for it to be wrong. If a
 * sign turns out to be inverted on real hardware, gevr_engine.c is the one
 * file to change and everything that converts angles changes with it.
 */
typedef struct gevr_headaim_params {
    float turn_rate;       /* radians per second at full stick deflection  */
    float stick_deadzone;  /* 0..1, below this the stick does not turn     */
    float pitch_min;       /* radians, engine's downward look limit        */
    float pitch_max;       /* radians, engine's upward look limit          */
} gevr_headaim_params;

/* Sensible defaults; pitch limits match the engine's own clamp. */
void gevr_headaim_default_params(gevr_headaim_params *p);

void gevr_headaim_reset(gevr_headaim_state *st);

/* Treats the current head yaw as straight ahead. Bound to GEVR_ACT_RECENTRE. */
void gevr_headaim_recentre(gevr_headaim_state *st, gevr_euler head);

/*
 * Produces the absolute angles the engine should hold this frame.
 *
 * `head` is the headset's orientation in the VR layer's convention (radians,
 * +yaw left, +pitch up). `stick_x` is the turn stick, -1..1. `dt` is seconds.
 * Outputs are in the engine's units: degrees, with yaw normalised to [0,360).
 *
 * Returns 1 when the angles should be written, 0 when the engine should be
 * left alone -- during a menu, a cutscene or a control lock, where the engine
 * is driving the camera itself and overwriting it would fight the script.
 */
int gevr_headaim_update(gevr_headaim_state *st,
                        const gevr_headaim_params *p,
                        gevr_euler head,
                        float stick_x,
                        float dt,
                        int engine_has_control,
                        float *out_theta_deg,
                        float *out_verta_deg);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_HEADAIM_H */
