/*
 * vrhook.c - brings the VR layer up inside the host port.
 *
 * Four touch points, all no-ops without GE_VR:
 *
 *   vrHookInit()        after the GL context exists; starts OpenXR and, if it
 *                       comes up, installs the per-eye render seam.
 *   vrHookFrameBegin()  on the render thread, before the display list runs;
 *                       waits on the compositor and opens the XR frame.
 *   vrHookMirror()      with the composited frame still in the back buffer,
 *                       immediately before the desktop swap.
 *   vrHookFrameEnd()    on the render thread, after the display list; submits
 *                       the eyes and closes the XR frame.
 *
 * Begin and end are both on the render thread on purpose. An XR frame is a
 * strictly paired begin/end on the thread holding the graphics context, and
 * this host binds that context on shedThread only. An earlier version opened
 * the frame lazily from whichever caller reached the VR layer first, so that
 * poses were fresh when the game read them rather than when the renderer did;
 * on real hardware that put xrBeginFrame on mainThread with no context current
 * and deadlocked after two frames. gevr_xr_relocate_head now covers the
 * freshness the lazy open was for, without the frame calls.
 *
 * A runtime that fails to start is not fatal. The game carries on flat, which
 * is what someone without a headset plugged in should get.
 */
#include "../fast3d/gfx_stereo.h"

#include <stdio.h>
#include <string.h>

#include "system.h"

#ifdef GE_VR
#include "gevr_shim.h"
#include "gevr_stereo.h"
#include "gevr_config.h"
#endif

void vrHookShutdown(void);

#ifdef GE_VR
static gevr_stereo_ctx g_stereo;
static int g_up;

/*
 * Shutdown handshake with the host's quit path.
 *
 * The host quits by calling exit(0) from its event thread, which tears the
 * window and the GL context down under the render thread. If that thread is
 * inside xrEndFrame at the time, the runtime is left submitting against a
 * context that is being destroyed, and it blocks there for good -- which is
 * what happened on the first run that got this far: the game closed, and
 * shedThread never came back out of xrEndFrame.
 *
 * So the quit path asks first (vrHookRequestStop), and waits for the render
 * thread to finish whatever frame it is in (vrHookBusy) before exiting.
 * volatile rather than atomic to match the rest of this port's cross-thread
 * flags; these are single ints written by one thread and polled by another.
 */
static volatile int g_stopping;
static volatile int g_in_frame;

static int stereo_begin_eye(void *user, int eye, int *out_w, int *out_h)
{
    (void)user;
    return gevr_shim_begin_eye_target(eye, out_w, out_h);
}

static void stereo_end_eye(void *user, int eye)
{
    (void)user;
    gevr_shim_end_eye_target(eye);
}
#endif

#ifdef GE_VR
/*
 * Logs what the runtime and the renderer are each doing, for the first few
 * frames and then only when it changes.
 *
 * Deliberately not gated behind an env var. The failure this exists for --
 * frames rendering happily into a headset that shows nothing -- gives no other
 * outward sign, and asking someone to reproduce it a second time with tracing
 * switched on costs more than these lines do.
 */
