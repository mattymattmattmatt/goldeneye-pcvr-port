/*
 * gevr_stereo.h - VR-side policy for fast3d's per-eye render seam.
 *
 * The renderer holds five function pointers and no opinions; this is where the
 * opinions live. Kept separate from gevr_shim.c so it can be unit-tested
 * without the game or a runtime present.
 */
#ifndef GEVR_STEREO_H
#define GEVR_STEREO_H

#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GEVR_EYE_COUNT
#define GEVR_EYE_COUNT 2
#endif

typedef struct gevr_stereo_eye {
    int       valid;
    float     fov[4];     /* left, right, up, down half-angles, radians */
    gevr_vec3 offset;     /* eye relative to head, view space, game units */
} gevr_stereo_eye;

typedef struct gevr_stereo_ctx {
    int active;
    int alternate_eyes;   /* 1 = one eye per frame (AER), 0 = full stereo */
    unsigned frame_parity;/* incremented once per frame by the shim       */

    gevr_stereo_eye eye[GEVR_EYE_COUNT];

    /* Binding a swapchain image is the XR layer's job, not this file's. */
    void *user;
    int  (*begin_eye_cb)(void *user, int eye);
    void (*end_eye_cb)(void *user, int eye);
} gevr_stereo_ctx;

void gevr_stereo_install(gevr_stereo_ctx *ctx);
void gevr_stereo_uninstall(void);

/* Exposed for tests. */
int  gevr_stereo_recover_near_far(const float m[4][4], float *n, float *f);
void gevr_stereo_build_frustum(float l, float r, float b, float t,
                               float n, float f, float m[4][4]);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_STEREO_H */
