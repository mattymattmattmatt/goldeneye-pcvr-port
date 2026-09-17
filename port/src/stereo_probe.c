/*
 * stereo_probe.c - proves the per-eye render seam works in the real renderer.
 *
 * The unit test for the seam uses a stub backend, so it shows the interpreter
 * emits two eyes' worth of geometry and nothing about whether the real GL
 * path survives being driven twice in a frame. This closes that gap without a
 * headset: with GE_STEREO_PROBE=1 the seam is installed with a synthetic
 * "headset" that squeezes each eye into one half of the screen, so a single
 * frame dump shows the whole chain working or not.
 *
 * The squeeze is done in the projection rather than by setting a viewport,
 * because the display list sets its own viewport (gSPViewport) and would
 * overwrite anything bound in begin_eye. Scaling X in the projection is the
 * same path the real eye frustum takes, which is the point: it exercises what
 * ships, not a parallel route.
 *
 * Diagnostic only. Off unless the environment variable is set, and never
 * referenced by the VR path.
 */
#include "../fast3d/gfx_stereo.h"

#include <stdlib.h>
#include <string.h>

#include "system.h"

static int g_enabled;   /* 1 = inset probe, 2 = wide-frustum cull probe */
static int g_eyes_drawn[2];

static int probe_pass_count(void) { return 2; }
static int probe_eye_for_pass(int p) { return p; }

static int probe_begin_eye(int eye)
{
    if (eye >= 0 && eye < 2) {
        g_eyes_drawn[eye]++;
        /* Bounded: enough to see both eyes running on the first few frames
         * without drowning the log for the rest of the session. */
        if (g_eyes_drawn[eye] <= 3) {
            sysLogPrintf(LOG_NOTE, "GE_STEREO_PROBE: eye %d pass %d",
                         eye, g_eyes_drawn[eye]);
        }
    }
    return 1;
}

static void probe_end_eye(int eye) { (void)eye; }

static int g_adjust_count[2];

static void probe_adjust_projection(int eye, float m[4][4])
{
    int j;

    if (eye >= 0 && eye < 2) {
        g_adjust_count[eye]++;
        if (g_adjust_count[eye] <= 3) {
            sysLogPrintf(LOG_NOTE,
                         "GE_STEREO_PROBE: adjust eye %d #%d  m22=%.3f m23=%.3f",
                         eye, g_adjust_count[eye],
                         (double)m[2][2], (double)m[2][3]);
        }
    }
    /*
     * Eye 0 is left exactly as the game built it, and only eye 1 is
     * transformed. That makes a single frame self-contained: the untouched
     * view is the control and the shrunken copy drawn over it is the second
     * eye, so nothing has to be compared against a separate run.
     *
     * That matters more than it sounds. Two runs of this game do not reach the
     * same place at the same frame number -- the first attempt at this compared
     * frame 200 of one run against frame 200 of another and was looking at two
     * different rooms.
     *
     * Quarter size and pushed to the bottom-right, which no amount of ordinary
     * rendering would produce by accident.
     */
    if (eye == 0) {
        return;
    }

    if (g_enabled == 2) {
        /*
         * Cull-plane probe. Widens eye 1's frustum instead of shrinking it,
         * which is what a real headset does relative to the game's 4:3 view.
         *
         * The engine derives its frustum-cull plane normals and its fog/LOD
         * distance scale from the projection IT built (fr.c's D222 note:
         * currentPlayerSetPerspective -> c_perspfovy ->
         * currentPlayerSetCameraScale). Substituting a wider frustum at the
         * renderer does not tell the engine, so anything outside the nominal
         * frustum has already been culled before the renderer sees the list.
         *
         * If that is happening, widening here reveals *nothing new* at the
         * edges -- the extra field of view is filled with background rather
         * than the walls that are really there. That is the symptom to look
         * for, and it is why this is a separate mode: shrinking the frustum
         * (mode 1) can never show it.
         */
        /*
         * Widen, then inset. Both in one eye so the frame keeps its control:
         * eye 0 is the nominal view at full size, and the corner holds the
         * same scene through a frustum 1/0.55 wider. Overdrawing eye 0 with a
         * full-screen wide view instead would leave nothing to compare against
         * -- and comparing across runs does not work, because two runs are in
         * different rooms at the same frame number.
         *
         * The widen factor is GE_STEREO_WIDEN (default 0.55, i.e. 1/0.55 =
         * 1.8x the field of view), so the point at which culling starts to
         * show can be found by sweeping it rather than argued about.
         */
        {
            const char *wv = getenv("GE_STEREO_WIDEN");
            float widen = wv && *wv ? (float)atof(wv) : 0.55f;
            float k;

            if (!(widen > 0.01f)) { widen = 0.55f; }
            k = widen * 0.25f;
            for (j = 0; j < 4; j++) {
                m[j][0] *= k;
                m[j][1] *= k;
            }
        }
        for (j = 0; j < 4; j++) {
            m[j][0] += 0.70f * m[j][3];
            m[j][1] += -0.70f * m[j][3];
        }
        return;
    }

    for (j = 0; j < 4; j++) {
        m[j][0] *= 0.25f;
        m[j][1] *= 0.25f;
    }
    /* Shift in NDC: x_clip += s * w_clip, and w_clip rides on column 3. */
    for (j = 0; j < 4; j++) {
        m[j][0] += 0.70f * m[j][3];
        m[j][1] += -0.70f * m[j][3];
    }
}

static const struct GfxStereoHooks g_probe_hooks = {
    probe_pass_count, probe_eye_for_pass, probe_begin_eye, probe_end_eye,
    probe_adjust_projection,
};

void stereoProbeInit(void)
{
    const char *v = getenv("GE_STEREO_PROBE");

    if (!v || !*v || *v == '0') {
        return;
    }
    if (gfx_stereo_hooks_installed()) {
        /* VR came up first and owns the seam. Two sets of hooks cannot both
         * be installed, and a diagnostic must never win that race. */
        sysLogPrintf(LOG_INFO,
                     "GE_STEREO_PROBE ignored: VR is live and owns the "
                     "per-eye seam.");
        return;
    }
    g_enabled = atoi(v);
    if (g_enabled < 1) { g_enabled = 1; }
    gfx_set_stereo_hooks(&g_probe_hooks);
    sysLogPrintf(LOG_NOTE,
                 "GE_STEREO_PROBE=%d: per-eye seam installed (%s)", g_enabled,
                 g_enabled == 2 ? "eye 1 frustum WIDENED, cull-plane probe"
                                : "eye 1 as a quarter-size inset");
}

void stereoProbeReport(void)
{
    if (!g_enabled) {
        return;
    }
    sysLogPrintf(LOG_NOTE, "GE_STEREO_PROBE: eye passes drawn L=%d R=%d",
                 g_eyes_drawn[0], g_eyes_drawn[1]);
}
