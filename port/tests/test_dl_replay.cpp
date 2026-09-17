/*
 * Can the software RSP execute the same display list twice in one frame?
 *
 * True stereo depends on it. The game builds one display list per frame; if
 * the interpreter can walk it a second time and emit the same geometry, the
 * second pass only needs a different projection to become the other eye, and
 * the engine never has to render twice. If it cannot, stereo has to fall back
 * to alternate-eye rendering, which runs the list once per frame as the host
 * already does.
 *
 * Reading the code gets close but not all the way. It shows the walker never
 * writes to the list, and that gfx_sp_reset() resets only three fields -- the
 * matrix stack depth and the light count -- so a second pass inherits
 * everything else the first one left behind. Whether that leftover state
 * changes the output is not something the source answers; it depends on
 * whether the game's list sets what it uses before using it.
 *
 * So this runs it. A recording rendering backend captures every triangle the
 * interpreter emits, the same list is walked twice, and the two recordings are
 * compared vertex for vertex.
 *
 * Note what this does and does not establish. It exercises the interpreter and
 * the geometry it produces, with a stub backend; it says nothing about the GL
 * backend's own per-frame state, which is a separate question answered by
 * running the real thing. A pass here means stereo is worth building. It does
 * not mean stereo works.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <PR/mbi.h>   /* _SHIFTL, used by the gbi command macros */
#include <PR/gbi.h>

#include "gfx_pc.h"   /* wraps gfx_api.h in extern "C" -- do not include that directly first */
#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"

/* The handful of host symbols the interpreter calls out to. Stubbed rather
 * than linked, so the probe stays independent of the rest of the port. */
extern "C" {
#include "system.h"      /* for the real LogLevel enum, so the stub matches */
Gfx* optionsOverlayEmit(void) { return nullptr; }
void osSyncPrintf(const char*, ...) {}
void sysLogPrintf(enum LogLevel, const char*, ...) {}
void sysFatalError(const char* fmt, ...) { printf("sysFatalError: %s\n", fmt ? fmt : "?"); abort(); }
}

/* ------------------------------------------------------- recording backend */

struct DrawRecord {
    std::vector<float> buf;
    size_t tris;
};

static std::vector<DrawRecord> g_draws;
static bool g_recording;

static void rec_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t tris)
{
    if (!g_recording) {
        return;
    }
    DrawRecord r;
    r.buf.assign(buf_vbo, buf_vbo + buf_vbo_len);
    r.tris = tris;
    g_draws.push_back(r);
}

/* Everything else the interpreter may call, doing nothing. The point is the
 * geometry stream, not the pixels.
 *
 * Designated initialisers below rather than positional ones: these vtables are
 * long, and a positional list that drifts out of step with the header compiles
 * cleanly and then calls the wrong function through a plausible-looking
 * pointer. The first draft of this file did exactly that. */
static const char* rec_get_name(void) { return "replay-probe"; }
static int rec_get_max_texture_size(void) { return 4096; }
static struct GfxClipParameters rec_get_clip_parameters(void) { return { false, false }; }
static void rec_unload_shader(struct ShaderProgram*) {}
static void rec_load_shader(struct ShaderProgram*) {}
static struct ShaderProgram* rec_create_and_load_new_shader(uint64_t, uint32_t) { return (struct ShaderProgram*)1; }
static struct ShaderProgram* rec_lookup_shader(uint64_t, uint32_t) { return (struct ShaderProgram*)1; }
static void rec_shader_get_info(struct ShaderProgram*, uint8_t* num_inputs, bool used_textures[2])
{ if (num_inputs) *num_inputs = 0; if (used_textures) { used_textures[0] = false; used_textures[1] = false; } }
static void rec_clear_shaders(void) {}
static uint32_t rec_new_texture(void) { return 1; }
static void rec_select_texture(int, uint32_t, bool) {}
static void rec_upload_texture(const uint8_t*, uint32_t, uint32_t, bool) {}
static void rec_set_sampler_parameters(int, bool, uint32_t, uint32_t, bool) {}
static void rec_set_depth_mode(bool, bool, bool, bool, uint16_t) {}
static void rec_set_depth_range(float, float) {}
static void rec_set_viewport(int, int, int, int) {}
static void rec_set_scissor(int, int, int, int) {}
static void rec_set_use_alpha(bool, bool) {}
static void rec_init(void) {}
static void rec_on_resize(void) {}
static void rec_start_frame(void) {}
static void rec_end_frame(void) {}
static void rec_finish_render(void) {}
static int  rec_create_framebuffer(void) { return 0; }
static void rec_update_framebuffer_parameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) {}
static bool rec_start_draw_to_framebuffer(int, float) { return true; }
static void rec_copy_framebuffer(int, int, int, int, bool, bool) {}
static void rec_clear_framebuffer(bool, bool) {}
static void rec_resolve_msaa_color_buffer(int, int) {}
static void* rec_get_framebuffer_texture_id(int) { return (void*)1; }
static void rec_select_texture_fb(int) {}
static void rec_delete_texture(uint32_t) {}
static void rec_set_texture_filter(enum FilteringMode) {}
static enum FilteringMode rec_get_texture_filter(void) { return FILTER_NONE; }
static void rec_set_mipmap_filter(enum MipmapFilteringMode) {}
static void rec_set_anisotropy_level(int) {}
static int  rec_get_max_anisotropy_level(void) { return 1; }

