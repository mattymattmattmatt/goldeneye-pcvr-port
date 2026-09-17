#include "gevr_camera.h"

#include <string.h>

void gevr_camera_init(gevr_camera *cam, const gevr_config *cfg)
{
    if (!cam) {
        return;
    }
    memset(cam, 0, sizeof(*cam));
    cam->standing_height = cfg ? cfg->player_height : 1.75f;
}

void gevr_camera_recenter(gevr_camera *cam, const gevr_pose *head, float body_yaw)
{
    if (!cam || !head) {
        return;
    }

    cam->recenter_origin = head->position;
    cam->have_origin = 1;

    /* Fold the head's current heading into the body yaw so that after a
     * recenter, looking straight ahead means facing the same way in-game. */
    cam->body_yaw = gevr_wrap_pi(body_yaw + gevr_quat_yaw_of(head->orientation));

    if (head->position.y > 0.5f && head->position.y < 2.5f) {
        cam->standing_height = head->position.y;
    }
}

/* Head position relative to the recentred play-space origin, in metres,
 * with the vertical component removed (height is handled separately so a
 * crouch does not also translate the camera down twice). */
static gevr_vec3 head_planar_offset(const gevr_camera *cam, const gevr_pose *head)
{
    gevr_vec3 d;

    if (!cam->have_origin) {
        return gevr_v3(0.0f, 0.0f, 0.0f);
    }
    d = gevr_v3_sub(head->position, cam->recenter_origin);
    d.y = 0.0f;
    return d;
}

gevr_vec3 gevr_camera_room_offset(const gevr_camera *cam,
                                  const gevr_config *cfg,
                                  const gevr_pose *head)
{
    gevr_vec3 local;
    gevr_quat heading;

    if (!cam || !cfg || !head) {
        return gevr_v3(0.0f, 0.0f, 0.0f);
    }

    local = head_planar_offset(cam, head);
    heading = gevr_quat_yaw(cam->body_yaw);
    return gevr_v3_scale(gevr_quat_rotate(heading, local), cfg->world_scale);
}

float gevr_camera_crouch_offset(const gevr_camera *cam,
                                const gevr_config *cfg,
                                const gevr_pose *head)
{
    if (!cam || !cfg || !head || !cam->have_origin) {
        return 0.0f;
    }
    return (head->position.y - cam->standing_height) * cfg->world_scale;
}

void gevr_camera_build_eye(const gevr_camera *cam,
                           const gevr_config *cfg,
                           const gevr_camera_params *params,
                           const gevr_pose *head,
                           const gevr_pose *eye,
                           const float eye_fov[4],
                           gevr_eye_view *out)
{
    gevr_quat heading;
    gevr_pose eye_local;
    gevr_pose world;
    gevr_vec3 eye_offset;
    float z_near, z_far;

    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!cam || !cfg || !params || !head || !eye || !eye_fov) {
        return;
    }

    /* Eye relative to head, still in metres. Doing it this way rather than
     * assuming a symmetric IPD means canted displays (Pimax, and the Quest's
     * own slight cant) come out right for free. */
    eye_local = gevr_pose_mul(gevr_pose_inverse(*head), *eye);

    /* Play-space heading. The engine's yaw is deliberately not used here:
     * see the header comment on why the view leads the servo. */
    heading = gevr_quat_yaw(cam->body_yaw);

    world.orientation = gevr_quat_norm(gevr_quat_mul(heading, head->orientation));
    world.position = params->eye_pos;

    /* Roomscale translation, scaled into game units. */
    {
        gevr_vec3 planar = gevr_v3_scale(
            gevr_quat_rotate(heading, head_planar_offset(cam, head)),
            cfg->world_scale);
        world.position = gevr_v3_add(world.position, planar);
    }

    /* Interpupillary offset. ipd_scale below 1 shrinks the stereo baseline,
     * which makes the low-poly world read as larger and is a common comfort
     * tweak at this art scale. */
    eye_offset = gevr_quat_rotate(world.orientation,
                                  gevr_v3_scale(eye_local.position,
                                                cfg->world_scale * cfg->ipd_scale));

    out->pose.orientation = gevr_quat_norm(
        gevr_quat_mul(world.orientation, eye_local.orientation));
    out->pose.position = gevr_v3_add(world.position, eye_offset);

    {
        gevr_mat4 m = gevr_mat4_from_pose(out->pose);
        out->view = gevr_mat4_rigid_inverse(&m);
    }

    z_near = cfg->z_near * cfg->world_scale;
    z_far = (cfg->z_far > cfg->z_near) ? (cfg->z_far * cfg->world_scale) : 0.0f;

    out->fov[0] = eye_fov[0];
    out->fov[1] = eye_fov[1];
    out->fov[2] = eye_fov[2];
    out->fov[3] = eye_fov[3];

    out->proj = gevr_projection_from_fov(eye_fov[0], eye_fov[1],
                                         eye_fov[2], eye_fov[3],
                                         z_near, z_far);
    out->valid = 1;
}
