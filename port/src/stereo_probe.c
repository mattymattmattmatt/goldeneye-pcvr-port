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

static int g_enabled;
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
    g_enabled = 1;
    gfx_set_stereo_hooks(&g_probe_hooks);
    sysLogPrintf(LOG_NOTE,
                 "GE_STEREO_PROBE: per-eye seam installed; each eye is "
                 "squeezed into half the screen so one frame shows both.");
}

void stereoProbeReport(void)
{
    if (!g_enabled) {
        return;
    }
    sysLogPrintf(LOG_NOTE, "GE_STEREO_PROBE: eye passes drawn L=%d R=%d",
                 g_eyes_drawn[0], g_eyes_drawn[1]);
}