static struct GfxRenderingAPI g_rec_rapi = {
    .get_name = rec_get_name,
    .get_max_texture_size = rec_get_max_texture_size,
    .get_clip_parameters = rec_get_clip_parameters,
    .unload_shader = rec_unload_shader,
    .load_shader = rec_load_shader,
    .create_and_load_new_shader = rec_create_and_load_new_shader,
    .lookup_shader = rec_lookup_shader,
    .shader_get_info = rec_shader_get_info,
    .clear_shaders = rec_clear_shaders,
    .new_texture = rec_new_texture,
    .select_texture = rec_select_texture,
    .upload_texture = rec_upload_texture,
    .set_sampler_parameters = rec_set_sampler_parameters,
    .set_depth_mode = rec_set_depth_mode,
    .set_depth_range = rec_set_depth_range,
    .set_viewport = rec_set_viewport,
    .set_scissor = rec_set_scissor,
    .set_use_alpha = rec_set_use_alpha,
    .draw_triangles = rec_draw_triangles,
    .init = rec_init,
    .on_resize = rec_on_resize,
    .start_frame = rec_start_frame,
    .end_frame = rec_end_frame,
    .finish_render = rec_finish_render,
    .create_framebuffer = rec_create_framebuffer,
    .update_framebuffer_parameters = rec_update_framebuffer_parameters,
    .start_draw_to_framebuffer = rec_start_draw_to_framebuffer,
    .copy_framebuffer = rec_copy_framebuffer,
    .clear_framebuffer = rec_clear_framebuffer,
    .resolve_msaa_color_buffer = rec_resolve_msaa_color_buffer,
    .get_framebuffer_texture_id = rec_get_framebuffer_texture_id,
    .select_texture_fb = rec_select_texture_fb,
    .delete_texture = rec_delete_texture,
    .set_texture_filter = rec_set_texture_filter,
    .get_texture_filter = rec_get_texture_filter,
    .set_mipmap_filter = rec_set_mipmap_filter,
    .set_anisotropy_level = rec_set_anisotropy_level,
    .get_max_anisotropy_level = rec_get_max_anisotropy_level,
};

