#include "gevr_math.h"

#include <string.h>

gevr_euler gevr_quat_to_euler(gevr_quat q)
{
    gevr_euler e;
    float sinp;

    q = gevr_quat_norm(q);

    /* Y-up, -Z forward, intrinsic yaw(Y) -> pitch(X) -> roll(Z). */
    sinp = 2.0f * (q.w * q.x - q.y * q.z);
    sinp = gevr_clampf(sinp, -1.0f, 1.0f);
    e.pitch = asinf(sinp);

    if (sinp > 0.99999f || sinp < -0.99999f) {
        /* Gimbal lock: pitch is +/-90 degrees, yaw and roll are the same axis.
         * Put the whole rotation into yaw and zero the roll, so a player who
         * cranes straight up keeps a stable heading instead of spinning. */
        e.yaw = atan2f(-2.0f * (q.x * q.z - q.w * q.y),
                        1.0f - 2.0f * (q.y * q.y + q.z * q.z));
        e.roll = 0.0f;
        return e;
    }

    e.yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z),
                   1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    e.roll = atan2f(2.0f * (q.w * q.z + q.x * q.y),
                    1.0f - 2.0f * (q.x * q.x + q.z * q.z));
    return e;
}

gevr_quat gevr_euler_to_quat(gevr_euler e)
{
    gevr_quat qy = gevr_quat_axis_angle(gevr_v3(0.0f, 1.0f, 0.0f), e.yaw);
    gevr_quat qx = gevr_quat_axis_angle(gevr_v3(1.0f, 0.0f, 0.0f), e.pitch);
    gevr_quat qz = gevr_quat_axis_angle(gevr_v3(0.0f, 0.0f, 1.0f), e.roll);
    return gevr_quat_norm(gevr_quat_mul(gevr_quat_mul(qy, qx), qz));
}

float gevr_quat_yaw_of(gevr_quat q)
{
    gevr_vec3 f = gevr_quat_forward(q);
    float horiz = sqrtf(f.x * f.x + f.z * f.z);

    if (horiz < 1e-4f) {
        /* Looking near-vertically: forward carries no heading. Use the head's
         * up vector, which points along the heading when you look straight up
         * and against it when you look straight down. */
        gevr_vec3 u = gevr_quat_up(q);
        if (f.y > 0.0f) {
            u = gevr_v3_scale(u, -1.0f);
        }
        return atan2f(-u.x, -u.z);
    }
    return atan2f(-f.x, -f.z);
}

gevr_mat4 gevr_mat4_identity(void)
{
    gevr_mat4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

gevr_mat4 gevr_mat4_mul(const gevr_mat4 *a, const gevr_mat4 *b)
{
    gevr_mat4 r;
    int c, i;

    for (c = 0; c < 4; c++) {
        for (i = 0; i < 4; i++) {
            r.m[c * 4 + i] = a->m[0 * 4 + i] * b->m[c * 4 + 0]
                           + a->m[1 * 4 + i] * b->m[c * 4 + 1]
                           + a->m[2 * 4 + i] * b->m[c * 4 + 2]
                           + a->m[3 * 4 + i] * b->m[c * 4 + 3];
        }
    }
    return r;
}

gevr_mat4 gevr_mat4_from_pose(gevr_pose p)
{
    gevr_mat4 r;
    gevr_quat q = gevr_quat_norm(p.orientation);
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    r.m[0]  = 1.0f - 2.0f * (yy + zz);
    r.m[1]  =        2.0f * (xy + wz);
    r.m[2]  =        2.0f * (xz - wy);
    r.m[3]  = 0.0f;

    r.m[4]  =        2.0f * (xy - wz);
    r.m[5]  = 1.0f - 2.0f * (xx + zz);
    r.m[6]  =        2.0f * (yz + wx);
    r.m[7]  = 0.0f;

    r.m[8]  =        2.0f * (xz + wy);
    r.m[9]  =        2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    r.m[11] = 0.0f;

    r.m[12] = p.position.x;
    r.m[13] = p.position.y;
    r.m[14] = p.position.z;
    r.m[15] = 1.0f;
    return r;
}

gevr_mat4 gevr_mat4_rigid_inverse(const gevr_mat4 *m)
{
    gevr_mat4 r;
    float tx = m->m[12], ty = m->m[13], tz = m->m[14];

    /* Transpose the rotation block. */
    r.m[0] = m->m[0]; r.m[1] = m->m[4]; r.m[2]  = m->m[8];  r.m[3]  = 0.0f;
    r.m[4] = m->m[1]; r.m[5] = m->m[5]; r.m[6]  = m->m[9];  r.m[7]  = 0.0f;
    r.m[8] = m->m[2]; r.m[9] = m->m[6]; r.m[10] = m->m[10]; r.m[11] = 0.0f;

    /* -R^T * t */
    r.m[12] = -(r.m[0] * tx + r.m[4] * ty + r.m[8]  * tz);
    r.m[13] = -(r.m[1] * tx + r.m[5] * ty + r.m[9]  * tz);
    r.m[14] = -(r.m[2] * tx + r.m[6] * ty + r.m[10] * tz);
    r.m[15] = 1.0f;
    return r;
}

gevr_pose gevr_pose_identity(void)
{
    gevr_pose p;
    p.orientation = gevr_quat_identity();
    p.position = gevr_v3(0.0f, 0.0f, 0.0f);
    return p;
}

gevr_pose gevr_pose_mul(gevr_pose parent, gevr_pose child)
{
    gevr_pose r;
    r.orientation = gevr_quat_norm(gevr_quat_mul(parent.orientation, child.orientation));
    r.position = gevr_v3_add(parent.position,
                             gevr_quat_rotate(parent.orientation, child.position));
    return r;
}

gevr_pose gevr_pose_inverse(gevr_pose p)
{
    gevr_pose r;
    r.orientation = gevr_quat_conj(gevr_quat_norm(p.orientation));
    r.position = gevr_v3_scale(gevr_quat_rotate(r.orientation, p.position), -1.0f);
    return r;
}

gevr_mat4 gevr_projection_from_fov(float angle_left, float angle_right,
                                   float angle_up, float angle_down,
                                   float z_near, float z_far)
{
    gevr_mat4 r;
    float tan_l = tanf(angle_left);
    float tan_r = tanf(angle_right);
    float tan_u = tanf(angle_up);
    float tan_d = tanf(angle_down);
    float w = tan_r - tan_l;
    float h = tan_u - tan_d;

    memset(r.m, 0, sizeof(r.m));

    if (w <= 1e-6f || h <= 1e-6f || z_near <= 0.0f) {
        return gevr_mat4_identity();
    }

    r.m[0]  = 2.0f / w;
    r.m[5]  = 2.0f / h;
    r.m[8]  = (tan_r + tan_l) / w;
    r.m[9]  = (tan_u + tan_d) / h;
    r.m[11] = -1.0f;

    if (z_far <= z_near) {
        /* Infinite far plane. Keeps skyboxes from clipping when a level's
         * far distance is smaller than the headset's comfortable range. */
        r.m[10] = -1.0f;
        r.m[14] = -2.0f * z_near;
    } else {
        r.m[10] = -(z_far + z_near) / (z_far - z_near);
        r.m[14] = -(2.0f * z_far * z_near) / (z_far - z_near);
    }
    return r;
}

gevr_mat4 gevr_projection_perspective(float fovy_rad, float aspect,
                                      float z_near, float z_far)
{
    float half_h = fovy_rad * 0.5f;
    float half_w = atanf(tanf(half_h) * aspect);
    return gevr_projection_from_fov(-half_w, half_w, half_h, -half_h, z_near, z_far);
}
