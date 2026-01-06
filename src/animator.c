/*
 * VRM Overlay - Animator Implementation
 */

#include "animator.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>

float g_anim_blend_time_default = 3.0f;
float g_anim_blend_group_factors[BLEND_GROUP_COUNT] = {
    1.0f, /* root */
    1.0f, /* spine */
    0.8f, /* head */
    0.9f, /* arms */
    0.7f, /* hands */
    1.0f, /* legs */
    1.0f, /* feet */
    0.5f, /* face */
    1.0f  /* other */
};

static bool animation_is_loopable(Animator *anim, int anim_idx);

void animator_init(Animator *anim) {
    memset(anim, 0, sizeof(Animator));
    anim->speed = 1.0f;
    anim->current_animation = -1;
    anim->next_animation = -1;
    anim->loop_remaining = 0;
    anim->next_loop_remaining = 0;
    anim->current_loopable = false;
    anim->next_loopable = false;
    anim->loop_blend_active = false;
}

void animator_cleanup(Animator *anim) {
    free(anim->snap_translations);
    free(anim->snap_rotations);
    free(anim->snap_scales);
    anim->snap_translations = NULL;
    anim->snap_rotations = NULL;
    anim->snap_scales = NULL;
    anim->snap_node_count = 0;

    free(anim->blend_time_factors);
    anim->blend_time_factors = NULL;
    anim->blend_node_count = 0;
}

void animator_set_model(Animator *anim, GltfModel *model) {
    animator_cleanup(anim);
    anim->model = model;
    anim->time = 0;
    anim->current_animation = (model && model->animation_count > 0) ? 0 : -1;
    anim->next_animation = -1;
    anim->loop_remaining = (anim->current_animation >= 0) ? animator_compute_loop_remaining(anim, anim->current_animation) : 0;
    anim->current_loopable = (anim->current_animation >= 0) ? animation_is_loopable(anim, anim->current_animation) : false;
    anim->next_loop_remaining = anim->loop_remaining;
    anim->next_loopable = anim->current_loopable;
    anim->loop_blend_active = false;
}

/* Linear interpolation */
static float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

