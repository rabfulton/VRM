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

// --- Blending API ---

// Start a blend transition from current animation to `to_index` over `duration`
// seconds. `from_index` is the current animation, `from_time` is current
// playback time.
void ozz_bridge_blend_start(int from_index, float from_time, int to_index,
                            float duration);

// Advance blend state by `dt` seconds and sample/blend into model.
// Returns true if blend is still active, false if complete or not blending.
bool ozz_bridge_blend_tick(GltfModel *model, float dt);

// Check if a blend is currently active.
bool ozz_bridge_blend_active(void);

// Get current target animation index (-1 if not blending).
int ozz_bridge_blend_target(void);

// Get current blend ratio (0.0 = from, 1.0 = to).
float ozz_bridge_blend_ratio(void);

// Snapshot the current pose for later blending.
// Call this BEFORE loading a new animation to enable blending from current
// pose.
void ozz_bridge_snapshot_pose(GltfModel *model, int anim_index, float time_sec);

// Returns true if a snapshot is currently available.
bool ozz_bridge_snapshot_available(void);

// Start a blend from the snapshot (captured by ozz_bridge_snapshot_pose)
// to animation `to_index` over `duration` seconds.
// Returns true if blending was started.
bool ozz_bridge_blend_from_snapshot(int to_index, float duration);

#ifdef __cplusplus
}
#endif

#endif
