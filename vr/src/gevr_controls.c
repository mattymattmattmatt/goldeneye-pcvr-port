#include "gevr_controls.h"

#include <stdlib.h>
#include <string.h>

/* Full crosshair pan speed is reached at 70 on the aim pad: the engine
 * computes (stick - 60) / 10 and clamps at 1.0 (bondview2.c). Emitting 80
 * would work but wastes half the band's resolution. */
#define GEVR_N64_AIM_FULL 70

/* ------------------------------------------------------------- utilities */

signed char gevr_encode_stick(float v)
{
    float mag;
    int out;

    v = gevr_clampf(v, -1.0f, 1.0f);
    if (v > -1e-4f && v < 1e-4f) {
        return 0;
    }

    /* Add the notch back on. The engine subtracts 5 before using the value,
     * so a raw 5 is indistinguishable from centre and a bare scale to 80
     * would silently eat the first 6% of every stick throw. */
    mag = (v < 0.0f ? -v : v) * (float)(GEVR_N64_STICK_MAX - GEVR_N64_STICK_NOTCH);
    out = (int)(mag + (float)GEVR_N64_STICK_NOTCH + 0.5f);

    if (out > GEVR_N64_STICK_MAX) {
        out = GEVR_N64_STICK_MAX;
    }
    return (signed char)(v < 0.0f ? -out : out);
}

signed char gevr_encode_aim_stick(float v)
{
    float mag;
    int out;

    v = gevr_clampf(v, -1.0f, 1.0f);
    if (v > -1e-3f && v < 1e-3f) {
        return 0;
    }

    mag = (float)GEVR_N64_AIM_THRESHOLD
        + (v < 0.0f ? -v : v) * (float)(GEVR_N64_AIM_FULL - GEVR_N64_AIM_THRESHOLD);
    out = (int)(mag + 0.5f);

    /* Must strictly exceed the threshold or the engine reads it as centred. */
    if (out <= GEVR_N64_AIM_THRESHOLD) {
        out = GEVR_N64_AIM_THRESHOLD + 1;
    }
    if (out > GEVR_N64_STICK_MAX) {
        out = GEVR_N64_STICK_MAX;
    }
    return (signed char)(v < 0.0f ? -out : out);
}

void gevr_apply_stick_shaping(float *x, float *y, float deadzone, float curve)
{
    float mx = *x;
    float my = *y;
    float mag = sqrtf(mx * mx + my * my);
    float t;

    if (mag <= 1e-6f) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    /* Radial, not per-axis: a per-axis deadzone carves a square hole out of
     * the stick and makes diagonal walking snag. */
    if (mag <= deadzone) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    t = (mag - deadzone) / (1.0f - deadzone);
    if (t > 1.0f) {
        t = 1.0f;
    }
    if (curve != 1.0f && curve > 0.0f) {
        t = powf(t, curve);
    }

    *x = (mx / mag) * t;
    *y = (my / mag) * t;
}

static float source_value(const gevr_input_state *in, gevr_source src)
{
    switch (src) {
    case GEVR_SRC_TRIGGER_L: return in->trigger_l;
    case GEVR_SRC_TRIGGER_R: return in->trigger_r;
    case GEVR_SRC_GRIP_L:    return in->grip_l;
    case GEVR_SRC_GRIP_R:    return in->grip_r;
    case GEVR_SRC_A_RIGHT:   return (in->buttons & GEVR_BTN_A_RIGHT)     ? 1.0f : 0.0f;
    case GEVR_SRC_B_RIGHT:   return (in->buttons & GEVR_BTN_B_RIGHT)     ? 1.0f : 0.0f;
    case GEVR_SRC_X_LEFT:    return (in->buttons & GEVR_BTN_X_LEFT)      ? 1.0f : 0.0f;
    case GEVR_SRC_Y_LEFT:    return (in->buttons & GEVR_BTN_Y_LEFT)      ? 1.0f : 0.0f;
    case GEVR_SRC_MENU:      return (in->buttons & GEVR_BTN_MENU)        ? 1.0f : 0.0f;
    case GEVR_SRC_STICK_L:   return (in->buttons & GEVR_BTN_STICK_LEFT)  ? 1.0f : 0.0f;
    case GEVR_SRC_STICK_R:   return (in->buttons & GEVR_BTN_STICK_RIGHT) ? 1.0f : 0.0f;
    default:                 return 0.0f;
    }
}

/* Analog sources latch at 60% so a light rest of the finger does not fire. */
static int source_pressed(const gevr_input_state *in, gevr_source src)
{
    return source_value(in, src) >= 0.6f;
}

/* --------------------------------------------------------------- binding */

