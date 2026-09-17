/*
 * gevr_xr.h - OpenXR session management for the GoldenEye VR layer.
 *
 * Targets SteamVR on the desktop, which is what a Quest 3 reaches through
 * Virtual Desktop or Link. Nothing here is Meta-specific: the same binary
 * drives Index, Vive and WMR through their own OpenXR runtimes.
 *
 * The module owns the OpenXR instance, session, swapchains, reference spaces
 * and action set, and hands the rest of the layer two things per frame: a
 * filled gevr_input_state, and a GL texture per eye to render into.
 */
#ifndef GEVR_XR_H
#define GEVR_XR_H

#include "gevr_config.h"
#include "gevr_input.h"
#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_EYE_LEFT   0
#define GEVR_EYE_RIGHT  1
#define GEVR_EYE_COUNT  2

typedef struct gevr_xr gevr_xr;

typedef struct gevr_xr_desc {
    const char        *app_name;
    unsigned           app_version;
    const gevr_config *cfg;
    /* When set, the layer creates its own SDL window and GL context. The
     * integrated build passes 0 and supplies an existing context instead. */
    int                create_window;
} gevr_xr_desc;

typedef enum gevr_frame_status {
    GEVR_FRAME_RENDER = 0,  /* should render and submit */
    GEVR_FRAME_SKIP,        /* session not visible; submit nothing */
    GEVR_FRAME_EXIT         /* runtime asked us to quit */
} gevr_frame_status;

/* Per-eye render target for the current frame. */
typedef struct gevr_eye_target {
    unsigned gl_texture;    /* colour attachment from the swapchain image */
    unsigned gl_depth;      /* layer-owned depth renderbuffer             */
    int      width, height;
    gevr_pose pose;         /* eye pose in the tracking space, metres     */
    float    fov[4];        /* left, right, up, down half-angles          */
} gevr_eye_target;

int  gevr_xr_create(gevr_xr **out, const gevr_xr_desc *desc);
void gevr_xr_destroy(gevr_xr *xr);

/* Human-readable description of the last failure. Never NULL. */
const char *gevr_xr_last_error(const gevr_xr *xr);

/* Runtime and system names, for the log banner and the calibrate report. */
const char *gevr_xr_runtime_name(const gevr_xr *xr);
const char *gevr_xr_system_name(const gevr_xr *xr);
/* Interaction profile currently bound to each hand, or "" if none. */
const char *gevr_xr_profile_name(const gevr_xr *xr, int hand);

/* Pumps the event queue and updates session state. Call once per frame before
 * gevr_xr_begin_frame. */
gevr_frame_status gevr_xr_poll(gevr_xr *xr);

/* Waits on the runtime's frame cadence, syncs actions and locates views.
 * Fills *in with this frame's controller and head state. */
gevr_frame_status gevr_xr_begin_frame(gevr_xr *xr, gevr_input_state *in);

/* Acquires the swapchain image for one eye. Returns 0 on success. */
int  gevr_xr_acquire_eye(gevr_xr *xr, int eye, gevr_eye_target *out);
void gevr_xr_release_eye(gevr_xr *xr, int eye);

/* Submits the projection layer built from the eyes released this frame. */
void gevr_xr_end_frame(gevr_xr *xr);

void gevr_xr_haptics(gevr_xr *xr, const gevr_haptic_request *req);

/* Re-establishes the tracking origin at the player's current pose. */
void gevr_xr_recenter(gevr_xr *xr);

/* Predicted display time of the frame being built, in nanoseconds. */
long long gevr_xr_predicted_display_time(const gevr_xr *xr);

/* Recommended per-eye render size, after cfg->render_scale. */
void gevr_xr_recommended_size(const gevr_xr *xr, int *w, int *h);

/* True once the runtime reports a stage (roomscale) space is available. */
int  gevr_xr_has_stage_space(const gevr_xr *xr);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_XR_H */
