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

    gevr_input_state input;
    gevr_eye_view    eye_view[GEVR_EYE_COUNT];
    gevr_eye_target  eye_target[GEVR_EYE_COUNT];

    int              active;
    int              rendering;
    int              frame_open;   /* xrWaitFrame done for this game frame */
    int              current_eye;
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
 * Opening the frame is idempotent and can be triggered from either end.
 *
 * The poses have to be fresh when the GAME reads them, not when the renderer
 * does: joyPoll runs per frame before the tick, head aim writes vv_theta
 * during MoveBond, and the display list is built from that. If the frame were
 * only opened at render time, every pose would be one frame behind the input
 * that used it -- which is precisely the lag head aim exists to remove.
 *
 * Rather than guess at the host's thread and call order, whichever entry point
 * is reached first in a frame opens it: the pad read, the head-aim query, or
 * the renderer. gevr_shim_frame_end closes it.
 */
static void ensure_frame_open(void)
{
    gevr_frame_status st;

    if (!g_vr.active || g_vr.frame_open) {
        return;
    }
    g_vr.frame_open = 1;

    st = gevr_xr_poll(g_vr.xr);
    if (st == GEVR_FRAME_EXIT) {
        gevr_shim_shutdown();
        return;
    }

    st = gevr_xr_begin_frame(g_vr.xr, &g_vr.input);
    g_vr.rendering = (st == GEVR_FRAME_RENDER);
}

void gevr_shim_frame_begin(void)
{
    ensure_frame_open();
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

int gevr_shim_head_aim(float *out_theta_deg, float *out_verta_deg)
{
    gevr_game_state game;
    gevr_euler head;
    float stick_x;

    if (!g_vr.active || !out_theta_deg || !out_verta_deg) {
        return 0;
    }
    if (g_vr.cfg.aim_mode != GEVR_AIM_HEAD) {
        return 0;
    }
    ensure_frame_open();
    if (!g_vr.input.head_valid) {
        /* Tracking lost. Leaving the angles alone freezes the view where it
         * was, which is the least alarming thing that can happen; writing a
         * stale or zeroed pose would snap the player somewhere they are not
         * looking. */
        return 0;
    }

    read_game_state(&game);

    head = gevr_quat_to_euler(g_vr.input.head.orientation);

    /* The right stick turns the body. Reading the raw runtime axis rather than
     * the synthesised pads keeps head aim working even if the pad mapping is
     * reconfigured -- and the pads carry the aim axes, which head aim is
     * replacing outright. */
    stick_x = g_vr.input.turn_x;

    return gevr_headaim_update(&g_vr.headaim, &g_vr.headaim_p, head, stick_x,
                               g_vr.input.dt,
                               game.menu_open || game.controls_locked,
                               out_theta_deg, out_verta_deg);
}

int gevr_shim_alternate_eyes(void)
{
    return g_vr.cfg.alternate_eyes ? 1 : 0;
}

int gevr_shim_begin_eye_target(int eye)
{
    gevr_eye_target t;
    unsigned fbo;

    if (!g_vr.active || !g_vr.rendering || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return 0;
    }
    if (gevr_xr_acquire_eye(g_vr.xr, eye, &t) != 0) {
        return 0;
    }

    fbo = gevr_gl_framebuffer_for(t.gl_texture, t.gl_depth);
    if (!fbo) {
        /* Acquired but unusable: release it again rather than leaving the
         * swapchain image checked out, which would wedge the runtime on the
         * next frame. */
        gevr_xr_release_eye(g_vr.xr, eye);
        return 0;
    }

    gevr_gl_bind_framebuffer(fbo, t.width, t.height);
    g_vr.eye_target[eye] = t;
    g_vr.current_eye = eye;
    return 1;
}

void gevr_shim_end_eye_target(int eye)
{
    if (!g_vr.active || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
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
    fbo = gevr_gl_framebuffer_for(t->gl_texture, 0);
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
        gevr_gl_blit_mirror(fbo, t->width, t->height, dw, dh);
    }
}

void gevr_shim_publish_eyes(struct gevr_stereo_ctx *ctx)
{
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

    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        const gevr_eye_target *t = &g_vr.eye_target[e];
        gevr_vec3 d;

        ctx->eye[e].fov[0] = t->fov[0];
        ctx->eye[e].fov[1] = t->fov[1];
        ctx->eye[e].fov[2] = t->fov[2];
        ctx->eye[e].fov[3] = t->fov[3];

        /*
         * Offset of the eye from the head, in game units.
         *
         * Only the translation is taken. The head's rotation is already in the
         * engine's view matrix, because head aim wrote it into vv_theta and
         * vv_verta; using the eye's full pose here would turn the world twice.
         *
         * ipd_scale below 1 pulls the eyes together, which flattens the stereo
         * effect. Some players want that at N64 scale, where true separation
         * can make the world read as a diorama.
         */
        d.x = t->pose.position.x - g_vr.input.head.position.x;
        d.y = t->pose.position.y - g_vr.input.head.position.y;
        d.z = t->pose.position.z - g_vr.input.head.position.z;

        ctx->eye[e].offset.x = d.x * g_vr.cfg.world_scale * g_vr.cfg.ipd_scale;
        ctx->eye[e].offset.y = d.y * g_vr.cfg.world_scale * g_vr.cfg.ipd_scale;
        ctx->eye[e].offset.z = d.z * g_vr.cfg.world_scale * g_vr.cfg.ipd_scale;

        /* A zero-width frustum means the runtime has not reported this eye
         * yet; the hook leaves the game's own projection alone in that case. */
        ctx->eye[e].valid = (t->fov[1] > t->fov[0]) && (t->fov[2] > t->fov[3]);
    }
}

