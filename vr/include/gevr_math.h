/*
 * gevr_math.h - minimal vector / quaternion / matrix math for the GoldenEye VR layer.
 *
 * Conventions (OpenXR native, and what the rest of this layer assumes):
 *   - Right-handed, Y up, -Z forward.
 *   - Quaternions are (x, y, z, w), unit length.
 *   - gevr_mat4 is COLUMN-MAJOR (OpenGL layout): m[col * 4 + row].
 *   - Angles are radians unless a name says _deg.
 *
 * The GoldenEye engine uses its own angle conventions; conversion happens in
 * gevr_camera.c / gevr_controls.c, never here. This header stays engine-agnostic
 * so it can be unit tested headlessly.
 */
#ifndef GEVR_MATH_H
#define GEVR_MATH_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GEVR_PI
#define GEVR_PI 3.14159265358979323846f
#endif

#define GEVR_DEG2RAD(d) ((d) * (GEVR_PI / 180.0f))
#define GEVR_RAD2DEG(r) ((r) * (180.0f / GEVR_PI))

typedef struct gevr_vec3 {
    float x, y, z;
} gevr_vec3;

typedef struct gevr_quat {
    float x, y, z, w;
} gevr_quat;

typedef struct gevr_mat4 {
    float m[16]; /* column-major */
} gevr_mat4;

/* A rigid transform: orientation then position. Mirrors XrPosef. */
typedef struct gevr_pose {
    gevr_quat orientation;
    gevr_vec3 position;
} gevr_pose;

/* Yaw/pitch/roll decomposition of an orientation, in the convention above:
 *   yaw   - rotation about +Y, 0 == facing -Z, positive turns LEFT
 *   pitch - positive looks UP
 *   roll  - positive rolls the head to the RIGHT (clockwise from behind) */
typedef struct gevr_euler {
    float yaw, pitch, roll;
} gevr_euler;

/* ---------------------------------------------------------------- vectors */

