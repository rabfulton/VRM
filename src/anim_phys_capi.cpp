/*
 * VRM Overlay - Animation/Physics Runtime (stub implementation)
 */

#include "anim_phys_capi.h"

struct AnimPhysRuntime {
  GltfModel *model = nullptr;
  AnimPhysMode mode = ANIMPHYS_MODE_ANIMATION;
};

AnimPhysRuntime *animphys_create(void) { return new AnimPhysRuntime(); }

void animphys_destroy(AnimPhysRuntime *rt) { delete rt; }

void animphys_set_model(AnimPhysRuntime *rt, GltfModel *model) {
  if (!rt)
    return;
  rt->model = model;
}

void animphys_set_mode(AnimPhysRuntime *rt, AnimPhysMode mode) {
  if (!rt)
    return;
  rt->mode = mode;
}

void animphys_tick(AnimPhysRuntime *rt, float render_dt) {
  (void)rt;
  (void)render_dt;
}

bool animphys_is_enabled(void) {
#ifdef VRM_USE_OZZ_JOLT
  return true;
#else
  return false;
#endif
}