int gevr_shim_get_pads(OSContPad *out, int max)
{
    gevr_game_state game;
    gevr_n64_pad pads[GEVR_PAD_COUNT];
    gevr_haptic_request haptics;
    int i;

    if (!g_vr.active || !out || max <= 0) {
        return 0;
    }
    ensure_frame_open();

    /* The entire mapping assumes the engine is routing two pads the Goodhead
     * way. Forcing it every frame is cheap and stops a stray options-menu
     * change from silently breaking the controls mid-level. */
    if (cur_player_get_control_type() != CONTROLLER_CONFIG_GOODHEAD) {
        cur_player_set_control_type(CONTROLLER_CONFIG_GOODHEAD);
    }

    read_game_state(&game);
    gevr_controls_update(&g_vr.controls, &g_vr.cfg, &g_vr.input, &game,
                         pads, &haptics);

    if (g_vr.cfg.haptics_scale > 0.0f) {
        gevr_xr_haptics(g_vr.xr, &haptics);
    }

    if (gevr_controls_take_recenter(&g_vr.controls)) {
        gevr_xr_recenter(g_vr.xr);
        gevr_camera_recenter(&g_vr.cam, &g_vr.input.head, g_vr.controls.body_yaw);
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

void gevr_shim_begin_eye(int eye)
{
    gevr_camera_params params;
    unsigned fbo;

    if (!g_vr.active || !g_vr.rendering ||
        eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }

    g_vr.current_eye = eye;

    if (gevr_xr_acquire_eye(g_vr.xr, eye, &g_vr.eye_target[eye]) != 0) {
        return;
    }

    memset(&params, 0, sizeof(params));
    if (g_CurrentPlayer) {
        /* The engine's own camera position, which is where movement and
         * collision have put Bond this frame. */
        params.eye_pos = gevr_v3(g_CurrentPlayer->headpos.f[0],
                                 g_CurrentPlayer->eyeheight,
                                 g_CurrentPlayer->headpos.f[2]);
        params.yaw = gevr_engine_yaw_to_vr(g_CurrentPlayer->vv_theta);
        params.pitch = gevr_engine_pitch_to_vr(g_CurrentPlayer->vv_verta);
        params.fov_y = GEVR_DEG2RAD(g_CurrentPlayer->zoominfovy);
    }

    gevr_camera_build_eye(&g_vr.cam, &g_vr.cfg, &params, &g_vr.input.head,
                          &g_vr.eye_target[eye].pose,
                          g_vr.eye_target[eye].fov,
                          &g_vr.eye_view[eye]);

    fbo = gevr_gl_framebuffer_for(g_vr.eye_target[eye].gl_texture,
                                  g_vr.eye_target[eye].gl_depth);
    gevr_gl_bind_framebuffer(fbo, g_vr.eye_target[eye].width,
                             g_vr.eye_target[eye].height);
    gevr_gl_clear(0.0f, 0.0f, 0.0f, 1.0f);
}

void gevr_shim_end_eye(int eye)
{
    if (!g_vr.active || !g_vr.rendering ||
        eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }

    gevr_gl_draw_vignette(g_vr.controls.vignette);
    gevr_xr_release_eye(g_vr.xr, eye);
}

int gevr_shim_eye_projection(float out_mtx[16], float *out_persp_norm)
{
    int eye = g_vr.current_eye;

    if (!g_vr.active || !g_vr.rendering || !out_mtx) {
        return 0;
    }
    if (!g_vr.eye_view[eye].valid) {
        return 0;
    }

    memcpy(out_mtx, g_vr.eye_view[eye].proj.m, sizeof(float) * 16);
    if (out_persp_norm) {
        /* guPerspective's perspNorm feeds the RSP's perspective correction.
         * The renderer backend supplies its own; 0xFFFF is the neutral value
         * for a backend that does not use it. */
        *out_persp_norm = 1.0f;
    }
    return 1;
}

int gevr_shim_eye_view(float out_mtx[16])
{
    int eye = g_vr.current_eye;

    if (!g_vr.active || !g_vr.rendering || !out_mtx) {
        return 0;
    }
    if (!g_vr.eye_view[eye].valid) {
        return 0;
    }
    memcpy(out_mtx, g_vr.eye_view[eye].view.m, sizeof(float) * 16);
    return 1;
}

/* Column-major (OpenGL) -> row-major (libultra). Element [c][r] of one is
 * element [r][c] of the other. */
static void transpose_to_n64(const float *src_cm, float out[4][4])
{
    int r, c;

    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            out[c][r] = src_cm[r * 4 + c];
        }
    }
}

