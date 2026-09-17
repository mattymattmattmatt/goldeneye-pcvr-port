/*
 * Headless tests for the parts of the VR layer that have a right answer:
 * axis encoding, stick shaping, angle wrapping, the head servo's stability,
 * snap-turn latching, Goodhead pad routing and the projection matrix.
 *
 * These run in CI with no headset and no GPU. Everything that needs a runtime
 * lives in the calibrate tool instead.
 */
#include "gevr_camera.h"
#include "gevr_config.h"
#include "gevr_controls.h"
#include "gevr_engine.h"
#include "gevr_math.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

static void check(int cond, const char *what, const char *file, int line)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("  FAIL %s:%d: %s\n", file, line, what);
    }
}

static void check_near(float got, float want, float tol, const char *what,
                       const char *file, int line)
{
    float d = got - want;
    if (d < 0.0f) {
        d = -d;
    }
    g_checks++;
    if (d > tol) {
        g_failures++;
        printf("  FAIL %s:%d: %s (got %.6f, want %.6f +/- %.6f)\n",
               file, line, what, got, want, tol);
    }
}

#define CHECK(c)               check((c), #c, __FILE__, __LINE__)
#define CHECK_NEAR(g, w, t)    check_near((g), (w), (t), #g " ~= " #w, __FILE__, __LINE__)

/* ------------------------------------------------------------- encoding */

static void test_encode_stick(void)
{
    printf("encode_stick\n");

    CHECK(gevr_encode_stick(0.0f) == 0);

    /* Full deflection must reach the N64 maximum, not 75. */
    CHECK(gevr_encode_stick(1.0f) == GEVR_N64_STICK_MAX);
    CHECK(gevr_encode_stick(-1.0f) == -GEVR_N64_STICK_MAX);

    /* Clamped beyond the unit range rather than wrapping a signed char. */
    CHECK(gevr_encode_stick(4.0f) == GEVR_N64_STICK_MAX);
    CHECK(gevr_encode_stick(-4.0f) == -GEVR_N64_STICK_MAX);

    /* The whole point of the notch compensation: the smallest non-zero input
     * must survive the engine subtracting its deadzone of 5. */
    CHECK(gevr_encode_stick(0.01f) > GEVR_N64_STICK_NOTCH);
    CHECK(gevr_encode_stick(-0.01f) < -GEVR_N64_STICK_NOTCH);

    /* Monotonic, so the response curve is not quantised into steps. */
    {
        int i;
        int prev = gevr_encode_stick(0.0f);
        for (i = 1; i <= 100; i++) {
            int cur = gevr_encode_stick((float)i / 100.0f);
            CHECK(cur >= prev);
            prev = cur;
        }
    }

    /* Half deflection lands halfway up the usable band, not halfway to 80. */
    CHECK_NEAR((float)gevr_encode_stick(0.5f),
               (float)GEVR_N64_STICK_NOTCH
               + 0.5f * (float)(GEVR_N64_STICK_MAX - GEVR_N64_STICK_NOTCH),
               1.0f);
}

static void test_encode_aim_stick(void)
{
    printf("encode_aim_stick\n");

    CHECK(gevr_encode_aim_stick(0.0f) == 0);

    /* Anything non-zero must clear the aim threshold, or the engine reads it
     * as centred and the crosshair never moves. */
    CHECK(gevr_encode_aim_stick(0.01f) > GEVR_N64_AIM_THRESHOLD);
    CHECK(gevr_encode_aim_stick(-0.01f) < -GEVR_N64_AIM_THRESHOLD);
    CHECK(gevr_encode_aim_stick(1.0f) <= GEVR_N64_STICK_MAX);
    CHECK(gevr_encode_aim_stick(1.0f) > GEVR_N64_AIM_THRESHOLD);
}