static float ease_smoothstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static bool name_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen) {
            char hc = p[i];
            if (!hc) break;
            char nc = needle[i];
            if (tolower((unsigned char)hc) != tolower((unsigned char)nc)) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static BlendGroup classify_blend_group(const char *name) {
    if (!name) return BLEND_GROUP_OTHER;
    if (name_contains_ci(name, "face") || name_contains_ci(name, "jaw") || name_contains_ci(name, "eye")) {
        return BLEND_GROUP_FACE;
    }
    if (name_contains_ci(name, "hand") || name_contains_ci(name, "wrist")) {
        return BLEND_GROUP_HANDS;
    }
    if (name_contains_ci(name, "foot") || name_contains_ci(name, "ankle") || name_contains_ci(name, "toe")) {
        return BLEND_GROUP_FEET;
    }
    if (name_contains_ci(name, "arm") || name_contains_ci(name, "shoulder") || name_contains_ci(name, "elbow") ||
        name_contains_ci(name, "forearm")) {
        return BLEND_GROUP_ARMS;
    }
    if (name_contains_ci(name, "leg") || name_contains_ci(name, "thigh") || name_contains_ci(name, "calf") ||
        name_contains_ci(name, "knee")) {
        return BLEND_GROUP_LEGS;
    }
    if (name_contains_ci(name, "head") || name_contains_ci(name, "neck")) {
        return BLEND_GROUP_HEAD;
    }
    if (name_contains_ci(name, "spine") || name_contains_ci(name, "chest")) {
        return BLEND_GROUP_SPINE;
    }
    if (name_contains_ci(name, "hips") || name_contains_ci(name, "pelvis") || name_contains_ci(name, "root")) {
        return BLEND_GROUP_ROOT;
    }
    return BLEND_GROUP_OTHER;
}

static void ensure_blend_factors(Animator *anim) {
    if (!anim->model) return;
    int n = anim->model->node_count;
    if (anim->blend_node_count >= n) return;

    free(anim->blend_time_factors);
    anim->blend_time_factors = malloc((size_t)n * sizeof(float));
    anim->blend_node_count = n;

    for (int i = 0; i < n; i++) {
        GltfNode *node = &anim->model->nodes[i];
        BlendGroup group = classify_blend_group(node->name);
        float factor = g_anim_blend_group_factors[group];
        if (factor <= 0.0f || factor > 1.0f) factor = 1.0f;
        anim->blend_time_factors[i] = factor;
    }
}

/* Spherical linear interpolation for quaternions */
static void slerp(const float *a, const float *b, float t, float *out) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

    float b2[4];
    if (dot < 0.0f) {
        dot = -dot;
        b2[0] = -b[0]; b2[1] = -b[1]; b2[2] = -b[2]; b2[3] = -b[3];
    } else {
        b2[0] = b[0]; b2[1] = b[1]; b2[2] = b[2]; b2[3] = b[3];
    }

    if (dot > 0.9995f) {
        /* Linear interpolation for very close quaternions */
        out[0] = lerp(a[0], b2[0], t);
        out[1] = lerp(a[1], b2[1], t);
        out[2] = lerp(a[2], b2[2], t);
        out[3] = lerp(a[3], b2[3], t);
    } else {
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        out[0] = wa * a[0] + wb * b2[0];
        out[1] = wa * a[1] + wb * b2[1];
        out[2] = wa * a[2] + wb * b2[2];
        out[3] = wa * a[3] + wb * b2[3];
    }

    /* Normalize */
    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
    if (len > 0.0001f) {
        out[0] /= len; out[1] /= len; out[2] /= len; out[3] /= len;
    }
}

static void quat_mul(const float *a, const float *b, float *out) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

static void quat_inv(const float *q, float *out) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

static void quat_rotate_vec(const float *q, const float *v, float *out) {
    float vq[4] = {v[0], v[1], v[2], 0.0f};
    float qinv[4];
    float t1[4], t2[4];
    quat_inv(q, qinv);
    quat_mul(q, vq, t1);
    quat_mul(t1, qinv, t2);
    out[0] = t2[0];
    out[1] = t2[1];
    out[2] = t2[2];
}

static void apply_global_anim_correction(GltfModel *model, GltfAnimation *anim) {
    if (!model || !anim) return;
    /* Check if correction quaternion is identity - skip if so */
    float *corr = model->anim_global_correction;
    if (corr[0] == 0.0f && corr[1] == 0.0f && corr[2] == 0.0f && corr[3] == 1.0f) return;
    
    float corr_inv[4];
    quat_inv(corr, corr_inv);
    for (int i = 0; i < model->node_count; i++) {
        uint8_t mask = (anim->node_mask && i < anim->node_mask_count) ? anim->node_mask[i] : 0;
        if (mask & 2) {
            float t1[4], t2[4];
            quat_mul(corr, model->nodes[i].rotation, t1);
            quat_mul(t1, corr_inv, t2);
            model->nodes[i].rotation[0] = t2[0];
            model->nodes[i].rotation[1] = t2[1];
            model->nodes[i].rotation[2] = t2[2];
            model->nodes[i].rotation[3] = t2[3];
        }
        if (mask & 1) {
            float v[3];
            quat_rotate_vec(corr, model->nodes[i].translation, v);
            model->nodes[i].translation[0] = v[0];
            model->nodes[i].translation[1] = v[1];
            model->nodes[i].translation[2] = v[2];
        }
    }
}

/* Find keyframe index for time t */
static int find_keyframe(const GltfAnimSampler *s, float t) {
    for (int i = 0; i < s->keyframe_count - 1; i++) {
        if (t >= s->input[i] && t < s->input[i + 1]) {
            return i;
        }
    }
    return s->keyframe_count - 2;
}

