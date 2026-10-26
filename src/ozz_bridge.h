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

// Start from the last recorded animation-layer pose/velocity (before IK).
// Without recorded history, the current model pose is used. Duration is wall time;
// group factors multiply it. The legacy source index/time arguments are ignored.
void ozz_bridge_blend_start(int from_index, float from_time, int to_index,
                            float to_time, float duration, float speed);

// Advance by real dt and sample at the Animator-owned target playback time.
// Returns true if blend is still active, false if complete or not blending.
bool ozz_bridge_blend_tick(GltfModel *model, float dt, float target_time);
// Apply corrections to an already sampled target (for contact observation).
bool ozz_bridge_blend_apply(GltfModel *model, float dt);
void ozz_bridge_record_pose(GltfModel *model, float dt);
// Pause without overwriting animation-layer history with a post-IK model pose.
void ozz_bridge_reset_velocity(void);
void ozz_bridge_blend_cancel(void);

// Check if a blend is currently active.
bool ozz_bridge_blend_active(void);

// Get current target animation index (-1 if not blending).
int ozz_bridge_blend_target(void);

// Get current blend ratio (0.0 = from, 1.0 = to).
float ozz_bridge_blend_ratio(void);

// Snapshot the last recorded animation-layer pose for later blending.
// Call this BEFORE loading a new animation to enable blending from current
// pose.
void ozz_bridge_snapshot_pose(GltfModel *model, int anim_index, float time_sec);

// Returns true if a snapshot is currently available.
bool ozz_bridge_snapshot_available(void);

// Invalidates any stored snapshot (e.g., after changing correction mode).
void ozz_bridge_snapshot_invalidate(void);

// Start a blend from the snapshot (captured by ozz_bridge_snapshot_pose)
// to animation `to_index` over `duration` seconds.
// Returns true if blending was started.
bool ozz_bridge_blend_from_snapshot(int to_index, float to_time, float duration, float speed);

#ifdef __cplusplus
}
#endif

#endif
