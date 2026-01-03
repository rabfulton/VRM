/*
 * VRM Overlay - Animator
 */

#ifndef ANIMATOR_H
#define ANIMATOR_H

#include "gltf_loader.h"
#include <stdbool.h>

typedef struct {
    GltfModel *model;
    int current_animation;
    float time;
    float speed;
    bool playing;
    
    /* Blending state */
    int next_animation;       /* -1 if not blending */
    float blend_time;
    float blend_duration;
    float next_anim_time;
    
    /* Pose snapshot for blending */
    float *snap_translations; /* 3 * node_count */
    float *snap_rotations;    /* 4 * node_count */
    float *snap_scales;       /* 3 * node_count */
    int snap_node_count;
} Animator;

void animator_init(Animator *anim);
void animator_set_model(Animator *anim, GltfModel *model);
void animator_update(Animator *anim, float dt);
void animator_play(Animator *anim, float speed);
void animator_pause(Animator *anim);
void animator_set_time(Animator *anim, float t);
void animator_set_animation(Animator *anim, int index);
void animator_blend_to(Animator *anim, int index, float duration);
void animator_set_loop_blend(Animator *anim, float duration);
void animator_cleanup(Animator *anim);

#endif
