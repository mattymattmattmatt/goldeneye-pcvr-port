/*
 * gevr_config.h - tunables for the VR layer, loaded from gevr.ini.
 *
 * Everything a player is likely to want to change lives here rather than in
 * a #define, because comfort settings are personal and re-building the game
 * to try a different snap angle is not a reasonable ask.
 */
#ifndef GEVR_CONFIG_H
#define GEVR_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gevr_turn_mode {
    GEVR_TURN_SNAP = 0,   /* discrete steps; the comfortable default */
    GEVR_TURN_SMOOTH,     /* continuous; better for experienced players */
    GEVR_TURN_OFF         /* physical turning only (roomscale / swivel chair) */
} gevr_turn_mode;

typedef enum gevr_locomotion_ref {
    GEVR_LOCO_HEAD = 0,   /* stick forward == where you are looking */
    GEVR_LOCO_HAND,       /* stick forward == where the left hand points */
    GEVR_LOCO_BODY        /* stick forward == the turned body heading */
} gevr_locomotion_ref;

typedef enum gevr_aim_mode {
    GEVR_AIM_HEAD = 0,    /* gun follows the view; closest to the N64 feel */
    GEVR_AIM_CONTROLLER   /* gun follows the right controller ray */
} gevr_aim_mode;

typedef struct gevr_config {
    /* ---- world ---- */
    float world_scale;        /* GoldenEye units per real metre. GE is roughly
                               * centimetre-scaled, so ~100 is the start point;
                               * the calibrate tool measures it properly. */
    float player_height;      /* metres; used when no stage space is available */
    float ipd_scale;          /* 1.0 = true stereo. Lower flattens the world,
                               * which some players prefer at N64 scale. */

    /* ---- turning ---- */
    gevr_turn_mode turn_mode;
    float snap_degrees;       /* per snap step */
    float snap_threshold;     /* stick deflection that triggers a snap */
    float snap_release;       /* deflection below which the snap re-arms */
    float smooth_turn_dps;    /* degrees/sec at full deflection */

    /* ---- locomotion ---- */
    gevr_locomotion_ref locomotion_ref;
    float move_deadzone;      /* radial, 0..1 */
    float move_curve;         /* response exponent; 1.0 linear, >1 finer near centre */
    float turn_deadzone;
    float turn_curve;

    /* ---- aiming ---- */
    gevr_aim_mode aim_mode;
    float aim_yaw_gain;       /* crosshair pan per radian of hand/head offset */
    float aim_pitch_gain;

    /* ---- head servo ----
     * The engine owns the camera angles and uses them for hitscan, so the VR
     * layer drives them toward the headset with a proportional controller
     * rather than fighting them. Gains are in stick-units per radian. */
    float servo_yaw_gain;
    float servo_pitch_gain;
    float servo_max;          /* clamp on emitted stick magnitude, 0..1 */
    float servo_deadband;     /* radians of error to ignore, stops jitter */

    /* ---- comfort ---- */
    int   vignette_enabled;
    float vignette_strength;  /* 0..1 at full movement speed */
    int   invert_pitch;
    float haptics_scale;      /* 0 disables rumble */

    /* ---- rendering ---- */
    float render_scale;       /* multiplier on the runtime's recommended size */
    float z_near;             /* metres */
    float z_far;              /* metres; <= z_near selects an infinite far plane */
    int   hud_enabled;
    float hud_distance;       /* metres in front of the head */
    float hud_scale;
    int   mirror_window;      /* show a flat mirror on the desktop */

    /* ---- misc ---- */
    int   swap_sticks;        /* left/right thumbstick roles */
    int   left_handed;        /* mirror the hand roles */
} gevr_config;

/* Fills cfg with the defaults documented in vr/config/gevr.ini. */
void gevr_config_defaults(gevr_config *cfg);

/* Applies key=value lines from an INI file over the top of cfg. Unknown keys
 * are reported but do not fail the load, so a config from a newer build still
 * works. Returns 0 on success, -1 if the file could not be opened. */
int  gevr_config_load(gevr_config *cfg, const char *path);

/* Applies a single "key=value" pair. Returns 0 if the key was recognised.
 * Exposed so the calibrate tool can accept live tweaks. */
int  gevr_config_set(gevr_config *cfg, const char *key, const char *value);

/* Clamps every field into a sane range. Called automatically after loading;
 * a hand-edited ini should never be able to produce an unplayable session. */
void gevr_config_validate(gevr_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_CONFIG_H */
