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
 * publish_eyes hands over this frame's per-eye frustum and translation. It is
 * called once per frame from the render thread, after the XR frame is open and
 * xrLocateViews has run -- not from inside the hook, which fires during the
 * renderer's list walk where calling into OpenXR is not an option.
 *
 * The frustum is replaced rather than adjusted. A headset's is asymmetric and
 * guPerspective can only express a symmetric one; passing the runtime's own
 * half-angles through unchanged is what keeps straight lines straight, where a
 * symmetric approximation shears the world toward the nose.
 */
struct gevr_stereo_ctx;
int  gevr_shim_begin_eye_target(int eye);
void gevr_shim_end_eye_target(int eye);
void gevr_shim_publish_eyes(struct gevr_stereo_ctx *ctx);
int  gevr_shim_alternate_eyes(void);
int  gevr_shim_current_eye(void);

/* Blits one eye onto the desktop window, so the monitor shows what the player
 * sees rather than the empty framebuffer the eye passes left behind. */
void gevr_shim_blit_mirror(void);

/*
 * Positional (6DoF) tracking is not exposed here.
 *
 * It used to be, as a roomscale offset the game was meant to add to the
 * player's camera position -- and nothing ever called it, because there is no
 * good place in the engine to add it. GoldenEye derives the camera position
 * from the collision capsule, so writing to it either drags the capsule along
 * (lean through a wall and you teleport, or get stuck in it) or is overwritten
 * on the next tick.
 *
 * So the offset is applied where the engine has no opinion: it is folded into
 * the per-eye translation published to the renderer, alongside the IPD, in
 * gevr_shim_publish_eyes. Leaning moves the view and nothing else -- shots
 * still come from where the engine thinks Bond stands, which is the honest
 * trade for not letting a player lean their head through cover and shoot from
 * inside it.
 */

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
#define gevr_shim_current_eye()        (0)
#define gevr_shim_blit_mirror()        ((void)0)

#endif /* GE_VR */

#endif /* GEVR_SHIM_H */
