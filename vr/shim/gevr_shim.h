/*
 * gevr_shim.h - the seam between the GoldenEye engine and the VR layer.
 *
 * Everything in this header is a no-op unless GE_VR is defined. The N64 ROM
 * target never defines it, so the matching build sees an empty translation
 * unit and unchanged call sites. joy.c in particular contains code commented
 * "required for matching", so the guards are not optional politeness.
 *
 * Call order for one VR frame:
 *
 *   gevr_shim_frame_begin()      once, before the game ticks
 *   gevr_shim_get_pads()         after joyConsumeSamplesWrapper()
 *   ... the game ticks as normal ...
 *   for each eye:
 *       gevr_shim_begin_eye(e)
 *       ... the game renders its display list ...
 *       gevr_shim_end_eye(e)
 *   gevr_shim_frame_end()
 *
 * The engine keeps ownership of movement, collision and hitscan. The VR layer
 * only supplies input and per-eye view/projection.
 */
#ifndef GEVR_SHIM_H
#define GEVR_SHIM_H

#ifdef GE_VR

/* For OSContPad, which gevr_shim_get_pads fills. This header is only reached
 * from game sources, which have the SDK headers available anyway. */
#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up OpenXR. Returns 0 on success; on failure the game should carry on
 * flat rather than refusing to start, so a broken runtime is not fatal. */
int  gevr_shim_init(void);
void gevr_shim_shutdown(void);
int  gevr_shim_active(void);

/* Waits on the compositor and reads this frame's poses and controllers. */
void gevr_shim_frame_begin(void);
void gevr_shim_frame_end(void);

/*
 * Fills `out` with the VR-derived pads -- 0 aims, 1 moves -- and returns how
 * many were written. Also forces the current player's control style to
 * Goodhead, because the whole mapping depends on that routing.
 *
 * The caller writes them into its own controller state rather than this
 * reaching in to do it. struct contdata is private to joy.c, and it should
 * stay that way: the seam is "here are the pads", not "let me edit your
 * ring buffer". Requires OSContPad, so include <ultra64.h> first -- joy.c
 * already does.
 */
int gevr_shim_get_pads(OSContPad *out, int max);

/*
 * Absolute head aim. Fills the angles the engine should hold this frame, in
 * its own units (vv_theta in degrees [0,360), vv_verta in signed degrees), and
 * returns 1 when they should be written.
 *
 * Call this from MoveBond immediately BEFORE bondviewApplyVertaTheta(), not
 * after. That call derives vv_costheta, vv_sintheta, vv_cosverta and
 * vv_sinverta from the angles, and the view matrix, the gun direction, hitscan
 * and the radar all read the derived values. Writing before it means the whole
 * engine agrees with the headset in the same frame; writing after it leaves
 * every one of them a frame stale.
 *
 * Returns 0 during menus, cutscenes and control locks, where the engine is
 * driving the camera itself and must be left alone.
 */
int gevr_shim_head_aim(float *out_theta_deg, float *out_verta_deg);

/*
 * Render-target and per-eye state for the fast3d stereo seam.
 *
 * begin_eye_target acquires that eye's swapchain image and binds it, returning
 * 0 if the runtime declined the frame -- the renderer then skips the pass
 * rather than drawing into nothing.
 *
 * publish_eyes refreshes the FOVs and eye offsets the projection hook reads.
 * It is called once per frame from the host, not from inside the hook: the
 * hook runs during the renderer's list walk, and calling into OpenXR in the
 * middle of a draw is asking for trouble.
 */
struct gevr_stereo_ctx;
int  gevr_shim_begin_eye_target(int eye);
void gevr_shim_end_eye_target(int eye);
void gevr_shim_publish_eyes(struct gevr_stereo_ctx *ctx);
int  gevr_shim_alternate_eyes(void);

/* Blits one eye onto the desktop window, so the monitor shows what the player
 * sees rather than the empty framebuffer the eye passes left behind. */
void gevr_shim_blit_mirror(void);

/* 0 = left, 1 = right. Between begin and end the swapchain image for that eye
 * is bound and the viewport is set. */
void gevr_shim_begin_eye(int eye);
void gevr_shim_end_eye(int eye);
int  gevr_shim_current_eye(void);

/* Fills a projection matrix for the eye currently being rendered. Returns 1
 * if it wrote one, 0 if the caller should fall back to guPerspective.
 *
 * A headset frustum is asymmetric and guPerspective can only express a
 * symmetric one, so this must replace it rather than adjust it. Passing the
 * runtime's own angles through unchanged is what keeps straight lines straight;
 * substituting a symmetric approximation shears the world toward the nose. */
int  gevr_shim_eye_projection(float out_mtx[16], float *out_persp_norm);

/* Per-eye view matrix in game world units, for the same eye. */
int  gevr_shim_eye_view(float out_mtx[16]);

/* The same two matrices in libultra's convention.
 *
 * guPerspectiveF and guLookAtF build row-vector matrices (the vertex is a row
 * on the left, v * M), while the VR layer works in the OpenGL column-vector
 * convention (M * v). The two forms are transposes of each other, so writing a
 * gevr matrix straight into an f32[4][4] destined for guMtxF2L would render a
 * scrambled frustum. These two do the transpose explicitly. */
int  gevr_shim_eye_projection_n64(float out[4][4]);
int  gevr_shim_eye_view_n64(float out[4][4]);

/* Roomscale offset to add to the player's camera position this frame, in game
 * units, and the crouch offset in game units (negative when crouching). */
void gevr_shim_room_offset(float *out_x, float *out_y, float *out_z);
float gevr_shim_crouch_offset(void);

#ifdef __cplusplus
}
#endif

#else /* !GE_VR */

/* The ROM build compiles these to nothing. */
#define gevr_shim_init()            (0)
#define gevr_shim_shutdown()        ((void)0)
#define gevr_shim_active()          (0)
#define gevr_shim_frame_begin()     ((void)0)
#define gevr_shim_frame_end()       ((void)0)
#define gevr_shim_get_pads(o, n)    (0)
#define gevr_shim_head_aim(t, v)    (0)
#define gevr_shim_begin_eye_target(e)  (0)
#define gevr_shim_end_eye_target(e)    ((void)0)
#define gevr_shim_publish_eyes(c)      ((void)0)
#define gevr_shim_alternate_eyes()     (0)
#define gevr_shim_blit_mirror()        ((void)0)
#define gevr_shim_begin_eye(e)      ((void)0)
#define gevr_shim_end_eye(e)        ((void)0)
#define gevr_shim_current_eye()     (0)
#define gevr_shim_eye_projection_n64(m) (0)
#define gevr_shim_eye_view_n64(m)       (0)

#endif /* GE_VR */

#endif /* GEVR_SHIM_H */