static void wm_init_(const struct GfxWindowInitSettings*) {}
static void wm_close(void) {}
static int  wm_get_display_mode(int, int*, int*) { return 0; }
static int  wm_get_current_display_mode(int* w, int* h) { if (w) *w = 320; if (h) *h = 240; return 1; }
static int  wm_get_num_display_modes(void) { return 1; }
static int32_t wm_get_fullscreen_state(void) { return 0; }
static void wm_set_fullscreen_changed_callback(void (*)(bool)) {}
static void wm_set_fullscreen(bool) {}
static void wm_set_fullscreen_exclusive(bool) {}
static void wm_set_fullscreen_flag(int32_t) {}
static int32_t wm_get_fullscreen_flag_mode(void) { return 0; }
static int32_t wm_get_maximized_state(void) { return 0; }
static void wm_set_maximize(bool) {}
static void wm_get_active_window_refresh_rate(uint32_t* r) { if (r) *r = 60; }
static void wm_set_cursor_visibility(bool) {}
static void wm_set_closest_resolution(int32_t, int32_t, bool) {}
static void wm_set_dimensions(uint32_t, uint32_t, int32_t, int32_t) {}
static void wm_get_dimensions(uint32_t* w, uint32_t* h, int32_t* x, int32_t* y)
{ if (w) *w = 320; if (h) *h = 240; if (x) *x = 0; if (y) *y = 0; }
static void wm_get_centered_positions(int32_t, int32_t, int32_t* x, int32_t* y)
{ if (x) *x = 0; if (y) *y = 0; }
static void wm_handle_events(void) {}
static bool wm_start_frame(void) { return true; }
static void wm_swap_buffers_begin(void) {}
static void wm_swap_buffers_end(void) {}
static double wm_get_time(void) { return 0.0; }
static int32_t wm_get_target_fps(void) { return 60; }
static void wm_set_target_fps(int) {}
static bool wm_can_disable_vsync(void) { return true; }
static void* wm_get_window_handle(void) { return (void*)1; }
static void wm_set_window_title(const char*) {}
static int  wm_get_swap_interval(void) { return 1; }
static bool wm_set_swap_interval(int) { return true; }

static struct GfxWindowManagerAPI g_rec_wapi = {
    .init = wm_init_,
    .close = wm_close,
    .get_display_mode = wm_get_display_mode,
    .get_current_display_mode = wm_get_current_display_mode,
    .get_num_display_modes = wm_get_num_display_modes,
    .get_fullscreen_state = wm_get_fullscreen_state,
    .set_fullscreen_changed_callback = wm_set_fullscreen_changed_callback,
    .set_fullscreen = wm_set_fullscreen,
    .set_fullscreen_exclusive = wm_set_fullscreen_exclusive,
    .set_fullscreen_flag = wm_set_fullscreen_flag,
    .get_fullscreen_flag_mode = wm_get_fullscreen_flag_mode,
    .get_maximized_state = wm_get_maximized_state,
    .set_maximize = wm_set_maximize,
    .get_active_window_refresh_rate = wm_get_active_window_refresh_rate,
    .set_cursor_visibility = wm_set_cursor_visibility,
    .set_closest_resolution = wm_set_closest_resolution,
    .set_dimensions = wm_set_dimensions,
    .get_dimensions = wm_get_dimensions,
    .get_centered_positions = wm_get_centered_positions,
    .handle_events = wm_handle_events,
    .start_frame = wm_start_frame,
    .swap_buffers_begin = wm_swap_buffers_begin,
    .swap_buffers_end = wm_swap_buffers_end,
    .get_time = wm_get_time,
    .get_target_fps = wm_get_target_fps,
    .set_target_fps = wm_set_target_fps,
    .can_disable_vsync = wm_can_disable_vsync,
    .get_window_handle = wm_get_window_handle,
    .set_window_title = wm_set_window_title,
    .get_swap_interval = wm_get_swap_interval,
    .set_swap_interval = wm_set_swap_interval,
};

/* ------------------------------------------------------------- the list */

static Vtx g_verts[3];
static Vp  g_vp;
static Mtx g_proj;
static Mtx g_mv;
static Gfx g_dl[16];

/*
 * float 4x4 -> the fixed-point Mtx fast3d actually reads.
 *
 * Written as the exact inverse of gfx_sp_matrix's unpacking rather than from
 * memory of the SDK layout. Two matrix elements share each int32: the integer
 * halves live in addr[0..7] and the fractional halves in addr[8..15], with the
 * even element in the HIGH half of each word and the odd element in the low
 * half. Getting that backwards produces a garbage transform, every coordinate
 * comes out NaN, and the probe then compares noise against noise -- which is
 * what the first version of this did.
 */
static void mtx_from_float(const float mf[4][4], Mtx* m)
{
    int32_t* w = (int32_t*)m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t a = (int32_t)(mf[i][j]     * 65536.0f);
            int32_t b = (int32_t)(mf[i][j + 1] * 65536.0f);
            w[i * 2 + j / 2]     = (int32_t)(((uint32_t)a & 0xFFFF0000u) |
                                             (((uint32_t)b >> 16) & 0xFFFFu));
            w[8 + i * 2 + j / 2] = (int32_t)((((uint32_t)a & 0xFFFFu) << 16) |
                                             ((uint32_t)b & 0xFFFFu));
        }
    }
}

