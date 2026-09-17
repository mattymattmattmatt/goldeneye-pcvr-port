/*
 * Every GL entry point is resolved dynamically through SDL rather than linked
 * against libGL. On Windows the system only exports GL 1.1, so anything newer
 * has to come from wglGetProcAddress anyway; doing it uniformly keeps one code
 * path and removes the link-time dependency entirely.
 */
#include "gevr_gl.h"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef unsigned  GLenum;
typedef unsigned  GLuint;
typedef int       GLint;
typedef int       GLsizei;
typedef char      GLchar;
typedef float     GLfloat;
typedef unsigned  GLbitfield;
typedef unsigned char GLboolean;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE                        0
#define GL_TRUE                         1
#define GL_TRIANGLES                    0x0004
#define GL_LINES                        0x0001
#define GL_TRIANGLE_STRIP               0x0005
#define GL_DEPTH_BUFFER_BIT             0x00000100
#define GL_COLOR_BUFFER_BIT             0x00004000
#define GL_DEPTH_TEST                   0x0B71
#define GL_BLEND                        0x0BE2
#define GL_SRC_ALPHA                    0x0302
#define GL_ONE_MINUS_SRC_ALPHA          0x0303
#define GL_TEXTURE_2D                   0x0DE1
#define GL_TEXTURE0                     0x84C0
#define GL_FLOAT                        0x1406
#define GL_FRAMEBUFFER                  0x8D40
#define GL_READ_FRAMEBUFFER             0x8CA8
#define GL_DRAW_FRAMEBUFFER             0x8CA9
#define GL_RENDERBUFFER                 0x8D41
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_DEPTH_ATTACHMENT             0x8D00
#define GL_DEPTH_COMPONENT24            0x81A6
#define GL_FRAMEBUFFER_COMPLETE         0x8CD5
#define GL_ARRAY_BUFFER                 0x8892
#define GL_STATIC_DRAW                  0x88E4
#define GL_DYNAMIC_DRAW                 0x88E8
#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_LINEAR                       0x2601
#define GL_NEAREST                      0x2600
#define GL_CULL_FACE                    0x0B44

/* --------------------------------------------------------------- loader */