static void test_stick_shaping(void)
{
    float x, y;

    printf("stick_shaping\n");

    /* Inside the radial deadzone, dead. */
    x = 0.10f; y = 0.0f;
    gevr_apply_stick_shaping(&x, &y, 0.15f, 1.0f);
    CHECK(x == 0.0f && y == 0.0f);

    /* Just outside, alive but small: no jump at the boundary. */
    x = 0.16f; y = 0.0f;
    gevr_apply_stick_shaping(&x, &y, 0.15f, 1.0f);
    CHECK(x > 0.0f && x < 0.05f);

    /* A full diagonal must still reach full magnitude, otherwise diagonal
     * movement is permanently slower than cardinal movement. */
    x = 0.7071f; y = 0.7071f;
    gevr_apply_stick_shaping(&x, &y, 0.15f, 1.0f);
    CHECK_NEAR(sqrtf(x * x + y * y), 1.0f, 0.01f);

    /* Direction is preserved through the curve. */
    x = 0.3f; y = 0.9f;
    gevr_apply_stick_shaping(&x, &y, 0.15f, 2.0f);
    CHECK_NEAR(atan2f(y, x), atan2f(0.9f, 0.3f), 0.001f);

    /* Over-deflection past the unit circle is clamped, not amplified. */
    x = 1.4f; y = 0.0f;
    gevr_apply_stick_shaping(&x, &y, 0.15f, 1.0f);
    CHECK_NEAR(x, 1.0f, 0.001f);
}

/* ---------------------------------------------------------------- angles */

static void test_wrap(void)
{
    printf("wrap_pi\n");

    CHECK_NEAR(gevr_wrap_pi(0.0f), 0.0f, 1e-5f);
    CHECK_NEAR(gevr_wrap_pi(GEVR_PI * 2.0f), 0.0f, 1e-4f);
    CHECK_NEAR(gevr_wrap_pi(-GEVR_PI * 2.0f), 0.0f, 1e-4f);

    /* The seam: an error of "almost a full turn one way" must come back as a
     * small error the other way, or the servo spins the player. */
    CHECK_NEAR(gevr_wrap_pi(GEVR_PI * 1.99f), -GEVR_PI * 0.01f, 1e-3f);
    CHECK_NEAR(gevr_wrap_pi(-GEVR_PI * 1.99f), GEVR_PI * 0.01f, 1e-3f);

    {
        int i;
        for (i = -20; i <= 20; i++) {
            float a = (float)i * 0.7f;
            float w = gevr_wrap_pi(a);
            CHECK(w > -GEVR_PI - 1e-4f && w <= GEVR_PI + 1e-4f);
        }
    }
}

static void test_quat_yaw(void)
{
    gevr_quat q;
    gevr_euler e;

    printf("quat yaw/pitch\n");

    CHECK_NEAR(gevr_quat_yaw_of(gevr_quat_identity()), 0.0f, 1e-5f);

    q = gevr_quat_yaw(GEVR_DEG2RAD(90.0f));
    CHECK_NEAR(gevr_quat_yaw_of(q), GEVR_DEG2RAD(90.0f), 1e-3f);

    q = gevr_quat_yaw(GEVR_DEG2RAD(-90.0f));
    CHECK_NEAR(gevr_quat_yaw_of(q), GEVR_DEG2RAD(-90.0f), 1e-3f);

    /* Pitch is positive looking up. */
    q = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f), GEVR_DEG2RAD(30.0f));
    e = gevr_quat_to_euler(q);
    CHECK_NEAR(e.pitch, GEVR_DEG2RAD(30.0f), 1e-3f);

    /* Looking straight up must not produce a NaN or a random heading. */
    q = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f), GEVR_DEG2RAD(89.999f));
    {
        float yaw = gevr_quat_yaw_of(q);
        CHECK(yaw == yaw); /* not NaN */
        CHECK(yaw > -GEVR_PI - 1e-3f && yaw < GEVR_PI + 1e-3f);
    }

    /* Yaw then pitch round-trips. */
    {
        gevr_euler in;
        in.yaw = GEVR_DEG2RAD(35.0f);
        in.pitch = GEVR_DEG2RAD(-20.0f);
        in.roll = 0.0f;
        q = gevr_euler_to_quat(in);
        e = gevr_quat_to_euler(q);
        CHECK_NEAR(e.yaw, in.yaw, 1e-3f);
        CHECK_NEAR(e.pitch, in.pitch, 1e-3f);
    }
}

