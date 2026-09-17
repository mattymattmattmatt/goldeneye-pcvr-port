/*
 * vrhook.c - brings the VR layer up inside the host port.
 *
 * Three touch points, all no-ops without GE_VR:
 *
 *   vrHookInit()        after the GL context exists; starts OpenXR and, if it
 *                       comes up, installs the per-eye render seam.
 *   vrHookFrameEnd()    after the frame is presented; closes the XR frame.
 *
 * There is deliberately no vrHookFrameBegin. The XR frame is opened lazily by
 * whichever part of the game reaches the VR layer first -- the pad read in
 * joy.c, the head-aim query in MoveBond, or the renderer. That ordering
 * matters: joyPoll runs per frame before the tick and head aim writes
 * vv_theta during MoveBond, so the poses have to be fresh when the GAME reads
 * them. Opening the frame at render time instead would leave every pose one
 * frame behind the input that used it, which is the exact lag head aim exists
 * to remove.
 *
 * A runtime that fails to start is not fatal. The game carries on flat, which
 * is what someone without a headset plugged in should get.
 */
#include "../fast3d/gfx_stereo.h"

#include <string.h>

#include "system.h"

#ifdef GE_VR
#include "gevr_shim.h"
#include "gevr_stereo.h"
#include "gevr_config.h"
#endif

#ifdef GE_VR
static gevr_stereo_ctx g_stereo;
static int g_up;

static int stereo_begin_eye(void *user, int eye)
{
    (void)user;
    return gevr_shim_begin_eye_target(eye);
}

static void stereo_end_eye(void *user, int eye)
{
    (void)user;
    gevr_shim_end_eye_target(eye);
}
#endif

void vrHookInit(void)
{
#ifdef GE_VR
    if (gevr_shim_init() != 0 || !gevr_shim_active()) {
        sysLogPrintf(LOG_INFO,
                     "VR: no usable OpenXR runtime; running flat.");
        return;
    }

    memset(&g_stereo, 0, sizeof(g_stereo));
    g_stereo.active = 1;
    g_stereo.alternate_eyes = gevr_shim_alternate_eyes();
    g_stereo.begin_eye_cb = stereo_begin_eye;
    g_stereo.end_eye_cb = stereo_end_eye;
    gevr_stereo_install(&g_stereo);
    g_up = 1;

    sysLogPrintf(LOG_NOTE, "VR: up (%s eye rendering)",
                 g_stereo.alternate_eyes ? "alternate" : "full stereo");
#endif
}

void vrHookFrameEnd(void)
{
#ifdef GE_VR
    if (!g_up) {
        return;
    }
    /* Refresh the per-eye state the projection hook reads, then close the XR
     * frame. Done here rather than in the hook itself because the hook runs
     * inside the renderer's list walk, where calling into OpenXR would put
     * runtime calls in the middle of a draw. */
    /*
     * Put an eye on the desktop window before the frame closes.
     *
     * Without this the window shows nothing useful once VR is live: the eye
     * passes render into the swapchain framebuffers, and gfx_run's tail then
     * presents framebuffer 0, which nothing has drawn into. Someone watching
     * the monitor would reasonably conclude the game had hung.
     */
    gevr_shim_blit_mirror();

    gevr_shim_publish_eyes(&g_stereo);
    gevr_shim_frame_end();
    g_stereo.frame_parity++;
#endif
}

void vrHookShutdown(void)
{
#ifdef GE_VR
    if (!g_up) {
        return;
    }
    gevr_stereo_uninstall();
    gevr_shim_shutdown();
    g_up = 0;
#endif
}
