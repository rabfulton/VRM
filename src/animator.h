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

    /* Motion cache (per node) */
    float *prev_translations;     /* 3 * node_count */
    float *prev_rotations;        /* 4 * node_count */
    float *vel_translations;      /* 3 * node_count */
    float *vel_rotations;         /* 3 * node_count (angular velocity) */
    float *prev_vel_translations; /* 3 * node_count */
    float *prev_vel_rotations;    /* 3 * node_count */
    float *acc_translations;      /* 3 * node_count */
    float *acc_rotations;         /* 3 * node_count (angular accel) */
    float *off_translations;      /* 3 * node_count (inertial offset) */
    float *off_rotations;         /* 3 * node_count (inertial offset, rot vec) */
    float *inertial_vel_trans;    /* 3 * node_count */
    float *inertial_vel_rot;      /* 3 * node_count */
    int motion_node_count;
    bool motion_initialized;

    /* Blend time factor per node (fraction of master blend time) */
    float *blend_time_factors; /* 1 * node_count */
    int blend_node_count;

    /* Looping control */
    int loop_remaining;       /* -1 = infinite, 0 = no loop, >0 = repeats left */
    int next_loop_remaining;  /* Pending loop policy for next animation */
    bool current_loopable;
    bool next_loopable;
    bool loop_blend_active;
} Animator;

typedef enum {
    BLEND_GROUP_ROOT = 0,
    BLEND_GROUP_SPINE,
    BLEND_GROUP_HEAD,
    BLEND_GROUP_ARMS,
    BLEND_GROUP_HANDS,
    BLEND_GROUP_LEGS,
    BLEND_GROUP_FEET,
    BLEND_GROUP_FACE,
    BLEND_GROUP_OTHER,
    BLEND_GROUP_COUNT
} BlendGroup;

extern float g_anim_blend_time_default;
extern float g_anim_inertial_half_life_ratio;
extern float g_anim_blend_group_factors[BLEND_GROUP_COUNT];

void animator_init(Animator *anim);
void animator_set_model(Animator *anim, GltfModel *model);
void animator_update(Animator *anim, float dt);
void animator_play(Animator *anim, float speed);
void animator_pause(Animator *anim);
void animator_set_time(Animator *anim, float t);
void animator_set_animation(Animator *anim, int index);
void animator_blend_to(Animator *anim, int index, float duration, int loop_remaining);
int animator_compute_loop_remaining(Animator *anim, int index);
void animator_cleanup(Animator *anim);
void animator_sample_frame(GltfModel *model, int anim_idx, float time);

#endif
