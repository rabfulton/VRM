/*
 * VRM Overlay - ozz-animation bridge (C ABI)
 *
 * This module converts loaded glTF animations to ozz runtime animations
 * (once per model load) and can sample them into GltfModel node TRS.
 *
 * It is intended as an incremental stepping stone toward a full ozz-driven
 * animator/state machine.
 */

#ifndef OZZ_BRIDGE_H
#define OZZ_BRIDGE_H

#include <stdbool.h>

#include "gltf_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ozz_bridge_set_model(GltfModel *model);
void ozz_bridge_clear_model(void);

// Samples animation `anim_index` at `time_sec` and writes into model->nodes[*].
// Returns false if the model/animation isn't available.
bool ozz_bridge_sample_into_model(GltfModel *model, int anim_index,
                                  float time_sec);

#ifdef __cplusplus
}
#endif

#endif
