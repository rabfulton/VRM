#ifndef VRM_FOOT_LOCK_H
#define VRM_FOOT_LOCK_H
#include "gltf_loader.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int hip, knee, ankle, toes;
    bool supported, planted, has_reference, wants_contact;
    float length, ground_height, stable_time, weight, error;
    float anchor[3], anchor_rotation[4];
    float reference[3], reference_rotation[4], speed;
    float pole[3], knee_axis[3];
} FootPlant;

typedef struct {
    GltfNode *nodes;
    int node_count, pelvis;
    bool enabled, calibrate_floor, clip_allowed;
    float pelvis_offset, pelvis_velocity, floor_offset;
    FootPlant feet[2];
} FootLock;

void foot_lock_init(FootLock *lock);
void foot_lock_set_model(FootLock *lock, GltfModel *model);
void foot_lock_reset(FootLock *lock);
void foot_lock_new_clip(FootLock *lock);
void foot_lock_reset_reference(FootLock *lock);
/* Observe the unblended target pose; apply after transition corrections. */
void foot_lock_observe(FootLock *lock, GltfModel *model, float dt);
void foot_lock_apply(FootLock *lock, GltfModel *model, float dt);
#ifdef __cplusplus
}
#endif
#endif