static const struct { const char *name; gevr_source src; } k_source_names[] = {
    { "none",      GEVR_SRC_NONE      },
    { "trigger_l", GEVR_SRC_TRIGGER_L },
    { "trigger_r", GEVR_SRC_TRIGGER_R },
    { "grip_l",    GEVR_SRC_GRIP_L    },
    { "grip_r",    GEVR_SRC_GRIP_R    },
    { "a",         GEVR_SRC_A_RIGHT   },
    { "b",         GEVR_SRC_B_RIGHT   },
    { "x",         GEVR_SRC_X_LEFT    },
    { "y",         GEVR_SRC_Y_LEFT    },
    { "menu",      GEVR_SRC_MENU      },
    { "stick_l",   GEVR_SRC_STICK_L   },
    { "stick_r",   GEVR_SRC_STICK_R   }
};

static const struct { const char *name; gevr_action act; } k_action_names[] = {
    { "fire",     GEVR_ACT_FIRE     },
    { "aim",      GEVR_ACT_AIM      },
    { "use",      GEVR_ACT_USE      },
    { "weapon",   GEVR_ACT_WEAPON   },
    { "pause",    GEVR_ACT_PAUSE    },
    { "crouch",   GEVR_ACT_CROUCH   },
    { "recenter", GEVR_ACT_RECENTER }
};

void gevr_controls_init(gevr_controls *c)
{
    memset(c, 0, sizeof(*c));

    /* Defaults chosen so the physical hand doing a thing matches the thing:
     * the right hand holds the gun and fires, the left hand steadies it into
     * aim mode. Pad assignment follows the Goodhead routing exactly. */
    c->bind[GEVR_ACT_FIRE].source     = GEVR_SRC_TRIGGER_R;
    c->bind[GEVR_ACT_FIRE].pad        = GEVR_PAD_MOVE;
    c->bind[GEVR_ACT_FIRE].n64_bit    = GEVR_N64_Z;

    c->bind[GEVR_ACT_AIM].source      = GEVR_SRC_TRIGGER_L;
    c->bind[GEVR_ACT_AIM].pad         = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_AIM].n64_bit     = GEVR_N64_Z;

    c->bind[GEVR_ACT_USE].source      = GEVR_SRC_A_RIGHT;
    c->bind[GEVR_ACT_USE].pad         = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_USE].n64_bit     = GEVR_N64_A;

    c->bind[GEVR_ACT_WEAPON].source   = GEVR_SRC_B_RIGHT;
    c->bind[GEVR_ACT_WEAPON].pad      = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_WEAPON].n64_bit  = GEVR_N64_B;

    c->bind[GEVR_ACT_PAUSE].source    = GEVR_SRC_Y_LEFT;
    c->bind[GEVR_ACT_PAUSE].pad       = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_PAUSE].n64_bit   = GEVR_N64_START;

    c->bind[GEVR_ACT_CROUCH].source   = GEVR_SRC_GRIP_L;
    c->bind[GEVR_ACT_CROUCH].pad      = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_CROUCH].n64_bit  = GEVR_N64_R;

    c->bind[GEVR_ACT_RECENTER].source = GEVR_SRC_X_LEFT;
    c->bind[GEVR_ACT_RECENTER].pad    = GEVR_PAD_AIM;
    c->bind[GEVR_ACT_RECENTER].n64_bit = 0; /* never reaches the engine */
}

int gevr_controls_bind_by_name(gevr_controls *c, const char *action,
                               const char *source)
{
    size_t i;
    int act = -1;
    gevr_source src = GEVR_SRC_COUNT;

    if (!c || !action || !source) {
        return -1;
    }

    for (i = 0; i < sizeof(k_action_names) / sizeof(k_action_names[0]); i++) {
        if (strcmp(k_action_names[i].name, action) == 0) {
            act = (int)k_action_names[i].act;
            break;
        }
    }
    if (act < 0) {
        return -1;
    }

    for (i = 0; i < sizeof(k_source_names) / sizeof(k_source_names[0]); i++) {
        if (strcmp(k_source_names[i].name, source) == 0) {
            src = k_source_names[i].src;
            break;
        }
    }
    if (src == GEVR_SRC_COUNT) {
        return -1;
    }

    c->bind[act].source = src;
    return 0;
}

/* ------------------------------------------------------------------ turn */

static void update_turn(gevr_controls *c, const gevr_config *cfg,
                        float turn_x, float dt, gevr_haptic_request *haptics)
{
    float mag = (turn_x < 0.0f) ? -turn_x : turn_x;

    switch (cfg->turn_mode) {
    case GEVR_TURN_SNAP:
        if (!c->snap_latched && mag >= cfg->snap_threshold) {
            /* +yaw is left; pushing the stick right must turn right. */
            float step = GEVR_DEG2RAD(cfg->snap_degrees);
            c->body_yaw = gevr_wrap_pi(c->body_yaw + (turn_x < 0.0f ? step : -step));
            c->snap_latched = 1;

            if (haptics && cfg->haptics_scale > 0.0f) {
                haptics->right = 1;
                haptics->amplitude = 0.35f * cfg->haptics_scale;
                haptics->duration = 0.02f;
            }
        } else if (c->snap_latched && mag <= cfg->snap_release) {
            c->snap_latched = 0;
        }
        break;

    case GEVR_TURN_SMOOTH:
        c->body_yaw = gevr_wrap_pi(c->body_yaw
                                   - turn_x * GEVR_DEG2RAD(cfg->smooth_turn_dps) * dt);
        break;

    case GEVR_TURN_OFF:
    default:
        break;
    }
}

