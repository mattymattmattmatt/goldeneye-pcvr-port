/*
 * Tests for absolute head aim.
 *
 * The property that matters is the one the servo could not have: the engine's
 * angle must equal the headset's angle, not approach it. Several of these
 * would pass trivially for a servo given enough settling frames, so the ones
 * that matter check a *single* frame after a large, fast head movement --
 * which is exactly where a servo lags and where a headset makes people ill.
 */
#include "gevr_headaim.h"

#include <math.h>
#include <stdio.h>

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
#define CHECK(c) check((c), #c, __FILE__, __LINE__)

static void check_near(float a, float b, float tol,
                       const char *what, const char *file, int line)
{
    float d = a - b;
    if (d < 0.0f) { d = -d; }
    g_checks++;
    if (!(d <= tol)) {
        g_failures++;
        printf("  FAIL %s:%d: %s (%.4f vs %.4f, tol %.4f)\n",
               file, line, what, (double)a, (double)b, (double)tol);
    }
}
#define CHECK_NEAR(a, b, tol) check_near((a), (b), (tol), #a " ~= " #b, __FILE__, __LINE__)

#define DEG (3.14159265358979323846f / 180.0f)

static gevr_euler euler(float yaw_deg, float pitch_deg)
{
    gevr_euler e;
    e.yaw = yaw_deg * DEG;
    e.pitch = pitch_deg * DEG;
    e.roll = 0.0f;
    return e;
}

/* Head aim with the stick untouched: the engine angle must track the head
 * exactly, in one frame, however far it jumped. */
static void test_tracks_head_exactly_in_one_frame(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float theta, verta;
    int ok;

    printf("headaim: one frame is enough, however fast the head moved\n");

    gevr_headaim_default_params(&p);
    gevr_headaim_reset(&st);

    /* Establish straight ahead. */
    ok = gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.0f, 1.0f / 90.0f, 0,
                             &theta, &verta);
    CHECK(ok == 1);
    CHECK_NEAR(theta, 0.0f, 0.01f);

    /* A 45-degree snap of the head, in a single frame. A proportional servo
     * would emit a fraction of this and need many frames to arrive. */
    ok = gevr_headaim_update(&st, &p, euler(45.0f, 0.0f), 0.0f, 1.0f / 90.0f, 0,
                             &theta, &verta);
    CHECK(ok == 1);
    /* The engine counts yaw the other way round (gevr_engine.c), so 45 degrees
     * of head-left is 315 in vv_theta. Asserting the converted value rather
     * than the raw one keeps this test honest about what actually gets
     * written. */
    CHECK_NEAR(theta, 315.0f, 0.01f);

    /* And straight back again, same frame budget. */
    ok = gevr_headaim_update(&st, &p, euler(-30.0f, 0.0f), 0.0f, 1.0f / 90.0f, 0,
                             &theta, &verta);
    CHECK(ok == 1);
    CHECK_NEAR(theta, 30.0f, 0.01f);
}

/* Holding the head still while the stick turns must move the body and keep the
 * head offset applied on top of it. */
static void test_stick_turns_body_under_the_head(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float theta, verta;

    printf("headaim: the stick turns the body, the head offsets it\n");

    gevr_headaim_default_params(&p);
    p.turn_rate = 90.0f * DEG;      /* 90 deg/s, exact for round numbers */
    p.stick_deadzone = 0.0f;
    gevr_headaim_reset(&st);

    gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.0f, 0.0f, 0, &theta, &verta);
    CHECK_NEAR(theta, 0.0f, 0.01f);

    /* Full deflection for one second = 90 degrees of body turn. */
    gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 1.0f, 1.0f, 0, &theta, &verta);
    CHECK_NEAR(theta, 270.0f, 0.01f);          /* 90 left == 270 in engine yaw */

    /* Now add 20 degrees of neck on top, with the stick released. */
    gevr_headaim_update(&st, &p, euler(20.0f, 0.0f), 0.0f, 0.0f, 0, &theta, &verta);
    CHECK_NEAR(theta, 250.0f, 0.01f);
}

/* Pitch is clamped to the engine's own limits rather than allowed to run to
 * the player's neck limit. */
static void test_pitch_clamped(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float theta, verta;

    printf("headaim: pitch stops where the engine's own clamp stops\n");

    gevr_headaim_default_params(&p);
    gevr_headaim_reset(&st);

    gevr_headaim_update(&st, &p, euler(0.0f, 89.0f), 0.0f, 0.0f, 0, &theta, &verta);
    CHECK_NEAR(verta, 80.0f, 0.01f);

    gevr_headaim_update(&st, &p, euler(0.0f, -89.0f), 0.0f, 0.0f, 0, &theta, &verta);
    CHECK_NEAR(verta, -80.0f, 0.01f);

    /* Not wrapped into [0,360): the engine keeps verta signed. */
    CHECK(verta < 0.0f);
}

