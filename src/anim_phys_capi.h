/*
 * VRM Overlay - Animation/Physics Runtime (C API)
 *
 * This is a thin C ABI boundary around the (future) C++ animation system
 * (ozz-animation) and physics backend (Jolt).
 *
 * For now it is a stub so we can land build plumbing and integrate incrementally.
 */

#ifndef ANIM_PHYS_CAPI_H
#define ANIM_PHYS_CAPI_H

#include <stdbool.h>

typedef struct GltfModel GltfModel;

typedef struct AnimPhysRuntime AnimPhysRuntime;

typedef enum {
  ANIMPHYS_MODE_ANIMATION = 0,
  ANIMPHYS_MODE_RAGDOLL = 1,
  ANIMPHYS_MODE_ACTIVE_RAGDOLL = 2,
} AnimPhysMode;

AnimPhysRuntime *animphys_create(void);
void animphys_destroy(AnimPhysRuntime *rt);

void animphys_set_model(AnimPhysRuntime *rt, GltfModel *model);
void animphys_set_mode(AnimPhysRuntime *rt, AnimPhysMode mode);

// Called once per rendered frame; implementation may internally fixed-step.
void animphys_tick(AnimPhysRuntime *rt, float render_dt);

// Returns true when an ozz/Jolt-backed implementation is compiled in.
bool animphys_is_enabled(void);

#endif

