/*
 * gevr_stereo.c - the VR side of fast3d's per-eye render seam.
 *
 * fast3d calls out through GfxStereoHooks once per eye; this supplies the
 * answers. Everything here is policy, which is why it lives in vr/ and the
 * renderer only holds the five function pointers.
 *
 * The projection is the interesting part. The game hands the renderer a
 * perspective matrix it built for a flat screen, and what the headset needs is
 * an off-axis frustum with the eye's own half-angles. Rather than have the
 * game build the VR matrix -- which it cannot, since the game builds its list
 * once and knows nothing about eyes -- the frustum is substituted here, at the
 * moment the interpreter loads it, once per eye.
 *
 * Near and far are recovered from the matrix the game supplied rather than
 * assumed. The game changes them (sniper zoom, the watch, cutscene cameras),
 * and a hard-coded pair would clip the world differently from the way the game
 * intended in exactly those cases.
 */
#include "gevr_stereo.h"

#include "gevr_camera.h"
#include "gevr_config.h"
#include "gevr_math.h"
#include "gevr_xr.h"

#include "gfx_stereo.h"

#include <math.h>
#include <string.h>

static gevr_stereo_ctx *g_ctx;

/* ------------------------------------------------------------- projection */

/*
 * Recover the near and far planes from a row-vector perspective matrix.
 *
 * For the form the game builds:
 *   m[2][2] = (F+N)/(N-F)      m[3][2] = 2FN/(N-F)
 * so with A = m[2][2] and B = m[3][2]:
 *   N = B/(A-1)                F = B/(A+1)
 *
 * Returns 0 if the matrix is not a perspective projection of that form -- an
 * orthographic one, say, which the game uses for some 2D layers. Those are
 * left alone: an off-axis frustum applied to a HUD would put the interface at
 * a different depth in each eye and make it impossible to focus on.
 */
static int recover_near_far(const float m[4][4], float *out_n, float *out_f)
{
    float a = m[2][2];
    float b = m[3][2];

    /* Perspective has -1 in [2][3]; orthographic has 0 there and 1 in [3][3]. */
    if (m[2][3] > -0.5f) {
        return 0;
    }
    if (fabsf(a - 1.0f) < 1e-6f || fabsf(a + 1.0f) < 1e-6f) {
        return 0;
    }

    *out_n = b / (a - 1.0f);
    *out_f = b / (a + 1.0f);

    if (!(*out_n > 0.0f) || !(*out_f > *out_n)) {
        return 0;
    }
    return 1;
}

/* glFrustum in the row-vector convention the game and fast3d use. */
static void build_frustum(float l, float r, float b, float t,
                          float n, float f, float m[4][4])
{
    memset(m, 0, sizeof(float) * 16);
    m[0][0] = (2.0f * n) / (r - l);
    m[1][1] = (2.0f * n) / (t - b);
    m[2][0] = (r + l) / (r - l);
    m[2][1] = (t + b) / (t - b);
    m[2][2] = (f + n) / (n - f);
    m[2][3] = -1.0f;
    m[3][2] = (2.0f * f * n) / (n - f);
}

static void adjust_projection(int eye, float m[4][4])
{
    float n, f, l, r, b, t;
    const float *fov;

    if (!g_ctx || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }
    if (!g_ctx->eye[eye].valid) {
        return;
    }
    if (!recover_near_far(m, &n, &f)) {
        return;   /* not a perspective projection; leave it alone */
    }

    /* OpenXR half-angles: left and down are negative, so tan() carries the
     * sign and the frustum comes out asymmetric on its own. */
    fov = g_ctx->eye[eye].fov;
    l = tanf(fov[0]) * n;
    r = tanf(fov[1]) * n;
    t = tanf(fov[2]) * n;
    b = tanf(fov[3]) * n;

    if (!(r > l) || !(t > b)) {
        return;   /* nonsense from the runtime; better the flat frustum */
    }

    build_frustum(l, r, b, t, n, f, m);

    /*
     * The eye offset is a view-space translation, so it belongs in the
     * projection here rather than in the view matrix: with row vectors,
     * v * MV * T(-e) * P is the same as v * MV * (T(-e) * P), and folding it
     * into the matrix we are already replacing costs nothing.
     *
     * Only the offset of the eye from the head is used. The head's own
     * rotation is already in the engine's view matrix, because head aim wrote
     * it into vv_theta/vv_verta; taking the full eye pose here would apply
     * that rotation a second time.
     */
    {
        gevr_vec3 e = g_ctx->eye[eye].offset;
        int j;

        for (j = 0; j < 4; j++) {
            m[3][j] += -e.x * m[0][j] + -e.y * m[1][j] + -e.z * m[2][j];
        }
    }
}

/* ------------------------------------------------------------------ passes */

static int pass_count(void)
{
    if (!g_ctx || !g_ctx->active) {
        return 1;
    }
    /*
     * Alternate-eye rendering draws one eye per frame and leaves the other
     * eye's image standing in the compositor, which halves the render cost for
     * a half-frame disparity between the eyes. It is the reason the R.E.A.L.
     * mods can run demanding games in a headset at all. Here the game is
     * cheap, so full stereo is the default and this is the fallback for
     * hardware that cannot hold the frame rate.
     */
    return g_ctx->alternate_eyes ? 1 : GEVR_EYE_COUNT;
}

static int eye_for_pass(int pass)
{
    if (!g_ctx || !g_ctx->alternate_eyes) {
        return pass;
    }
    return g_ctx->frame_parity & 1;
}

static int begin_eye(int eye, int *out_w, int *out_h)
{
    if (!g_ctx || !g_ctx->active || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return 0;
    }
    if (!g_ctx->begin_eye_cb) {
        return 0;
    }
    return g_ctx->begin_eye_cb(g_ctx->user, eye, out_w, out_h);
}

static void end_eye(int eye)
{
    if (!g_ctx || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }
    if (g_ctx->end_eye_cb) {
        g_ctx->end_eye_cb(g_ctx->user, eye);
    }
}

static const struct GfxStereoHooks g_hooks = {
    pass_count, eye_for_pass, begin_eye, end_eye, adjust_projection,
};

void gevr_stereo_install(gevr_stereo_ctx *ctx)
{
    g_ctx = ctx;
    gfx_set_stereo_hooks(ctx ? &g_hooks : NULL);
}

void gevr_stereo_uninstall(void)
{
    g_ctx = NULL;
    gfx_set_stereo_hooks(NULL);
}

int gevr_stereo_recover_near_far(const float m[4][4], float *n, float *f)
{
    return recover_near_far(m, n, f);
}

void gevr_stereo_build_frustum(float l, float r, float b, float t,
                               float n, float f, float m[4][4])
{
    build_frustum(l, r, b, t, n, f, m);
}
