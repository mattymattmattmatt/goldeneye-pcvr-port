#include "gevr_xr.h"
#include "gevr_gl.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_GRAPHICS_API_OPENGL
#ifdef _WIN32
#  define XR_USE_PLATFORM_WIN32
#  include <windows.h>
#else
#  define XR_USE_PLATFORM_XLIB
#  include <X11/Xlib.h>
#  include <GL/glx.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define GEVR_MAX_SWAPCHAIN_IMAGES 8

typedef struct gevr_swapchain {
    XrSwapchain             handle;
    int                     width, height;
    uint32_t                image_count;
    XrSwapchainImageOpenGLKHR images[GEVR_MAX_SWAPCHAIN_IMAGES];
    unsigned                depth[GEVR_MAX_SWAPCHAIN_IMAGES];
    uint32_t                acquired_index;
    int                     acquired;
} gevr_swapchain;

struct gevr_xr {
    gevr_config      cfg;

    XrInstance       instance;
    XrSystemId       system;
    XrSession        session;
    XrSessionState   state;
    int              session_running;
    int              exit_requested;

    XrSpace          base_space;       /* stage if available, else local */
    XrSpace          view_space;
    int              has_stage;

    /* Recentre is implemented by re-creating base_space with an offset,
     * which is the only way to move the origin for a LOCAL space and works
     * identically for STAGE. */
    XrReferenceSpaceType base_space_type;
    XrPosef          base_offset;

    gevr_swapchain   eyes[GEVR_EYE_COUNT];
    XrView           views[GEVR_EYE_COUNT];
    XrCompositionLayerProjectionView proj_views[GEVR_EYE_COUNT];
    int              views_valid;

    int              rec_width, rec_height;

    XrFrameState     frame_state;
    int              frame_active;
    int              should_render;
    XrTime           last_display_time;

    /* ---- input ---- */
    XrActionSet      action_set;
    XrAction         a_move, a_turn;
    XrAction         a_trigger[2], a_grip[2];
    XrAction         a_btn_a, a_btn_b, a_btn_x, a_btn_y, a_menu;
    XrAction         a_stick_click[2];
    XrAction         a_pose_aim[2], a_pose_grip[2];
    XrAction         a_haptic[2];
    XrSpace          space_aim[2], space_grip[2];
    XrPath           hand_path[2];
    char             profile_name[2][XR_MAX_PATH_LENGTH];

    SDL_Window      *window;
    SDL_GLContext    gl_context;
    int              owns_window;

    char             runtime_name[XR_MAX_RUNTIME_NAME_SIZE];
    char             system_name[XR_MAX_SYSTEM_NAME_SIZE];
    char             error[512];
};

/* --------------------------------------------------------------- helpers */

static void set_error(gevr_xr *xr, const char *fmt, ...)
{
    va_list ap;
    if (!xr) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(xr->error, sizeof(xr->error), fmt, ap);
    va_end(ap);
}

static const char *xr_result_str(gevr_xr *xr, XrResult r)
{
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (xr && xr->instance != XR_NULL_HANDLE &&
        XR_SUCCEEDED(xrResultToString(xr->instance, r, buf))) {
        return buf;
    }
    snprintf(buf, sizeof(buf), "XrResult(%d)", (int)r);
    return buf;
}

#define XR_CHECK(xr, call, what)                                              \
    do {                                                                      \
        XrResult _r = (call);                                                 \
        if (XR_FAILED(_r)) {                                                  \
            set_error((xr), "%s failed: %s", (what), xr_result_str((xr), _r)); \
            return -1;                                                        \
        }                                                                     \
    } while (0)

const char *gevr_xr_last_error(const gevr_xr *xr)
{
    return (xr && xr->error[0]) ? xr->error : "";
}

const char *gevr_xr_runtime_name(const gevr_xr *xr)
{
    return xr ? xr->runtime_name : "";
}

const char *gevr_xr_system_name(const gevr_xr *xr)
{
    return xr ? xr->system_name : "";
}

const char *gevr_xr_profile_name(const gevr_xr *xr, int hand)
{
    if (!xr || hand < 0 || hand > 1) {
        return "";
    }
    return xr->profile_name[hand];
}

int gevr_xr_has_stage_space(const gevr_xr *xr)
{
    return xr ? xr->has_stage : 0;
}

long long gevr_xr_predicted_display_time(const gevr_xr *xr)
{
    return xr ? (long long)xr->frame_state.predictedDisplayTime : 0;
}

void gevr_xr_recommended_size(const gevr_xr *xr, int *w, int *h)
{
    if (!xr) {
        return;
    }
    if (w) { *w = xr->rec_width; }
    if (h) { *h = xr->rec_height; }
}

static gevr_pose from_xr_pose(const XrPosef *p)
{
    gevr_pose r;
    r.orientation.x = p->orientation.x;
    r.orientation.y = p->orientation.y;
    r.orientation.z = p->orientation.z;
    r.orientation.w = p->orientation.w;
    r.position.x = p->position.x;
    r.position.y = p->position.y;
    r.position.z = p->position.z;
    return r;
}

/* ---------------------------------------------------------- instance */