/* Sample animation at time t */
static void sample_animation(GltfModel *model, GltfAnimation *anim, float t) {
    for (int i = 0; i < model->node_count; i++) {
        GltfNode *node = &model->nodes[i];
        if (node->is_spring_bone) continue; /* Don't reset spring bones */
        node->translation[0] = node->base_translation[0];
        node->translation[1] = node->base_translation[1];
        node->translation[2] = node->base_translation[2];
        node->rotation[0] = node->base_rotation[0];
        node->rotation[1] = node->base_rotation[1];
        node->rotation[2] = node->base_rotation[2];
        node->rotation[3] = node->base_rotation[3];
        node->scale[0] = node->base_scale[0];
        node->scale[1] = node->base_scale[1];
        node->scale[2] = node->base_scale[2];
    }

    for (int i = 0; i < anim->channel_count; i++) {
        GltfAnimChannel *ch = &anim->channels[i];
        if (ch->target_node < 0 || ch->target_node >= model->node_count) continue;
        if (ch->sampler_index < 0 || ch->sampler_index >= anim->sampler_count) continue;

        GltfAnimSampler *s = &anim->samplers[ch->sampler_index];
        if (s->keyframe_count < 2) continue;

        GltfNode *node = &model->nodes[ch->target_node];
        //if (node->is_spring_bone) continue;

        /* Clamp time */
        float ct = t;
        if (ct < s->input[0]) ct = s->input[0];
        if (ct > s->input[s->keyframe_count - 1]) ct = s->input[s->keyframe_count - 1];

        int k = find_keyframe(s, ct);
        float t0 = s->input[k];
        float t1 = s->input[k + 1];
        float factor = (t1 > t0) ? (ct - t0) / (t1 - t0) : 0.0f;

        float *v0 = &s->output[k * s->output_stride];
        float *v1 = &s->output[(k + 1) * s->output_stride];

        switch (ch->path) {
            case GLTF_PATH_TRANSLATION:
                node->translation[0] = lerp(v0[0], v1[0], factor);
                node->translation[1] = lerp(v0[1], v1[1], factor);
                node->translation[2] = lerp(v0[2], v1[2], factor);
                break;
            case GLTF_PATH_ROTATION: {
                slerp(v0, v1, factor, node->rotation);
                break;
            }
            case GLTF_PATH_SCALE:
                node->scale[0] = lerp(v0[0], v1[0], factor);
                node->scale[1] = lerp(v0[1], v1[1], factor);
                node->scale[2] = lerp(v0[2], v1[2], factor);
                break;
            case GLTF_PATH_WEIGHTS:
                /* TODO: morph target weights */
                break;
        }
    }
    apply_global_anim_correction(model, anim);
}


static void copy_pose_arrays(GltfModel *model, float *t, float *r, float *s) {
    int n = model->node_count;
    for (int i = 0; i < n; i++) {
        GltfNode *node = &model->nodes[i];
        memcpy(&t[i * 3], node->translation, 3 * sizeof(float));
        memcpy(&r[i * 4], node->rotation, 4 * sizeof(float));
        memcpy(&s[i * 3], node->scale, 3 * sizeof(float));
    }
}

static void restore_pose_arrays(GltfModel *model, const float *t, const float *r, const float *s) {
    int n = model->node_count;
    for (int i = 0; i < n; i++) {
        GltfNode *node = &model->nodes[i];
        memcpy(node->translation, &t[i * 3], 3 * sizeof(float));
        memcpy(node->rotation, &r[i * 4], 4 * sizeof(float));
        memcpy(node->scale, &s[i * 3], 3 * sizeof(float));
    }
}

