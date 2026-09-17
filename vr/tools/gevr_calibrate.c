/*
 * ge007vr-calibrate - a runnable OpenXR harness for the VR control layer.
 *
 * This is the piece you can put on your head before the renderer is finished.
 * It brings up a real OpenXR session, binds the real action set, runs the real
 * gevr_controls mapper, and feeds the synthesised N64 pads into a small stand-in
 * for the engine's movement integrator. You walk around a one-metre grid using
 * exactly the control scheme the game will use.
 *
 * What it is for:
 *   - confirming the headset and both controllers are seen by the runtime
 *   - feeling the Goodhead twin-stick mapping and tuning snap/deadzone/gain
 *   - measuring world_scale: walk a known number of grid squares and compare
 *   - proving the head servo tracks without judder on your actual hardware
 *
 * The stand-in integrator is deliberately simple. It is not the game; it is a
 * consistent target so the control tuning you do here carries over.
 */
#include "gevr_camera.h"
#include "gevr_config.h"
#include "gevr_controls.h"
#include "gevr_gl.h"
#include "gevr_xr.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Matches the engine's aim-stick behaviour closely enough to tune against:
 * the stick acts as a turn rate with the engine's deadzone removed first. */
static float integrate_angle(float angle, signed char stick, float dt,
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
    return angle - rate * dt;
}

static float stick_unit(signed char v)
{
    float s = (float)v;
    float mag = (s < 0.0f) ? -s : s;

    if (mag <= (float)GEVR_N64_STICK_NOTCH) {
        return 0.0f;
    }
    mag = (mag - (float)GEVR_N64_STICK_NOTCH)
        / (float)(GEVR_N64_STICK_MAX - GEVR_N64_STICK_NOTCH);
    return (s < 0.0f) ? -mag : mag;
}

typedef struct sim_state {
    gevr_vec3 pos;      /* game units */
    float     yaw;
    float     pitch;
} sim_state;

/* Bond walks at roughly this many game units per second at full stick. */
#define SIM_WALK_SPEED 250.0f
#define SIM_TURN_DPS   180.0f

static void sim_step(sim_state *s, const gevr_n64_pad pads[GEVR_PAD_COUNT],
                     float dt)
{
    float fwd = stick_unit(pads[GEVR_PAD_MOVE].stick_y);
    float strafe = stick_unit(pads[GEVR_PAD_MOVE].stick_x);
    float cs, sn;

    s->yaw = gevr_wrap_pi(integrate_angle(s->yaw, pads[GEVR_PAD_AIM].stick_x,
                                          dt, SIM_TURN_DPS));
    s->pitch = gevr_clampf(integrate_angle(s->pitch, pads[GEVR_PAD_AIM].stick_y,
                                           dt, SIM_TURN_DPS),
                           GEVR_DEG2RAD(-85.0f), GEVR_DEG2RAD(85.0f));

    /* Yaw 0 faces -Z, positive yaw turns left. */
    cs = cosf(s->yaw);
    sn = sinf(s->yaw);
    s->pos.x += (-fwd * sn + strafe * cs) * SIM_WALK_SPEED * dt;
    s->pos.z += (-fwd * cs - strafe * sn) * SIM_WALK_SPEED * dt;
}

