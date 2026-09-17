/*
 * gevr_gl.h - the small slice of OpenGL the VR layer needs on its own.
 *
 * The game's own renderer draws the world; this module only handles the parts
 * that belong to the headset rather than to the game: wrapping swapchain
 * images in framebuffers, the comfort vignette, the floating HUD quad, and
 * the desktop mirror. Entry points are resolved through SDL so the layer does
 * not need a GL loader dependency.
 */
#ifndef GEVR_GL_H
#define GEVR_GL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves GL entry points. Must be called with a current GL context.
 * Returns 0 on success, -1 if a required function is missing. */
int  gevr_gl_init(void);
void gevr_gl_shutdown(void);

const char *gevr_gl_last_error(void);

/* Depth buffer to pair with a swapchain colour image. */
unsigned gevr_gl_create_depth(int width, int height);
void     gevr_gl_delete_depth(unsigned renderbuffer);

/* A framebuffer bound to the given colour texture and depth renderbuffer.
 * Cached per (texture, depth) pair, because swapchain images cycle through a
 * small fixed set and recreating an FBO every frame is wasteful. */
unsigned gevr_gl_framebuffer_for(unsigned color_tex, unsigned depth_rb);

void gevr_gl_bind_framebuffer(unsigned fbo, int width, int height);
void gevr_gl_clear(float r, float g, float b, float a);

/* Darkens the periphery by `strength` (0..1). Drawn last, into whatever
 * framebuffer is bound. Cheap enough to run every eye every frame. */
void gevr_gl_draw_vignette(float strength);

/* Blits an eye texture to the desktop mirror window. */
void gevr_gl_blit_mirror(unsigned src_fbo, int src_w, int src_h,
                         int dst_w, int dst_h);

/* Immediate-mode-style textured quad helper used by the calibrate tool and
 * the HUD panel. Vertices are in normalised device coordinates. */
void gevr_gl_draw_textured_quad(unsigned texture,
                                float x0, float y0, float x1, float y1);

/* Debug scene for the calibrate tool: a floor grid, a horizon reference and
 * a pair of hand markers, drawn with the supplied view/projection. */
void gevr_gl_draw_calibration_scene(const float *view, const float *proj,
                                    const float *hand_l_matrix,
                                    const float *hand_r_matrix,
                                    float world_scale);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_GL_H */