static bool animation_is_loopable(Animator *anim, int anim_idx) {
    if (!anim || !anim->model) return false;
    if (anim_idx < 0 || anim_idx >= anim->model->animation_count) return false;

    GltfModel *model = anim->model;
    GltfAnimation *animation = &model->animations[anim_idx];
    if (animation->duration <= 0.0f) return false;

    int n = model->node_count;
    float *saved_t = malloc((size_t)n * 3 * sizeof(float));
    float *saved_r = malloc((size_t)n * 4 * sizeof(float));
    float *saved_s = malloc((size_t)n * 3 * sizeof(float));
    float *start_t = malloc((size_t)n * 3 * sizeof(float));
    float *start_r = malloc((size_t)n * 4 * sizeof(float));
    float *start_s = malloc((size_t)n * 3 * sizeof(float));
    float *end_t = malloc((size_t)n * 3 * sizeof(float));
    float *end_r = malloc((size_t)n * 4 * sizeof(float));
    float *end_s = malloc((size_t)n * 3 * sizeof(float));

    if (!saved_t || !saved_r || !saved_s || !start_t || !start_r || !start_s || !end_t || !end_r || !end_s) {
        free(saved_t); free(saved_r); free(saved_s);
        free(start_t); free(start_r); free(start_s);
        free(end_t); free(end_r); free(end_s);
        return false;
    }

    copy_pose_arrays(model, saved_t, saved_r, saved_s);

    sample_animation(model, animation, 0.0f);
    copy_pose_arrays(model, start_t, start_r, start_s);
    sample_animation(model, animation, animation->duration);
    copy_pose_arrays(model, end_t, end_r, end_s);

    restore_pose_arrays(model, saved_t, saved_r, saved_s);
    gltf_update_transforms(model);

    free(saved_t); free(saved_r); free(saved_s);

    const float pos_eps = 5e-3f;
    const float pos_eps_sq = pos_eps * pos_eps;
    const float scale_eps = 5e-3f;
    const float rot_angle = 0.2f;
    const float rot_dot_min = cosf(rot_angle * 0.5f);

    bool loopable = true;
    for (int i = 0; i < n; i++) {
        GltfNode *node = &model->nodes[i];
        if (node->is_spring_bone) continue;
        uint8_t mask = (animation->node_mask && i < animation->node_mask_count) ? animation->node_mask[i] : 0;
        if (mask == 0) continue;

        if (mask & 1) {
            float dx = start_t[i * 3 + 0] - end_t[i * 3 + 0];
            float dy = start_t[i * 3 + 1] - end_t[i * 3 + 1];
            float dz = start_t[i * 3 + 2] - end_t[i * 3 + 2];
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 > pos_eps_sq) {
                printf("Loop check fail: anim=%s node=%s idx=%d translation delta=(%.4f, %.4f, %.4f)\n",
                       animation->name, node->name, i, dx, dy, dz);
                loopable = false; break;
            }
        }
        if (mask & 2) {
            float dot = start_r[i * 4 + 0] * end_r[i * 4 + 0] +
                        start_r[i * 4 + 1] * end_r[i * 4 + 1] +
                        start_r[i * 4 + 2] * end_r[i * 4 + 2] +
                        start_r[i * 4 + 3] * end_r[i * 4 + 3];
            if (dot < 0.0f) dot = -dot;
            if (dot < rot_dot_min) {
                printf("Loop check fail: anim=%s node=%s idx=%d rotation dot=%.4f\n",
                       animation->name, node->name, i, dot);
                loopable = false; break;
            }
        }
        if (mask & 4) {
            float dx = start_s[i * 3 + 0] - end_s[i * 3 + 0];
            float dy = start_s[i * 3 + 1] - end_s[i * 3 + 1];
            float dz = start_s[i * 3 + 2] - end_s[i * 3 + 2];
            if (fabsf(dx) > scale_eps || fabsf(dy) > scale_eps || fabsf(dz) > scale_eps) {
                printf("Loop check fail: anim=%s node=%s idx=%d scale delta=(%.4f, %.4f, %.4f)\n",
                       animation->name, node->name, i, dx, dy, dz);
                loopable = false; break;
            }
        }
    }

    free(start_t); free(start_r); free(start_s);
    free(end_t); free(end_r); free(end_s);
    return loopable;
}