int gevr_shim_eye_projection_n64(float out[4][4])
{
    int eye = g_vr.current_eye;

    if (!g_vr.active || !g_vr.rendering || !out ||
        !g_vr.eye_view[eye].valid) {
        return 0;
    }
    transpose_to_n64(g_vr.eye_view[eye].proj.m, out);
    return 1;
}

int gevr_shim_eye_view_n64(float out[4][4])
{
    int eye = g_vr.current_eye;

    if (!g_vr.active || !g_vr.rendering || !out ||
        !g_vr.eye_view[eye].valid) {
        return 0;
    }
    transpose_to_n64(g_vr.eye_view[eye].view.m, out);
    return 1;
}

void gevr_shim_room_offset(float *out_x, float *out_y, float *out_z)
{
    gevr_vec3 off = gevr_v3(0.0f, 0.0f, 0.0f);

    if (g_vr.active && g_vr.input.head_valid) {
        off = gevr_camera_room_offset(&g_vr.cam, &g_vr.cfg, &g_vr.input.head);
    }
    if (out_x) { *out_x = off.x; }
    if (out_y) { *out_y = off.y; }
    if (out_z) { *out_z = off.z; }
}

float gevr_shim_crouch_offset(void)
{
    if (!g_vr.active || !g_vr.input.head_valid) {
        return 0.0f;
    }
    return gevr_camera_crouch_offset(&g_vr.cam, &g_vr.cfg, &g_vr.input.head);
}

#endif /* GE_VR */
