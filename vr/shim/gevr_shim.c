#include "gevr_shim.h"

#include "gevr_headaim.h"
#include "gevr_stereo.h"
#include "gevr_gl.h"

#ifdef GE_VR

#include "gevr_camera.h"
#include "gevr_config.h"
#include "gevr_controls.h"
#include "gevr_engine.h"
#include "gevr_gl.h"
#include "gevr_xr.h"

#include <stdio.h>
#include <string.h>

/* Engine headers. Only reached in the VR build, so the ROM target never sees
 * this include set at all. */
#include "bondconstants.h"
#include "bondtypes.h"
#include "game/bondview.h"
#include "game/player.h"   /* g_CurrentPlayer */
#include "game/lv.h"
#include "game/options.h"
#include "joy.h"

extern struct contdata *g_ContDataPtr;

/* The button bit values in gevr_input.h are duplicated from include/PR/os.h so
 * the VR layer and its headless tests do not need libultra. If the two ever
 * drift, the mapping silently sends the wrong buttons, so pin them here where
 * both headers are in scope. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(GEVR_N64_A     == A_BUTTON,     "A bit mismatch");
_Static_assert(GEVR_N64_B     == B_BUTTON,     "B bit mismatch");
_Static_assert(GEVR_N64_Z     == Z_TRIG,       "Z bit mismatch");
_Static_assert(GEVR_N64_START == START_BUTTON, "Start bit mismatch");
_Static_assert(GEVR_N64_L     == L_TRIG,       "L bit mismatch");
_Static_assert(GEVR_N64_R     == R_TRIG,       "R bit mismatch");
#endif

static struct {
    gevr_xr         *xr;
    gevr_config      cfg;
    gevr_controls    controls;
    gevr_camera      cam;

    gevr_headaim_state headaim;
    gevr_headaim_params headaim_p;

    /*
     * Written whole by the render thread at frame begin and read by both
     * threads. gevr_xr_begin_frame clears its output before refilling it, so
     * it is given a staging copy and this is assigned once, complete -- a game
     * thread reading mid-frame then sees one frame's input or the next, never
     * the zeroed gap in between, which would have read as tracking loss.
     */
    gevr_input_state input;

    /*
     * The game thread's own, fresher head pose (see refresh_head). Kept apart
     * from `input` so the two threads never write the same bytes: the render
     * thread owns `input`, the game thread owns this.
     */
    gevr_pose        head_live;
    int              head_live_valid;

    gevr_eye_target  eye_target[GEVR_EYE_COUNT];

    int              active;
    int              rendering;
    int              frame_open;   /* xrWaitFrame done for this game frame */
    int              current_eye;

    /*
     * Whether head aim wrote the engine's view angles on the most recent
     * game tick.
     *
     * The per-eye offset published to the renderer assumes the engine's view
     * rotation is the headset's, which is only true while head aim is driving
     * it. During menus, cutscenes and control locks the engine owns the camera
     * and the two diverge, so the 6DoF translation is withheld -- a positional
     * offset applied against a camera pointing somewhere else is the one that
     * actually makes people ill. Written on the game thread, read on the
     * render thread; a single int, and a frame either way is harmless.
     */
    int              headaim_driving;

    /*
     * Where the eye pass gets to, counted rather than inferred.
     *
     * The first trace said only that the eye targets were empty, which is
     * true whether the pass was never called, the runtime refused the
     * swapchain image, or the framebuffer came back incomplete. Three
     * counters and one error string separate those, so the next run names
     * the failure instead of narrowing it.
     *
     * Counted per frame, not cumulatively. A running total changes on every
     * frame, which defeats the log's print-on-change filter and buries the
     * one line that matters under thousands of identical ones; a per-frame
     * count is steady while the situation is steady, and reads directly as
     * "two eyes attempted, neither drawn".
     */
    unsigned         n_eye_call;   /* begin_eye_target entered          */
    unsigned         n_eye_acqf;   /* xrAcquireSwapchainImage refused   */
    unsigned         n_eye_fbof;   /* no usable framebuffer for the tex */
    unsigned         n_eye_ok;     /* bound, and the eye was drawn      */
    char             eye_err[128];
} g_vr;

int gevr_shim_active(void)
{
    return g_vr.active;
}

int gevr_shim_current_eye(void)
{
    return g_vr.current_eye;
}