/* ------------------------------------------------------------- projection */

static void test_projection(void)
{
    gevr_mat4 p;
    float half;

    printf("projection\n");

    /* A symmetric frustum built through the asymmetric path must match the
     * textbook perspective matrix. */
    half = GEVR_DEG2RAD(45.0f);
    p = gevr_projection_from_fov(-half, half, half, -half, 0.1f, 100.0f);

    CHECK_NEAR(p.m[0], 1.0f / tanf(half), 1e-4f);
    CHECK_NEAR(p.m[5], 1.0f / tanf(half), 1e-4f);
    CHECK_NEAR(p.m[8], 0.0f, 1e-5f);
    CHECK_NEAR(p.m[9], 0.0f, 1e-5f);
    CHECK_NEAR(p.m[11], -1.0f, 1e-5f);

    /* An asymmetric frustum, which is what a real HMD reports, must produce
     * a non-zero centre offset. This is exactly what guPerspective cannot do. */
    p = gevr_projection_from_fov(GEVR_DEG2RAD(-50.0f), GEVR_DEG2RAD(45.0f),
                                 GEVR_DEG2RAD(45.0f), GEVR_DEG2RAD(-45.0f),
                                 0.1f, 100.0f);
    CHECK(p.m[8] != 0.0f);
    CHECK_NEAR(p.m[9], 0.0f, 1e-5f);

    /* Infinite far plane path. */
    p = gevr_projection_from_fov(-half, half, half, -half, 0.1f, 0.0f);
    CHECK_NEAR(p.m[10], -1.0f, 1e-5f);
    CHECK_NEAR(p.m[14], -0.2f, 1e-5f);

    /* A near plane at zero must not produce infinities. */
    p = gevr_projection_from_fov(-half, half, half, -half, 0.0f, 100.0f);
    CHECK_NEAR(p.m[0], 1.0f, 1e-5f); /* identity fallback */
}

/* ------------------------------------------------------- Goodhead routing */

static void base_input(gevr_input_state *in)
{
    memset(in, 0, sizeof(*in));
    in->dt = 1.0f / 90.0f;
    in->head.orientation = gevr_quat_identity();
    in->hand_l.orientation = gevr_quat_identity();
    in->hand_r.orientation = gevr_quat_identity();
    in->head_valid = 1;
    in->hand_l_valid = 1;
    in->hand_r_valid = 1;
}

static void test_pad_routing(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];

    printf("Goodhead pad routing\n");

    gevr_controls_init(&c);
    gevr_config_defaults(&cfg);
    memset(&game, 0, sizeof(game));
    base_input(&in);

    /* Left stick forward: must appear on the MOVE pad and leave the AIM pad
     * stick alone, because the engine reads strafe/walk only from pad 1. */
    in.move_y = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK(pads[GEVR_PAD_MOVE].stick_y > 60);
    CHECK(pads[GEVR_PAD_AIM].stick_y == 0);

    /* Left stick right: strafe, again on the move pad. */
    base_input(&in);
    in.move_x = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK(pads[GEVR_PAD_MOVE].stick_x > 60);

    /* Fire is pad 1's Z, aim is pad 0's Z. Getting these two the wrong way
     * round makes the trigger toggle aim mode instead of shooting. */
    base_input(&in);
    in.trigger_r = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK((pads[GEVR_PAD_MOVE].buttons & GEVR_N64_Z) != 0);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_Z) == 0);

    base_input(&in);
    in.trigger_l = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_Z) != 0);
    CHECK((pads[GEVR_PAD_MOVE].buttons & GEVR_N64_Z) == 0);

    /* A light finger rest must not fire. */
    base_input(&in);
    in.trigger_r = 0.3f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK((pads[GEVR_PAD_MOVE].buttons & GEVR_N64_Z) == 0);

    /* Buttons. */
    base_input(&in);
    in.buttons = GEVR_BTN_A_RIGHT | GEVR_BTN_B_RIGHT | GEVR_BTN_Y_LEFT;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_A) != 0);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_B) != 0);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_START) != 0);
}