#define GEVR_GL_FUNCS(X)                                                      \
    X(void,   glViewport,              (GLint, GLint, GLsizei, GLsizei))       \
    X(void,   glClearColor,            (GLfloat, GLfloat, GLfloat, GLfloat))   \
    X(void,   glClear,                 (GLbitfield))                           \
    X(void,   glEnable,                (GLenum))                               \
    X(void,   glDisable,               (GLenum))                               \
    X(void,   glBlendFunc,             (GLenum, GLenum))                       \
    X(void,   glDrawArrays,            (GLenum, GLint, GLsizei))               \
    X(void,   glActiveTexture,         (GLenum))                               \
    X(void,   glBindTexture,           (GLenum, GLuint))                       \
    X(void,   glGenFramebuffers,       (GLsizei, GLuint *))                    \
    X(void,   glDeleteFramebuffers,    (GLsizei, const GLuint *))              \
    X(void,   glBindFramebuffer,       (GLenum, GLuint))                       \
    X(void,   glFramebufferTexture2D,  (GLenum, GLenum, GLenum, GLuint, GLint))\
    X(void,   glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))     \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                              \
    X(void,   glGenRenderbuffers,      (GLsizei, GLuint *))                    \
    X(void,   glDeleteRenderbuffers,   (GLsizei, const GLuint *))              \
    X(void,   glBindRenderbuffer,      (GLenum, GLuint))                       \
    X(void,   glRenderbufferStorage,   (GLenum, GLenum, GLsizei, GLsizei))     \
    X(void,   glBlitFramebuffer,       (GLint, GLint, GLint, GLint, GLint,     \
                                        GLint, GLint, GLint, GLbitfield,       \
                                        GLenum))                               \
    X(GLuint, glCreateShader,          (GLenum))                               \
    X(void,   glShaderSource,          (GLuint, GLsizei, const GLchar *const *,\
                                        const GLint *))                        \
    X(void,   glCompileShader,         (GLuint))                               \
    X(void,   glGetShaderiv,           (GLuint, GLenum, GLint *))              \
    X(void,   glGetShaderInfoLog,      (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,   glDeleteShader,          (GLuint))                               \
    X(GLuint, glCreateProgram,         (void))                                 \
    X(void,   glAttachShader,          (GLuint, GLuint))                       \
    X(void,   glLinkProgram,           (GLuint))                               \
    X(void,   glGetProgramiv,          (GLuint, GLenum, GLint *))              \
    X(void,   glGetProgramInfoLog,     (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,   glDeleteProgram,         (GLuint))                               \
    X(void,   glUseProgram,            (GLuint))                               \
    X(GLint,  glGetUniformLocation,    (GLuint, const GLchar *))               \
    X(void,   glUniform1f,             (GLint, GLfloat))                       \
    X(void,   glUniform1i,             (GLint, GLint))                         \
    X(void,   glUniform4f,             (GLint, GLfloat, GLfloat, GLfloat,      \
                                        GLfloat))                              \
    X(void,   glUniformMatrix4fv,      (GLint, GLsizei, GLboolean,             \
                                        const GLfloat *))                      \
    X(void,   glGenVertexArrays,       (GLsizei, GLuint *))                    \
    X(void,   glDeleteVertexArrays,    (GLsizei, const GLuint *))              \
    X(void,   glBindVertexArray,       (GLuint))                               \
    X(void,   glGenBuffers,            (GLsizei, GLuint *))                    \
    X(void,   glDeleteBuffers,         (GLsizei, const GLuint *))              \
    X(void,   glBindBuffer,            (GLenum, GLuint))                       \
    X(void,   glBufferData,            (GLenum, GLsizeiptr, const void *,      \
                                        GLenum))                               \
    X(void,   glVertexAttribPointer,   (GLuint, GLint, GLenum, GLboolean,      \
                                        GLsizei, const void *))                \
    X(void,   glEnableVertexAttribArray, (GLuint))

#define X(ret, name, args) static ret (*p_##name) args;
GEVR_GL_FUNCS(X)
#undef X

static char  s_error[256];
static int   s_ready;

const char *gevr_gl_last_error(void)
{
    return s_error;
}

int gevr_gl_init(void)
{
    s_error[0] = '\0';

#define X(ret, name, args)                                                    \
    p_##name = (ret (*) args)SDL_GL_GetProcAddress(#name);                    \
    if (!p_##name) {                                                          \
        snprintf(s_error, sizeof(s_error), "missing GL entry point: %s", #name); \
        return -1;                                                            \
    }
    GEVR_GL_FUNCS(X)
#undef X

    s_ready = 1;
    return 0;
}

/* ---------------------------------------------------------- fbo caching */

typedef struct fbo_entry {
    unsigned color;
    unsigned depth;
    unsigned fbo;
} fbo_entry;

/* Runtimes hand out a handful of swapchain images and cycle them, so a tiny
 * fixed cache covers every image both eyes will ever use. */
#define GEVR_FBO_CACHE 16
static fbo_entry s_fbos[GEVR_FBO_CACHE];
static int       s_fbo_count;

unsigned gevr_gl_create_depth(int width, int height)
{
    GLuint rb = 0;

    if (!s_ready) {
        return 0;
    }
    p_glGenRenderbuffers(1, &rb);
    p_glBindRenderbuffer(GL_RENDERBUFFER, rb);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    p_glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return rb;
}

void gevr_gl_delete_depth(unsigned renderbuffer)
{
    GLuint rb = renderbuffer;
    if (s_ready && rb) {
        p_glDeleteRenderbuffers(1, &rb);
    }
}

unsigned gevr_gl_framebuffer_for(unsigned color_tex, unsigned depth_rb)
{
    GLuint fbo = 0;
    int i;

    if (!s_ready) {
        return 0;
    }

    for (i = 0; i < s_fbo_count; i++) {
        if (s_fbos[i].color == color_tex && s_fbos[i].depth == depth_rb) {
            return s_fbos[i].fbo;
        }
    }

    p_glGenFramebuffers(1, &fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, color_tex, 0);
    if (depth_rb) {
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER, depth_rb);
    }

    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(s_error, sizeof(s_error),
                 "incomplete framebuffer for swapchain texture %u", color_tex);
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        p_glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (s_fbo_count < GEVR_FBO_CACHE) {
        s_fbos[s_fbo_count].color = color_tex;
        s_fbos[s_fbo_count].depth = depth_rb;
        s_fbos[s_fbo_count].fbo = fbo;
        s_fbo_count++;
    }
    return fbo;
}

void gevr_gl_bind_framebuffer(unsigned fbo, int width, int height)
{
    if (!s_ready) {
        return;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glViewport(0, 0, width, height);
}

void gevr_gl_clear(float r, float g, float b, float a)
{
    if (!s_ready) {
        return;
    }
    p_glClearColor(r, g, b, a);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* -------------------------------------------------------------- shaders */

static GLuint compile(GLenum stage, const char *src)
{
    GLuint sh = p_glCreateShader(stage);
    GLint ok = 0;

    p_glShaderSource(sh, 1, &src, NULL);
    p_glCompileShader(sh);
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLsizei n = 0;
        p_glGetShaderInfoLog(sh, (GLsizei)sizeof(s_error), &n, s_error);
        p_glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs, prog;
    GLint ok = 0;

    if (!vs) {
        return 0;
    }
    fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!fs) {
        p_glDeleteShader(vs);
        return 0;
    }

    prog = p_glCreateProgram();
    p_glAttachShader(prog, vs);
    p_glAttachShader(prog, fs);
    p_glLinkProgram(prog);
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);

    if (!ok) {
        GLsizei n = 0;
        p_glGetProgramInfoLog(prog, (GLsizei)sizeof(s_error), &n, s_error);
        p_glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* ------------------------------------------------------------- vignette */

static GLuint s_vig_prog, s_vig_vao, s_vig_vbo;
static GLint  s_vig_strength_loc;

static const char *k_vig_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_pos;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* Radial falloff kept deliberately soft: a hard edge reads as a black ring
 * and draws the eye instead of quietly narrowing the field of view. */
static const char *k_vig_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform float u_strength;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    float r = length(v_uv);\n"
    "    float inner = mix(1.05, 0.35, clamp(u_strength, 0.0, 1.0));\n"
    "    float a = smoothstep(inner, inner + 0.45, r);\n"
    "    frag = vec4(0.0, 0.0, 0.0, a * clamp(u_strength, 0.0, 1.0));\n"
    "}\n";

static void ensure_vignette(void)
{
    static const GLfloat quad[] = {
        -1.0f, -1.0f,  3.0f, -1.0f,  -1.0f,  3.0f
    };

    if (s_vig_prog) {
        return;
    }
    s_vig_prog = link_program(k_vig_vs, k_vig_fs);
    if (!s_vig_prog) {
        return;
    }
    s_vig_strength_loc = p_glGetUniformLocation(s_vig_prog, "u_strength");

    p_glGenVertexArrays(1, &s_vig_vao);
    p_glBindVertexArray(s_vig_vao);
    p_glGenBuffers(1, &s_vig_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vig_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(GLfloat),
                            (const void *)0);
    p_glBindVertexArray(0);
}

void gevr_gl_draw_vignette(float strength)
{
    if (!s_ready || strength <= 0.001f) {
        return;
    }
    ensure_vignette();
    if (!s_vig_prog) {
        return;
    }

    p_glDisable(GL_DEPTH_TEST);
    p_glEnable(GL_BLEND);
    p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    p_glUseProgram(s_vig_prog);
    p_glUniform1f(s_vig_strength_loc, strength);
    p_glBindVertexArray(s_vig_vao);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glBindVertexArray(0);
    p_glUseProgram(0);

    p_glDisable(GL_BLEND);
    p_glEnable(GL_DEPTH_TEST);
}

void gevr_gl_blit_mirror(unsigned src_fbo, int src_w, int src_h,
                         int dst_w, int dst_h)
{
    if (!s_ready) {
        return;
    }
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    p_glBlitFramebuffer(0, 0, src_w, src_h, 0, 0, dst_w, dst_h,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ------------------------------------------------------- textured quad */

static GLuint s_quad_prog, s_quad_vao, s_quad_vbo;
static GLint  s_quad_rect_loc, s_quad_tex_loc;

static const char *k_quad_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_corner;\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(a_corner.x, 1.0 - a_corner.y);\n"
    "    vec2 p = mix(u_rect.xy, u_rect.zw, a_corner);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *k_quad_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() { frag = texture(u_tex, v_uv); }\n";

static void ensure_quad(void)
{
    static const GLfloat corners[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f
    };

    if (s_quad_prog) {
        return;
    }
    s_quad_prog = link_program(k_quad_vs, k_quad_fs);
    if (!s_quad_prog) {
        return;
    }
    s_quad_rect_loc = p_glGetUniformLocation(s_quad_prog, "u_rect");
    s_quad_tex_loc = p_glGetUniformLocation(s_quad_prog, "u_tex");

    p_glGenVertexArrays(1, &s_quad_vao);
    p_glBindVertexArray(s_quad_vao);
    p_glGenBuffers(1, &s_quad_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(corners), corners,
                   GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                            2 * (GLsizei)sizeof(GLfloat), (const void *)0);
    p_glBindVertexArray(0);
}

void gevr_gl_draw_textured_quad(unsigned texture,
                                float x0, float y0, float x1, float y1)
{
    if (!s_ready) {
        return;
    }
    ensure_quad();
    if (!s_quad_prog) {
        return;
    }

    p_glUseProgram(s_quad_prog);
    p_glUniform4f(s_quad_rect_loc, x0, y0, x1, y1);
    p_glUniform1i(s_quad_tex_loc, 0);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, texture);
    p_glBindVertexArray(s_quad_vao);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    p_glBindVertexArray(0);
    p_glUseProgram(0);
}

/* -------------------------------------------------- calibration scene */

static GLuint s_scene_prog, s_scene_vao, s_scene_vbo;
static GLint  s_scene_mvp_loc, s_scene_color_loc;
static int    s_scene_vertex_count;

static const char *k_scene_vs =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";

static const char *k_scene_fs =
    "#version 330 core\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main() { frag = u_color; }\n";

/* A 1 metre grid. Being able to count squares is what makes world_scale
 * calibratable by eye: walk one square, see how far Bond moved. */
static void ensure_scene(float world_scale)
{
    GLfloat verts[4 * 21 * 3 * 2];
    int n = 0;
    int i;
    const int half = 10;
    const float step = world_scale;
    const float extent = (float)half * world_scale;

    if (s_scene_prog) {
        return;
    }
    s_scene_prog = link_program(k_scene_vs, k_scene_fs);
    if (!s_scene_prog) {
        return;
    }
    s_scene_mvp_loc = p_glGetUniformLocation(s_scene_prog, "u_mvp");
    s_scene_color_loc = p_glGetUniformLocation(s_scene_prog, "u_color");

    for (i = -half; i <= half; i++) {
        float t = (float)i * step;
        verts[n++] = -extent; verts[n++] = 0.0f; verts[n++] = t;
        verts[n++] =  extent; verts[n++] = 0.0f; verts[n++] = t;
        verts[n++] = t; verts[n++] = 0.0f; verts[n++] = -extent;
        verts[n++] = t; verts[n++] = 0.0f; verts[n++] =  extent;
    }
    s_scene_vertex_count = n / 3;

    p_glGenVertexArrays(1, &s_scene_vao);
    p_glBindVertexArray(s_scene_vao);
    p_glGenBuffers(1, &s_scene_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_scene_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(GLfloat) * (size_t)n),
                   verts, GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                            3 * (GLsizei)sizeof(GLfloat), (const void *)0);
    p_glBindVertexArray(0);
}

static void mat4_mul(float *out, const float *a, const float *b)
{
    int c, r;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0]
                           + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2]
                           + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

void gevr_gl_draw_calibration_scene(const float *view, const float *proj,
                                    const float *hand_l_matrix,
                                    const float *hand_r_matrix,
                                    float world_scale)
{
    float vp[16], mvp[16];

    if (!s_ready || !view || !proj) {
        return;
    }
    ensure_scene(world_scale);
    if (!s_scene_prog) {
        return;
    }

    mat4_mul(vp, proj, view);

    p_glEnable(GL_DEPTH_TEST);
    p_glUseProgram(s_scene_prog);
    p_glBindVertexArray(s_scene_vao);

    p_glUniformMatrix4fv(s_scene_mvp_loc, 1, GL_FALSE, vp);
    p_glUniform4f(s_scene_color_loc, 0.25f, 0.55f, 0.35f, 1.0f);
    p_glDrawArrays(GL_LINES, 0, s_scene_vertex_count);

    /* Hand markers reuse the grid geometry shrunk down to a small cross, so
     * the tool stays a single draw path with no extra vertex data. */
    if (hand_l_matrix) {
        mat4_mul(mvp, vp, hand_l_matrix);
        p_glUniformMatrix4fv(s_scene_mvp_loc, 1, GL_FALSE, mvp);
        p_glUniform4f(s_scene_color_loc, 0.9f, 0.7f, 0.2f, 1.0f);
        p_glDrawArrays(GL_LINES, 0, 4);
    }
    if (hand_r_matrix) {
        mat4_mul(mvp, vp, hand_r_matrix);
        p_glUniformMatrix4fv(s_scene_mvp_loc, 1, GL_FALSE, mvp);
        p_glUniform4f(s_scene_color_loc, 0.3f, 0.7f, 0.95f, 1.0f);
        p_glDrawArrays(GL_LINES, 0, 4);
    }

    p_glBindVertexArray(0);
    p_glUseProgram(0);
}

void gevr_gl_shutdown(void)
{
    int i;

    if (!s_ready) {
        return;
    }
    for (i = 0; i < s_fbo_count; i++) {
        GLuint f = s_fbos[i].fbo;
        p_glDeleteFramebuffers(1, &f);
    }
    s_fbo_count = 0;

    if (s_vig_prog)   { p_glDeleteProgram(s_vig_prog);   s_vig_prog = 0; }
    if (s_quad_prog)  { p_glDeleteProgram(s_quad_prog);  s_quad_prog = 0; }
    if (s_scene_prog) { p_glDeleteProgram(s_scene_prog); s_scene_prog = 0; }
    s_ready = 0;
}
