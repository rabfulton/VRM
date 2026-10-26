/*
 * VRM Spring Bone Physics
 */

#ifndef SPRING_BONES_H
#define SPRING_BONES_H

#include "gltf_loader.h"

extern float g_spring_stiffness, g_spring_drag, g_spring_gravity;
extern int g_spring_cloth_mode;
extern float g_spring_cloth_stiffness;
extern float g_spring_cloth_drag;
extern float g_spring_cloth_gravity;
extern float g_spring_cloth_taper;
extern int g_spring_debug_collisions;
extern int g_spring_garment_collisions;

typedef struct {
    int garment_chains, arm_colliders, generated_arm_colliders;
    int contacts, unresolved_contacts, steps;
    float max_penetration, garment_arm_penetration;
    double dropped_time;
} SpringStats;
SpringStats spring_bones_stats(const GltfModel *model);

/* Initialize spring bone runtime state (call after model load) */
void spring_bones_init(GltfModel *model);

/* Reset spring bones to rest pose */
void spring_bones_reset(GltfModel *model);

/* Update spring bone simulation (call each frame after animation) */
void spring_bones_update(GltfModel *model, float dt);

/* Cleanup spring bone data */
void spring_bones_cleanup(GltfModel *model);

#endif /* SPRING_BONES_H */
