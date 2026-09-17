/*
 * gfx_stereo.h - the seam that lets one display list be drawn once per eye.
 *
 * Kept deliberately small and inert. With no hooks installed, gfx_run behaves
 * exactly as it did before, so a build without VR is unchanged and merges from
 * upstream stay cheap. Every decision that could be called policy -- how many
 * eyes this frame, which one, what the projection becomes, where it is drawn
 * -- lives behind these function pointers in vr/, not in the renderer.
 *
 * Why the loop has to be inside gfx_run rather than around it: gfx_run is a
 * whole-frame function. It calls the window and rendering APIs' start_frame,
 * clears the framebuffer, walks the list, appends the options overlay, flushes
 * and runs the present path. Calling it twice would have the second eye clear
 * the first. So the eye loop wraps the inner walk only.
 *
 * Replaying the walk is safe: port/tests/test_dl_replay.cpp checks that the
 * interpreter emits identical geometry for the same list walked twice inside
 * one frame.
 */
#ifndef GFX_STEREO_H
#define GFX_STEREO_H

#ifdef __cplusplus
extern "C" {
#endif

struct GfxStereoHooks {
    /*
     * Passes to make this frame: 1 or 2. Returning 1 covers both mono and
     * alternate-eye rendering -- the difference is only which eye eye_index()
     * then names, so AER costs no extra code path here.
     */
    int (*pass_count)(void);

    /* Which eye pass `p` draws: 0 left, 1 right. */
    int (*eye_for_pass)(int p);

    /*
     * Called before and after each pass, to bind that eye's target and to
     * hand the finished image to the compositor. begin_eye returns 0 to skip
     * the pass, which is how a lost or throttled runtime declines a frame
     * without the renderer needing to know why.
     */
    int  (*begin_eye)(int eye);
    void (*end_eye)(int eye);

    /*
     * Applied to every projection matrix the list loads, in place, before the
     * interpreter stores it. This is where the eye's asymmetric frustum and
     * its eye offset go.
     *
     * It runs per G_MTX_PROJECTION rather than once per pass on purpose: the
     * game loads a projection more than once a frame (the world, then the sky
     * and the HUD), and those want different treatment. Giving the hook the
     * matrix each time lets vr/ decide, which is where that judgement belongs.
     */
    void (*adjust_projection)(int eye, float m[4][4]);
};

/* NULL restores the unmodified single-pass behaviour. */
void gfx_set_stereo_hooks(const struct GfxStereoHooks *hooks);

/* Whether anything currently owns the seam. Lets a diagnostic stand aside for
 * a real headset instead of silently replacing it. */
int gfx_stereo_hooks_installed(void);

/* The eye currently being drawn, or 0 when no hooks are installed. Exposed so
 * the game-side VR hooks can agree with the renderer about which eye is in
 * flight without a second source of truth. */
int gfx_stereo_current_eye(void);

#ifdef __cplusplus
}
#endif

#endif /* GFX_STEREO_H */