/* ------------------------------------------------------------ snap turn */

static void test_snap_turn(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];
    float after_first;
    int i;

    printf("snap turn\n");

    gevr_controls_init(&c);
    gevr_config_defaults(&cfg);
    cfg.turn_mode = GEVR_TURN_SNAP;
    cfg.snap_degrees = 30.0f;
    memset(&game, 0, sizeof(game));

    /* Holding the stick over must produce exactly one snap, not one per
     * frame. This is the single most common snap-turn bug. */
    base_input(&in);
    in.turn_x = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    after_first = c.body_yaw;
    CHECK_NEAR(after_first, GEVR_DEG2RAD(-30.0f), 1e-3f);

    for (i = 0; i < 60; i++) {
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    }
    CHECK_NEAR(c.body_yaw, after_first, 1e-4f);

    /* Releasing re-arms it. */
    in.turn_x = 0.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    in.turn_x = 1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK_NEAR(c.body_yaw, GEVR_DEG2RAD(-60.0f), 1e-3f);

    /* Pushing left turns the other way. */
    gevr_controls_init(&c);
    base_input(&in);
    in.turn_x = -1.0f;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK_NEAR(c.body_yaw, GEVR_DEG2RAD(30.0f), 1e-3f);

    /* Smooth turn accumulates instead. */
    gevr_controls_init(&c);
    cfg.turn_mode = GEVR_TURN_SMOOTH;
    cfg.smooth_turn_dps = 90.0f;
    base_input(&in);
    in.turn_x = 1.0f;
    in.dt = 0.1f;
    for (i = 0; i < 5; i++) {
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    }
    CHECK_NEAR(c.body_yaw, GEVR_DEG2RAD(-45.0f), 1e-2f);
}

/* ----------------------------------------------------------- head servo */

/* Stand-in for the engine's turn integrator: the aim stick acts as a rate,
 * with the engine's own deadzone of 5 subtracted first. The exact constant
 * does not matter to the test; convergence and lack of oscillation do. */
static float engine_step(float angle, signed char stick, float dt,
                         float max_rate_dps)
{
    float s = (float)stick;
    float mag = (s < 0.0f) ? -s : s;
    float rate;

    if (mag <= (float)GEVR_N64_STICK_NOTCH) {
        return angle;
    }
    mag -= (float)GEVR_N64_STICK_NOTCH;
    rate = (mag / (float)(GEVR_N64_STICK_MAX - GEVR_N64_STICK_NOTCH))
         * GEVR_DEG2RAD(max_rate_dps);
    if (s < 0.0f) {
        rate = -rate;
    }
    return gevr_wrap_pi(angle - rate * dt);
}

/* Pitch does not wrap in the engine, it clamps: you cannot look past straight
 * up. Modelling that here is what makes a runaway servo show up as a pegged
 * angle instead of quietly wrapping around to something plausible. */
static float engine_pitch_step(float angle, signed char stick, float dt,
                               float max_rate_dps)
{
    float next = engine_step(angle, stick, dt, max_rate_dps);
    return gevr_clampf(next, GEVR_DEG2RAD(-85.0f), GEVR_DEG2RAD(85.0f));
}