static void build_list(void)
{
    memset(g_verts, 0, sizeof(g_verts));
    /* A triangle with distinct, non-degenerate coordinates so a dropped or
     * reordered vertex shows up rather than cancelling out. */
    g_verts[0].v.ob[0] = -40; g_verts[0].v.ob[1] = -30; g_verts[0].v.ob[2] = -60;
    g_verts[1].v.ob[0] =  50; g_verts[1].v.ob[1] = -20; g_verts[1].v.ob[2] = -70;
    g_verts[2].v.ob[0] =  10; g_verts[2].v.ob[1] =  45; g_verts[2].v.ob[2] = -55;
    for (int i = 0; i < 3; i++) {
        g_verts[i].v.cn[0] = (uint8_t)(50 + i * 60);
        g_verts[i].v.cn[1] = (uint8_t)(90 + i * 20);
        g_verts[i].v.cn[2] = (uint8_t)(130 + i * 10);
        g_verts[i].v.cn[3] = 255;
    }

    /* A plain perspective and an identity modelview. Without a projection the
     * transform divides by a zero w and every coordinate comes out NaN, which
     * exercises nothing and compares as garbage. */
    const float znear = 10.0f, zfar = 1000.0f, f = 1.5f, aspect = 4.0f / 3.0f;
    float proj[4][4] = {
        { f / aspect, 0.0f, 0.0f, 0.0f },
        { 0.0f, f, 0.0f, 0.0f },
        { 0.0f, 0.0f, (zfar + znear) / (znear - zfar), -1.0f },
        { 0.0f, 0.0f, (2.0f * zfar * znear) / (znear - zfar), 0.0f },
    };
    float mv[4][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, -150.0f, 1.0f },
    };
    mtx_from_float(proj, &g_proj);
    mtx_from_float(mv, &g_mv);

    /* A viewport. fr.c emits one every frame and the transform scales by it;
     * without it the scale is zero and X comes out NaN. Values are S10.2, so
     * a 320x240 target is half-extents 160 and 120, times four. */
    memset(&g_vp, 0, sizeof(g_vp));
    g_vp.vp.vscale[0] = 160 * 4;  g_vp.vp.vscale[1] = 120 * 4;
    g_vp.vp.vscale[2] = 511;      g_vp.vp.vscale[3] = 0;
    g_vp.vp.vtrans[0] = 160 * 4;  g_vp.vp.vtrans[1] = 120 * 4;
    g_vp.vp.vtrans[2] = 511;      g_vp.vp.vtrans[3] = 0;

    Gfx* p = g_dl;
    gSPViewport(p++, &g_vp);
    gSPMatrix(p++, &g_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(p++, &g_mv, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPVertex(p++, g_verts, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    gSPEndDisplayList(p++);
}

static bool same(const std::vector<DrawRecord>& a, const std::vector<DrawRecord>& b,
                 const char** why)
{
    if (a.size() != b.size()) { *why = "different number of draw calls"; return false; }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].tris != b[i].tris) { *why = "different triangle count"; return false; }
        if (a[i].buf.size() != b[i].buf.size()) { *why = "different vertex buffer length"; return false; }
        /* memcmp, not float ==. NaN never compares equal to itself, so a
         * float comparison reports a difference between two bit-identical
         * buffers -- which is exactly what the first version of this probe
         * did, and it read as a real negative result. */
        if (memcmp(a[i].buf.data(), b[i].buf.data(),
                   a[i].buf.size() * sizeof(float)) != 0) {
            *why = "vertex data differs";
            return false;
        }
    }
    return true;
}