void animator_update(Animator *anim, float dt) {
    if (!anim->model || !anim->playing) return;
    if (anim->current_animation < 0 || anim->current_animation >= anim->model->animation_count) return;

    GltfModel *model = anim->model;

    /* Blending mode */
    if (anim->next_animation >= 0) {
        anim->blend_time += dt;
        
        GltfAnimation *next_anim = &model->animations[anim->next_animation];
        
        float t = anim->blend_time / anim->blend_duration;
        if (t >= 1.0f) {
            int prev = anim->current_animation;
            int next = anim->next_animation;
            anim->current_animation = anim->next_animation;
            anim->time = anim->next_anim_time;
            anim->next_animation = -1;
            anim->loop_remaining = anim->next_loop_remaining;
            anim->current_loopable = anim->next_loopable;
            anim->loop_blend_active = false;
            printf("Blend complete: from=%d to=%d\n", prev, next);
            fflush(stdout);
        }
        float eased_t = ease_smoothstep(t);
        
        /* Sample target animation before advancing time so we hit t=0 on blend start. */
        sample_animation(model, next_anim, anim->next_anim_time);
        ensure_blend_factors(anim);
        
        /* Blend with snapshot */
        for (int i = 0; i < model->node_count && i < anim->snap_node_count; i++) {
            GltfNode *node = &model->nodes[i];
            if (node->is_spring_bone) continue;
            
            float *st = &anim->snap_translations[i * 3];
            float *sr = &anim->snap_rotations[i * 4];
            float *ss = &anim->snap_scales[i * 3];
            
            float factor = (anim->blend_time_factors && i < anim->blend_node_count) ? anim->blend_time_factors[i] : 1.0f;
            if (factor <= 0.0f || factor > 1.0f) factor = 1.0f;
            float bone_t = eased_t;
            if (factor < 1.0f) {
                float denom = anim->blend_duration * factor;
                if (denom > 0.0f) {
                    float raw = anim->blend_time / denom;
                    if (raw > 1.0f) raw = 1.0f;
                    bone_t = ease_smoothstep(raw);
                }
            }

            node->translation[0] = lerp(st[0], node->translation[0], bone_t);
            node->translation[1] = lerp(st[1], node->translation[1], bone_t);
            node->translation[2] = lerp(st[2], node->translation[2], bone_t);
            
            float blended_rot[4];
            slerp(sr, node->rotation, bone_t, blended_rot);
            memcpy(node->rotation, blended_rot, sizeof(float) * 4);
            
            node->scale[0] = lerp(ss[0], node->scale[0], bone_t);
            node->scale[1] = lerp(ss[1], node->scale[1], bone_t);
            node->scale[2] = lerp(ss[2], node->scale[2], bone_t);
        }


        if (!anim->loop_blend_active) {
            anim->next_anim_time += dt * anim->speed;
            /* Wrap next animation time */
            if (next_anim->duration > 0) {
                while (anim->next_anim_time > next_anim->duration)
                    anim->next_anim_time -= next_anim->duration;
            }
        }

        gltf_update_transforms(model);
        return;
    }

    /* Normal playback */
    GltfAnimation *animation = &model->animations[anim->current_animation];
    anim->time += dt * anim->speed;

    /* Loop */
    if (animation->duration > 0) {
        if (anim->time > animation->duration) {
            if (anim->loop_remaining == 0) {
                anim->time = animation->duration;
            } else {
                if (!anim->current_loopable) {
                    int remaining = anim->loop_remaining;
                    if (remaining > 0) remaining--;
                    sample_animation(model, animation, animation->duration);
                    gltf_update_transforms(model);
                    float loop_blend_time = animator_compute_auto_blend_time(anim, anim->current_animation);
                    animator_blend_to(anim, anim->current_animation, loop_blend_time, remaining);
                    anim->next_anim_time = 0.0f;
                    anim->loop_blend_active = true;
                    return;
                }
                while (anim->time > animation->duration) {
                    anim->time -= animation->duration;
                    if (anim->loop_remaining > 0) {
                        anim->loop_remaining--;
                        if (anim->loop_remaining == 0) {
                            anim->time = animation->duration;
                            break;
                        }
                    }
                }
            }
        }
        while (anim->time < 0) {
            if (anim->loop_remaining == 0) {
                anim->time = 0.0f;
                break;
            }
            anim->time += animation->duration;
        }
    }

    sample_animation(model, animation, anim->time);
    gltf_update_transforms(model);
}