static void test_servo_converges(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];
    float worst_overshoot = 0.0f;
    int i;

    printf("head servo\n");

    gevr_controls_init(&c);
    gevr_config_defaults(&cfg);
    cfg.turn_mode = GEVR_TURN_OFF;
    memset(&game, 0, sizeof(game));

    /* Player turns their head 40 degrees left. The engine's yaw starts at 0
     * and must be driven onto the headset's within a handful of frames. */
    base_input(&in);
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(40.0f));

    for (i = 0; i < 240; i++) {
        float err;
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
        game.yaw = engine_step(game.yaw, pads[GEVR_PAD_AIM].stick_x,
                               1.0f / 90.0f, 180.0f);

        err = gevr_wrap_pi(GEVR_DEG2RAD(40.0f) - game.yaw);
        /* Overshoot would show up as the error changing sign and growing. */
        if (i > 30 && err < -worst_overshoot) {
            worst_overshoot = -err;
        }
    }

    CHECK_NEAR(game.yaw, GEVR_DEG2RAD(40.0f), GEVR_DEG2RAD(1.5f));
    CHECK(worst_overshoot < GEVR_DEG2RAD(2.0f));

    /* And it settles: no limit-cycle hunting once it has arrived. */
    {
        float a = game.yaw;
        for (i = 0; i < 90; i++) {
            gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
            game.yaw = engine_step(game.yaw, pads[GEVR_PAD_AIM].stick_x,
                                   1.0f / 90.0f, 180.0f);
        }
        CHECK_NEAR(game.yaw, a, GEVR_DEG2RAD(1.0f));
    }

    /* The seam: headset at +175 degrees, engine at -175. The shortest path is
     * 10 degrees, not 350. A sign error here spins the player on their heel. */
    gevr_controls_init(&c);
    memset(&game, 0, sizeof(game));
    game.yaw = GEVR_DEG2RAD(-175.0f);
    base_input(&in);
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(175.0f));

    for (i = 0; i < 240; i++) {
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
        game.yaw = engine_step(game.yaw, pads[GEVR_PAD_AIM].stick_x,
                               1.0f / 90.0f, 180.0f);
    }
    CHECK_NEAR(gevr_wrap_pi(game.yaw - GEVR_DEG2RAD(175.0f)), 0.0f,
               GEVR_DEG2RAD(2.0f));

    /* Pitch converges too. */
    gevr_controls_init(&c);
    memset(&game, 0, sizeof(game));
    base_input(&in);
    in.head.orientation = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f),
                                               GEVR_DEG2RAD(-25.0f));
    for (i = 0; i < 240; i++) {
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
        game.pitch = engine_pitch_step(game.pitch, pads[GEVR_PAD_AIM].stick_y,
                                       1.0f / 90.0f, 180.0f);
    }
    CHECK_NEAR(game.pitch, GEVR_DEG2RAD(-25.0f), GEVR_DEG2RAD(2.0f));

    /* Looking up converges too, and does not peg at the limit. */
    gevr_controls_init(&c);
    memset(&game, 0, sizeof(game));
    base_input(&in);
    in.head.orientation = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f),
                                               GEVR_DEG2RAD(35.0f));
    for (i = 0; i < 240; i++) {
        gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
        game.pitch = engine_pitch_step(game.pitch, pads[GEVR_PAD_AIM].stick_y,
                                       1.0f / 90.0f, 180.0f);
    }
    CHECK_NEAR(game.pitch, GEVR_DEG2RAD(35.0f), GEVR_DEG2RAD(2.0f));
}

/* invert_pitch must flip the emitted stick, not the angle the servo aims for.
 * Getting this backwards converges the camera on the mirror image of where the
 * player is actually looking. */
static void test_invert_pitch_flips_output(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];
    signed char normal, inverted;

    printf("invert_pitch\n");

    gevr_config_defaults(&cfg);
    cfg.turn_mode = GEVR_TURN_OFF;
    memset(&game, 0, sizeof(game));
    base_input(&in);
    in.head.orientation = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f),
                                               GEVR_DEG2RAD(30.0f));

    gevr_controls_init(&c);
    cfg.invert_pitch = 0;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    normal = pads[GEVR_PAD_AIM].stick_y;

    gevr_controls_init(&c);
    cfg.invert_pitch = 1;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    inverted = pads[GEVR_PAD_AIM].stick_y;

    CHECK(normal != 0);
    CHECK(inverted == -normal);

    /* Yaw is unaffected by the pitch inversion. */
    gevr_controls_init(&c);
    base_input(&in);
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(30.0f));
    cfg.invert_pitch = 0;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    normal = pads[GEVR_PAD_AIM].stick_x;
    gevr_controls_init(&c);
    cfg.invert_pitch = 1;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK(pads[GEVR_PAD_AIM].stick_x == normal);
}