static void vrTraceFrame(void)
{
    static char prev[256];
    static char prev_profile[256];
    static int frames;
    static int input_seen;
    char now[256];

    gevr_shim_debug_line(now, (int)sizeof(now));

    if (frames < 5 || strcmp(now, prev) != 0) {
        sysLogPrintf(LOG_NOTE, "VR: %s", now);
        memcpy(prev, now, sizeof(prev));
    }
    if (frames < 1000) {
        frames++;
    }

    /*
     * The controllers get two lines, once each, and then never again.
     *
     * They cannot go in the line above: the stick readings change every frame,
     * so they would defeat its print-on-change filter and bury it. And they
     * have to be said at all, because every way this chain can fail looks the
     * same from outside -- a rejected binding list, an action set that was
     * never attached, a runtime that picked a profile we did not suggest, and
     * a controller that is genuinely switched off all produce a game that
     * ignores the controller in exactly the same silence.
     *
     * The first is the profile the runtime settled on, whenever it changes;
     * the second is the first moment any control is actually used, which
     * proves the whole path in one line.
     */
    {
        char line[256];
        int active = gevr_shim_input_line(line, (int)sizeof(line));

        if (strcmp(line, prev_profile) != 0) {
            const char *p = strstr(line, "profile=");
            if (p && strcmp(p, prev_profile) != 0) {
                sysLogPrintf(LOG_NOTE, "VR: controller %s", p);
                snprintf(prev_profile, sizeof(prev_profile), "%s", p);
            }
        }
        if (active && !input_seen) {
            input_seen = 1;
            sysLogPrintf(LOG_NOTE, "VR: first controller input -- %s", line);
        }
    }
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
    /* One REJECTED here and that controller has no bindings at all. The call
     * is all-or-nothing per profile, and it is otherwise silent. */
    sysLogPrintf(LOG_NOTE, "VR: bindings %s", gevr_shim_binding_summary());
#endif
}

void vrHookFrameBegin(void)
{
#ifdef GE_VR
    if (!g_up) {
        return;
    }
    /*
     * Wait on the compositor, open the frame, sync actions and locate the
     * views -- then publish the per-eye state the projection hook will read
     * during the walk.
     *
     * Publishing here rather than from the hook itself is what keeps OpenXR
     * calls out of the middle of a draw: gfx_run's adjust_projection callback
     * fires per G_MTX_PROJECTION, inside the list walk.
     *
     * The eye poses come from xrLocateViews, which gevr_xr_begin_frame has
     * just done, so they are available before the first eye is acquired.
     */
    if (g_stopping) {
        return;
    }
    g_in_frame = 1;

    gevr_shim_frame_begin();

    /*
     * If the runtime went away -- headset off the head long enough, Virtual
     * Desktop disconnected, the session exited -- fall back to flat instead of
     * to nothing.
     *
     * Leaving the seam installed would be worse than useless: pass_count still
     * asks for two eye passes, begin_eye declines both now that the shim is
     * inactive, and every pass is skipped. The player gets a black window and
     * no indication why. Uninstalling restores the single unhooked pass, which
     * is the flat game.
     */
    if (!gevr_shim_active()) {
        sysLogPrintf(LOG_NOTE, "VR: runtime gone; falling back to flat.");
        vrHookShutdown();
        g_in_frame = 0;
        return;
    }

    gevr_shim_publish_eyes(&g_stereo);
#endif
}

void vrHookMirror(void)
{
#ifdef GE_VR
    if (!g_up) {
        return;
    }
    /*
     * Put an eye on the desktop window.
     *
     * Without this the window shows nothing useful once VR is live: the eye
     * passes render into the swapchain framebuffers, and gfx_run's tail then
     * presents framebuffer 0, which nothing has drawn into. Someone watching
     * the monitor would reasonably conclude the game had hung.
     *
     * It has to run from the pre-swap hook rather than after gfx_run, because
     * gfx_run swaps at its own tail (gfx_wapi->swap_buffers_begin). Blitting
     * afterwards drew into the NEXT frame's back buffer, which the next
     * gfx_run then cleared -- so the mirror was never once presented.
     */
    gevr_shim_blit_mirror();
#endif
}

void vrHookFrameEnd(void)
{
#ifdef GE_VR
    if (!g_up) {
        return;
    }
    gevr_shim_frame_end();
    g_stereo.frame_parity++;
    g_in_frame = 0;

    /* After the walk, so it reports the frame that just rendered rather than
     * the one before it -- which is what made the first trace read as though
     * the eye targets were permanently empty. */
    vrTraceFrame();
#endif
}

/* Asks the VR layer to stop opening frames. Safe from any thread. */
void vrHookRequestStop(void)
{
#ifdef GE_VR
    g_stopping = 1;
#endif
}

/* Whether the render thread is currently inside an XR frame. */
int vrHookBusy(void)
{
#ifdef GE_VR
    return g_in_frame;
#else
    return 0;
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
