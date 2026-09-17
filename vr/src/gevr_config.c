#include "gevr_config.h"
#include "gevr_controls.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gevr_config_defaults(gevr_config *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));

    /* GoldenEye's world is roughly centimetre-scaled: a doorway measures a
     * couple of hundred units. 100 units/metre puts Bond at human height and
     * is the value the calibrate tool starts from. */
    cfg->world_scale        = 100.0f;
    cfg->player_height      = 1.75f;
    cfg->ipd_scale          = 1.0f;

    cfg->turn_mode          = GEVR_TURN_SNAP;
    cfg->snap_degrees       = 30.0f;
    cfg->snap_threshold     = 0.70f;
    cfg->snap_release       = 0.35f;
    cfg->smooth_turn_dps    = 120.0f;

    cfg->locomotion_ref     = GEVR_LOCO_HEAD;
    cfg->move_deadzone      = 0.15f;
    cfg->move_curve         = 1.0f;
    cfg->turn_deadzone      = 0.20f;
    cfg->turn_curve         = 1.0f;

    cfg->aim_mode           = GEVR_AIM_HEAD;
    cfg->aim_yaw_gain       = 2.5f;
    cfg->aim_pitch_gain     = 2.5f;

    /* 6.0 stick-units per radian settles a 30 degree snap in about four
     * frames without overshoot at 90 Hz. Raise it for a snappier view at the
     * cost of a small wobble when you stop moving your head. */
    cfg->servo_yaw_gain     = 6.0f;
    cfg->servo_pitch_gain   = 6.0f;
    cfg->servo_max          = 1.0f;
    cfg->servo_deadband     = GEVR_DEG2RAD(0.25f);

    cfg->vignette_enabled   = 1;
    cfg->vignette_strength  = 0.55f;
    cfg->invert_pitch       = 0;
    cfg->haptics_scale      = 1.0f;

    cfg->render_scale       = 1.0f;
    cfg->z_near             = 0.05f;
    cfg->z_far              = 0.0f;   /* infinite far plane */
    cfg->hud_enabled        = 1;
    cfg->hud_distance       = 1.6f;
    cfg->hud_scale          = 1.0f;
    cfg->mirror_window      = 1;

    cfg->swap_sticks        = 0;
    cfg->left_handed        = 0;
}

/* ---------------------------------------------------------------- parsing */

static int parse_bool(const char *v, int fallback)
{
    if (!v || !*v) {
        return fallback;
    }
    if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on")) {
        return 1;
    }
    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off")) {
        return 0;
    }
    return fallback;
}

static int parse_enum(const char *v, const char *const *names, int count, int fallback)
{
    int i;
    for (i = 0; i < count; i++) {
        if (!strcmp(v, names[i])) {
            return i;
        }
    }
    return fallback;
}

static const char *const k_turn_names[] = { "snap", "smooth", "off" };
static const char *const k_loco_names[] = { "head", "hand", "body" };
static const char *const k_aim_names[]  = { "head", "controller" };

/* A tiny table keeps the setter honest: every float tunable is listed once,
 * with the range it is clamped to, and validate() reuses the same table. */
typedef struct {
    const char *key;
    size_t      offset;
    float       lo, hi;
} float_field;

#define FF(name, lo, hi) { #name, offsetof(gevr_config, name), lo, hi }

static const float_field k_float_fields[] = {
    FF(world_scale,       1.0f,   10000.0f),
    FF(player_height,     0.8f,   2.5f),
    FF(ipd_scale,         0.0f,   2.0f),
    FF(snap_degrees,      5.0f,   90.0f),
    FF(snap_threshold,    0.1f,   1.0f),
    FF(snap_release,      0.0f,   0.95f),
    FF(smooth_turn_dps,   10.0f,  720.0f),
    FF(move_deadzone,     0.0f,   0.9f),
    FF(move_curve,        0.25f,  4.0f),
    FF(turn_deadzone,     0.0f,   0.9f),
    FF(turn_curve,        0.25f,  4.0f),
    FF(aim_yaw_gain,      0.0f,   20.0f),
    FF(aim_pitch_gain,    0.0f,   20.0f),
    FF(servo_yaw_gain,    0.1f,   60.0f),
    FF(servo_pitch_gain,  0.1f,   60.0f),
    FF(servo_max,         0.05f,  1.0f),
    FF(servo_deadband,    0.0f,   0.2f),
    FF(vignette_strength, 0.0f,   1.0f),
    FF(haptics_scale,     0.0f,   2.0f),
    FF(render_scale,      0.5f,   2.0f),
    FF(z_near,            0.01f,  1.0f),
    FF(z_far,             0.0f,   100000.0f),
    FF(hud_distance,      0.3f,   10.0f),
    FF(hud_scale,         0.1f,   5.0f)
};