static void test_body_yaw_composes(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;

    printf("body yaw composition\n");

    gevr_controls_init(&c);
    gevr_config_defaults(&cfg);
    base_input(&in);

    /* Desired heading is the snap-turned body plus wherever the head looks. */
    c.body_yaw = GEVR_DEG2RAD(90.0f);
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(30.0f));
    CHECK_NEAR(gevr_controls_desired_yaw(&c, &in), GEVR_DEG2RAD(120.0f), 1e-3f);

    /* And it wraps rather than running off past 180. */
    c.body_yaw = GEVR_DEG2RAD(170.0f);
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(30.0f));
    CHECK_NEAR(gevr_controls_desired_yaw(&c, &in), GEVR_DEG2RAD(-160.0f), 1e-2f);
}

/* --------------------------------------------------------------- camera */

static void test_camera(void)
{
    gevr_camera cam;
    gevr_config cfg;
    gevr_camera_params params;
    gevr_pose head, eye_l, eye_r;
    gevr_eye_view vl, vr;
    float fov[4];
    float sep;

    printf("stereo camera\n");

    gevr_config_defaults(&cfg);
    cfg.world_scale = 100.0f;
    gevr_camera_init(&cam, &cfg);

    memset(&params, 0, sizeof(params));
    params.eye_pos = gevr_v3(0.0f, 160.0f, 0.0f);

    head = gevr_pose_identity();
    head.position = gevr_v3(0.0f, 1.7f, 0.0f);

    eye_l = head;
    eye_l.position.x = -0.032f;
    eye_r = head;
    eye_r.position.x = 0.032f;

    fov[0] = GEVR_DEG2RAD(-50.0f);
    fov[1] = GEVR_DEG2RAD(45.0f);
    fov[2] = GEVR_DEG2RAD(45.0f);
    fov[3] = GEVR_DEG2RAD(-50.0f);

    gevr_camera_recenter(&cam, &head, 0.0f);
    gevr_camera_build_eye(&cam, &cfg, &params, &head, &eye_l, fov, &vl);
    gevr_camera_build_eye(&cam, &cfg, &params, &head, &eye_r, fov, &vr);

    CHECK(vl.valid && vr.valid);

    /* 64 mm of IPD at 100 units per metre is 6.4 game units of separation. */
    sep = vr.pose.position.x - vl.pose.position.x;
    CHECK_NEAR(sep, 6.4f, 0.05f);

    /* ipd_scale must actually scale the baseline. */
    cfg.ipd_scale = 0.5f;
    gevr_camera_build_eye(&cam, &cfg, &params, &head, &eye_l, fov, &vl);
    gevr_camera_build_eye(&cam, &cfg, &params, &head, &eye_r, fov, &vr);
    CHECK_NEAR(vr.pose.position.x - vl.pose.position.x, 3.2f, 0.05f);
    cfg.ipd_scale = 1.0f;

    /* The eye sits at the engine's camera height, not the raw headset height:
     * the engine owns where Bond's eyes are. */
    gevr_camera_build_eye(&cam, &cfg, &params, &head, &eye_l, fov, &vl);
    CHECK_NEAR(vl.pose.position.y, 160.0f, 0.5f);

    /* Physically crouching reads as a negative offset in game units. */
    {
        gevr_pose low = head;
        low.position.y = 1.2f;
        CHECK_NEAR(gevr_camera_crouch_offset(&cam, &cfg, &low), -50.0f, 1.0f);
    }

    /* Stepping sideways in the guardian moves the camera by the same distance
     * scaled into game units. */
    {
        gevr_pose stepped = head;
        gevr_vec3 off;
        stepped.position.x = 0.5f;
        off = gevr_camera_room_offset(&cam, &cfg, &stepped);
        CHECK_NEAR(off.x, 50.0f, 1.0f);
        CHECK_NEAR(off.y, 0.0f, 1e-3f);
    }

    /* The view matrix must actually invert the eye pose. */
    {
        gevr_mat4 m = gevr_mat4_from_pose(vl.pose);
        gevr_mat4 prod = gevr_mat4_mul(&vl.view, &m);
        CHECK_NEAR(prod.m[0], 1.0f, 1e-3f);
        CHECK_NEAR(prod.m[5], 1.0f, 1e-3f);
        CHECK_NEAR(prod.m[10], 1.0f, 1e-3f);
        CHECK_NEAR(prod.m[12], 0.0f, 1e-2f);
        CHECK_NEAR(prod.m[13], 0.0f, 1e-2f);
        CHECK_NEAR(prod.m[14], 0.0f, 1e-2f);
    }
}

