/*
 * VRM Spring Bone Physics - VRMC_springBone-1.0 Spec Compliant
 */

#include "spring_bones.h"
#include "gltf_loader.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global tuning multipliers */
float g_spring_stiffness = 1.0f;
float g_spring_drag = 1.0f;
float g_spring_gravity = 0.1f;
int g_spring_cloth_mode = 0;
float g_spring_cloth_stiffness = 0.35f;
float g_spring_cloth_drag = 1.2f;
float g_spring_cloth_gravity = 0.1f;
float g_spring_cloth_taper = 0.6f;
int g_spring_debug_collisions = 0;
static int g_spring_settle_frames = 0;

/* Vector math */
static inline void v3_sub(const float *a, const float *b, float *out) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}
static inline void v3_add(const float *a, const float *b, float *out) {
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}
static inline void v3_scale(const float *v, float s, float *out) {
    out[0] = v[0] * s; out[1] = v[1] * s; out[2] = v[2] * s;
}
static inline float v3_dot(const float *a, const float *b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static inline float v3_len(const float *v) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}
static inline float v3_len_sq(const float *v) {
    return v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
}
static inline void v3_normalize(float *v) {
    float len = v3_len(v);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

/* Get world position from matrix */
static inline void get_world_pos(const float *m, float *p) {
    p[0] = m[12]; p[1] = m[13]; p[2] = m[14];
}

static void mat4_mul(const float *a, const float *b, float *out) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
}

static int mat4_inverse(const float *m, float *out) {
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
              m[4]  * m[11] * m[14] +
              m[8]  * m[6]  * m[15] -
              m[8]  * m[7]  * m[14] -
              m[12] * m[6]  * m[11] +
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] -
               m[8]  * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
              m[1]  * m[11] * m[14] +
              m[9]  * m[2] * m[15] -
              m[9]  * m[3] * m[14] -
              m[13] * m[2] * m[11] +
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
              m[0]  * m[11] * m[13] +
              m[8]  * m[1] * m[15] -
              m[8]  * m[3] * m[13] -
              m[12] * m[1] * m[11] +
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
              m[0]  * m[7] * m[14] +
              m[4]  * m[2] * m[15] -
              m[4]  * m[3] * m[14] -
              m[12] * m[2] * m[7] +
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
               m[0]  * m[6] * m[13] +
               m[4]  * m[1] * m[14] -
               m[4]  * m[2] * m[13] -
               m[12] * m[1] * m[6] +
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
              m[1] * m[7] * m[10] +
              m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] -
              m[9] * m[2] * m[7] +
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
               m[0] * m[7] * m[9] +
               m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] -
               m[8] * m[1] * m[7] +
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-8f) return 0;
    det = 1.0f / det;

    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
    return 1;
}

/* Transform point by matrix */
static void transform_point(const float *m, const float *local, float *world) {
    world[0] = m[0]*local[0] + m[4]*local[1] + m[8]*local[2] + m[12];
    world[1] = m[1]*local[0] + m[5]*local[1] + m[9]*local[2] + m[13];
    world[2] = m[2]*local[0] + m[6]*local[1] + m[10]*local[2] + m[14];
}

static void transform_vec(const float *m, const float *local, float *world) {
    world[0] = m[0]*local[0] + m[4]*local[1] + m[8]*local[2];
    world[1] = m[1]*local[0] + m[5]*local[1] + m[9]*local[2];
    world[2] = m[2]*local[0] + m[6]*local[1] + m[10]*local[2];
}

static void build_local_matrix(const float *t, const float *r, const float *s, float *out) {
    float qx = r[0], qy = r[1], qz = r[2], qw = r[3];
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    out[0] = (1 - 2*(yy + zz)) * s[0];
    out[1] = 2*(xy + wz) * s[0];
    out[2] = 2*(xz - wy) * s[0];
    out[3] = 0;

    out[4] = 2*(xy - wz) * s[1];
    out[5] = (1 - 2*(xx + zz)) * s[1];
    out[6] = 2*(yz + wx) * s[1];
    out[7] = 0;

    out[8] = 2*(xz + wy) * s[2];
    out[9] = 2*(yz - wx) * s[2];
    out[10] = (1 - 2*(xx + yy)) * s[2];
    out[11] = 0;

    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1;
}