static int create_instance(gevr_xr *xr, const gevr_xr_desc *desc)
{
    XrInstanceCreateInfo ci;
    XrInstanceProperties props;
    const char *exts[2];
    uint32_t ext_count = 0;
    uint32_t avail = 0;
    XrExtensionProperties *list = NULL;
    uint32_t i;
    int have_gl = 0;

    xrEnumerateInstanceExtensionProperties(NULL, 0, &avail, NULL);
    if (avail) {
        list = (XrExtensionProperties *)calloc(avail, sizeof(*list));
        if (!list) {
            set_error(xr, "out of memory enumerating extensions");
            return -1;
        }
        for (i = 0; i < avail; i++) {
            list[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        }
        xrEnumerateInstanceExtensionProperties(NULL, avail, &avail, list);
        for (i = 0; i < avail; i++) {
            if (!strcmp(list[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
                have_gl = 1;
            }
        }
        free(list);
    }

    if (!have_gl) {
        set_error(xr,
                  "the OpenXR runtime does not expose %s. "
                  "With SteamVR, make sure it is set as the active OpenXR runtime.",
                  XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
        return -1;
    }
    exts[ext_count++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    snprintf(ci.applicationInfo.applicationName,
             sizeof(ci.applicationInfo.applicationName), "%s",
             (desc && desc->app_name) ? desc->app_name : "GoldenEye VR");
    ci.applicationInfo.applicationVersion = desc ? desc->app_version : 1;
    snprintf(ci.applicationInfo.engineName,
             sizeof(ci.applicationInfo.engineName), "gevr");
    ci.applicationInfo.engineVersion = 1;
    ci.enabledExtensionCount = ext_count;
    ci.enabledExtensionNames = exts;

    XR_CHECK(xr, xrCreateInstance(&ci, &xr->instance), "xrCreateInstance");

    memset(&props, 0, sizeof(props));
    props.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (XR_SUCCEEDED(xrGetInstanceProperties(xr->instance, &props))) {
        snprintf(xr->runtime_name, sizeof(xr->runtime_name), "%s", props.runtimeName);
    }
    return 0;
}

static int get_system(gevr_xr *xr)
{
    XrSystemGetInfo gi;
    XrSystemProperties sp;

    memset(&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_SYSTEM_GET_INFO;
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XR_CHECK(xr, xrGetSystem(xr->instance, &gi, &xr->system), "xrGetSystem");

    memset(&sp, 0, sizeof(sp));
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (XR_SUCCEEDED(xrGetSystemProperties(xr->instance, xr->system, &sp))) {
        snprintf(xr->system_name, sizeof(xr->system_name), "%s", sp.systemName);
    }
    return 0;
}

/* ------------------------------------------------------------- graphics */

static int create_gl_context(gevr_xr *xr, const gevr_xr_desc *desc)
{
    if (!desc || !desc->create_window) {
        /* Integrated build: the game already owns a context. */
        xr->gl_context = SDL_GL_GetCurrentContext();
        xr->window = SDL_GL_GetCurrentWindow();
        if (!xr->gl_context) {
            set_error(xr, "create_window was 0 but no GL context is current");
            return -1;
        }
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        set_error(xr, "SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    xr->window = SDL_CreateWindow("GoldenEye VR",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  960, 540,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!xr->window) {
        set_error(xr, "SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }

    xr->gl_context = SDL_GL_CreateContext(xr->window);
    if (!xr->gl_context) {
        set_error(xr, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        return -1;
    }
    xr->owns_window = 1;

    /* The compositor paces us; a swap interval here would double-throttle. */
    SDL_GL_SetSwapInterval(0);

    if (gevr_gl_init() != 0) {
        set_error(xr, "GL init failed: %s", gevr_gl_last_error());
        return -1;
    }
    return 0;
}

static int create_session(gevr_xr *xr)
{
    XrSessionCreateInfo ci;
    XrGraphicsRequirementsOpenGLKHR req;
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_req = NULL;

    memset(&req, 0, sizeof(req));
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;

    /* Mandatory: the spec requires this call before xrCreateSession, and
     * runtimes do reject the session if it is skipped. */
    XR_CHECK(xr, xrGetInstanceProcAddr(xr->instance,
                                       "xrGetOpenGLGraphicsRequirementsKHR",
                                       (PFN_xrVoidFunction *)&get_req),
             "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)");
    XR_CHECK(xr, get_req(xr->instance, xr->system, &req),
             "xrGetOpenGLGraphicsRequirementsKHR");

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_SESSION_CREATE_INFO;
    ci.systemId = xr->system;

#ifdef _WIN32
    {
        XrGraphicsBindingOpenGLWin32KHR bind;
        memset(&bind, 0, sizeof(bind));
        bind.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
        bind.hDC = wglGetCurrentDC();
        bind.hGLRC = wglGetCurrentContext();
        if (!bind.hDC || !bind.hGLRC) {
            set_error(xr, "no current WGL context for the OpenXR graphics binding");
            return -1;
        }
        ci.next = &bind;
        XR_CHECK(xr, xrCreateSession(xr->instance, &ci, &xr->session),
                 "xrCreateSession");
    }
#else
    {
        XrGraphicsBindingOpenGLXlibKHR bind;
        SDL_SysWMinfo wm;

        SDL_VERSION(&wm.version);
        if (!xr->window || !SDL_GetWindowWMInfo(xr->window, &wm)) {
            set_error(xr, "SDL_GetWindowWMInfo failed: %s", SDL_GetError());
            return -1;
        }
        if (wm.subsystem != SDL_SYSWM_X11) {
            set_error(xr,
                      "this build needs an X11 window for the GLX binding; "
                      "run with SDL_VIDEODRIVER=x11 (XWayland is fine)");
            return -1;
        }

        memset(&bind, 0, sizeof(bind));
        bind.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
        bind.xDisplay = wm.info.x11.display;
        bind.glxDrawable = (GLXDrawable)wm.info.x11.window;
        bind.glxContext = glXGetCurrentContext();
        bind.visualid = 0;
        bind.glxFBConfig = 0;

        if (!bind.glxContext) {
            set_error(xr, "no current GLX context for the OpenXR graphics binding");
            return -1;
        }

        ci.next = &bind;
        XR_CHECK(xr, xrCreateSession(xr->instance, &ci, &xr->session),
                 "xrCreateSession");
    }
#endif
    return 0;
}

/* --------------------------------------------------------------- spaces */

static int create_base_space(gevr_xr *xr)
{
    XrReferenceSpaceCreateInfo ci;
    uint32_t count = 0, i;
    XrReferenceSpaceType *types = NULL;

    xrEnumerateReferenceSpaces(xr->session, 0, &count, NULL);
    if (count) {
        types = (XrReferenceSpaceType *)calloc(count, sizeof(*types));
        if (types) {
            xrEnumerateReferenceSpaces(xr->session, count, &count, types);
            for (i = 0; i < count; i++) {
                if (types[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
                    xr->has_stage = 1;
                }
            }
            free(types);
        }
    }

    /* LOCAL is preferred as the play origin even when STAGE exists: LOCAL is
     * head-relative at startup, which is what a seated player in Virtual
     * Desktop expects, and roomscale offsets are applied by gevr_camera. */
    xr->base_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xr->base_offset.orientation.x = 0.0f;
    xr->base_offset.orientation.y = 0.0f;
    xr->base_offset.orientation.z = 0.0f;
    xr->base_offset.orientation.w = 1.0f;
    xr->base_offset.position.x = 0.0f;
    xr->base_offset.position.y = 0.0f;
    xr->base_offset.position.z = 0.0f;

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    ci.referenceSpaceType = xr->base_space_type;
    ci.poseInReferenceSpace = xr->base_offset;
    XR_CHECK(xr, xrCreateReferenceSpace(xr->session, &ci, &xr->base_space),
             "xrCreateReferenceSpace(base)");

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    ci.poseInReferenceSpace.orientation.w = 1.0f;
    XR_CHECK(xr, xrCreateReferenceSpace(xr->session, &ci, &xr->view_space),
             "xrCreateReferenceSpace(view)");
    return 0;
}

/* ---------------------------------------------------------- swapchains */

static int64_t pick_swapchain_format(gevr_xr *xr)
{
    /* SRGB8_ALPHA8 first: the runtime then does the correct linear->sRGB
     * conversion on submit. RGBA8 is the fallback for runtimes that do not
     * offer an sRGB target. */
    static const int64_t preferred[] = { 0x8C43 /*SRGB8_ALPHA8*/, 0x8058 /*RGBA8*/ };
    uint32_t count = 0, i;
    size_t p;
    int64_t *formats;
    int64_t chosen = 0;

    xrEnumerateSwapchainFormats(xr->session, 0, &count, NULL);
    if (!count) {
        return 0;
    }
    formats = (int64_t *)calloc(count, sizeof(int64_t));
    if (!formats) {
        return 0;
    }
    xrEnumerateSwapchainFormats(xr->session, count, &count, formats);

    for (p = 0; p < sizeof(preferred) / sizeof(preferred[0]) && !chosen; p++) {
        for (i = 0; i < count; i++) {
            if (formats[i] == preferred[p]) {
                chosen = formats[i];
                break;
            }
        }
    }
    if (!chosen) {
        chosen = formats[0];
    }
    free(formats);
    return chosen;
}

static int create_swapchains(gevr_xr *xr)
{
    XrViewConfigurationView cfgviews[GEVR_EYE_COUNT];
    uint32_t view_count = 0;
    int64_t format;
    int e;
    uint32_t i;

    memset(cfgviews, 0, sizeof(cfgviews));
    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        cfgviews[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }

    XR_CHECK(xr, xrEnumerateViewConfigurationViews(
                     xr->instance, xr->system,
                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                     GEVR_EYE_COUNT, &view_count, cfgviews),
             "xrEnumerateViewConfigurationViews");

    if (view_count < GEVR_EYE_COUNT) {
        set_error(xr, "expected %d stereo views, runtime reported %u",
                  GEVR_EYE_COUNT, view_count);
        return -1;
    }

    format = pick_swapchain_format(xr);
    if (!format) {
        set_error(xr, "no usable swapchain format");
        return -1;
    }

    xr->rec_width = (int)((float)cfgviews[0].recommendedImageRectWidth
                          * xr->cfg.render_scale);
    xr->rec_height = (int)((float)cfgviews[0].recommendedImageRectHeight
                           * xr->cfg.render_scale);
    if (xr->rec_width < 16)  { xr->rec_width = 16; }
    if (xr->rec_height < 16) { xr->rec_height = 16; }
    if ((uint32_t)xr->rec_width > cfgviews[0].maxImageRectWidth) {
        xr->rec_width = (int)cfgviews[0].maxImageRectWidth;
    }
    if ((uint32_t)xr->rec_height > cfgviews[0].maxImageRectHeight) {
        xr->rec_height = (int)cfgviews[0].maxImageRectHeight;
    }

    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        XrSwapchainCreateInfo sci;
        gevr_swapchain *sc = &xr->eyes[e];
        uint32_t img_count = 0;

        memset(&sci, 0, sizeof(sci));
        sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
                       | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = format;
        sci.sampleCount = 1;
        sci.width = (uint32_t)xr->rec_width;
        sci.height = (uint32_t)xr->rec_height;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;

        XR_CHECK(xr, xrCreateSwapchain(xr->session, &sci, &sc->handle),
                 "xrCreateSwapchain");

        sc->width = xr->rec_width;
        sc->height = xr->rec_height;

        XR_CHECK(xr, xrEnumerateSwapchainImages(sc->handle, 0, &img_count, NULL),
                 "xrEnumerateSwapchainImages(count)");
        if (img_count > GEVR_MAX_SWAPCHAIN_IMAGES) {
            img_count = GEVR_MAX_SWAPCHAIN_IMAGES;
        }
        for (i = 0; i < img_count; i++) {
            sc->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }
        XR_CHECK(xr, xrEnumerateSwapchainImages(
                         sc->handle, img_count, &img_count,
                         (XrSwapchainImageBaseHeader *)sc->images),
                 "xrEnumerateSwapchainImages");
        sc->image_count = img_count;

        for (i = 0; i < img_count; i++) {
            sc->depth[i] = gevr_gl_create_depth(sc->width, sc->height);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- input */

static XrAction make_action(gevr_xr *xr, XrActionType type,
                            const char *name, const char *localized)
{
    XrActionCreateInfo ci;
    XrAction action = XR_NULL_HANDLE;

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_ACTION_CREATE_INFO;
    ci.actionType = type;
    snprintf(ci.actionName, sizeof(ci.actionName), "%s", name);
    snprintf(ci.localizedActionName, sizeof(ci.localizedActionName), "%s",
             localized);

    if (XR_FAILED(xrCreateAction(xr->action_set, &ci, &action))) {
        return XR_NULL_HANDLE;
    }
    return action;
}

typedef struct binding_def {
    XrAction    action;
    const char *path;
} binding_def;

static int suggest_profile(gevr_xr *xr, const char *profile,
                           const binding_def *defs, int count)
{
    XrInteractionProfileSuggestedBinding sb;
    XrActionSuggestedBinding *list;
    XrPath profile_path;
    int i, n = 0;
    XrResult r;

    if (XR_FAILED(xrStringToPath(xr->instance, profile, &profile_path))) {
        return -1;
    }

    list = (XrActionSuggestedBinding *)calloc((size_t)count, sizeof(*list));
    if (!list) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        XrPath p;
        if (defs[i].action == XR_NULL_HANDLE) {
            continue;
        }
        if (XR_FAILED(xrStringToPath(xr->instance, defs[i].path, &p))) {
            continue;
        }
        list[n].action = defs[i].action;
        list[n].binding = p;
        n++;
    }

    memset(&sb, 0, sizeof(sb));
    sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    sb.interactionProfile = profile_path;
    sb.suggestedBindings = list;
    sb.countSuggestedBindings = (uint32_t)n;

    /* A profile the runtime does not know is not an error worth failing on:
     * SteamVR only advertises the devices actually connected. */
    r = xrSuggestInteractionProfileBindings(xr->instance, &sb);
    free(list);
    return XR_SUCCEEDED(r) ? 0 : -1;
}

static int create_actions(gevr_xr *xr)
{
    XrActionSetCreateInfo asci;
    XrSessionActionSetsAttachInfo attach;
    int e;

    memset(&asci, 0, sizeof(asci));
    asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    snprintf(asci.actionSetName, sizeof(asci.actionSetName), "gameplay");
    snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName),
             "Gameplay");
    asci.priority = 0;
    XR_CHECK(xr, xrCreateActionSet(xr->instance, &asci, &xr->action_set),
             "xrCreateActionSet");

    xrStringToPath(xr->instance, "/user/hand/left", &xr->hand_path[0]);
    xrStringToPath(xr->instance, "/user/hand/right", &xr->hand_path[1]);

    xr->a_move = make_action(xr, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move");
    xr->a_turn = make_action(xr, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn");
    xr->a_trigger[0] = make_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_l", "Left Trigger");
    xr->a_trigger[1] = make_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_r", "Right Trigger");
    xr->a_grip[0] = make_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "grip_l", "Left Grip");
    xr->a_grip[1] = make_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "grip_r", "Right Grip");
    xr->a_btn_a = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_a", "A");
    xr->a_btn_b = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_b", "B");
    xr->a_btn_x = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_x", "X");
    xr->a_btn_y = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_y", "Y");
    xr->a_menu = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu");
    xr->a_stick_click[0] = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_l", "Left Stick Click");
    xr->a_stick_click[1] = make_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_r", "Right Stick Click");
    xr->a_pose_aim[0] = make_action(xr, XR_ACTION_TYPE_POSE_INPUT, "aim_l", "Left Aim");
    xr->a_pose_aim[1] = make_action(xr, XR_ACTION_TYPE_POSE_INPUT, "aim_r", "Right Aim");
    xr->a_pose_grip[0] = make_action(xr, XR_ACTION_TYPE_POSE_INPUT, "grip_pose_l", "Left Grip Pose");
    xr->a_pose_grip[1] = make_action(xr, XR_ACTION_TYPE_POSE_INPUT, "grip_pose_r", "Right Grip Pose");
    xr->a_haptic[0] = make_action(xr, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic_l", "Left Haptic");
    xr->a_haptic[1] = make_action(xr, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic_r", "Right Haptic");

    if (!xr->a_move || !xr->a_turn || !xr->a_pose_aim[0] || !xr->a_pose_aim[1]) {
        set_error(xr, "failed to create the core input actions");
        return -1;
    }

    /* ---- Oculus Touch: the Quest 3 path through Link or Virtual Desktop ---- */
    {
        const binding_def b[] = {
            { xr->a_move,          "/user/hand/left/input/thumbstick" },
            { xr->a_turn,          "/user/hand/right/input/thumbstick" },
            { xr->a_trigger[0],    "/user/hand/left/input/trigger/value" },
            { xr->a_trigger[1],    "/user/hand/right/input/trigger/value" },
            { xr->a_grip[0],       "/user/hand/left/input/squeeze/value" },
            { xr->a_grip[1],       "/user/hand/right/input/squeeze/value" },
            { xr->a_btn_a,         "/user/hand/right/input/a/click" },
            { xr->a_btn_b,         "/user/hand/right/input/b/click" },
            { xr->a_btn_x,         "/user/hand/left/input/x/click" },
            { xr->a_btn_y,         "/user/hand/left/input/y/click" },
            { xr->a_menu,          "/user/hand/left/input/menu/click" },
            { xr->a_stick_click[0],"/user/hand/left/input/thumbstick/click" },
            { xr->a_stick_click[1],"/user/hand/right/input/thumbstick/click" },
            { xr->a_pose_aim[0],   "/user/hand/left/input/aim/pose" },
            { xr->a_pose_aim[1],   "/user/hand/right/input/aim/pose" },
            { xr->a_pose_grip[0],  "/user/hand/left/input/grip/pose" },
            { xr->a_pose_grip[1],  "/user/hand/right/input/grip/pose" },
            { xr->a_haptic[0],     "/user/hand/left/output/haptic" },
            { xr->a_haptic[1],     "/user/hand/right/output/haptic" }
        };
        suggest_profile(xr, "/interaction_profiles/oculus/touch_controller",
                        b, (int)(sizeof(b) / sizeof(b[0])));
    }

    /* ---- Valve Index: A/B on both hands, no separate menu ---- */
    {
        const binding_def b[] = {
            { xr->a_move,          "/user/hand/left/input/thumbstick" },
            { xr->a_turn,          "/user/hand/right/input/thumbstick" },
            { xr->a_trigger[0],    "/user/hand/left/input/trigger/value" },
            { xr->a_trigger[1],    "/user/hand/right/input/trigger/value" },
            { xr->a_grip[0],       "/user/hand/left/input/squeeze/value" },
            { xr->a_grip[1],       "/user/hand/right/input/squeeze/value" },
            { xr->a_btn_a,         "/user/hand/right/input/a/click" },
            { xr->a_btn_b,         "/user/hand/right/input/b/click" },
            { xr->a_btn_x,         "/user/hand/left/input/a/click" },
            { xr->a_btn_y,         "/user/hand/left/input/b/click" },
            { xr->a_stick_click[0],"/user/hand/left/input/thumbstick/click" },
            { xr->a_stick_click[1],"/user/hand/right/input/thumbstick/click" },
            { xr->a_pose_aim[0],   "/user/hand/left/input/aim/pose" },
            { xr->a_pose_aim[1],   "/user/hand/right/input/aim/pose" },
            { xr->a_pose_grip[0],  "/user/hand/left/input/grip/pose" },
            { xr->a_pose_grip[1],  "/user/hand/right/input/grip/pose" },
            { xr->a_haptic[0],     "/user/hand/left/output/haptic" },
            { xr->a_haptic[1],     "/user/hand/right/output/haptic" }
        };
        suggest_profile(xr, "/interaction_profiles/valve/index_controller",
                        b, (int)(sizeof(b) / sizeof(b[0])));
    }

    /* ---- Vive wands: trackpad stands in for the thumbsticks ---- */
    {
        const binding_def b[] = {
            { xr->a_move,          "/user/hand/left/input/trackpad" },
            { xr->a_turn,          "/user/hand/right/input/trackpad" },
            { xr->a_trigger[0],    "/user/hand/left/input/trigger/value" },
            { xr->a_trigger[1],    "/user/hand/right/input/trigger/value" },
            { xr->a_grip[0],       "/user/hand/left/input/squeeze/click" },
            { xr->a_grip[1],       "/user/hand/right/input/squeeze/click" },
            { xr->a_menu,          "/user/hand/left/input/menu/click" },
            { xr->a_btn_y,         "/user/hand/right/input/menu/click" },
            { xr->a_stick_click[0],"/user/hand/left/input/trackpad/click" },
            { xr->a_stick_click[1],"/user/hand/right/input/trackpad/click" },
            { xr->a_pose_aim[0],   "/user/hand/left/input/aim/pose" },
            { xr->a_pose_aim[1],   "/user/hand/right/input/aim/pose" },
            { xr->a_pose_grip[0],  "/user/hand/left/input/grip/pose" },
            { xr->a_pose_grip[1],  "/user/hand/right/input/grip/pose" },
            { xr->a_haptic[0],     "/user/hand/left/output/haptic" },
            { xr->a_haptic[1],     "/user/hand/right/output/haptic" }
        };
        suggest_profile(xr, "/interaction_profiles/htc/vive_controller",
                        b, (int)(sizeof(b) / sizeof(b[0])));
    }

    /* ---- Windows Mixed Reality ---- */
    {
        const binding_def b[] = {
            { xr->a_move,          "/user/hand/left/input/thumbstick" },
            { xr->a_turn,          "/user/hand/right/input/thumbstick" },
            { xr->a_trigger[0],    "/user/hand/left/input/trigger/value" },
            { xr->a_trigger[1],    "/user/hand/right/input/trigger/value" },
            { xr->a_grip[0],       "/user/hand/left/input/squeeze/click" },
            { xr->a_grip[1],       "/user/hand/right/input/squeeze/click" },
            { xr->a_menu,          "/user/hand/left/input/menu/click" },
            { xr->a_btn_y,         "/user/hand/right/input/menu/click" },
            { xr->a_stick_click[0],"/user/hand/left/input/thumbstick/click" },
            { xr->a_stick_click[1],"/user/hand/right/input/thumbstick/click" },
            { xr->a_pose_aim[0],   "/user/hand/left/input/aim/pose" },
            { xr->a_pose_aim[1],   "/user/hand/right/input/aim/pose" },
            { xr->a_pose_grip[0],  "/user/hand/left/input/grip/pose" },
            { xr->a_pose_grip[1],  "/user/hand/right/input/grip/pose" },
            { xr->a_haptic[0],     "/user/hand/left/output/haptic" },
            { xr->a_haptic[1],     "/user/hand/right/output/haptic" }
        };
        suggest_profile(xr, "/interaction_profiles/microsoft/motion_controller",
                        b, (int)(sizeof(b) / sizeof(b[0])));
    }

    /* ---- Khronos simple controller: last-resort fallback ---- */
    {
        const binding_def b[] = {
            { xr->a_trigger[0],   "/user/hand/left/input/select/click" },
            { xr->a_trigger[1],   "/user/hand/right/input/select/click" },
            { xr->a_menu,         "/user/hand/left/input/menu/click" },
            { xr->a_btn_y,        "/user/hand/right/input/menu/click" },
            { xr->a_pose_aim[0],  "/user/hand/left/input/aim/pose" },
            { xr->a_pose_aim[1],  "/user/hand/right/input/aim/pose" },
            { xr->a_pose_grip[0], "/user/hand/left/input/grip/pose" },
            { xr->a_pose_grip[1], "/user/hand/right/input/grip/pose" },
            { xr->a_haptic[0],    "/user/hand/left/output/haptic" },
            { xr->a_haptic[1],    "/user/hand/right/output/haptic" }
        };
        suggest_profile(xr, "/interaction_profiles/khr/simple_controller",
                        b, (int)(sizeof(b) / sizeof(b[0])));
    }

    /* Action spaces for the poses we actually read. */
    for (e = 0; e < 2; e++) {
        XrActionSpaceCreateInfo si;

        memset(&si, 0, sizeof(si));
        si.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        si.action = xr->a_pose_aim[e];
        si.poseInActionSpace.orientation.w = 1.0f;
        XR_CHECK(xr, xrCreateActionSpace(xr->session, &si, &xr->space_aim[e]),
                 "xrCreateActionSpace(aim)");

        memset(&si, 0, sizeof(si));
        si.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        si.action = xr->a_pose_grip[e];
        si.poseInActionSpace.orientation.w = 1.0f;
        XR_CHECK(xr, xrCreateActionSpace(xr->session, &si, &xr->space_grip[e]),
                 "xrCreateActionSpace(grip)");
    }

    memset(&attach, 0, sizeof(attach));
    attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach.countActionSets = 1;
    attach.actionSets = &xr->action_set;
    XR_CHECK(xr, xrAttachSessionActionSets(xr->session, &attach),
             "xrAttachSessionActionSets");
    return 0;
}

/* --------------------------------------------------------- create/destroy */

int gevr_xr_create(gevr_xr **out, const gevr_xr_desc *desc)
{
    gevr_xr *xr;

    if (!out) {
        return -1;
    }
    *out = NULL;

    xr = (gevr_xr *)calloc(1, sizeof(*xr));
    if (!xr) {
        return -1;
    }

    if (desc && desc->cfg) {
        xr->cfg = *desc->cfg;
    } else {
        gevr_config_defaults(&xr->cfg);
    }

    xr->state = XR_SESSION_STATE_UNKNOWN;

    if (create_instance(xr, desc) != 0) { *out = xr; return -1; }
    if (get_system(xr) != 0)            { *out = xr; return -1; }
    if (create_gl_context(xr, desc) != 0) { *out = xr; return -1; }
    if (create_session(xr) != 0)        { *out = xr; return -1; }
    if (create_base_space(xr) != 0)     { *out = xr; return -1; }
    if (create_swapchains(xr) != 0)     { *out = xr; return -1; }
    if (create_actions(xr) != 0)        { *out = xr; return -1; }

    *out = xr;
    return 0;
}

void gevr_xr_destroy(gevr_xr *xr)
{
    int e;
    uint32_t i;

    if (!xr) {
        return;
    }

    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        for (i = 0; i < xr->eyes[e].image_count; i++) {
            gevr_gl_delete_depth(xr->eyes[e].depth[i]);
        }
        if (xr->eyes[e].handle) {
            xrDestroySwapchain(xr->eyes[e].handle);
        }
    }
    for (e = 0; e < 2; e++) {
        if (xr->space_aim[e])  { xrDestroySpace(xr->space_aim[e]); }
        if (xr->space_grip[e]) { xrDestroySpace(xr->space_grip[e]); }
    }
    if (xr->view_space)  { xrDestroySpace(xr->view_space); }
    if (xr->base_space)  { xrDestroySpace(xr->base_space); }
    if (xr->action_set)  { xrDestroyActionSet(xr->action_set); }
    if (xr->session)     { xrDestroySession(xr->session); }
    if (xr->instance)    { xrDestroyInstance(xr->instance); }

    if (xr->owns_window) {
        gevr_gl_shutdown();
        if (xr->gl_context) { SDL_GL_DeleteContext(xr->gl_context); }
        if (xr->window)     { SDL_DestroyWindow(xr->window); }
        SDL_Quit();
    }

    free(xr);
}

/* ---------------------------------------------------------------- events */

static void update_profiles(gevr_xr *xr)
{
    int h;

    for (h = 0; h < 2; h++) {
        XrInteractionProfileState st;
        uint32_t len = 0;
        char buf[XR_MAX_PATH_LENGTH];

        memset(&st, 0, sizeof(st));
        st.type = XR_TYPE_INTERACTION_PROFILE_STATE;

        xr->profile_name[h][0] = '\0';
        if (XR_FAILED(xrGetCurrentInteractionProfile(xr->session,
                                                     xr->hand_path[h], &st))) {
            continue;
        }
        if (st.interactionProfile == XR_NULL_PATH) {
            continue;
        }
        if (XR_SUCCEEDED(xrPathToString(xr->instance, st.interactionProfile,
                                        sizeof(buf), &len, buf))) {
            snprintf(xr->profile_name[h], sizeof(xr->profile_name[h]), "%s", buf);
        }
    }
}

gevr_frame_status gevr_xr_poll(gevr_xr *xr)
{
    XrEventDataBuffer ev;

    if (!xr) {
        return GEVR_FRAME_EXIT;
    }

    for (;;) {
        memset(&ev, 0, sizeof(ev));
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (xrPollEvent(xr->instance, &ev) != XR_SUCCESS) {
            break;
        }

        switch (ev.type) {
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            xr->exit_requested = 1;
            set_error(xr, "the OpenXR runtime is shutting down");
            break;

        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const XrEventDataSessionStateChanged *e =
                (const XrEventDataSessionStateChanged *)&ev;
            xr->state = e->state;

            if (e->state == XR_SESSION_STATE_READY && !xr->session_running) {
                XrSessionBeginInfo bi;
                memset(&bi, 0, sizeof(bi));
                bi.type = XR_TYPE_SESSION_BEGIN_INFO;
                bi.primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(xr->session, &bi))) {
                    xr->session_running = 1;
                }
            } else if (e->state == XR_SESSION_STATE_STOPPING) {
                xr->session_running = 0;
                xrEndSession(xr->session);
            } else if (e->state == XR_SESSION_STATE_EXITING ||
                       e->state == XR_SESSION_STATE_LOSS_PENDING) {
                xr->exit_requested = 1;
            }
            break;
        }

        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            update_profiles(xr);
            break;

        default:
            break;
        }
    }

    if (xr->exit_requested) {
        return GEVR_FRAME_EXIT;
    }
    return xr->session_running ? GEVR_FRAME_RENDER : GEVR_FRAME_SKIP;
}

/* ----------------------------------------------------------- input read */

static float read_float(gevr_xr *xr, XrAction action)
{
    XrActionStateGetInfo gi;
    XrActionStateFloat st;

    if (action == XR_NULL_HANDLE) {
        return 0.0f;
    }
    memset(&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
    gi.action = action;

    memset(&st, 0, sizeof(st));
    st.type = XR_TYPE_ACTION_STATE_FLOAT;

    if (XR_FAILED(xrGetActionStateFloat(xr->session, &gi, &st)) || !st.isActive) {
        return 0.0f;
    }
    return st.currentState;
}

static int read_bool(gevr_xr *xr, XrAction action)
{
    XrActionStateGetInfo gi;
    XrActionStateBoolean st;

    if (action == XR_NULL_HANDLE) {
        return 0;
    }
    memset(&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
    gi.action = action;

    memset(&st, 0, sizeof(st));
    st.type = XR_TYPE_ACTION_STATE_BOOLEAN;

    if (XR_FAILED(xrGetActionStateBoolean(xr->session, &gi, &st)) || !st.isActive) {
        return 0;
    }
    return st.currentState ? 1 : 0;
}

static void read_vec2(gevr_xr *xr, XrAction action, float *x, float *y)
{
    XrActionStateGetInfo gi;
    XrActionStateVector2f st;

    *x = 0.0f;
    *y = 0.0f;
    if (action == XR_NULL_HANDLE) {
        return;
    }
    memset(&gi, 0, sizeof(gi));
    gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
    gi.action = action;

    memset(&st, 0, sizeof(st));
    st.type = XR_TYPE_ACTION_STATE_VECTOR2F;

    if (XR_FAILED(xrGetActionStateVector2f(xr->session, &gi, &st)) || !st.isActive) {
        return;
    }
    *x = st.currentState.x;
    *y = st.currentState.y;
}

static int locate_pose(gevr_xr *xr, XrSpace space, XrTime time, gevr_pose *out)
{
    XrSpaceLocation loc;

    memset(&loc, 0, sizeof(loc));
    loc.type = XR_TYPE_SPACE_LOCATION;

    if (XR_FAILED(xrLocateSpace(space, xr->base_space, time, &loc))) {
        return 0;
    }
    if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return 0;
    }
    *out = from_xr_pose(&loc.pose);
    return 1;
}

gevr_frame_status gevr_xr_begin_frame(gevr_xr *xr, gevr_input_state *in)
{
    XrFrameWaitInfo wi;
    XrFrameBeginInfo bi;
    XrActiveActionSet active;
    XrActionsSyncInfo si;
    XrViewLocateInfo vli;
    XrViewState vs;
    uint32_t view_count = 0;
    int e;

    if (!xr) {
        return GEVR_FRAME_EXIT;
    }
    if (in) {
        memset(in, 0, sizeof(*in));
    }
    if (!xr->session_running) {
        return GEVR_FRAME_SKIP;
    }

    memset(&wi, 0, sizeof(wi));
    wi.type = XR_TYPE_FRAME_WAIT_INFO;
    memset(&xr->frame_state, 0, sizeof(xr->frame_state));
    xr->frame_state.type = XR_TYPE_FRAME_STATE;

    if (XR_FAILED(xrWaitFrame(xr->session, &wi, &xr->frame_state))) {
        return GEVR_FRAME_SKIP;
    }

    memset(&bi, 0, sizeof(bi));
    bi.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (XR_FAILED(xrBeginFrame(xr->session, &bi))) {
        return GEVR_FRAME_SKIP;
    }

    xr->frame_active = 1;
    xr->should_render = xr->frame_state.shouldRender ? 1 : 0;
    xr->views_valid = 0;

    if (in) {
        XrTime now = xr->frame_state.predictedDisplayTime;
        in->dt = (xr->last_display_time && now > xr->last_display_time)
               ? (float)((double)(now - xr->last_display_time) / 1e9)
               : (1.0f / 90.0f);
        xr->last_display_time = now;
    }

    /* ---- actions ---- */
    memset(&active, 0, sizeof(active));
    active.actionSet = xr->action_set;
    active.subactionPath = XR_NULL_PATH;

    memset(&si, 0, sizeof(si));
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;

    if (in && XR_SUCCEEDED(xrSyncActions(xr->session, &si))) {
        read_vec2(xr, xr->a_move, &in->move_x, &in->move_y);
        read_vec2(xr, xr->a_turn, &in->turn_x, &in->turn_y);

        in->trigger_l = read_float(xr, xr->a_trigger[0]);
        in->trigger_r = read_float(xr, xr->a_trigger[1]);
        in->grip_l = read_float(xr, xr->a_grip[0]);
        in->grip_r = read_float(xr, xr->a_grip[1]);

        if (read_bool(xr, xr->a_btn_a)) { in->buttons |= GEVR_BTN_A_RIGHT; }
        if (read_bool(xr, xr->a_btn_b)) { in->buttons |= GEVR_BTN_B_RIGHT; }
        if (read_bool(xr, xr->a_btn_x)) { in->buttons |= GEVR_BTN_X_LEFT; }
        if (read_bool(xr, xr->a_btn_y)) { in->buttons |= GEVR_BTN_Y_LEFT; }
        if (read_bool(xr, xr->a_menu))  { in->buttons |= GEVR_BTN_MENU; }
        if (read_bool(xr, xr->a_stick_click[0])) { in->buttons |= GEVR_BTN_STICK_LEFT; }
        if (read_bool(xr, xr->a_stick_click[1])) { in->buttons |= GEVR_BTN_STICK_RIGHT; }
        if (in->trigger_l >= 0.6f) { in->buttons |= GEVR_BTN_TRIGGER_L; }
        if (in->trigger_r >= 0.6f) { in->buttons |= GEVR_BTN_TRIGGER_R; }
        if (in->grip_l >= 0.6f)    { in->buttons |= GEVR_BTN_GRIP_L; }
        if (in->grip_r >= 0.6f)    { in->buttons |= GEVR_BTN_GRIP_R; }

        /* The aim pose is the controller's pointing ray, which is what both
         * gun aiming and hand-relative locomotion want. */
        in->hand_l_valid = locate_pose(xr, xr->space_aim[0],
                                       xr->frame_state.predictedDisplayTime,
                                       &in->hand_l);
        in->hand_r_valid = locate_pose(xr, xr->space_aim[1],
                                       xr->frame_state.predictedDisplayTime,
                                       &in->hand_r);
        in->head_valid = locate_pose(xr, xr->view_space,
                                     xr->frame_state.predictedDisplayTime,
                                     &in->head);
    }

    /* ---- views ---- */
    memset(&vli, 0, sizeof(vli));
    vli.type = XR_TYPE_VIEW_LOCATE_INFO;
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = xr->frame_state.predictedDisplayTime;
    vli.space = xr->base_space;

    memset(&vs, 0, sizeof(vs));
    vs.type = XR_TYPE_VIEW_STATE;
    for (e = 0; e < GEVR_EYE_COUNT; e++) {
        memset(&xr->views[e], 0, sizeof(xr->views[e]));
        xr->views[e].type = XR_TYPE_VIEW;
    }

    if (XR_SUCCEEDED(xrLocateViews(xr->session, &vli, &vs, GEVR_EYE_COUNT,
                                   &view_count, xr->views))
        && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)
        && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
        xr->views_valid = 1;
    }

    if (!xr->should_render || !xr->views_valid) {
        return GEVR_FRAME_SKIP;
    }
    return GEVR_FRAME_RENDER;
}

int gevr_xr_acquire_eye(gevr_xr *xr, int eye, gevr_eye_target *out)
{
    gevr_swapchain *sc;
    XrSwapchainImageAcquireInfo ai;
    XrSwapchainImageWaitInfo wi;

    if (!xr || !out || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    sc = &xr->eyes[eye];
    if (!sc->handle || sc->acquired) {
        return -1;
    }

    memset(&ai, 0, sizeof(ai));
    ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if (XR_FAILED(xrAcquireSwapchainImage(sc->handle, &ai, &sc->acquired_index))) {
        return -1;
    }

    memset(&wi, 0, sizeof(wi));
    wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(sc->handle, &wi))) {
        XrSwapchainImageReleaseInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        xrReleaseSwapchainImage(sc->handle, &ri);
        return -1;
    }
    sc->acquired = 1;

    out->gl_texture = sc->images[sc->acquired_index].image;
    out->gl_depth = sc->depth[sc->acquired_index];
    out->width = sc->width;
    out->height = sc->height;
    out->pose = from_xr_pose(&xr->views[eye].pose);
    out->fov[0] = xr->views[eye].fov.angleLeft;
    out->fov[1] = xr->views[eye].fov.angleRight;
    out->fov[2] = xr->views[eye].fov.angleUp;
    out->fov[3] = xr->views[eye].fov.angleDown;

    /* Record the projection view now, while the pose that was actually
     * rendered with is in hand. Submitting a different pose than the one the
     * frame was drawn for is the classic source of VR judder. */
    memset(&xr->proj_views[eye], 0, sizeof(xr->proj_views[eye]));
    xr->proj_views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    xr->proj_views[eye].pose = xr->views[eye].pose;
    xr->proj_views[eye].fov = xr->views[eye].fov;
    xr->proj_views[eye].subImage.swapchain = sc->handle;
    xr->proj_views[eye].subImage.imageRect.offset.x = 0;
    xr->proj_views[eye].subImage.imageRect.offset.y = 0;
    xr->proj_views[eye].subImage.imageRect.extent.width = sc->width;
    xr->proj_views[eye].subImage.imageRect.extent.height = sc->height;
    xr->proj_views[eye].subImage.imageArrayIndex = 0;
    return 0;
}

void gevr_xr_release_eye(gevr_xr *xr, int eye)
{
    XrSwapchainImageReleaseInfo ri;
    gevr_swapchain *sc;

    if (!xr || eye < 0 || eye >= GEVR_EYE_COUNT) {
        return;
    }
    sc = &xr->eyes[eye];
    if (!sc->acquired) {
        return;
    }

    memset(&ri, 0, sizeof(ri));
    ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    xrReleaseSwapchainImage(sc->handle, &ri);
    sc->acquired = 0;
}

void gevr_xr_end_frame(gevr_xr *xr)
{
    XrFrameEndInfo ei;
    XrCompositionLayerProjection layer;
    const XrCompositionLayerBaseHeader *layers[1];

    if (!xr || !xr->frame_active) {
        return;
    }

    memset(&ei, 0, sizeof(ei));
    ei.type = XR_TYPE_FRAME_END_INFO;
    ei.displayTime = xr->frame_state.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    if (xr->should_render && xr->views_valid) {
        memset(&layer, 0, sizeof(layer));
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.space = xr->base_space;
        layer.viewCount = GEVR_EYE_COUNT;
        layer.views = xr->proj_views;

        layers[0] = (const XrCompositionLayerBaseHeader *)&layer;
        ei.layerCount = 1;
        ei.layers = layers;
    } else {
        ei.layerCount = 0;
        ei.layers = NULL;
    }

    xrEndFrame(xr->session, &ei);
    xr->frame_active = 0;
}

void gevr_xr_haptics(gevr_xr *xr, const gevr_haptic_request *req)
{
    XrHapticVibration v;
    XrHapticActionInfo hi;
    int h;

    if (!xr || !req || (!req->left && !req->right)) {
        return;
    }

    memset(&v, 0, sizeof(v));
    v.type = XR_TYPE_HAPTIC_VIBRATION;
    v.amplitude = gevr_clampf(req->amplitude, 0.0f, 1.0f);
    v.duration = (XrDuration)(req->duration * 1e9);
    v.frequency = XR_FREQUENCY_UNSPECIFIED;

    for (h = 0; h < 2; h++) {
        if ((h == 0 && !req->left) || (h == 1 && !req->right)) {
            continue;
        }
        memset(&hi, 0, sizeof(hi));
        hi.type = XR_TYPE_HAPTIC_ACTION_INFO;
        hi.action = xr->a_haptic[h];
        xrApplyHapticFeedback(xr->session, &hi,
                              (const XrHapticBaseHeader *)&v);
    }
}

void gevr_xr_recenter(gevr_xr *xr)
{
    XrSpaceLocation loc;
    XrReferenceSpaceCreateInfo ci;
    XrSpace new_space = XR_NULL_HANDLE;
    float yaw, half;

    if (!xr || !xr->session_running || !xr->base_space) {
        return;
    }

    memset(&loc, 0, sizeof(loc));
    loc.type = XR_TYPE_SPACE_LOCATION;
    if (XR_FAILED(xrLocateSpace(xr->view_space, xr->base_space,
                                xr->frame_state.predictedDisplayTime, &loc))) {
        return;
    }
    if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return;
    }

    /* Keep only the yaw: carrying pitch or roll into the origin would tilt
     * the horizon, which is both wrong and immediately sickening. */
    {
        gevr_pose head = from_xr_pose(&loc.pose);
        yaw = gevr_quat_yaw_of(head.orientation);
    }
    half = yaw * 0.5f;

    /* The new origin is the old one composed with the head's current yaw and
     * planar position, so the player ends up where they are standing, facing
     * the way they are facing. */
    xr->base_offset.orientation.x = 0.0f;
    xr->base_offset.orientation.y = sinf(half);
    xr->base_offset.orientation.z = 0.0f;
    xr->base_offset.orientation.w = cosf(half);
    xr->base_offset.position.x = loc.pose.position.x;
    xr->base_offset.position.y = 0.0f;
    xr->base_offset.position.z = loc.pose.position.z;

    memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    ci.referenceSpaceType = xr->base_space_type;
    ci.poseInReferenceSpace = xr->base_offset;

    if (XR_FAILED(xrCreateReferenceSpace(xr->session, &ci, &new_space))) {
        return;
    }
    xrDestroySpace(xr->base_space);
    xr->base_space = new_space;
}
