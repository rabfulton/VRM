/*
 * VRM Overlay - Animator Implementation
 */

#include "animator.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

void animator_init(Animator *anim) {
    memset(anim, 0, sizeof(Animator));
    anim->speed = 1.0f;
    anim->current_animation = -1;
}

void animator_set_model(Animator *anim, GltfModel *model) {
    anim->model = model;
    anim->time = 0;
    anim->current_animation = (model && model->animation_count > 0) ? 0 : -1;
}

/* Linear interpolation */
static float lerp(float a, float b, float t) {
    return a + t * (b - a);
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
    if (!model || !anim || model->anim_correction_mode == 0) return;
    float corr[4] = {model->anim_global_correction[0],
                     model->anim_global_correction[1],
                     model->anim_global_correction[2],
                     model->anim_global_correction[3]};
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

void animator_update(Animator *anim, float dt) {
    if (!anim->model || !anim->playing) return;
    if (anim->current_animation < 0 || anim->current_animation >= anim->model->animation_count) return;

    GltfAnimation *animation = &anim->model->animations[anim->current_animation];

    anim->time += dt * anim->speed;

    /* Loop */
    if (animation->duration > 0) {
        while (anim->time > animation->duration) {
            anim->time -= animation->duration;
        }
        while (anim->time < 0) {
            anim->time += animation->duration;
        }
    }

    sample_animation(anim->model, animation, anim->time);
    gltf_update_transforms(anim->model);
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
    }
}