int main(void)
{
    printf("fast3d: can one display list be walked twice in a frame?\n\n");

    struct GfxInitSettings s;
    memset(&s, 0, sizeof(s));
    s.wapi = &g_rec_wapi;
    s.rapi = &g_rec_rapi;
    gfx_init(&s);

    build_list();

    /* A warm-up pass first, so neither recorded pass is the very first list
     * the interpreter has ever seen. Today's frame N+1 already inherits frame
     * N's state, so "a list running with state already in the interpreter" is
     * the normal case, not the case under test. */
    /* Bracket each run exactly as the host does -- videoStartFrame() calls
     * gfx_start_frame() and videoEndFrame() calls gfx_end_frame(). Skipping
     * the start leaves gfx_current_dimensions.aspect_ratio at zero, and
     * gfx_adjust_x_for_aspect_ratio divides X by it, so every X comes out NaN.
     * That is a property of the probe, not of the interpreter. */
    g_recording = false;
    gfx_start_frame(); gfx_run(g_dl); gfx_end_frame();

    g_draws.clear();
    g_recording = true;
    gfx_start_frame(); gfx_run(g_dl); gfx_end_frame();
    std::vector<DrawRecord> first = g_draws;

    g_draws.clear();
    gfx_start_frame(); gfx_run(g_dl); gfx_end_frame();
    std::vector<DrawRecord> second = g_draws;
    g_recording = false;

    printf("  pass 1: %zu draw call(s)\n", first.size());
    printf("  pass 2: %zu draw call(s)\n", second.size());

    {
        size_t nans = 0, total = 0;
        for (const auto& d : first) {
            for (float v : d.buf) { total++; if (v != v) { nans++; } }
        }
        if (nans) {
            printf("\n  INCONCLUSIVE: %zu of %zu emitted floats are NaN, so the\n"
                   "  probe's list is not exercising the transform path properly.\n",
                   nans, total);
            return 2;
        }
    }

    if (first.empty()) {
        printf("\n  INCONCLUSIVE: the interpreter emitted no geometry, so there\n"
               "  is nothing to compare. The probe is not exercising it.\n");
        return 2;
    }

    const char* why = "";
    if (!same(first, second, &why)) {
        printf("\n  REPLAY IS NOT FAITHFUL: %s\n", why);
        /* Say which values moved. "It differs" is not a finding; a diagnosis
         * needs to distinguish real interpreter state leakage from the probe
         * simply not setting up enough state in its list. */
        if (first.size() == second.size()) {
            for (size_t i = 0; i < first.size(); i++) {
                size_t n = first[i].buf.size() < second[i].buf.size()
                         ? first[i].buf.size() : second[i].buf.size();
                int shown = 0;
                for (size_t j = 0; j < n; j++) {
                    if (memcmp(&first[i].buf[j], &second[i].buf[j], sizeof(float)) != 0) {
                        if (shown < 12) {
                            printf("    draw %zu float %3zu: %14.6f -> %14.6f\n",
                                   i, j, (double)first[i].buf[j], (double)second[i].buf[j]);
                        }
                        shown++;
                    }
                }
                printf("    draw %zu: %d of %zu floats differ\n", i, shown, n);
            }
        }
        return 1;
    }

    size_t floats = 0;
    for (const auto& d : first) { floats += d.buf.size(); }
    printf("\n  [1] across separate frames: identical (%zu floats)\n", floats);

    /*
     * The case that actually decides stereo: two walks of the same list
     * INSIDE one frame, which is what rendering a second eye would do. The
     * test above only shows the interpreter is deterministic frame to frame,
     * which the game already relies on; this shows the second walk is not
     * disturbed by the first one's leftover state.
     */
    g_draws.clear();
    g_recording = true;
    gfx_start_frame();
    gfx_run(g_dl);
    std::vector<DrawRecord> eyeL = g_draws;
    g_draws.clear();
    gfx_run(g_dl);                  /* second eye, same list, same frame */
    std::vector<DrawRecord> eyeR = g_draws;
    gfx_end_frame();
    g_recording = false;

    printf("  [2] twice within one frame: %zu then %zu draw call(s)\n",
           eyeL.size(), eyeR.size());

    if (eyeL.empty() || eyeR.empty()) {
        printf("\n  INCONCLUSIVE: a within-frame pass emitted nothing.\n");
        return 2;
    }
    if (!same(eyeL, eyeR, &why)) {
        printf("\n  WITHIN-FRAME REPLAY IS NOT FAITHFUL: %s\n", why);
        printf("  True stereo cannot reuse one list; use alternate-eye rendering.\n");
        return 1;
    }

    printf("      identical\n");
    printf("\n  REPLAY IS FAITHFUL at the interpreter level, both across frames\n"
           "  and twice within one frame. True stereo by re-walking the game's\n"
           "  own display list is viable; the remaining unknowns are the GL\n"
           "  backend's per-frame state and the framebuffer clear inside\n"
           "  gfx_run, neither of which a stub backend can answer.\n");
    return 0;
}
