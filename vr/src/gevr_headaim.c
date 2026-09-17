#include "gevr_headaim.h"

#include "gevr_engine.h"

#include <math.h>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

#define RAD_TO_DEG (180.0f / M_PIf)

void gevr_headaim_default_params(gevr_headaim_params *p)
{
    if (!p) {
        return;
    }
    /* 120 deg/s at full deflection. Fast enough to spin round without the
     * stick feeling like a crank, slow enough that a twitchy thumb does not
     * throw the view across the room. */
    p->turn_rate = 120.0f / RAD_TO_DEG;
    p->stick_deadzone = 0.15f;

    /* The engine clamps its own pitch; these match it so the view stops at the
     * same place whether the player pitched with the stick or with their neck.
     * Looking further up than this in a headset would show the player a limit
     * the game never intended to have a boundary at. */
    p->pitch_min = -80.0f / RAD_TO_DEG;
    p->pitch_max =  80.0f / RAD_TO_DEG;

}

void gevr_headaim_reset(gevr_headaim_state *st)
{
    if (!st) {
        return;
    }
    st->base_yaw = 0.0f;
    st->have_recentre = 0;
    st->recentre_yaw = 0.0f;
}

void gevr_headaim_recentre(gevr_headaim_state *st, gevr_euler head)
{
    if (!st) {
        return;
    }
    /* Fold the old offset into the body yaw before moving the reference, so
     * recentring changes where "straight ahead" is on the player's neck
     * without spinning the character. */
    st->base_yaw += st->have_recentre ? (head.yaw - st->recentre_yaw) : 0.0f;
    st->recentre_yaw = head.yaw;
    st->have_recentre = 1;
}

static float apply_deadzone(float v, float dz)
{
    float mag = v < 0.0f ? -v : v;

    if (mag <= dz) {
        return 0.0f;
    }
    /* Rescale so the stick starts from zero at the edge of the deadzone
     * instead of jumping to dz's worth of turn the moment it is crossed. */
    if (dz >= 1.0f) {
        return 0.0f;
    }
    return (v < 0.0f ? -(mag - dz) : (mag - dz)) / (1.0f - dz);
}

int gevr_headaim_update(gevr_headaim_state *st,
                        const gevr_headaim_params *p,
                        gevr_euler head,
                        float stick_x,
                        float dt,
                        int engine_has_control,
                        float *out_theta_deg,
                        float *out_verta_deg)
{
    float head_yaw_off;
    float yaw, pitch;

    if (!st || !p || !out_theta_deg || !out_verta_deg) {
        return 0;
    }

    /* First pose seen: adopt it as straight ahead, so the player does not
     * start the level facing wherever the headset happened to be pointing
     * when the runtime came up. */
    if (!st->have_recentre) {
        st->recentre_yaw = head.yaw;
        st->have_recentre = 1;
    }

    /*
     * The stick still turns the body even while the engine holds the camera.
     * Suppressing it would make the stick feel dead during a cutscene and then
     * snap when control returned; letting it accumulate means the player comes
     * out of the cutscene facing where they steered.
     */
    {
        float turn = apply_deadzone(stick_x, p->stick_deadzone);

        if (dt > 0.0f) {
            st->base_yaw += turn * p->turn_rate * dt;
        }
    }

    if (engine_has_control) {
        /* Scripted camera: the engine is authoritative. Overwriting vv_theta
         * here would fight a cutscene or a death animation for control of the
         * view, and the engine would win on alternate frames -- which looks
         * like a fault in the headset rather than a fault here. */
        return 0;
    }

    head_yaw_off = head.yaw - st->recentre_yaw;

    yaw = st->base_yaw + head_yaw_off;
    pitch = head.pitch;

    if (pitch < p->pitch_min) { pitch = p->pitch_min; }
    if (pitch > p->pitch_max) { pitch = p->pitch_max; }

    /* Both conversions live in gevr_engine.c, which owns the sign convention.
     * Yaw is wrapped into [0,360) there, which is what vv_theta holds. */
    *out_theta_deg = gevr_vr_yaw_to_engine(yaw);

    /*
     * Pitch needs the extra wrap_180, and the reason is easy to miss.
     * gevr_vr_pitch_to_engine wraps into [0,360), which is right for
     * vv_verta360 but wrong for vv_verta: the engine keeps vv_verta *signed*
     * around zero -- it initialises it to -4.0 looking slightly down -- and
     * derives vv_verta360 from it, not the other way round. Writing 356 where
     * the engine expects -4 would sail straight past its own pitch clamp,
     * because 356 is not less than the upper limit. So it is brought back to
     * (-180,180] before it is written.
     */
    *out_verta_deg = gevr_wrap_180(gevr_vr_pitch_to_engine(pitch));

    return 1;
}
