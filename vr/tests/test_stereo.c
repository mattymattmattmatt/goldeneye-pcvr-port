/*
 * Tests for the per-eye projection substitution.
 *
 * The load-bearing property is a round trip: build a perspective matrix the
 * way the game does, recover its near and far planes, and get the same numbers
 * back. That is what lets the eye frustum inherit the game's own clipping
 * instead of a guess, and it fails loudly if the matrix convention is ever
 * misread -- which is exactly the mistake that produces a picture that looks
 * almost right and cannot be focused on.
 */
#include "gevr_stereo.h"

#include "gfx_stereo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The renderer is not linked here -- this exercises the projection maths, not
 * the install path -- so the one symbol gevr_stereo.c reaches for is stubbed.
 * Recording the argument keeps the stub honest: a test that silently accepted
 * a NULL install would not notice the seam never being fitted. */
static const struct GfxStereoHooks *g_installed;
void gfx_set_stereo_hooks(const struct GfxStereoHooks *h) { g_installed = h; }
int gfx_stereo_current_eye(void) { return 0; }

static int g_failures, g_checks;

static void check(int cond, const char *what, const char *file, int line)
{
    g_checks++;
    if (!cond) { g_failures++; printf("  FAIL %s:%d: %s\n", file, line, what); }
}
#define CHECK(c) check((c), #c, __FILE__, __LINE__)

static void check_near(float a, float b, float tol, const char *what,
                       const char *file, int line)
{
    float d = a - b; if (d < 0) d = -d;
    g_checks++;
    if (!(d <= tol)) {
        g_failures++;
        printf("  FAIL %s:%d: %s (%.5f vs %.5f)\n", file, line, what,
               (double)a, (double)b);
    }
}
#define CHECK_NEAR(a,b,t) check_near((a),(b),(t), #a " ~= " #b, __FILE__, __LINE__)

/* The matrix the game builds, in the same row-vector form guPerspectiveF
 * produces and fast3d consumes. */
static void game_perspective(float fovy, float aspect, float n, float f,
                             float m[4][4])
{
    float t = 1.0f / tanf(fovy * 0.5f);
    memset(m, 0, sizeof(float) * 16);
    m[0][0] = t / aspect;
    m[1][1] = t;
    m[2][2] = (f + n) / (n - f);
    m[2][3] = -1.0f;
    m[3][2] = (2.0f * f * n) / (n - f);
}

static void test_near_far_round_trip(void)
{
    const float cases[][2] = {
        { 10.0f, 1000.0f }, { 1.0f, 100.0f }, { 5.0f, 12000.0f },
        { 0.25f, 40.0f },   { 100.0f, 20000.0f },
    };
    printf("stereo: near/far survive a round trip through the matrix\n");

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        float m[4][4], n = 0, f = 0;
        game_perspective(1.05f, 4.0f / 3.0f, cases[i][0], cases[i][1], m);
        CHECK(gevr_stereo_recover_near_far(m, &n, &f) == 1);
        /* Relative tolerance: a far plane of 20000 cannot be checked to the
         * same absolute precision as a near plane of 0.25. */
        CHECK_NEAR(n / cases[i][0], 1.0f, 1e-3f);
        CHECK_NEAR(f / cases[i][1], 1.0f, 1e-3f);
    }
}

static void test_non_perspective_is_left_alone(void)
{
    float m[4][4];
    float n, f;

    printf("stereo: an orthographic matrix is declined, not mangled\n");

    /* Orthographic: no -1 in [2][3]. The HUD and 2D layers use these, and
     * giving them an off-axis frustum would put the interface at a different
     * depth in each eye. */
    memset(m, 0, sizeof(m));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
    CHECK(gevr_stereo_recover_near_far(m, &n, &f) == 0);

    /* Degenerate: a zero matrix must not be mistaken for anything. */
    memset(m, 0, sizeof(m));
    CHECK(gevr_stereo_recover_near_far(m, &n, &f) == 0);
}

static void test_frustum_is_asymmetric_and_recoverable(void)
{
    float m[4][4], n = 0, f = 0;

    printf("stereo: the eye frustum is off-axis and keeps its planes\n");

    /* A typical headset eye: wider toward the nose than away from it. */
    const float N = 10.0f, F = 5000.0f;
    float l = tanf(-0.90f) * N, r = tanf(0.75f) * N;
    float b = tanf(-0.85f) * N, t = tanf(0.85f) * N;

    gevr_stereo_build_frustum(l, r, b, t, N, F, m);

    /* Off-axis: the [2][0] term is what makes it asymmetric, and it must not
     * be zero or both eyes would see the same centred frustum. */
    CHECK(fabsf(m[2][0]) > 1e-4f);
    CHECK_NEAR(m[2][0], (r + l) / (r - l), 1e-5f);
    CHECK_NEAR(m[2][1], (t + b) / (t - b), 1e-5f);

    /* And the frustum this builds must be readable by the same recovery the
     * substitution relies on, or the second eye of a two-projection frame
     * would be measured against the wrong planes. */
    CHECK(gevr_stereo_recover_near_far(m, &n, &f) == 1);
    CHECK_NEAR(n / N, 1.0f, 1e-3f);
    CHECK_NEAR(f / F, 1.0f, 1e-3f);
}

static void test_symmetric_fov_matches_the_game(void)
{
    float g[4][4], e[4][4], n, f;

    printf("stereo: a symmetric eye reproduces the game's own matrix\n");

    /* With equal half-angles and the same aspect, the substituted frustum
     * should equal what guPerspectiveF built -- the substitution changes the
     * shape of the frustum, and nothing else. */
    const float N = 10.0f, F = 1000.0f, half = 0.5f, aspect = 4.0f / 3.0f;
    game_perspective(half * 2.0f, aspect, N, F, g);

    float t = tanf(half) * N;
    float r = t * aspect;
    gevr_stereo_build_frustum(-r, r, -t, t, N, F, e);

    CHECK(gevr_stereo_recover_near_far(g, &n, &f) == 1);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            CHECK_NEAR(e[i][j], g[i][j], 1e-4f);
        }
    }
}

int main(void)
{
    printf("ge007 VR: per-eye projection\n\n");
    test_near_far_round_trip();
    test_non_perspective_is_left_alone();
    test_frustum_is_asymmetric_and_recoverable();
    test_symmetric_fov_matches_the_game();

    /* Install and uninstall really do reach the renderer's seam. */
    printf("stereo: install and uninstall reach the renderer\n");
    {
        static gevr_stereo_ctx ctx;
        memset(&ctx, 0, sizeof(ctx));
        gevr_stereo_install(&ctx);
        CHECK(g_installed != NULL);
        gevr_stereo_uninstall();
        CHECK(g_installed == NULL);
    }
    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