void animator_play(Animator *anim, float speed) {
    anim->playing = true;
    anim->speed = speed;
}

void animator_pause(Animator *anim) {
    anim->playing = false;
}

void animator_set_time(Animator *anim, float t) {
    anim->time = t;
    if (anim->model && anim->current_animation >= 0) {
        GltfAnimation *animation = &anim->model->animations[anim->current_animation];
        sample_animation(anim->model, animation, anim->time);
        gltf_update_transforms(anim->model);
    }
}

void animator_set_animation(Animator *anim, int index) {
    if (!anim->model) return;
    if (index >= 0 && index < anim->model->animation_count) {
        anim->current_animation = index;
        anim->time = 0;
        anim->next_animation = -1;
        anim->loop_blend_active = false;
        anim->loop_remaining = animator_compute_loop_remaining(anim, index);
        anim->next_loop_remaining = anim->loop_remaining;
        anim->current_loopable = animation_is_loopable(anim, index);
        anim->next_loopable = anim->current_loopable;
    }
}

static void snapshot_pose(Animator *anim) {
    GltfModel *model = anim->model;
    int n = model->node_count;
    
    if (anim->snap_node_count < n) {
        free(anim->snap_translations);
        free(anim->snap_rotations);
        free(anim->snap_scales);
        anim->snap_translations = malloc(n * 3 * sizeof(float));
        anim->snap_rotations = malloc(n * 4 * sizeof(float));
        anim->snap_scales = malloc(n * 3 * sizeof(float));
        anim->snap_node_count = n;
    }
    
    for (int i = 0; i < n; i++) {
        GltfNode *node = &model->nodes[i];
        memcpy(&anim->snap_translations[i * 3], node->translation, 3 * sizeof(float));
        memcpy(&anim->snap_rotations[i * 4], node->rotation, 4 * sizeof(float));
        memcpy(&anim->snap_scales[i * 3], node->scale, 3 * sizeof(float));
    }
}

/* Compute quaternion angular distance in radians */
static float quat_angle_distance(const float *q1, const float *q2) {
    float dot = q1[0]*q2[0] + q1[1]*q2[1] + q1[2]*q2[2] + q1[3]*q2[3];
    if (dot < 0.0f) dot = -dot; /* Handle quaternion double-cover */
    if (dot > 1.0f) dot = 1.0f;
    return 2.0f * acosf(dot);
}

