/*
 * gevr_controls.h - VR controllers -> GoldenEye "2.4 Goodhead" dual pads.
 *
 * Why Goodhead: it is the only stock control style whose two analog sources
 * are already split into an aim stick and a move stick (see the routing note
 * in gevr_input.h). Feeding it two synthetic pads gives true twin-stick
 * without touching the player movement code at all.
 *
 * The one thing VR changes is where "look" comes from. On the N64 the aim
 * stick's Y axis pitches the camera. In VR the headset owns pitch, so the
 * layer runs a proportional servo that drives the engine's camera angles onto
 * the headset's. That keeps the engine authoritative for hitscan and collision
 * while the player's actual head decides where the view points.
 */
#ifndef GEVR_CONTROLS_H
#define GEVR_CONTROLS_H

#include "gevr_config.h"
#include "gevr_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the engine currently believes, supplied by the shim each frame and
 * expressed in the VR layer's conventions (radians, +yaw left, +pitch up). */
typedef struct gevr_game_state {
    float yaw;
    float pitch;
    int   aim_mode;         /* g_CurrentPlayer->insightaimmode */
    int   menu_open;        /* watch menu or pause: pass input through raw */
    int   controls_locked;  /* lvlGetControlsLockedFlag() */
} gevr_game_state;

/* Every VR source a game action can be bound to. */
typedef enum gevr_source {
    GEVR_SRC_NONE = 0,
    GEVR_SRC_TRIGGER_L,
    GEVR_SRC_TRIGGER_R,
    GEVR_SRC_GRIP_L,
    GEVR_SRC_GRIP_R,
    GEVR_SRC_A_RIGHT,
    GEVR_SRC_B_RIGHT,
    GEVR_SRC_X_LEFT,
    GEVR_SRC_Y_LEFT,
    GEVR_SRC_MENU,
    GEVR_SRC_STICK_L,
    GEVR_SRC_STICK_R,
    GEVR_SRC_COUNT
} gevr_source;

/* Game actions, in the order they appear in gevr.ini. */
typedef enum gevr_action {
    GEVR_ACT_FIRE = 0,   /* pad 1 Z - the trigger the fire code reads    */
    GEVR_ACT_AIM,        /* pad 0 Z - toggles/holds insightaimmode       */
    GEVR_ACT_USE,        /* A - doors, switches, objectives              */
    GEVR_ACT_WEAPON,     /* B - cycle weapon (engine ORs both pads)      */
    GEVR_ACT_PAUSE,      /* Start - watch menu                          */
    GEVR_ACT_CROUCH,     /* engine-dependent; see docs/VR/Controls.md    */
    GEVR_ACT_RECENTER,   /* VR-side only, never reaches the engine       */
    GEVR_ACT_COUNT
} gevr_action;

typedef struct gevr_binding {
    gevr_source    source;
    int            pad;      /* GEVR_PAD_AIM / GEVR_PAD_MOVE */
    unsigned short n64_bit;  /* 0 for VR-only actions */
} gevr_binding;

/* Persistent mapper state. Zero-initialise then call gevr_controls_init. */
typedef struct gevr_controls {
    gevr_binding bind[GEVR_ACT_COUNT];

    float body_yaw;        /* accumulated stick turn, radians, +left */
    int   snap_latched;
    float vignette;        /* smoothed 0..1 for the comfort overlay */

    unsigned prev_vr_buttons;
    int      recenter_requested;

    /* Set when the servo cannot keep up, e.g. the first frame after a snap.
     * The renderer uses it to decide whether to apply residual correction. */
    float last_yaw_error;
    float last_pitch_error;
} gevr_controls;

void gevr_controls_init(gevr_controls *c);

/* Resolves an action/source name pair from gevr.ini. Returns 0 on success. */
int  gevr_controls_bind_by_name(gevr_controls *c, const char *action,
                                const char *source);

/* Main entry point. Reads the VR state and the engine's current angles, and
 * writes the two synthetic pads. haptics may be NULL. */
void gevr_controls_update(gevr_controls *c,
                          const gevr_config *cfg,
                          const gevr_input_state *in,
                          const gevr_game_state *game,
                          gevr_n64_pad out_pads[GEVR_PAD_COUNT],
                          gevr_haptic_request *haptics);

/* The heading the player should be rendered at: body yaw plus head yaw.
 * The camera code needs this before the engine has caught up. */
float gevr_controls_desired_yaw(const gevr_controls *c,
                                const gevr_input_state *in);

/* True once, when a recenter binding was pressed. Clears the request. */
int  gevr_controls_take_recenter(gevr_controls *c);

/* Exposed for unit tests and the calibrate tool. */
signed char gevr_encode_stick(float v);
signed char gevr_encode_aim_stick(float v);
void        gevr_apply_stick_shaping(float *x, float *y, float deadzone, float curve);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_CONTROLS_H */
