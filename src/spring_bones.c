/*
 * VRM Spring Bone Physics Simulation
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
float g_spring_gravity = 1.0f;
static int g_spring_settle_frames = 0;

/* Vector helpers */
static void v3_sub(const float *a, const float *b, float *out) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}

static void v3_add(const float *a, const float *b, float *out) {
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}

static void v3_scale(const float *v, float s, float *out) {
    out[0] = v[0] * s; out[1] = v[1] * s; out[2] = v[2] * s;
}

static float v3_len(const float *v) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static void v3_normalize(float *v) {
    float len = v3_len(v);
    if (len > 0.0001f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

/* Get world position from node's world matrix */
static void get_world_pos(const float *matrix, float *pos) {
    pos[0] = matrix[12]; pos[1] = matrix[13]; pos[2] = matrix[14];
}

/* Transform local position by world matrix */
static void transform_point(const float *matrix, const float *local, float *world) {
    world[0] = matrix[0]*local[0] + matrix[4]*local[1] + matrix[8]*local[2] + matrix[12];
    world[1] = matrix[1]*local[0] + matrix[5]*local[1] + matrix[9]*local[2] + matrix[13];
    world[2] = matrix[2]*local[0] + matrix[6]*local[1] + matrix[10]*local[2] + matrix[14];
}

/* Initialize spring bone runtime state */
void spring_bones_init(GltfModel *model) {
    if (!model || model->spring_group_count == 0) return;
    
    int total_joints = 0;
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (group->bone_count == 0) continue;
        
        /* Allocate joints (one per bone) */
        group->joints = (SpringJoint *)calloc(group->bone_count, sizeof(SpringJoint));
        
        for (int i = 0; i < group->bone_count; i++) {
            SpringJoint *joint = &group->joints[i];
            joint->node = group->bones[i];
            
            if (joint->node >= 0 && joint->node < model->node_count) {
                GltfNode *node = &model->nodes[joint->node];
                
                /* Calculate bone length from translation */
                joint->bone_length = sqrtf(
                    node->translation[0] * node->translation[0] +
                    node->translation[1] * node->translation[1] +
                    node->translation[2] * node->translation[2]
                );
                if (joint->bone_length < 0.01f) joint->bone_length = 0.07f;
                
                /* Initialize tail position at rest */
                get_world_pos(node->world_matrix, joint->curr_pos);
                /* Offset by bone length in local Y (typical VRM bone direction) */
                float tail_local[3] = {0, -joint->bone_length, 0};
                transform_point(node->world_matrix, tail_local, joint->curr_pos);
                memcpy(joint->prev_pos, joint->curr_pos, sizeof(joint->curr_pos));
                total_joints++;
            }
        }
    }
    printf("Spring bones initialized: %d groups, %d joints\n", model->spring_group_count, total_joints);
}

/* Reset spring bones to rest pose */
void spring_bones_reset(GltfModel *model) {
    if (!model) return;
    
    g_spring_settle_frames = 5; /* Skip physics for 5 frames after reset */
    
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints) continue;
        
        for (int i = 0; i < group->bone_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node >= 0 && joint->node < model->node_count) {
                GltfNode *node = &model->nodes[joint->node];
                float tail_local[3] = {0, -joint->bone_length, 0};
                transform_point(node->world_matrix, tail_local, joint->curr_pos);
                memcpy(joint->prev_pos, joint->curr_pos, sizeof(joint->curr_pos));
                
                /* Also reset rotation to base */
                node->rotation[0] = node->base_rotation[0];
                node->rotation[1] = node->base_rotation[1];
                node->rotation[2] = node->base_rotation[2];
                node->rotation[3] = node->base_rotation[3];
            }
        }
    }
}