int gevr_shim_init(void)
{
    gevr_xr_desc desc;

    memset(&g_vr, 0, sizeof(g_vr));
    g_vr.current_eye = GEVR_EYE_LEFT;

    gevr_config_defaults(&g_vr.cfg);
    gevr_config_load(&g_vr.cfg, "gevr.ini");
    gevr_config_validate(&g_vr.cfg);

    gevr_controls_init(&g_vr.controls);
    gevr_camera_init(&g_vr.cam, &g_vr.cfg);
    gevr_headaim_default_params(&g_vr.headaim_p);
    gevr_headaim_reset(&g_vr.headaim);

    memset(&desc, 0, sizeof(desc));
    desc.app_name = "GoldenEye 007 VR";
    desc.app_version = 1;
    desc.cfg = &g_vr.cfg;
    /* The game already owns the window and GL context by this point. */
    desc.create_window = 0;

    if (gevr_xr_create(&g_vr.xr, &desc) != 0) {
        fprintf(stderr, "[gevr] VR unavailable: %s\n",
                g_vr.xr ? gevr_xr_last_error(g_vr.xr) : "out of memory");
        gevr_xr_destroy(g_vr.xr);
        g_vr.xr = NULL;
        /* Not fatal. A missing headset should drop the player into the flat
         * game, not refuse to boot. */
        return 0;
    }

    g_vr.active = 1;
    fprintf(stderr, "[gevr] %s / %s\n",
            gevr_xr_runtime_name(g_vr.xr), gevr_xr_system_name(g_vr.xr));
    return 0;
}

void gevr_shim_shutdown(void)
{
    if (g_vr.xr) {
        gevr_xr_destroy(g_vr.xr);
        g_vr.xr = NULL;
    }
    g_vr.active = 0;
}

/* Reads the engine's current camera angles in the VR layer's convention. */
static void read_game_state(gevr_game_state *game)
{
    memset(game, 0, sizeof(*game));

    if (!g_CurrentPlayer) {
        return;
    }

    game->yaw = gevr_engine_yaw_to_vr(g_CurrentPlayer->vv_theta);
    game->pitch = gevr_engine_pitch_to_vr(g_CurrentPlayer->vv_verta);
    game->aim_mode = g_CurrentPlayer->insightaimmode ? 1 : 0;
    game->menu_open =
        (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0) ? 1 : 0;
    game->controls_locked = lvlGetControlsLockedFlag() ? 1 : 0;
}

/*
 * The XR frame lifecycle lives on ONE thread: the renderer's.
 *
 * It used to be opened lazily by whichever part of the game reached the VR
 * layer first, so that the poses would be fresh when the GAME read them rather
 * than when the renderer did. That deadlocked on real hardware, and the reason
 * is structural rather than a detail of any one runtime.
 *
 * This host runs the game tick on mainThread and the display list on
 * shedThread, and the GL context is bound on shedThread only (see
 * gfx_sdl_make_context_current). xrBeginFrame and xrEndFrame are frame-scoped
 * AND touch the graphics binding the session was created with -- on
 * VirtualDesktopXR the trace goes straight into glGetError -- so opening the
 * frame from the game thread called them with no context current, against a
 * frame the render thread was expected to close. Two frames rendered, then
 * mainThread blocked in xrBeginFrame while shedThread waited for a graphics
 * task that could no longer arrive.
 *
 * So begin and end both run from the render thread, around gfx_run. The lag
 * that motivated the lazy open is dealt with instead by gevr_xr_relocate_head,
 * which the game thread calls to re-predict the head pose for the frame it is
 * building. That call is not frame-scoped and touches no GL state, so it is
 * safe where the frame calls are not -- and being predicted one display period
 * ahead, it is fresher than the lazily-opened frame ever was.
 */
void gevr_shim_frame_begin(void)
{
    gevr_frame_status st;

    if (!g_vr.active || g_vr.frame_open) {
        return;
    }
    g_vr.frame_open = 1;

    g_vr.n_eye_call = 0;
    g_vr.n_eye_acqf = 0;
    g_vr.n_eye_fbof = 0;
    g_vr.n_eye_ok = 0;
    g_vr.eye_err[0] = '\0';

    st = gevr_xr_poll(g_vr.xr);
    if (st == GEVR_FRAME_EXIT) {
        gevr_shim_shutdown();
        return;
    }

    {
        gevr_input_state staged;

        st = gevr_xr_begin_frame(g_vr.xr, &staged);
        g_vr.input = staged;
    }
    g_vr.rendering = (st == GEVR_FRAME_RENDER);

    if (g_vr.input.head_valid) {
        /*
         * Automatic origin capture happens here and only here, so one thread
         * takes it. The game thread cannot get there first in any case:
         * gevr_xr_relocate_head has no time base to predict against until a
         * frame has completed its wait. (A player pressing recenter still
         * moves the origin from the game thread, by which point this has long
         * since stopped writing -- it is once-only.)
         */
        gevr_camera_capture_origin(&g_vr.cam, &g_vr.input.head);

        if (!g_vr.head_live_valid) {
            g_vr.head_live = g_vr.input.head;
            g_vr.head_live_valid = 1;
        }
    }
}