/* ----------------------------------------------------------------- servo */

/* Proportional control of the engine's camera angle toward a target. The
 * engine consumes the aim stick as a turn *rate*, so a P term on angle error
 * is the natural pairing and settles without oscillating. */
static float servo_axis(float error, float gain, float deadband, float max_out)
{
    float mag = (error < 0.0f) ? -error : error;
    float out;

    if (mag <= deadband) {
        return 0.0f;
    }

    /* Subtract the deadband rather than stepping over it, so response is
     * continuous at the boundary and the view does not twitch. */
    error -= (error < 0.0f) ? -deadband : deadband;
    out = error * gain;
    return gevr_clampf(out, -max_out, max_out);
}

float gevr_controls_desired_yaw(const gevr_controls *c,
                                const gevr_input_state *in)
{
    float head_yaw = 0.0f;

    if (in && in->head_valid) {
        head_yaw = gevr_quat_yaw_of(in->head.orientation);
    }
    return gevr_wrap_pi(c->body_yaw + head_yaw);
}

int gevr_controls_take_recenter(gevr_controls *c)
{
    int r = c->recenter_requested;
    c->recenter_requested = 0;
    return r;
}

/* ------------------------------------------------------------------ main */

void gevr_controls_update(gevr_controls *c,
                          const gevr_config *cfg,
                          const gevr_input_state *in,
                          const gevr_game_state *game,
                          gevr_n64_pad out_pads[GEVR_PAD_COUNT],
                          gevr_haptic_request *haptics)
{
    float move_x, move_y, turn_x, turn_y;
    float head_yaw = 0.0f, head_pitch = 0.0f;
    float desired_yaw, desired_pitch;
    float err_yaw, err_pitch;
    float aim_sx = 0.0f, aim_sy = 0.0f;
    float dt;
    int i;
    int in_aim;

    memset(out_pads, 0, sizeof(gevr_n64_pad) * GEVR_PAD_COUNT);
    if (haptics) {
        memset(haptics, 0, sizeof(*haptics));
    }
    if (!c || !cfg || !in || !game) {
        return;
    }

    /* A hitched frame must not produce a giant turn step. */
    dt = gevr_clampf(in->dt, 1.0f / 1000.0f, 1.0f / 10.0f);

    move_x = in->move_x;
    move_y = in->move_y;
    turn_x = in->turn_x;
    turn_y = in->turn_y;

    if (cfg->swap_sticks) {
        float tx = move_x, ty = move_y;
        move_x = turn_x; move_y = turn_y;
        turn_x = tx;     turn_y = ty;
    }

    gevr_apply_stick_shaping(&move_x, &move_y, cfg->move_deadzone, cfg->move_curve);
    gevr_apply_stick_shaping(&turn_x, &turn_y, cfg->turn_deadzone, cfg->turn_curve);

    /* ---- buttons (always, so the player can unpause) ---- */
    for (i = 0; i < GEVR_ACT_COUNT; i++) {
        const gevr_binding *b = &c->bind[i];
        if (b->source == GEVR_SRC_NONE) {
            continue;
        }
        if (!source_pressed(in, b->source)) {
            continue;
        }
        if (i == GEVR_ACT_RECENTER) {
            /* Edge triggered: holding the button must not spin the view. */
            if (!(c->prev_vr_buttons & (1u << GEVR_ACT_RECENTER))) {
                c->recenter_requested = 1;
            }
            continue;
        }
        if (b->n64_bit && b->pad >= 0 && b->pad < GEVR_PAD_COUNT) {
            out_pads[b->pad].buttons |= b->n64_bit;
        }
    }

    c->prev_vr_buttons = 0;
    for (i = 0; i < GEVR_ACT_COUNT; i++) {
        if (c->bind[i].source != GEVR_SRC_NONE && source_pressed(in, c->bind[i].source)) {
            c->prev_vr_buttons |= (1u << i);
        }
    }

    /* While the watch menu or pause screen is up the engine wants ordinary
     * stick input to navigate; servoing the camera there would fight it. */
    if (game->menu_open) {
        out_pads[GEVR_PAD_AIM].stick_x = gevr_encode_stick(move_x);
        out_pads[GEVR_PAD_AIM].stick_y = gevr_encode_stick(move_y);
        c->vignette = 0.0f;
        return;
    }

    if (!game->controls_locked) {
        update_turn(c, cfg, turn_x, dt, haptics);
    }

    if (in->head_valid) {
        gevr_euler e = gevr_quat_to_euler(in->head.orientation);
        head_yaw = gevr_quat_yaw_of(in->head.orientation);
        head_pitch = e.pitch;
    }

    desired_yaw = gevr_wrap_pi(c->body_yaw + head_yaw);
    desired_pitch = head_pitch;

    err_yaw = gevr_wrap_pi(desired_yaw - game->yaw);
    err_pitch = desired_pitch - game->pitch;

    c->last_yaw_error = err_yaw;
    c->last_pitch_error = err_pitch;

    in_aim = game->aim_mode;

    if (in_aim && cfg->aim_mode == GEVR_AIM_CONTROLLER && in->hand_r_valid && in->head_valid) {
        /* In aim mode the engine freezes natural turning and pans the
         * crosshair instead, but only past +/-60 on the aim pad. Steer that
         * band from how far the right hand points away from the view. */
        float hand_yaw = gevr_quat_yaw_of(in->hand_r.orientation);
        gevr_euler he = gevr_quat_to_euler(in->hand_r.orientation);
        float dyaw = gevr_wrap_pi(hand_yaw - head_yaw);
        float dpitch = he.pitch - head_pitch;

        aim_sx = gevr_clampf(-dyaw * cfg->aim_yaw_gain, -1.0f, 1.0f);
        aim_sy = gevr_clampf(-dpitch * cfg->aim_pitch_gain, -1.0f, 1.0f);

        out_pads[GEVR_PAD_AIM].stick_x = gevr_encode_aim_stick(aim_sx);
        out_pads[GEVR_PAD_AIM].stick_y = gevr_encode_aim_stick(aim_sy);
    } else {
        float sx = servo_axis(err_yaw, cfg->servo_yaw_gain,
                              cfg->servo_deadband, cfg->servo_max);
        float sy = servo_axis(err_pitch, cfg->servo_pitch_gain,
                              cfg->servo_deadband, cfg->servo_max);

        /* Both axes come off the same physical stick and the engine
         * integrates them with the same sign, so both negate here.
         *
         * invert_pitch flips the OUTPUT polarity, not the target: the target
         * is always the true head pitch, because that is physically where the
         * camera has to end up. Folding the inversion into the target instead
         * makes the servo converge on the mirror image of where the player is
         * looking, and then run away to the pitch limit. The engine's own
         * "look up/down" option decides which polarity is correct, and the
         * shim reports it through cfg->invert_pitch. */
        out_pads[GEVR_PAD_AIM].stick_x = gevr_encode_stick(-sx);
        out_pads[GEVR_PAD_AIM].stick_y =
            gevr_encode_stick(cfg->invert_pitch ? sy : -sy);
    }

    /* ---- locomotion ---- */
    {
        float fx = move_x;
        float fy = move_y;

        if (cfg->locomotion_ref == GEVR_LOCO_HAND && in->hand_l_valid) {
            /* Rotate the stick vector by how far the left hand points away
             * from the view, so "forward" tracks the hand. */
            float hand_yaw = gevr_quat_yaw_of(in->hand_l.orientation);
            float rel = gevr_wrap_pi(hand_yaw - head_yaw);
            float cs = cosf(rel), sn = sinf(rel);
            float rx = fx * cs - fy * sn;
            float ry = fx * sn + fy * cs;
            fx = rx;
            fy = ry;
        } else if (cfg->locomotion_ref == GEVR_LOCO_BODY) {
            /* Forward follows the turned body, ignoring where the head looks. */
            float rel = gevr_wrap_pi(-head_yaw);
            float cs = cosf(rel), sn = sinf(rel);
            float rx = fx * cs - fy * sn;
            float ry = fx * sn + fy * cs;
            fx = rx;
            fy = ry;
        }

        out_pads[GEVR_PAD_MOVE].stick_x = gevr_encode_stick(fx);
        out_pads[GEVR_PAD_MOVE].stick_y = gevr_encode_stick(fy);
    }

    /* ---- comfort vignette ---- */
    {
        float speed = sqrtf(move_x * move_x + move_y * move_y);
        float turning = (cfg->turn_mode == GEVR_TURN_SMOOTH)
                      ? ((turn_x < 0.0f) ? -turn_x : turn_x) : 0.0f;
        float target = gevr_clampf(speed + turning * 0.5f, 0.0f, 1.0f)
                     * cfg->vignette_strength;
        float rate = 6.0f * dt;

        if (!cfg->vignette_enabled) {
            target = 0.0f;
        }
        if (rate > 1.0f) {
            rate = 1.0f;
        }
        c->vignette += (target - c->vignette) * rate;
    }
}