/* Sphere collision resolution */
static void resolve_collision(float *pos, const float *center, float radius, float hit_radius) {
    float diff[3];
    v3_sub(pos, center, diff);
    float dist = v3_len(diff);
    float min_dist = radius + hit_radius;
    
    if (dist < min_dist && dist > 0.0001f) {
        /* Push out of collision */
        v3_normalize(diff);
        pos[0] = center[0] + diff[0] * min_dist;
        pos[1] = center[1] + diff[1] * min_dist;
        pos[2] = center[2] + diff[2] * min_dist;
    }
}

/* Quaternion from rotation between two vectors */
static void quat_from_to(const float *from, const float *to, float *q) {
    float fn[3] = {from[0], from[1], from[2]};
    float tn[3] = {to[0], to[1], to[2]};
    v3_normalize(fn);
    v3_normalize(tn);
    
    float dot = fn[0]*tn[0] + fn[1]*tn[1] + fn[2]*tn[2];
    if (dot > 0.9999f) {
        q[0] = q[1] = q[2] = 0; q[3] = 1; return;
    }
    if (dot < -0.9999f) {
        q[0] = 1; q[1] = q[2] = 0; q[3] = 0; return;
    }
    /* Cross product for axis */
    float axis[3] = {
        fn[1]*tn[2] - fn[2]*tn[1],
        fn[2]*tn[0] - fn[0]*tn[2],
        fn[0]*tn[1] - fn[1]*tn[0]
    };
    q[0] = axis[0]; q[1] = axis[1]; q[2] = axis[2];
    q[3] = 1.0f + dot;
    float len = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (len > 0.0001f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* Quaternion multiply: out = a * b */
static void quat_mul(const float *a, const float *b, float *out) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

/* Quaternion conjugate (inverse for unit quaternions) */
static void quat_conj(const float *q, float *out) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

/* Extract rotation from 3x3 part of 4x4 matrix */
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
        q[3] = (m[6] - m[9]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[4] + m[1]) / s;
        q[2] = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        float s = 2.0f * sqrtf(1.0f + m[5] - m[0] - m[10]);
        q[3] = (m[8] - m[2]) / s;
        q[0] = (m[4] + m[1]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[9] + m[6]) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + m[10] - m[0] - m[5]);
        q[3] = (m[1] - m[4]) / s;
        q[0] = (m[8] + m[2]) / s;
        q[1] = (m[9] + m[6]) / s;
        q[2] = 0.25f * s;
    }
    float len = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (len > 0.0001f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

/* Update spring bone simulation */
void spring_bones_update(GltfModel *model, float dt) {
    if (!model || model->spring_group_count == 0 || dt <= 0) return;
    
    /* During settle period, just snap to rest pose */
    if (g_spring_settle_frames > 0) {
        g_spring_settle_frames--;
        for (int g = 0; g < model->spring_group_count; g++) {
            SpringBoneGroup *group = &model->spring_groups[g];
            if (!group->joints) continue;
            for (int i = 0; i < group->bone_count; i++) {
                SpringJoint *joint = &group->joints[i];
                if (joint->node >= 0 && joint->node < model->node_count) {
                    GltfNode *node = &model->nodes[joint->node];
                    float tail_local[3] = {0, -joint->bone_length, 0};
                    transform_point(node->world_matrix, tail_local, joint->curr_pos);
                    memcpy(joint->prev_pos, joint->curr_pos, sizeof(float)*3);
                }
            }
        }
        return;
    }
    
    if (dt > 0.033f) dt = 0.033f;
    
    for (int g = 0; g < model->spring_group_count; g++) {
        SpringBoneGroup *group = &model->spring_groups[g];
        if (!group->joints || group->bone_count == 0) continue;
        
        float stiffness = group->stiffness * g_spring_stiffness * 80.0f;
        float drag = 1.0f - (group->drag * g_spring_drag * 0.15f);
        if (drag < 0.3f) drag = 0.3f;
        
        for (int i = 0; i < group->bone_count; i++) {
            SpringJoint *joint = &group->joints[i];
            if (joint->node < 0 || joint->node >= model->node_count) continue;
            
            GltfNode *node = &model->nodes[joint->node];
            
            float head_pos[3];
            get_world_pos(node->world_matrix, head_pos);
            
            /* Rest tail in world space */
            float rest_local[3] = {0, -joint->bone_length, 0};
            float rest_world[3];
            transform_point(node->world_matrix, rest_local, rest_world);
            
            /* Verlet with strong damping */
            float vel[3];
            v3_sub(joint->curr_pos, joint->prev_pos, vel);
            
            /* Clamp velocity to prevent explosion on animation loop */
            float vel_len = v3_len(vel);
            if (vel_len > joint->bone_length * 0.5f) {
                v3_scale(vel, (joint->bone_length * 0.5f) / vel_len, vel);
            }
            v3_scale(vel, drag, vel);
            
            /* Strong spring force */
            float spring[3];
            v3_sub(rest_world, joint->curr_pos, spring);
            v3_scale(spring, stiffness * dt * dt, spring);
            
            /* Weak gravity */
            float grav[3];
            v3_scale(group->gravity_dir, group->gravity_power * g_spring_gravity * dt * dt * 5.0f, grav);
            
            memcpy(joint->prev_pos, joint->curr_pos, sizeof(float)*3);
            v3_add(joint->curr_pos, vel, joint->curr_pos);
            v3_add(joint->curr_pos, spring, joint->curr_pos);
            v3_add(joint->curr_pos, grav, joint->curr_pos);
            
            /* Constrain to bone length */
            float to_tail[3];
            v3_sub(joint->curr_pos, head_pos, to_tail);
            float len = v3_len(to_tail);
            if (len > 0.0001f) {
                v3_scale(to_tail, joint->bone_length / len, to_tail);
                v3_add(head_pos, to_tail, joint->curr_pos);
            }
            
            /* Collisions */
            for (int c = 0; c < group->collider_group_count; c++) {
                int cg_idx = group->collider_groups[c];
                if (cg_idx < 0 || cg_idx >= model->spring_collider_count) continue;
                SpringColliderGroup *cg = &model->spring_colliders[cg_idx];
                if (cg->node < 0 || cg->node >= model->node_count) continue;
                GltfNode *cg_node = &model->nodes[cg->node];
                for (int j = 0; j < cg->collider_count; j++) {
                    SpringCollider *col = &cg->colliders[j];
                    float col_world[3];
                    transform_point(cg_node->world_matrix, col->offset, col_world);
                    resolve_collision(joint->curr_pos, col_world, col->radius, group->hit_radius);
                }
            }
            
            /* Get parent world rotation */
            float parent_world_rot[4] = {0,0,0,1};
            if (node->parent_index >= 0 && node->parent_index < model->node_count) {
                mat4_to_quat(model->nodes[node->parent_index].world_matrix, parent_world_rot);
            }
            
            /* World space delta rotation */
            float rest_dir[3], curr_dir[3];
            v3_sub(rest_world, head_pos, rest_dir);
            v3_sub(joint->curr_pos, head_pos, curr_dir);
            
            float world_delta[4];
            quat_from_to(rest_dir, curr_dir, world_delta);
            
            /* Convert to local: local_delta = parent_inv * world_delta * parent */
            float parent_inv[4], tmp[4], local_delta[4];
            quat_conj(parent_world_rot, parent_inv);
            quat_mul(parent_inv, world_delta, tmp);
            quat_mul(tmp, parent_world_rot, local_delta);
            
            /* Apply to base rotation */
            float new_rot[4];
            quat_mul(local_delta, node->base_rotation, new_rot);
            
            node->rotation[0] = new_rot[0];
            node->rotation[1] = new_rot[1];
            node->rotation[2] = new_rot[2];
            node->rotation[3] = new_rot[3];
        }
    }
}

/* Cleanup spring bone data */
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