static inline gevr_vec3 gevr_v3(float x, float y, float z)
{
    gevr_vec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

static inline gevr_vec3 gevr_v3_add(gevr_vec3 a, gevr_vec3 b)
{
    return gevr_v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline gevr_vec3 gevr_v3_sub(gevr_vec3 a, gevr_vec3 b)
{
    return gevr_v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline gevr_vec3 gevr_v3_scale(gevr_vec3 a, float s)
{
    return gevr_v3(a.x * s, a.y * s, a.z * s);
}

static inline float gevr_v3_dot(gevr_vec3 a, gevr_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline gevr_vec3 gevr_v3_cross(gevr_vec3 a, gevr_vec3 b)
{
    return gevr_v3(a.y * b.z - a.z * b.y,
                   a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x);
}

static inline float gevr_v3_len(gevr_vec3 a)
{
    return sqrtf(gevr_v3_dot(a, a));
}

static inline gevr_vec3 gevr_v3_norm(gevr_vec3 a)
{
    float l = gevr_v3_len(a);
    return (l > 1e-8f) ? gevr_v3_scale(a, 1.0f / l) : gevr_v3(0.0f, 0.0f, 0.0f);
}

/* ------------------------------------------------------------ quaternions */

static inline gevr_quat gevr_quat_identity(void)
{
    gevr_quat q;
    q.x = 0.0f; q.y = 0.0f; q.z = 0.0f; q.w = 1.0f;
    return q;
}

static inline gevr_quat gevr_quat_mul(gevr_quat a, gevr_quat b)
{
    gevr_quat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

static inline gevr_quat gevr_quat_conj(gevr_quat q)
{
    gevr_quat r;
    r.x = -q.x; r.y = -q.y; r.z = -q.z; r.w = q.w;
    return r;
}

static inline gevr_quat gevr_quat_norm(gevr_quat q)
{
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) {
        return gevr_quat_identity();
    }
    l = 1.0f / l;
    q.x *= l; q.y *= l; q.z *= l; q.w *= l;
    return q;
}

static inline gevr_quat gevr_quat_axis_angle(gevr_vec3 axis, float angle)
{
    gevr_quat q;
    gevr_vec3 a = gevr_v3_norm(axis);
    float s = sinf(angle * 0.5f);
    q.x = a.x * s; q.y = a.y * s; q.z = a.z * s;
    q.w = cosf(angle * 0.5f);
    return q;
}

/* Rotation about +Y (the yaw axis). */
static inline gevr_quat gevr_quat_yaw(float yaw)
{
    gevr_quat q;
    q.x = 0.0f;
    q.y = sinf(yaw * 0.5f);
    q.z = 0.0f;
    q.w = cosf(yaw * 0.5f);
    return q;
}

static inline gevr_vec3 gevr_quat_rotate(gevr_quat q, gevr_vec3 v)
{
    /* v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v) */
    gevr_vec3 u = gevr_v3(q.x, q.y, q.z);
    gevr_vec3 t = gevr_v3_add(gevr_v3_cross(u, v), gevr_v3_scale(v, q.w));
    return gevr_v3_add(v, gevr_v3_scale(gevr_v3_cross(u, t), 2.0f));
}

/* Unit forward (-Z), right (+X) and up (+Y) of an orientation. */
static inline gevr_vec3 gevr_quat_forward(gevr_quat q)
{
    return gevr_quat_rotate(q, gevr_v3(0.0f, 0.0f, -1.0f));
}

static inline gevr_vec3 gevr_quat_right(gevr_quat q)
{
    return gevr_quat_rotate(q, gevr_v3(1.0f, 0.0f, 0.0f));
}

static inline gevr_vec3 gevr_quat_up(gevr_quat q)
{
    return gevr_quat_rotate(q, gevr_v3(0.0f, 1.0f, 0.0f));
}

/* ------------------------------------------------------------------ angles */

/* Wrap to (-pi, pi]. The servo in gevr_controls.c depends on this being exact
 * at the seam, otherwise a player facing due south gets a full-speed spin. */
static inline float gevr_wrap_pi(float a)
{
    a = fmodf(a + GEVR_PI, 2.0f * GEVR_PI);
    if (a < 0.0f) {
        a += 2.0f * GEVR_PI;
    }
    return a - GEVR_PI;
}

static inline float gevr_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* ------------------------------------------------------------- public API */

gevr_euler  gevr_quat_to_euler(gevr_quat q);
gevr_quat   gevr_euler_to_quat(gevr_euler e);

/* Yaw only, taken from the orientation's forward vector. Degenerate when
 * looking straight up/down, where it falls back to the head's up vector so a
 * player leaning back over a railing does not lose their heading. */
float       gevr_quat_yaw_of(gevr_quat q);

gevr_mat4   gevr_mat4_identity(void);
gevr_mat4   gevr_mat4_mul(const gevr_mat4 *a, const gevr_mat4 *b);
gevr_mat4   gevr_mat4_from_pose(gevr_pose p);
/* Inverse of a rigid (rotation + translation) transform: the view matrix. */
gevr_mat4   gevr_mat4_rigid_inverse(const gevr_mat4 *m);
gevr_pose   gevr_pose_identity(void);
gevr_pose   gevr_pose_mul(gevr_pose parent, gevr_pose child);
gevr_pose   gevr_pose_inverse(gevr_pose p);

/* Off-axis (asymmetric) projection built from the four half-angles an OpenXR
 * runtime reports. Symmetric guPerspective cannot express a real HMD frustum,
 * which is why the VR build replaces it. Angles in radians; angle_left and
 * angle_down are normally negative. */
gevr_mat4   gevr_projection_from_fov(float angle_left, float angle_right,
                                     float angle_up, float angle_down,
                                     float z_near, float z_far);

/* Symmetric convenience wrapper, used by the flat mirror window. */
gevr_mat4   gevr_projection_perspective(float fovy_rad, float aspect,
                                        float z_near, float z_far);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_MATH_H */