static void print_banner(gevr_xr *xr, const gevr_config *cfg)
{
    int w = 0, h = 0;

    gevr_xr_recommended_size(xr, &w, &h);

    printf("\n");
    printf("  GoldenEye VR - control calibration\n");
    printf("  ------------------------------------------------------------\n");
    printf("  runtime        : %s\n", gevr_xr_runtime_name(xr));
    printf("  system         : %s\n", gevr_xr_system_name(xr));
    printf("  per-eye target : %d x %d  (render_scale %.2f)\n", w, h,
           (double)cfg->render_scale);
    printf("  stage space    : %s\n",
           gevr_xr_has_stage_space(xr) ? "available" : "not available (seated)");
    printf("  world scale    : %.1f units/metre\n", (double)cfg->world_scale);
    printf("  turn mode      : %s",
           cfg->turn_mode == GEVR_TURN_SNAP ? "snap" :
           cfg->turn_mode == GEVR_TURN_SMOOTH ? "smooth" : "off");
    if (cfg->turn_mode == GEVR_TURN_SNAP) {
        printf("  (%.0f deg)", (double)cfg->snap_degrees);
    }
    printf("\n");
    printf("  ------------------------------------------------------------\n");
    printf("  left stick  : move (strafe + walk)   -> synthetic pad 1\n");
    printf("  right stick : turn                   -> synthetic pad 0\n");
    printf("  head        : look (servo drives pad 0 pitch/yaw)\n");
    printf("  L trigger   : aim mode   R trigger : fire\n");
    printf("  X           : recenter   Y         : pause\n");
    printf("  ------------------------------------------------------------\n");
    printf("  Walk one grid square = 1 real metre = %.0f game units.\n",
           (double)cfg->world_scale);
    printf("  Ctrl-C in this terminal to quit.\n\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    gevr_config cfg;
    gevr_controls controls;
    gevr_camera cam;
    gevr_xr *xr = NULL;
    gevr_xr_desc desc;
    sim_state sim;
    const char *config_path = "gevr.ini";
    int i;
    int frames = 0;
    int running = 1;

    gevr_config_defaults(&cfg);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (!strncmp(argv[i], "--set=", 6)) {
            char buf[256];
            char *eq;
            snprintf(buf, sizeof(buf), "%s", argv[i] + 6);
            eq = strchr(buf, '=');
            if (eq) {
                *eq = '\0';
                if (gevr_config_set(&cfg, buf, eq + 1) != 0) {
                    fprintf(stderr, "unknown setting: %s\n", buf);
                }
            }
        } else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--config gevr.ini] [--set=key=value ...]\n",
                   argv[0]);
            return 0;
        }
    }

    if (gevr_config_load(&cfg, config_path) == 0) {
        printf("loaded config from %s\n", config_path);
    } else {
        printf("no config at %s, using defaults\n", config_path);
    }
    gevr_config_validate(&cfg);

    gevr_controls_init(&controls);
    gevr_camera_init(&cam, &cfg);

    memset(&sim, 0, sizeof(sim));

    memset(&desc, 0, sizeof(desc));
    desc.app_name = "GoldenEye VR Calibration";
    desc.app_version = 1;
    desc.cfg = &cfg;
    desc.create_window = 1;

    if (gevr_xr_create(&xr, &desc) != 0) {
        fprintf(stderr, "\nOpenXR startup failed: %s\n\n",
                xr ? gevr_xr_last_error(xr) : "out of memory");
        fprintf(stderr,
                "Check that SteamVR is running and is the active OpenXR\n"
                "runtime, and that the headset is connected (for a Quest 3,\n"
                "that means Virtual Desktop or Link is streaming).\n\n");
        gevr_xr_destroy(xr);
        return 1;
    }

    print_banner(xr, &cfg);

    while (running) {
        gevr_input_state in;
        gevr_game_state game;
        gevr_n64_pad pads[GEVR_PAD_COUNT];
        gevr_haptic_request haptics;
        gevr_frame_status st;
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN &&
                       ev.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            }
        }

        st = gevr_xr_poll(xr);
        if (st == GEVR_FRAME_EXIT) {
            break;
        }

        st = gevr_xr_begin_frame(xr, &in);
        if (st == GEVR_FRAME_EXIT) {
            break;
        }

        memset(&game, 0, sizeof(game));
        game.yaw = sim.yaw;
        game.pitch = sim.pitch;

        gevr_controls_update(&controls, &cfg, &in, &game, pads, &haptics);
        gevr_xr_haptics(xr, &haptics);

        if (gevr_controls_take_recenter(&controls)) {
            gevr_xr_recenter(xr);
            gevr_camera_recenter(&cam, &in.head, controls.body_yaw);
            controls.body_yaw = 0.0f;
            cam.body_yaw = 0.0f;
            printf("recentred\n");
            fflush(stdout);
        }

        cam.body_yaw = controls.body_yaw;
        sim_step(&sim, pads, in.dt);

        if (st == GEVR_FRAME_RENDER) {
            gevr_camera_params params;
            int eye;

            memset(&params, 0, sizeof(params));
            params.eye_pos = sim.pos;
            params.eye_pos.y += cfg.player_height * cfg.world_scale;
            params.yaw = sim.yaw;
            params.pitch = sim.pitch;
            params.fov_y = GEVR_DEG2RAD(60.0f);

            for (eye = 0; eye < GEVR_EYE_COUNT; eye++) {
                gevr_eye_target tgt;
                gevr_eye_view view;
                unsigned fbo;

                if (gevr_xr_acquire_eye(xr, eye, &tgt) != 0) {
                    continue;
                }

                gevr_camera_build_eye(&cam, &cfg, &params, &in.head,
                                      &tgt.pose, tgt.fov, &view);

                fbo = gevr_gl_framebuffer_for(tgt.gl_texture, tgt.gl_depth);
                gevr_gl_bind_framebuffer(fbo, tgt.width, tgt.height);
                gevr_gl_clear(0.05f, 0.06f, 0.09f, 1.0f);

                if (view.valid) {
                    gevr_gl_draw_calibration_scene(view.view.m, view.proj.m,
                                                   NULL, NULL, cfg.world_scale);
                }
                gevr_gl_draw_vignette(controls.vignette);

                gevr_xr_release_eye(xr, eye);

                if (eye == GEVR_EYE_LEFT && cfg.mirror_window) {
                    gevr_gl_blit_mirror(fbo, tgt.width, tgt.height, 960, 540);
                }
            }
        }

        gevr_xr_end_frame(xr);

        /* A once-a-second line is enough to read while wearing a headset that
         * is covering your eyes; anything faster is unreadable scrollback. */
        if ((frames % 90) == 0) {
            printf("\rhead y/p %6.1f/%6.1f  body %6.1f  pad0 %4d,%4d  "
                   "pad1 %4d,%4d  sim y %6.1f  pos %7.0f,%7.0f  L[%s] R[%s]",
                   (double)GEVR_RAD2DEG(gevr_quat_yaw_of(in.head.orientation)),
                   (double)GEVR_RAD2DEG(gevr_quat_to_euler(in.head.orientation).pitch),
                   (double)GEVR_RAD2DEG(controls.body_yaw),
                   pads[GEVR_PAD_AIM].stick_x, pads[GEVR_PAD_AIM].stick_y,
                   pads[GEVR_PAD_MOVE].stick_x, pads[GEVR_PAD_MOVE].stick_y,
                   (double)GEVR_RAD2DEG(sim.yaw),
                   (double)sim.pos.x, (double)sim.pos.z,
                   in.hand_l_valid ? "ok" : "--",
                   in.hand_r_valid ? "ok" : "--");
            fflush(stdout);
        }
        frames++;
    }

    printf("\n\nshutting down\n");
    gevr_xr_destroy(xr);
    return 0;
}