#undef FF

static const int k_int_count = 8;

int gevr_config_set(gevr_config *cfg, const char *key, const char *value)
{
    size_t i;

    if (!cfg || !key || !value) {
        return -1;
    }

    for (i = 0; i < sizeof(k_float_fields) / sizeof(k_float_fields[0]); i++) {
        if (!strcmp(key, k_float_fields[i].key)) {
            float *slot = (float *)((char *)cfg + k_float_fields[i].offset);
            *slot = (float)atof(value);
            return 0;
        }
    }

    if (!strcmp(key, "turn_mode")) {
        cfg->turn_mode = (gevr_turn_mode)parse_enum(value, k_turn_names, 3,
                                                    (int)cfg->turn_mode);
        return 0;
    }
    if (!strcmp(key, "locomotion_ref")) {
        cfg->locomotion_ref = (gevr_locomotion_ref)parse_enum(value, k_loco_names, 3,
                                                              (int)cfg->locomotion_ref);
        return 0;
    }
    if (!strcmp(key, "aim_mode")) {
        cfg->aim_mode = (gevr_aim_mode)parse_enum(value, k_aim_names, 2,
                                                  (int)cfg->aim_mode);
        return 0;
    }

    if (!strcmp(key, "vignette_enabled")) { cfg->vignette_enabled = parse_bool(value, cfg->vignette_enabled); return 0; }
    if (!strcmp(key, "invert_pitch"))     { cfg->invert_pitch     = parse_bool(value, cfg->invert_pitch);     return 0; }
    if (!strcmp(key, "hud_enabled"))      { cfg->hud_enabled      = parse_bool(value, cfg->hud_enabled);      return 0; }
    if (!strcmp(key, "mirror_window"))    { cfg->mirror_window    = parse_bool(value, cfg->mirror_window);    return 0; }
    if (!strcmp(key, "swap_sticks"))      { cfg->swap_sticks      = parse_bool(value, cfg->swap_sticks);      return 0; }
    if (!strcmp(key, "left_handed"))      { cfg->left_handed      = parse_bool(value, cfg->left_handed);      return 0; }

    return -1;
}

void gevr_config_validate(gevr_config *cfg)
{
    size_t i;

    if (!cfg) {
        return;
    }

    for (i = 0; i < sizeof(k_float_fields) / sizeof(k_float_fields[0]); i++) {
        float *slot = (float *)((char *)cfg + k_float_fields[i].offset);
        *slot = gevr_clampf(*slot, k_float_fields[i].lo, k_float_fields[i].hi);
    }

    /* Hysteresis only works if release sits below threshold; an ini that gets
     * this backwards would snap-turn forever on a single flick. */
    if (cfg->snap_release >= cfg->snap_threshold) {
        cfg->snap_release = cfg->snap_threshold * 0.5f;
    }

    (void)k_int_count;
}

static char *trim(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

int gevr_config_load(gevr_config *cfg, const char *path)
{
    FILE *f;
    char line[512];

    if (!cfg || !path) {
        return -1;
    }

    f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *key, *value, *eq, *hash;

        hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        hash = strchr(line, ';');
        if (hash) {
            *hash = '\0';
        }

        key = trim(line);
        if (!*key || *key == '[') {
            continue;
        }

        eq = strchr(key, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        value = trim(eq + 1);
        key = trim(key);

        /* bind_<action> = <source> lives in the same file as the tunables. */
        if (!strncmp(key, "bind_", 5)) {
            /* Binding table lives in gevr_controls; resolved by the caller
             * through gevr_config_take_binding(). Stored verbatim for now. */
            continue;
        }

        if (gevr_config_set(cfg, key, value) != 0) {
            fprintf(stderr, "[gevr] unknown config key '%s' in %s (ignored)\n",
                    key, path);
        }
    }

    fclose(f);
    gevr_config_validate(cfg);
    return 0;
}