/* --------------------------------------------------------------- config */

static void test_config(void)
{
    gevr_config cfg;

    printf("config\n");

    gevr_config_defaults(&cfg);
    CHECK(cfg.world_scale > 0.0f);
    CHECK(cfg.turn_mode == GEVR_TURN_SNAP);

    CHECK(gevr_config_set(&cfg, "snap_degrees", "45") == 0);
    CHECK_NEAR(cfg.snap_degrees, 45.0f, 1e-4f);

    CHECK(gevr_config_set(&cfg, "turn_mode", "smooth") == 0);
    CHECK(cfg.turn_mode == GEVR_TURN_SMOOTH);

    CHECK(gevr_config_set(&cfg, "aim_mode", "controller") == 0);
    CHECK(cfg.aim_mode == GEVR_AIM_CONTROLLER);

    CHECK(gevr_config_set(&cfg, "vignette_enabled", "off") == 0);
    CHECK(cfg.vignette_enabled == 0);

    /* Unknown keys are rejected but must not corrupt anything. */
    CHECK(gevr_config_set(&cfg, "not_a_real_key", "1") != 0);

    /* Out-of-range values get clamped rather than producing an unplayable
     * session. */
    gevr_config_set(&cfg, "snap_degrees", "9999");
    gevr_config_set(&cfg, "world_scale", "-5");
    gevr_config_validate(&cfg);
    CHECK(cfg.snap_degrees <= 90.0f);
    CHECK(cfg.world_scale >= 1.0f);

    /* Hysteresis must survive a backwards ini. */
    gevr_config_defaults(&cfg);
    cfg.snap_threshold = 0.5f;
    cfg.snap_release = 0.9f;
    gevr_config_validate(&cfg);
    CHECK(cfg.snap_release < cfg.snap_threshold);
}

static void test_bind_by_name(void)
{
    gevr_controls c;

    printf("bindings\n");

    gevr_controls_init(&c);
    CHECK(gevr_controls_bind_by_name(&c, "fire", "grip_r") == 0);
    CHECK(c.bind[GEVR_ACT_FIRE].source == GEVR_SRC_GRIP_R);

    /* Rebinding must not move the action to a different pad: fire is pad 1's
     * Z whatever button the player puts it on. */
    CHECK(c.bind[GEVR_ACT_FIRE].pad == GEVR_PAD_MOVE);
    CHECK(c.bind[GEVR_ACT_FIRE].n64_bit == GEVR_N64_Z);

    CHECK(gevr_controls_bind_by_name(&c, "fire", "nonsense") != 0);
    CHECK(gevr_controls_bind_by_name(&c, "nonsense", "grip_r") != 0);
}

