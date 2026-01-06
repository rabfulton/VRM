/*
 * VRM Spring Bone Physics
 */

#ifndef SPRING_BONES_H
#define SPRING_BONES_H

#include "gltf_loader.h"

extern int g_spring_cloth_mode;
extern float g_spring_cloth_stiffness;
extern float g_spring_cloth_drag;
extern float g_spring_cloth_gravity;
extern float g_spring_cloth_taper;
extern int g_spring_debug_collisions;

/* Initialize spring bone runtime state (call after model load) */
void spring_bones_init(GltfModel *model);

/* Reset spring bones to rest pose */
void spring_bones_reset(GltfModel *model);

/* Update spring bone simulation (call each frame after animation) */
void spring_bones_update(GltfModel *model, float dt);

/* Cleanup spring bone data */
void spring_bones_cleanup(GltfModel *model);

#endif /* SPRING_BONES_H */