void gevr_shim_frame_end(void)
{
    if (!g_vr.active || !g_vr.frame_open) {
        g_vr.frame_open = 0;
        return;
    }
    gevr_xr_end_frame(g_vr.xr);
    g_vr.rendering = 0;
    g_vr.frame_open = 0;
}

/*
 * Bring the head pose up to date for the tick the game is running now.
 *
 * Called from the game thread, where the frame calls are off limits. Only the
 * head moves: the controller state in g_vr.input stays as the render thread's
 * xrSyncActions left it, one frame old. That asymmetry is deliberate -- a
 * stick or button a frame late is imperceptible, a horizon a frame late is
 * what makes a headset unpleasant, and syncing actions from two threads would
 * race over which sync the runtime honours.
 */
static void refresh_head(void)
{
    gevr_pose head;

    if (!g_vr.active) {
        return;
    }
    if (!gevr_xr_relocate_head(g_vr.xr, &head)) {
        /* No fresher pose available -- before the first frame, or tracking
         * momentarily lost. Keep whatever the last frame established rather
         * than invalidating it, so the view holds still instead of snapping. */
        return;
    }
    g_vr.head_live = head;
    g_vr.head_live_valid = 1;
}

int gevr_shim_head_aim(float *out_theta_deg, float *out_verta_deg)
{
    gevr_game_state game;
    gevr_euler head;
    float stick_x;

    if (!g_vr.active || !out_theta_deg || !out_verta_deg) {
        return 0;
    }
    if (g_vr.cfg.aim_mode != GEVR_AIM_HEAD) {
        g_vr.headaim_driving = 0;
        return 0;
    }
    refresh_head();
    if (!g_vr.head_live_valid) {
        /* Tracking lost. Leaving the angles alone freezes the view where it
         * was, which is the least alarming thing that can happen; writing a
         * stale or zeroed pose would snap the player somewhere they are not
         * looking. */
        g_vr.headaim_driving = 0;
        return 0;
    }

    read_game_state(&game);

    head = gevr_quat_to_euler(g_vr.head_live.orientation);

    /* The right stick turns the body. Reading the raw runtime axis rather than
     * the synthesised pads keeps head aim working even if the pad mapping is
     * reconfigured -- and the pads carry the aim axes, which head aim is
     * replacing outright. */
    stick_x = g_vr.input.turn_x;

    g_vr.headaim_driving =
        gevr_headaim_update(&g_vr.headaim, &g_vr.headaim_p, head, stick_x,
                            g_vr.input.dt,
                            game.menu_open || game.controls_locked,
                            out_theta_deg, out_verta_deg);
    return g_vr.headaim_driving;
}

int gevr_shim_alternate_eyes(void)
{
    return g_vr.cfg.alternate_eyes ? 1 : 0;
}

int gevr_shim_begin_eye_target(int eye, int *out_w, int *out_h)
{
    gevr_eye_target t;
    unsigned fbo;

    if (!g_vr.active || !g_vr.rendering || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return 0;
    }
    g_vr.n_eye_call++;

    if (gevr_xr_acquire_eye(g_vr.xr, eye, &t) != 0) {
        g_vr.n_eye_acqf++;
        snprintf(g_vr.eye_err, sizeof(g_vr.eye_err), "acquire: %s",
                 gevr_xr_last_error(g_vr.xr));
        return 0;
    }

    fbo = gevr_gl_framebuffer_for(t.gl_texture, t.gl_depth);
    if (!fbo) {
        /* Acquired but unusable: release it again rather than leaving the
         * swapchain image checked out, which would wedge the runtime on the
         * next frame. */
        g_vr.n_eye_fbof++;
        snprintf(g_vr.eye_err, sizeof(g_vr.eye_err), "fbo: %s (tex=%u depth=%u)",
                 gevr_gl_last_error(), t.gl_texture, t.gl_depth);
        gevr_xr_release_eye(g_vr.xr, eye);
        return 0;
    }

    gevr_gl_bind_framebuffer(fbo, t.width, t.height);
    g_vr.eye_target[eye] = t;
    g_vr.current_eye = eye;

    /* The renderer scales the frame to this, and it is not the window size.
     * Without it the game draws a window-sized image into one corner of the
     * eye and the headset shows mostly clear colour. */
    if (out_w) { *out_w = t.width; }
    if (out_h) { *out_h = t.height; }
    g_vr.n_eye_ok++;
    return 1;
}