static void update_node_transform(GltfModel *model, int node_idx, const float *parent_matrix) {
    if (node_idx < 0 || node_idx >= model->node_count) return;
    GltfNode *node = &model->nodes[node_idx];
    float local[16];
    build_local_matrix(node->translation, node->rotation, node->scale, local);
    mat4_mul(parent_matrix, local, node->world_matrix);
    for (int i = 0; i < node->child_count; i++) {
        update_node_transform(model, node->children[i], node->world_matrix);
    }
}

/* Quaternion operations */
static void quat_mul(const float *a, const float *b, float *out) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

static void quat_rotate_vec3(const float *q, const float *v, float *out) {
    float qv[3] = {q[0], q[1], q[2]};
    float uv[3], uuv[3];
    uv[0] = qv[1]*v[2] - qv[2]*v[1];
    uv[1] = qv[2]*v[0] - qv[0]*v[2];
    uv[2] = qv[0]*v[1] - qv[1]*v[0];
    uuv[0] = qv[1]*uv[2] - qv[2]*uv[1];
    uuv[1] = qv[2]*uv[0] - qv[0]*uv[2];
    uuv[2] = qv[0]*uv[1] - qv[1]*uv[0];
    out[0] = v[0] + 2.0f * (q[3]*uv[0] + uuv[0]);
    out[1] = v[1] + 2.0f * (q[3]*uv[1] + uuv[1]);
    out[2] = v[2] + 2.0f * (q[3]*uv[2] + uuv[2]);
}