static void test_menu_passthrough(void)
{
    gevr_controls c;
    gevr_config cfg;
    gevr_input_state in;
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];

    printf("menu passthrough\n");

    gevr_controls_init(&c);
    gevr_config_defaults(&cfg);
    memset(&game, 0, sizeof(game));
    game.menu_open = 1;

    /* With the watch menu up the left stick must drive the menu cursor on
     * pad 0, and the servo must not be fighting it. */
    base_input(&in);
    in.move_y = 1.0f;
    in.head.orientation = gevr_quat_yaw(GEVR_DEG2RAD(40.0f));
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK(pads[GEVR_PAD_AIM].stick_y > 60);

    /* Pause must still be reachable, or the player is stuck in the menu. */
    base_input(&in);
    in.buttons = GEVR_BTN_Y_LEFT;
    gevr_controls_update(&c, &cfg, &in, &game, pads, NULL);
    CHECK((pads[GEVR_PAD_AIM].buttons & GEVR_N64_START) != 0);
}


/* --------------------------------------------------- engine angle units */

static void test_engine_angles(void)
{
    printf("engine angle conversion\n");

    /* Wrapping matches the engine's own [0,360) loops. */
    CHECK_NEAR(gevr_wrap_360(370.0f), 10.0f, 1e-3f);
    CHECK_NEAR(gevr_wrap_360(-10.0f), 350.0f, 1e-3f);
    CHECK_NEAR(gevr_wrap_360(0.0f), 0.0f, 1e-3f);
    CHECK_NEAR(gevr_wrap_180(350.0f), -10.0f, 1e-3f);
    CHECK_NEAR(gevr_wrap_180(190.0f), -170.0f, 1e-3f);

    /* The sign flip is the whole point: the engine counts yaw clockwise,
     * the VR layer counts it counter-clockwise. */
    CHECK_NEAR(gevr_engine_yaw_to_vr(90.0f), GEVR_DEG2RAD(-90.0f), 1e-3f);
    CHECK_NEAR(gevr_engine_yaw_to_vr(270.0f), GEVR_DEG2RAD(90.0f), 1e-3f);
    CHECK_NEAR(gevr_engine_yaw_to_vr(0.0f), 0.0f, 1e-4f);

    /* Round trips both ways, including across the seam. */
    {
        int i;
        for (i = 0; i < 360; i += 7) {
            float deg = (float)i;
            float back = gevr_vr_yaw_to_engine(gevr_engine_yaw_to_vr(deg));
            CHECK_NEAR(gevr_wrap_180(back - deg), 0.0f, 1e-2f);
        }
        for (i = -170; i <= 180; i += 13) {
            float rad = GEVR_DEG2RAD((float)i);
            float back = gevr_engine_yaw_to_vr(gevr_vr_yaw_to_engine(rad));
            CHECK_NEAR(gevr_wrap_pi(back - rad), 0.0f, 1e-3f);
        }
    }

    /* Engine output is always in the range the engine itself maintains. */
    {
        int i;
        for (i = -400; i <= 400; i += 37) {
            float e = gevr_vr_yaw_to_engine(GEVR_DEG2RAD((float)i));
            CHECK(e >= 0.0f && e < 360.0f);
        }
    }

    /* Pitch: values above 180 are the engine's way of storing negatives. */
    CHECK_NEAR(gevr_engine_pitch_to_vr(350.0f), GEVR_DEG2RAD(-10.0f), 1e-3f);
    CHECK_NEAR(gevr_engine_pitch_to_vr(20.0f), GEVR_DEG2RAD(20.0f), 1e-3f);
    {
        int i;
        for (i = -80; i <= 80; i += 11) {
            float rad = GEVR_DEG2RAD((float)i);
            float back = gevr_engine_pitch_to_vr(gevr_vr_pitch_to_engine(rad));
            CHECK_NEAR(back, rad, 1e-3f);
        }
    }
}

int main(void)
{
    printf("gevr control-layer tests\n\n");

    test_encode_stick();
    test_encode_aim_stick();
    test_stick_shaping();
    test_wrap();
    test_quat_yaw();
    test_projection();
    test_pad_routing();
    test_snap_turn();
    test_servo_converges();
    test_invert_pitch_flips_output();
    test_body_yaw_composes();
    test_camera();
    test_config();
    test_bind_by_name();
    test_menu_passthrough();
    test_engine_angles();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
