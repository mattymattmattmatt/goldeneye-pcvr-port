#include "gevr_engine.h"

/* +1 means "engine pitch counts positive when looking up", which is the
 * reading that matches speedVertaUp raising vv_verta in bondview2.c. If the
 * view comes out upside down on real hardware, this is the constant to flip;
 * gevr_config's invert_pitch flips the servo output independently, for the
 * player's own look-inversion preference. */
#define GEVR_ENGINE_PITCH_SIGN (+1.0f)

float gevr_wrap_360(float deg)
{
    deg = fmodf(deg, 360.0f);
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    return deg;
}

float gevr_wrap_180(float deg)
{
    deg = gevr_wrap_360(deg);
    if (deg > 180.0f) {
        deg -= 360.0f;
    }
    return deg;
}

float gevr_engine_yaw_to_vr(float vv_theta_deg)
{
    return gevr_wrap_pi(GEVR_DEG2RAD(-gevr_wrap_180(vv_theta_deg)));
}

float gevr_vr_yaw_to_engine(float vr_yaw_rad)
{
    return gevr_wrap_360(-GEVR_RAD2DEG(gevr_wrap_pi(vr_yaw_rad)));
}

float gevr_engine_pitch_to_vr(float vv_verta_deg)
{
    return gevr_wrap_pi(GEVR_DEG2RAD(GEVR_ENGINE_PITCH_SIGN
                                     * gevr_wrap_180(vv_verta_deg)));
}

float gevr_vr_pitch_to_engine(float vr_pitch_rad)
{
    return gevr_wrap_360(GEVR_ENGINE_PITCH_SIGN
                         * GEVR_RAD2DEG(gevr_wrap_pi(vr_pitch_rad)));
}