static void mat4_to_quat(const float *m, float *q) {
    float trace = m[0] + m[5] + m[10];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        q[3] = 0.25f / s;
        q[0] = (m[6] - m[9]) * s;
        q[1] = (m[8] - m[2]) * s;
        q[2] = (m[1] - m[4]) * s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        float s = 2.0f * sqrtf(1.0f + m[0] - m[5] - m[10]);
        q[3] = (m[6] - m[9]) / s; q[0] = 0.25f * s;
        q[1] = (m[4] + m[1]) / s; q[2] = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        float s = 2.0f * sqrtf(1.0f + m[5] - m[0] - m[10]);
        q[3] = (m[8] - m[2]) / s; q[0] = (m[4] + m[1]) / s;
        q[1] = 0.25f * s; q[2] = (m[9] + m[6]) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + m[10] - m[0] - m[5]);
        q[3] = (m[1] - m[4]) / s; q[0] = (m[8] + m[2]) / s;
        q[1] = (m[9] + m[6]) / s; q[2] = 0.25f * s;
    }
    float len = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (len > 1e-6f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* fromToQuaternion - rotation from vector 'from' to vector 'to' */
static void quat_from_to(const float *from, const float *to, float *q) {
    float fn[3] = {from[0], from[1], from[2]};
    float tn[3] = {to[0], to[1], to[2]};
    v3_normalize(fn); v3_normalize(tn);
    float d = v3_dot(fn, tn);
    if (d > 0.9999f) { q[0]=q[1]=q[2]=0; q[3]=1; return; }
    if (d < -0.9999f) { q[0]=1; q[1]=q[2]=0; q[3]=0; return; }
    float axis[3] = { fn[1]*tn[2]-fn[2]*tn[1], fn[2]*tn[0]-fn[0]*tn[2], fn[0]*tn[1]-fn[1]*tn[0] };
    q[0]=axis[0]; q[1]=axis[1]; q[2]=axis[2]; q[3]=1.0f+d;
    float len = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (len > 1e-6f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* Sphere collider - returns distance (negative = collision) and direction */
static void collide_sphere(const float *tail, float joint_radius,
                           const float *center, float col_radius,
                           float *distance, float *direction) {
    float delta[3];
    v3_sub(tail, center, delta);
    float mag = v3_len(delta);
    *distance = mag - col_radius - joint_radius;
    if (mag > 1e-6f) {
        direction[0] = delta[0]/mag;
        direction[1] = delta[1]/mag;
        direction[2] = delta[2]/mag;
    } else {
        direction[0] = 0; direction[1] = 1; direction[2] = 0;
    }
}

/* Capsule collider - per spec pseudocode */
static void collide_capsule(const float *tail, float joint_radius,
                            const float *offset, const float *cap_tail, float col_radius,
                            float *distance, float *direction) {
    float delta[3], offset_to_tail[3];
    v3_sub(tail, offset, delta);
    v3_sub(cap_tail, offset, offset_to_tail);
    float dot = v3_dot(offset_to_tail, delta);

    float sq_len = v3_len_sq(offset_to_tail);
    if (sq_len > 1e-8f) {
        if (dot < 0.0f) {
            /* Joint at head side - use delta as is */
        } else if (dot > sq_len) {
            /* Joint at tail side */
            v3_sub(delta, offset_to_tail, delta);
        } else {
            /* Joint between head and tail */
            float t = dot / sq_len;
            float proj[3];
            v3_scale(offset_to_tail, t, proj);
            v3_sub(delta, proj, delta);
        }
    }

    float mag = v3_len(delta);
    *distance = mag - col_radius - joint_radius;
    if (mag > 1e-6f) {
        direction[0] = delta[0]/mag;
        direction[1] = delta[1]/mag;
        direction[2] = delta[2]/mag;
    } else {
        direction[0] = 0; direction[1] = 1; direction[2] = 0;
    }
}

static int find_neck_node(const GltfModel *model) {
    if (!model || !model->humanoid_names || !model->humanoid_bones) return -1;
    for (int i = 0; i < model->humanoid_bone_count; i++) {
        if (strcmp(model->humanoid_names[i], "neck") == 0 ||
            strcmp(model->humanoid_names[i], "Neck") == 0) {
            int idx = model->humanoid_bones[i];
            if (idx >= 0 && idx < model->node_count) return idx;
        }
    }
    return -1;
}

/* Initialize spring bones per spec */
void spring_bones_init(GltfModel *model) {
    if (!model || model->spring_group_count == 0) return;

    int total_joints = 0;
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (group->bone_count < 2) continue;

        float bone_len_sum = 0.0f;
        int bone_len_count = 0;

        /* Per spec: N joints create N-1 joint pairs */
        if (!group->joints) {
            group->joint_count = group->bone_count - 1;
            group->joints = (SpringJoint *)calloc(group->joint_count, sizeof(SpringJoint));
        }

        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            int head_idx = group->bones[i];
            int tail_idx = group->bones[i + 1];
            joint->node = head_idx;

            if (head_idx < 0 || head_idx >= model->node_count ||
                tail_idx < 0 || tail_idx >= model->node_count) continue;

            GltfNode *head = &model->nodes[head_idx];
            GltfNode *tail = &model->nodes[tail_idx];

            /* Store initial local rotation and matrix */
            memcpy(joint->initial_local_rotation, head->base_rotation, 4*sizeof(float));
            build_local_matrix(head->base_translation, head->base_rotation, head->base_scale,
                               joint->initial_local_matrix);

            /* Calculate bone axis (direction to tail in local space) */
            float head_pos[3], tail_pos[3];
            get_world_pos(head->world_matrix, head_pos);
            get_world_pos(tail->world_matrix, tail_pos);

            /* Bone length in world space */
            float diff[3];
            v3_sub(tail_pos, head_pos, diff);
            joint->bone_length = v3_len(diff);
            if (joint->bone_length < 0.001f) joint->bone_length = 0.07f;
            bone_len_sum += joint->bone_length;
            bone_len_count++;

            /* Bone axis: direction to tail in head's local space */
            float head_inv[16];
            float local_tail[3] = {0, 0, 0};
            if (mat4_inverse(head->world_matrix, head_inv)) {
                transform_point(head_inv, tail_pos, local_tail);
            } else {
                local_tail[0] = tail->translation[0];
                local_tail[1] = tail->translation[1];
                local_tail[2] = tail->translation[2];
            }
            joint->bone_axis[0] = local_tail[0];
            joint->bone_axis[1] = local_tail[1];
            joint->bone_axis[2] = local_tail[2];
            float axis_len = v3_len(joint->bone_axis);
            if (axis_len > 1e-6f) {
                v3_scale(joint->bone_axis, 1.0f/axis_len, joint->bone_axis);
            } else {
                joint->bone_axis[0] = 0; joint->bone_axis[1] = -1; joint->bone_axis[2] = 0;
            }

            /* Initialize tail positions */
            memcpy(joint->current_tail, tail_pos, 3*sizeof(float));
            memcpy(joint->prev_tail, tail_pos, 3*sizeof(float));

            total_joints++;
        }

        group->avg_bone_length = bone_len_count > 0 ? bone_len_sum / (float)bone_len_count : 0.0f;
    }
    printf("Spring bones initialized: %d groups, %d joint pairs\n",
           model->spring_group_count, total_joints);
}

/* Reset to rest pose */
void spring_bones_reset(GltfModel *model) {
    if (!model) return;
    g_spring_settle_frames = 5;

    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints) continue;

        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node < 0 || joint->node >= model->node_count) continue;

            GltfNode *head = &model->nodes[joint->node];
            int tail_idx = group->bones[i + 1];
            if (tail_idx < 0 || tail_idx >= model->node_count) continue;
            GltfNode *tail = &model->nodes[tail_idx];

            float tail_pos[3];
            get_world_pos(tail->world_matrix, tail_pos);
            memcpy(joint->current_tail, tail_pos, 3*sizeof(float));
            memcpy(joint->prev_tail, tail_pos, 3*sizeof(float));

            memcpy(head->rotation, head->base_rotation, 4*sizeof(float));
        }
    }
}