/* While the engine owns the camera, nothing is written -- but the stick still
 * accumulates, so control resumes facing where the player steered. */
static void test_engine_control_is_respected(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float theta = -1.0f, verta = -1.0f;
    int ok;

    printf("headaim: a scripted camera is left alone\n");

    gevr_headaim_default_params(&p);
    p.turn_rate = 90.0f * DEG;
    p.stick_deadzone = 0.0f;
    gevr_headaim_reset(&st);

    ok = gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 1.0f, 1.0f, 1,
                             &theta, &verta);
    CHECK(ok == 0);
    CHECK(theta == -1.0f);      /* untouched */

    /* Control returns: the second of stick turning still counted. */
    ok = gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.0f, 0.0f, 0,
                             &theta, &verta);
    CHECK(ok == 1);
    CHECK_NEAR(theta, 270.0f, 0.01f);
}

/* Recentring moves where "straight ahead" sits on the neck without spinning
 * the character. */
static void test_recentre_does_not_spin_the_player(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float before, after, verta;

    printf("headaim: recentring moves the neck reference, not the player\n");

    gevr_headaim_default_params(&p);
    gevr_headaim_reset(&st);

    gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.0f, 0.0f, 0, &before, &verta);

    /* Player has twisted 30 degrees in their chair and recentres there. */
    gevr_headaim_update(&st, &p, euler(30.0f, 0.0f), 0.0f, 0.0f, 0, &before, &verta);
    CHECK_NEAR(before, 330.0f, 0.01f);

    gevr_headaim_recentre(&st, euler(30.0f, 0.0f));

    /* Same physical head position, but it is now straight ahead: the view must
     * not have moved. */
    gevr_headaim_update(&st, &p, euler(30.0f, 0.0f), 0.0f, 0.0f, 0, &after, &verta);
    CHECK_NEAR(after, before, 0.01f);

    /* And turning the neck from the new reference behaves normally. */
    gevr_headaim_update(&st, &p, euler(40.0f, 0.0f), 0.0f, 0.0f, 0, &after, &verta);
    CHECK_NEAR(after, before - 10.0f, 0.01f);  /* engine yaw runs the other way */
}

/* Whatever the head does, vv_theta must stay inside the range the engine
 * normalises to, and vv_verta must stay signed around zero rather than
 * wrapping -- the distinction the pitch conversion exists to preserve. */
static void test_outputs_stay_in_engine_range(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    int i;

    printf("headaim: outputs stay in the ranges the engine expects\n");

    gevr_headaim_default_params(&p);
    p.stick_deadzone = 0.0f;
    gevr_headaim_reset(&st);

    for (i = -720; i <= 720; i += 13) {
        float theta, verta;
        float pitch_deg = (float)((i % 170) - 85);

        gevr_headaim_update(&st, &p, euler((float)i, pitch_deg),
                            (i & 1) ? 1.0f : -1.0f, 1.0f / 90.0f, 0,
                            &theta, &verta);

        CHECK(theta >= 0.0f && theta < 360.0f);
        /* Signed, and never the 356-instead-of--4 form that would defeat the
         * engine's pitch clamp. */
        CHECK(verta > -180.0f && verta <= 180.0f);
        CHECK(verta >= -85.0f && verta <= 85.0f);
    }
}

/* The deadzone must not cause a step: just past it, the turn starts from zero. */
static void test_deadzone_has_no_step(void)
{
    gevr_headaim_state st;
    gevr_headaim_params p;
    float a, b, verta;

    printf("headaim: the stick deadzone does not step\n");

    gevr_headaim_default_params(&p);
    p.turn_rate = 90.0f * DEG;
    p.stick_deadzone = 0.2f;
    gevr_headaim_reset(&st);

    gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.2f, 1.0f, 0, &a, &verta);
    CHECK_NEAR(a, 0.0f, 0.01f);                 /* at the edge: still still */

    gevr_headaim_update(&st, &p, euler(0.0f, 0.0f), 0.201f, 1.0f, 0, &b, &verta);
    /* Just past the edge: a crawl, not a jump. Engine yaw decreases as the VR
     * yaw increases, so a tiny left turn lands just below 360. */
    CHECK(b > 359.8f && b < 360.0f);
}

int main(void)
{
    printf("ge007 VR: absolute head aim\n\n");

    test_tracks_head_exactly_in_one_frame();
    test_stick_turns_body_under_the_head();
    test_pitch_clamped();
    test_engine_control_is_respected();
    test_recentre_does_not_spin_the_player();
    test_outputs_stay_in_engine_range();
    test_deadzone_has_no_step();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