void gevr_shim_end_eye_target(int eye)
{
    if (!g_vr.active || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }

    /* Before the release, while the image is still ours to write to. */
    if (g_vr.cfg.flip_eyes_y) {
        const gevr_eye_target *t = &g_vr.eye_target[eye];

        if (t->gl_texture && t->width > 0 && t->height > 0) {
            gevr_gl_flip_target(gevr_gl_framebuffer_for(t->gl_texture, t->gl_depth),
                                t->width, t->height);
        }
    }

    gevr_xr_release_eye(g_vr.xr, eye);
}

/* Supplied by the host; see port/src/vrhook.c. Declared rather than included
 * so the shim keeps no dependency on the host's headers. */
extern void gevr_shim_window_size(int *w, int *h);

void gevr_shim_blit_mirror(void)
{
    const gevr_eye_target *t;
    unsigned fbo;

    if (!g_vr.active || !g_vr.rendering) {
        return;
    }
    t = &g_vr.eye_target[GEVR_EYE_LEFT];
    if (!t->gl_texture || t->width <= 0 || t->height <= 0) {
        return;
    }

    /* Bind the window (framebuffer 0) and blit the left eye into it. The eye's
     * swapchain image has already been released by this point, but the texture
     * is still readable -- the runtime only reuses it a frame later, and the
     * blit is a read. */
    /* Same (colour, depth) key the eye pass used, so this reuses that
     * framebuffer instead of creating a second one per swapchain image -- which
     * is a different cache entry for the same texture, and on a runtime handing
     * out the maximum number of images would push the cache over. */
    fbo = gevr_gl_framebuffer_for(t->gl_texture, t->gl_depth);
    if (!fbo) {
        return;
    }
    /* Destination size comes from the host's own window rather than a new
     * config key: the window is whatever the player sized it to, and a
     * VR-specific mirror resolution would just be a second thing to keep in
     * sync with it. */
    {
        int dw = 0, dh = 0;

        gevr_shim_window_size(&dw, &dh);
        if (dw <= 0 || dh <= 0) {
            dw = t->width;
            dh = t->height;
        }
        gevr_gl_blit_mirror(fbo, t->width, t->height, dw, dh,
                            g_vr.cfg.flip_eyes_y);
    }
}

void gevr_shim_publish_eyes(struct gevr_stereo_ctx *ctx)
{
    int positional;
    int e;

    if (!ctx) {
        return;
    }
    if (!g_vr.active) {
        for (e = 0; e < GEVR_EYE_COUNT; e++) {
            ctx->eye[e].valid = 0;
        }
        return;
    }

    /*
     * Whether the 6DoF translation is safe to apply this frame.
     *
     * The offsets below are published in the head's frame, which is the view's
     * frame only while head aim is steering it. See gevr_camera_eye_offset.
     */
    positional = g_vr.input.head_valid && g_vr.headaim_driving;

    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        gevr_pose eye_pose;
        float     fov[4];
        gevr_vec3 off;

        /*
         * Straight from this frame's xrLocateViews rather than from the eye
         * targets. The targets are only filled when a swapchain image is
         * acquired, which happens partway through the walk -- reading them
         * here would publish the PREVIOUS frame's frustum for this frame's
         * geometry.
         */
        if (!gevr_xr_eye_view(g_vr.xr, e, &eye_pose, fov)) {
            ctx->eye[e].valid = 0;
            continue;
        }

        ctx->eye[e].fov[0] = fov[0];
        ctx->eye[e].fov[1] = fov[1];
        ctx->eye[e].fov[2] = fov[2];
        ctx->eye[e].fov[3] = fov[3];

        off = gevr_camera_eye_offset(&g_vr.cam, &g_vr.cfg, &g_vr.input.head,
                                     &eye_pose, positional);
        ctx->eye[e].offset.x = off.x;
        ctx->eye[e].offset.y = off.y;
        ctx->eye[e].offset.z = off.z;

        /* A zero-width frustum means the runtime has not reported this eye
         * yet; the hook leaves the game's own projection alone in that case. */
        ctx->eye[e].valid = (fov[1] > fov[0]) && (fov[2] > fov[3]);
    }
}