/* Update per VRMC_springBone-1.0 spec algorithm */
void spring_bones_update(GltfModel *model, float dt) {
    if (!model || model->spring_group_count == 0 || dt <= 0) return;

    /* Settle period - snap to rest */
    if (g_spring_settle_frames > 0) {
        g_spring_settle_frames--;
        for (int g = 0; g < model->spring_group_count; g++) {
            SpringBoneGroup *group = &model->spring_groups[g];
            if (!group->joints) continue;
            for (int i = 0; i < group->joint_count; i++) {
                SpringJoint *joint = &group->joints[i];
                int tail_idx = group->bones[i + 1];
                if (tail_idx < 0 || tail_idx >= model->node_count) continue;
                float tail_pos[3];
                get_world_pos(model->nodes[tail_idx].world_matrix, tail_pos);
                memcpy(joint->current_tail, tail_pos, 3*sizeof(float));
                memcpy(joint->prev_tail, tail_pos, 3*sizeof(float));
            }
        }
        return;
    }

    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints || group->joint_count == 0) continue;

        int neck_idx = find_neck_node(model);
        int root_idx = group->bones ? group->bones[0] : -1;
        int cloth_chain = 0;
        if (neck_idx >= 0 && root_idx >= 0 && root_idx < model->node_count) {
            float neck_pos[3], root_pos[3];
            get_world_pos(model->nodes[neck_idx].world_matrix, neck_pos);
            get_world_pos(model->nodes[root_idx].world_matrix, root_pos);
            cloth_chain = root_pos[1] < neck_pos[1];
        }

        /* Process joints root to tip (spec: calculation order) */
        for (int i = 0; i < group->joint_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node < 0 || joint->node >= model->node_count) continue;

            GltfNode *node = &model->nodes[joint->node];

            /* Get parameters */
            float drag = joint->drag_force;
            float stiffness = joint->stiffness;
            float grav_power = joint->gravity_power;
            float *grav_dir = joint->gravity_dir;
            float hit_radius = joint->hit_radius;
            if (grav_power == 0.0f) grav_power = 0.1f; /* Fallback: zero gravity causes issues; revisit with better collision handling. */

            /* Apply global tuning */
            drag *= g_spring_drag;
            stiffness *= g_spring_stiffness;
            grav_power *= g_spring_gravity;
            if (g_spring_cloth_mode && cloth_chain) {
                float t = (group->joint_count > 1) ? (float)i / (float)(group->joint_count - 1) : 0.0f;
                float taper = 1.0f - g_spring_cloth_taper * t;
                float length_scale = 1.0f;
                if (group->avg_bone_length > 1e-6f) {
                    length_scale = sqrtf(joint->bone_length / group->avg_bone_length);
                    if (length_scale < 0.5f) length_scale = 0.5f;
                    if (length_scale > 1.5f) length_scale = 1.5f;
                }
                stiffness *= g_spring_cloth_stiffness;
                stiffness *= taper * length_scale;
                drag *= g_spring_cloth_drag;
                grav_power *= g_spring_cloth_gravity;
            }

            float world_pos[3];
            get_world_pos(node->world_matrix, world_pos);

            /* Get parent world rotation */
            float parent_rot[4] = {0, 0, 0, 1};
            if (node->parent_index >= 0 && node->parent_index < model->node_count) {
                mat4_to_quat(model->nodes[node->parent_index].world_matrix, parent_rot);
            }

            /* === STEP 1: Inertia calculation (per spec) === */

            /* Calculate rest direction in world space */
            float combined_rot[4];
            quat_mul(parent_rot, joint->initial_local_rotation, combined_rot);
            float rest_dir[3];
            quat_rotate_vec3(combined_rot, joint->bone_axis, rest_dir);

            /* Inertia: (currentTail - prevTail) * (1.0 - dragForce) */
            float inertia[3];
            if (group->center >= 0 && group->center < model->node_count) {
                const float *center_world = model->nodes[group->center].world_matrix;
                float center_inv[16];
                if (mat4_inverse(center_world, center_inv)) {
                    float curr_center[3], prev_center[3], inertia_center[3];
                    transform_point(center_inv, joint->current_tail, curr_center);
                    transform_point(center_inv, joint->prev_tail, prev_center);
                    v3_sub(curr_center, prev_center, inertia_center);
                    v3_scale(inertia_center, 1.0f - drag, inertia_center);
                    transform_vec(center_world, inertia_center, inertia);
                } else {
                    v3_sub(joint->current_tail, joint->prev_tail, inertia);
                    v3_scale(inertia, 1.0f - drag, inertia);
                }
            } else {
                v3_sub(joint->current_tail, joint->prev_tail, inertia);
                v3_scale(inertia, 1.0f - drag, inertia);
            }

            /* Stiffness: dt * parentWorldRotation * initialLocalRotation * boneAxis * stiffness */
            float stiff_force[3];
            v3_scale(rest_dir, dt * stiffness, stiff_force);

            /* External (gravity): dt * gravityDir * gravityPower */
            float external[3];
            float grav_dir_use[3] = {grav_dir[0], grav_dir[1], grav_dir[2]};
            v3_scale(grav_dir_use, dt * grav_power, external);

            /* nextTail = currentTail + inertia + stiffness + external */
            float next_tail[3];
            memcpy(next_tail, joint->current_tail, 3*sizeof(float));
            v3_add(next_tail, inertia, next_tail);
            v3_add(next_tail, stiff_force, next_tail);
            v3_add(next_tail, external, next_tail);

            /* Constrain length: nextTail = worldPos + normalize(nextTail - worldPos) * boneLength */
            float to_tail[3];
            v3_sub(next_tail, world_pos, to_tail);
            float len = v3_len(to_tail);
            if (len > 1e-6f) {
                v3_scale(to_tail, joint->bone_length / len, to_tail);
            }
            v3_add(world_pos, to_tail, next_tail);

            /* === STEP 2: Collision with colliders === */
            for (int c = 0; c < group->collider_group_count; c++) {
                int cg_idx = group->collider_groups[c];
                if (cg_idx < 0 || cg_idx >= model->spring_collider_count) continue;
                SpringColliderGroup *cg = &model->spring_colliders[cg_idx];

                for (int j = 0; j < cg->collider_count; j++) {
                    SpringCollider *col = &cg->colliders[j];
                    if (col->node < 0 || col->node >= model->node_count) continue;

                    GltfNode *col_node = &model->nodes[col->node];
                    float col_world[3];
                    transform_point(col_node->world_matrix, col->offset, col_world);
                    float col_radius = col->radius;

                    float distance, direction[3];

                    if (col->is_capsule) {
                        float tail_world[3];
                        transform_point(col_node->world_matrix, col->tail, tail_world);
                        collide_capsule(next_tail, hit_radius, col_world, tail_world,
                                       col_radius, &distance, direction);
                    } else {
                        collide_sphere(next_tail, hit_radius, col_world, col_radius,
                                      &distance, direction);
                    }

                    if (g_spring_debug_collisions) {
                        printf("Spring col: group=%d joint=%d col_node=%d dist=%.4f hit=%.4f col=%.4f\n",
                               g, i, col->node, distance, hit_radius, col_radius);
                    }
                    if (distance < 0.0f) {
                        /* Push out: nextTail = nextTail - direction * distance */
                        float push[3];
                        v3_scale(direction, -distance, push);
                        v3_add(next_tail, push, next_tail);

                        /* Re-constrain length */
                        v3_sub(next_tail, world_pos, to_tail);
                        len = v3_len(to_tail);
                        if (len > 1e-6f) {
                            v3_scale(to_tail, joint->bone_length / len, to_tail);
                        }
                        v3_add(world_pos, to_tail, next_tail);
                    }
                }
            }

            /* === STEP 3: Apply rotation === */

            /* Update state */
            memcpy(joint->prev_tail, joint->current_tail, 3*sizeof(float));
            memcpy(joint->current_tail, next_tail, 3*sizeof(float));

            float parent_world[16];
            if (node->parent_index >= 0 && node->parent_index < model->node_count) {
                memcpy(parent_world, model->nodes[node->parent_index].world_matrix, sizeof(parent_world));
            } else {
                float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                memcpy(parent_world, identity, sizeof(parent_world));
            }

            float rest_world[16];
            mat4_mul(parent_world, joint->initial_local_matrix, rest_world);

            float rest_inv[16];
            float new_rot[4];
            if (mat4_inverse(rest_world, rest_inv)) {
                float to_local[3];
                transform_point(rest_inv, next_tail, to_local);
                v3_normalize(to_local);
                float local_delta[4];
                quat_from_to(joint->bone_axis, to_local, local_delta);
                quat_mul(joint->initial_local_rotation, local_delta, new_rot);
            } else {
                memcpy(new_rot, joint->initial_local_rotation, sizeof(new_rot));
            }

            /* Normalize */
            float qlen = sqrtf(new_rot[0]*new_rot[0] + new_rot[1]*new_rot[1] +
                              new_rot[2]*new_rot[2] + new_rot[3]*new_rot[3]);
            if (qlen > 1e-6f) {
                node->rotation[0] = new_rot[0] / qlen;
                node->rotation[1] = new_rot[1] / qlen;
                node->rotation[2] = new_rot[2] / qlen;
                node->rotation[3] = new_rot[3] / qlen;
            }

            update_node_transform(model, joint->node, parent_world);
        }
    }
}

/* Cleanup */
void spring_bones_cleanup(GltfModel *model) {
    if (!model) return;

    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        free(group->joints);
        free(group->bones);
        free(group->collider_groups);
    }
    free(model->spring_groups);
    model->spring_groups = NULL;
    model->spring_group_count = 0;

    for (int c = 0; c < model->spring_collider_count; c++) {
        free(model->spring_colliders[c].colliders);
    }
    free(model->spring_colliders);
    model->spring_colliders = NULL;
    model->spring_collider_count = 0;
}