/* Compute blend time based on pose difference */
float animator_compute_auto_blend_time(Animator *anim, int to_anim) {
    if (!anim || !anim->model) return g_anim_blend_time_default;
    GltfModel *model = anim->model;
    if (to_anim < 0 || to_anim >= model->animation_count) return g_anim_blend_time_default;
    
    int n = model->node_count;
    if (n <= 0) return g_anim_blend_time_default;
    
    /* Save current pose */
    float *saved_t = malloc((size_t)n * 3 * sizeof(float));
    float *saved_r = malloc((size_t)n * 4 * sizeof(float));
    float *saved_s = malloc((size_t)n * 3 * sizeof(float));
    if (!saved_t || !saved_r || !saved_s) {
        free(saved_t);
        free(saved_r);
        free(saved_s);
        return g_anim_blend_time_default;
    }
    copy_pose_arrays(model, saved_t, saved_r, saved_s);
    
    /* Sample target animation at t=0 */
    GltfAnimation *target = &model->animations[to_anim];
    sample_animation(model, target, 0.0f);
    
    /* Find max pose distance (translation/rotation/scale) */
    const float rot_weight = 0.5f;
    const float scale_weight = 0.5f;
    float max_dist = 0.0f;
    for (int i = 0; i < n; i++) {
        GltfNode *node = &model->nodes[i];
        uint8_t mask = target->node_mask ? ((i < target->node_mask_count) ? target->node_mask[i] : 0) : 7;
        if (mask == 0) continue;

        float trans_dist = 0.0f;
        float rot_dist = 0.0f;
        float scale_dist = 0.0f;

        if (mask & 1) {
            float dx = saved_t[i * 3 + 0] - node->translation[0];
            float dy = saved_t[i * 3 + 1] - node->translation[1];
            float dz = saved_t[i * 3 + 2] - node->translation[2];
            trans_dist = sqrtf(dx*dx + dy*dy + dz*dz);
        }
        if (mask & 2) {
            rot_dist = quat_angle_distance(&saved_r[i * 4], node->rotation);
        }
        if (mask & 4) {
            float sx = fabsf(saved_s[i * 3 + 0] - node->scale[0]);
            float sy = fabsf(saved_s[i * 3 + 1] - node->scale[1]);
            float sz = fabsf(saved_s[i * 3 + 2] - node->scale[2]);
            scale_dist = fmaxf(sx, fmaxf(sy, sz));
        }

        float dist = fmaxf(trans_dist, fmaxf(rot_dist * rot_weight, scale_dist * scale_weight));
        if (dist > max_dist) max_dist = dist;
    }
    
    /* Restore original pose */
    restore_pose_arrays(model, saved_t, saved_r, saved_s);
    gltf_update_transforms(model);
    free(saved_t);
    free(saved_r);
    free(saved_s);
    
    /* Map distance to blend time */
    float blend_time = max_dist * 9.0f;
    if (blend_time < 0.45f) blend_time = 0.45f;
    if (blend_time > 6.0f) blend_time = 6.0f;
    
    printf("Auto blend time: max_dist=%.3f -> %.2fs\n", max_dist, blend_time);
    
    return blend_time;
}

void animator_blend_to(Animator *anim, int index, float duration, int loop_remaining) {
    if (!anim->model) return;
    if (index < 0 || index >= anim->model->animation_count) return;
    bool target_loopable = animation_is_loopable(anim, index);
    if (duration <= 0.0f) {
        animator_set_animation(anim, index);
        anim->loop_remaining = loop_remaining;
        anim->next_loop_remaining = loop_remaining;
        anim->current_loopable = target_loopable;
        anim->next_loopable = target_loopable;
        return;
    }
    
    snapshot_pose(anim);
    ensure_blend_factors(anim);
    anim->next_animation = index;
    anim->blend_time = 0.0f;
    anim->blend_duration = duration;
    anim->next_anim_time = 0.0f;
    anim->next_loop_remaining = loop_remaining;
    anim->next_loopable = target_loopable;
    anim->loop_blend_active = false;
    printf("Blend start: from=%d to=%d duration=%.2f\n",
           anim->current_animation, index, duration);
    fflush(stdout);
}

int animator_compute_loop_remaining(Animator *anim, int index) {
    return animation_is_loopable(anim, index) ? -1 : 0;
}

void animator_sample_frame(GltfModel *model, int anim_idx, float time) {
    if (!model || anim_idx < 0 || anim_idx >= model->animation_count) return;
    sample_animation(model, &model->animations[anim_idx], time);
}
