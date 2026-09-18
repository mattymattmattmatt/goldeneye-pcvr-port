/*
 * gevr_camera.h - stereo view and projection for the GoldenEye VR layer.
 *
 * Division of labour between the engine and this file:
 *
 *   The engine owns the player's POSITION. Movement, collision, stairs and
 *   lifts all stay exactly as Rare wrote them, driven by the synthetic move
 *   pad from gevr_controls.c.
 *
 *   The VR layer owns the view ORIENTATION. The camera is built from the live
 *   headset pose rather than from the engine's yaw/pitch, because the servo
 *   that keeps the engine in sync necessarily lags by a frame or two and a
 *   lagging horizon in a headset is nauseating. The engine's angles still
 *   decide where bullets go; the servo keeps the two within a fraction of a
 *   degree during normal play.
 *
 * Units: OpenXR poses arrive in metres. Everything handed back to the engine
 * is in GoldenEye world units, scaled by cfg->world_scale.
 */
#ifndef GEVR_CAMERA_H
#define GEVR_CAMERA_H

#include "gevr_config.h"
#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the engine tells us about the player, in game units and radians. */
typedef struct gevr_camera_params {
    gevr_vec3 eye_pos;   /* the engine's camera position, game units       */
    float     yaw;       /* engine camera yaw, VR convention (+left)       */
    float     pitch;     /* engine camera pitch, VR convention (+up)       */
    float     fov_y;     /* engine's current vertical FOV, radians. Used
                          * only for the flat mirror; the headset's own FOV
                          * drives the stereo views and must not be
                          * overridden or the world warps.                 */
} gevr_camera_params;

/* One eye's worth of render state, ready for the graphics backend. */
typedef struct gevr_eye_view {
    gevr_pose pose;   /* eye pose in game world space, game units */
    gevr_mat4 view;   /* world -> eye */
    gevr_mat4 proj;   /* off-axis projection, game-unit near/far */
    float     fov[4]; /* left, right, up, down half-angles, radians */
    int       valid;
} gevr_eye_view;

/* Roomscale bookkeeping: where the play space origin sits and which way it
 * faces. Recentering rewrites this rather than moving the player. */
typedef struct gevr_camera {
    gevr_vec3 recenter_origin;  /* head position at the last recenter, metres */
    float     body_yaw;         /* stick-accumulated heading, radians         */
    float     standing_height;  /* calibrated head height, metres             */
    int       have_origin;
} gevr_camera;

void gevr_camera_init(gevr_camera *cam, const gevr_config *cfg);

/* Snaps the play space so the player's current head position and heading
 * become the neutral pose. Safe to call mid-level. */
void gevr_camera_recenter(gevr_camera *cam, const gevr_pose *head, float body_yaw);

/* Adopts the head's current POSITION as the play-space origin, leaving the
 * heading alone, and only if no origin has been taken yet.
 *
 * Positional tracking is measured from that origin, so without it every
 * offset is relative to wherever the runtime happens to put its own origin --
 * for a stage space, the floor in the middle of the guardian, which can be a
 * metre or more from a seated player. Taking it on the first valid pose means
 * 6DoF is correct from the first frame instead of waiting for the player to
 * discover the recenter button. Unlike recenter it does not touch body_yaw,
 * because at start-up the engine has not yet decided which way Bond faces. */
void gevr_camera_capture_origin(gevr_camera *cam, const gevr_pose *head);

/* Builds one eye. head and eye are in the same reference space; eye_fov comes
 * straight from XrCompositionLayerProjectionView::fov. */
void gevr_camera_build_eye(const gevr_camera *cam,
                           const gevr_config *cfg,
                           const gevr_camera_params *params,
                           const gevr_pose *head,
                           const gevr_pose *eye,
                           const float eye_fov[4],
                           gevr_eye_view *out);

/* The roomscale offset the engine should apply to the player's position so
 * that leaning and stepping inside the guardian move Bond too. Returned in
 * game units, already rotated into world space. The shim adds this to the
 * camera position; it deliberately does NOT move the collision capsule, so
 * leaning through a wall shows geometry rather than teleporting the player. */
gevr_vec3 gevr_camera_room_offset(const gevr_camera *cam,
                                  const gevr_config *cfg,
                                  const gevr_pose *head);

/* Height of the head above the calibrated standing height, in game units.
 * Negative when the player physically crouches. */
float gevr_camera_crouch_offset(const gevr_camera *cam,
                                const gevr_config *cfg,
                                const gevr_pose *head);

/*
 * Head displacement from the play-space origin, in METRES, expressed in the
 * head's own frame rather than the world's -- so +x is to the player's right,
 * +y up, -z the way they are looking, whichever way that is.
 *
 * That frame is the one the renderer needs. The per-eye offset is applied as a
 * translation after the engine's view matrix, and while head aim is driving
 * the view, the engine's view rotation IS the head's rotation (plus the
 * stick-accumulated body yaw, which cancels: the same yaw maps tracking space
 * into world space). So rotating the raw displacement into head-local space is
 * all that separates a 6DoF offset that tracks the player from one that swings
 * around the room as they turn their head.
 *
 * All three axes are kept, including the vertical: ducking should lower the
 * view. cfg->room_scale multiplies it and cfg->room_limit clamps its length.
 */
gevr_vec3 gevr_camera_room_offset_local(const gevr_camera *cam,
                                        const gevr_config *cfg,
                                        const gevr_pose *head);

/*
 * The whole translation one eye needs, in GAME UNITS and in the head's frame:
 * the interpupillary offset, plus the 6DoF displacement when `positional` is
 * set. Ready to be applied after the engine's view matrix.
 *
 * `positional` is the caller's answer to "is the headset steering the view
 * this frame?". When the engine has taken the camera -- a menu, a cutscene, a
 * control lock -- pass 0. The head-local frame is only the view's frame while
 * head aim is driving it, and a positional offset applied to a camera pointing
 * somewhere else reads as the world sliding of its own accord. The eye
 * separation is left in either way: a few centimetres misaligned is a
 * negligible stereo error, where tens of game units is not.
 */
gevr_vec3 gevr_camera_eye_offset(const gevr_camera *cam,
                                 const gevr_config *cfg,
                                 const gevr_pose *head,
                                 const gevr_pose *eye,
                                 int positional);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_CAMERA_H */