/*
 * A line for the log describing why the headset is or is not showing anything.
 *
 * The renderer-side facts and the runtime-side ones both matter and neither is
 * visible from the other: the runtime can be perfectly happy while the eye
 * passes are being skipped, and the passes can run while the runtime discards
 * every layer. Both ends go on one line.
 */
void gevr_shim_debug_line(char *buf, int len)
{
    char xrline[192];
    const gevr_eye_target *t;

    if (!buf || len <= 0) {
        return;
    }
    if (!g_vr.active) {
        snprintf(buf, (size_t)len, "inactive");
        return;
    }

    gevr_xr_debug_line(g_vr.xr, xrline, sizeof(xrline));
    t = &g_vr.eye_target[GEVR_EYE_LEFT];

    snprintf(buf, (size_t)len,
             "%s | rendering=%d headaim=%d head=%d tex0=%u %dx%d "
             "| eye call=%u ok=%u acqfail=%u fbofail=%u%s%s",
             xrline, g_vr.rendering, g_vr.headaim_driving,
             g_vr.head_live_valid, t->gl_texture, t->width, t->height,
             g_vr.n_eye_call, g_vr.n_eye_ok,
             g_vr.n_eye_acqf, g_vr.n_eye_fbof,
             g_vr.eye_err[0] ? " | " : "", g_vr.eye_err);
}

int gevr_shim_get_pads(OSContPad *out, int max)
{
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];
    gevr_haptic_request haptics;
    gevr_input_state in;
    int i;

    if (!g_vr.active || !out || max <= 0) {
        return 0;
    }
    refresh_head();

    /*
     * The frame's input with the head brought up to date.
     *
     * A copy rather than a reference: g_vr.input belongs to the render thread,
     * which may replace it at any point during this tick, and the control
     * mapping should see one consistent set of readings rather than half of
     * each. The sticks and buttons stay as the last xrSyncActions left them --
     * see refresh_head on why only the head is chased.
     */
    in = g_vr.input;
    if (g_vr.head_live_valid) {
        in.head = g_vr.head_live;
        in.head_valid = 1;
    }

    /*
     * No player yet, nothing to drive.
     *
     * joy.c polls the controllers from the moment the game starts, which is
     * well before a level exists and g_CurrentPlayer is set. The control-type
     * accessors below dereference it without checking -- options.c's
     * cur_player_get_control_type is a bare `return g_CurrentPlayer->...` --
     * so reaching them early is an access violation deep in the player struct,
     * which is exactly how this crashed on the first real run: a null read at
     * offset 0x36b0, a few frames after startup.
     *
     * read_game_state further down does guard, which is what made the bug easy
     * to miss: the guard was there, just after the two calls that needed it.
     * Returning no pads leaves joy.c holding the real controller state, which
     * is the right answer before the player exists.
     */
    if (!g_CurrentPlayer) {
        return 0;
    }

    /* The entire mapping assumes the engine is routing two pads the Goodhead
     * way. Forcing it every frame is cheap and stops a stray options-menu
     * change from silently breaking the controls mid-level. */
    if (cur_player_get_control_type() != CONTROLLER_CONFIG_GOODHEAD) {
        cur_player_set_control_type(CONTROLLER_CONFIG_GOODHEAD);
    }

    read_game_state(&game);
    gevr_controls_update(&g_vr.controls, &g_vr.cfg, &in, &game,
                         pads, &haptics);

    if (g_vr.cfg.haptics_scale > 0.0f) {
        gevr_xr_haptics(g_vr.xr, &haptics);
    }

    if (gevr_controls_take_recenter(&g_vr.controls)) {
        gevr_xr_recenter(g_vr.xr);
        gevr_camera_recenter(&g_vr.cam, &in.head, g_vr.controls.body_yaw);
        g_vr.controls.body_yaw = 0.0f;
    }
    g_vr.cam.body_yaw = g_vr.controls.body_yaw;

    for (i = 0; i < GEVR_PAD_COUNT && i < max; i++) {
        out[i].stick_x = pads[i].stick_x;
        out[i].stick_y = pads[i].stick_y;
        out[i].button = pads[i].buttons;
        /* Spelled `errno`, genuinely: OSContPad predates that being a
         * reserved name. Nothing here may include <errno.h>, or the macro
         * eats the member. */
        out[i].errno = 0;
    }
    return i;
}

#endif /* GE_VR */
