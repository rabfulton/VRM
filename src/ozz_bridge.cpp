/*
 * VRM Overlay - ozz-animation bridge implementation
 */

#include "ozz_bridge.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"
#include "ozz/base/span.h"

namespace {

// Blend state for cross-fade transitions
struct BlendState {
  int from_anim = -1;
  int to_anim = -1;
  float from_time = 0.f;
  float to_time = 0.f;
  float blend_time = 0.f;
  float blend_duration = 0.f;
  bool active = false;
  bool use_snapshot = false; // If true, blend from locals_from snapshot instead
                             // of sampling from_anim
};

struct OzzModelContext {
  GltfModel *model = nullptr;
  ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
  std::vector<ozz::unique_ptr<ozz::animation::Animation>> animations;
  std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>>
      sampling_contexts;
  std::vector<int> joint_to_gltf_node; // runtime joint index -> gltf node index

  // Single-animation sampling buffer
  ozz::vector<ozz::math::SoaTransform> locals;

  // Blending buffers
  ozz::vector<ozz::math::SoaTransform> locals_from;
  ozz::vector<ozz::math::SoaTransform> locals_to;
  ozz::vector<ozz::math::SoaTransform> locals_blended;

  BlendState blend;
};

OzzModelContext g_ctx;

// Persistent snapshot buffer (survives ozz_bridge_set_model calls)
// Stores SoaTransform in ozz-space (before glTF correction is applied)
static ozz::vector<ozz::math::SoaTransform> g_snapshot_locals;
static bool g_snapshot_valid = false;

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static void quat_mul(const float *a, const float *b, float *out) {
  out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void quat_inv(const float *q, float *out) {
  out[0] = -q[0];
  out[1] = -q[1];
  out[2] = -q[2];
  out[3] = q[3];
}

static void quat_rotate_vec(const float *q, const float *v, float *out) {
  float vq[4] = {v[0], v[1], v[2], 0.0f};
  float qinv[4];
  float t1[4], t2[4];
  quat_inv(q, qinv);
  quat_mul(q, vq, t1);
  quat_mul(t1, qinv, t2);
  out[0] = t2[0];
  out[1] = t2[1];
  out[2] = t2[2];
}

static void apply_global_anim_correction(GltfModel *model,
                                         GltfAnimation *anim) {
  if (!model || !anim)
    return;
  float *corr = model->anim_global_correction;
  if (corr[0] == 0.0f && corr[1] == 0.0f && corr[2] == 0.0f && corr[3] == 1.0f)
    return;

  float corr_inv[4];
  quat_inv(corr, corr_inv);
  for (int i = 0; i < model->node_count; i++) {
    uint8_t mask =
        (anim->node_mask && i < anim->node_mask_count) ? anim->node_mask[i] : 0;
    if (mask & 2) {
      float t1[4], t2[4];
      quat_mul(corr, model->nodes[i].rotation, t1);
      quat_mul(t1, corr_inv, t2);
      model->nodes[i].rotation[0] = t2[0];
      model->nodes[i].rotation[1] = t2[1];
      model->nodes[i].rotation[2] = t2[2];
      model->nodes[i].rotation[3] = t2[3];
    }
    if (mask & 1) {
      float v[3];
      quat_rotate_vec(corr, model->nodes[i].translation, v);
      model->nodes[i].translation[0] = v[0];
      model->nodes[i].translation[1] = v[1];
      model->nodes[i].translation[2] = v[2];
    }
  }
}

static void
build_raw_skeleton_joint(GltfModel *model, int node_index,
                         ozz::animation::offline::RawSkeleton::Joint &out,
                         std::vector<int> &joint_to_gltf_node) {
  const GltfNode *node = &model->nodes[node_index];
  out.name = std::to_string(node_index);

  out.transform.translation.x = node->base_translation[0];
  out.transform.translation.y = node->base_translation[1];
  out.transform.translation.z = node->base_translation[2];
  out.transform.rotation.x = node->base_rotation[0];
  out.transform.rotation.y = node->base_rotation[1];
  out.transform.rotation.z = node->base_rotation[2];
  out.transform.rotation.w = node->base_rotation[3];
  out.transform.scale.x = node->base_scale[0];
  out.transform.scale.y = node->base_scale[1];
  out.transform.scale.z = node->base_scale[2];

  joint_to_gltf_node.push_back(node_index);

  out.children.clear();
  out.children.reserve((size_t)node->child_count);
  for (int i = 0; i < node->child_count; i++) {
    int child_index = node->children[i];
    out.children.emplace_back();
    build_raw_skeleton_joint(model, child_index, out.children.back(),
                             joint_to_gltf_node);
  }
}

struct NodeChannelInfo {
  const GltfAnimSampler *translation = nullptr;
  const GltfAnimSampler *rotation = nullptr;
  const GltfAnimSampler *scale = nullptr;
};

static void sanitize_times_and_push_translation(
    const GltfAnimSampler *s, float duration, const float *rest,
    ozz::animation::offline::RawAnimation::JointTrack::Translations &out) {
  out.clear();
  if (!s || s->keyframe_count < 1) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2]}});
    return;
  }
  float prev = -1.0f;
  for (int i = 0; i < s->keyframe_count; i++) {
    float t = clampf(s->input[i], 0.0f, duration);
    if (!out.empty() && t <= prev)
      continue;
    const float *v = &s->output[i * s->output_stride];
    out.push_back({t, {v[0], v[1], v[2]}});
    prev = t;
  }
  if (out.empty()) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2]}});
    return;
  }
  if (out.front().time > 0.0f) {
    out.insert(out.begin(), {0.0f, {rest[0], rest[1], rest[2]}});
  }
  if (out.back().time < duration) {
    out.push_back({duration, out.back().value});
  }
}

static void sanitize_times_and_push_scale(
    const GltfAnimSampler *s, float duration, const float *rest,
    ozz::animation::offline::RawAnimation::JointTrack::Scales &out) {
  out.clear();
  if (!s || s->keyframe_count < 1) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2]}});
    return;
  }
  float prev = -1.0f;
  for (int i = 0; i < s->keyframe_count; i++) {
    float t = clampf(s->input[i], 0.0f, duration);
    if (!out.empty() && t <= prev)
      continue;
    const float *v = &s->output[i * s->output_stride];
    out.push_back({t, {v[0], v[1], v[2]}});
    prev = t;
  }
  if (out.empty()) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2]}});
    return;
  }
  if (out.front().time > 0.0f) {
    out.insert(out.begin(), {0.0f, {rest[0], rest[1], rest[2]}});
  }
  if (out.back().time < duration) {
    out.push_back({duration, out.back().value});
  }
}

static void sanitize_times_and_push_rotation(
    const GltfAnimSampler *s, float duration, const float *rest,
    ozz::animation::offline::RawAnimation::JointTrack::Rotations &out) {
  out.clear();
  if (!s || s->keyframe_count < 1) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2], rest[3]}});
    return;
  }
  float prev = -1.0f;
  for (int i = 0; i < s->keyframe_count; i++) {
    float t = clampf(s->input[i], 0.0f, duration);
    if (!out.empty() && t <= prev)
      continue;
    const float *v = &s->output[i * s->output_stride];
    float q[4] = {v[0], v[1], v[2], v[3]};
    float len =
        std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-6f) {
      q[0] /= len;
      q[1] /= len;
      q[2] /= len;
      q[3] /= len;
    } else {
      q[0] = rest[0];
      q[1] = rest[1];
      q[2] = rest[2];
      q[3] = rest[3];
    }
    out.push_back({t, {q[0], q[1], q[2], q[3]}});
    prev = t;
  }
  if (out.empty()) {
    out.push_back({0.0f, {rest[0], rest[1], rest[2], rest[3]}});
    return;
  }
  if (out.front().time > 0.0f) {
    out.insert(out.begin(), {0.0f, {rest[0], rest[1], rest[2], rest[3]}});
  }
  if (out.back().time < duration) {
    out.push_back({duration, out.back().value});
  }
}

} // namespace

bool ozz_bridge_set_model(GltfModel *model) {
  ozz_bridge_clear_model();
  if (!model || !model->loaded || model->node_count <= 0 ||
      model->animation_count <= 0) {
    return false;
  }

  g_ctx.model = model;

  ozz::animation::offline::RawSkeleton raw_skeleton;
  raw_skeleton.roots.reserve((size_t)model->scene_root_count);

  std::vector<int> joint_to_gltf_node;
  joint_to_gltf_node.reserve((size_t)model->node_count);

  for (int i = 0; i < model->scene_root_count; i++) {
    int root_index = model->scene_roots[i];
    raw_skeleton.roots.emplace_back();
    build_raw_skeleton_joint(model, root_index, raw_skeleton.roots.back(),
                             joint_to_gltf_node);
  }

  if (!raw_skeleton.Validate()) {
    fprintf(stderr, "ozz_bridge: raw skeleton invalid\n");
    ozz_bridge_clear_model();
    return false;
  }

  ozz::animation::offline::SkeletonBuilder skel_builder;
  g_ctx.skeleton = skel_builder(raw_skeleton);
  if (!g_ctx.skeleton) {
    fprintf(stderr, "ozz_bridge: failed to build runtime skeleton\n");
    ozz_bridge_clear_model();
    return false;
  }

  const int joint_count = g_ctx.skeleton->num_joints();
  if (joint_count != (int)joint_to_gltf_node.size()) {
    fprintf(stderr,
            "ozz_bridge: joint count mismatch skeleton=%d mapping=%zu\n",
            joint_count, joint_to_gltf_node.size());
    ozz_bridge_clear_model();
    return false;
  }
  g_ctx.joint_to_gltf_node = std::move(joint_to_gltf_node);

  const int num_soa_joints = g_ctx.skeleton->num_soa_joints();
  g_ctx.locals.resize(num_soa_joints);
  g_ctx.locals_from.resize(num_soa_joints);
  g_ctx.locals_to.resize(num_soa_joints);
  g_ctx.locals_blended.resize(num_soa_joints);
  g_ctx.blend = BlendState{};

  g_ctx.animations.clear();
  g_ctx.sampling_contexts.clear();
  g_ctx.animations.reserve((size_t)model->animation_count);
  g_ctx.sampling_contexts.reserve((size_t)model->animation_count);

  for (int anim_idx = 0; anim_idx < model->animation_count; anim_idx++) {
    const GltfAnimation *gltf_anim = &model->animations[anim_idx];
    float duration = gltf_anim->duration > 1e-6f ? gltf_anim->duration : 1.0f;

    std::vector<NodeChannelInfo> node_channels((size_t)model->node_count);
    for (int c = 0; c < gltf_anim->channel_count; c++) {
      const GltfAnimChannel *ch = &gltf_anim->channels[c];
      if (ch->target_node < 0 || ch->target_node >= model->node_count)
        continue;
      if (ch->sampler_index < 0 ||
          ch->sampler_index >= gltf_anim->sampler_count)
        continue;
      const GltfAnimSampler *s = &gltf_anim->samplers[ch->sampler_index];
      NodeChannelInfo &ci = node_channels[(size_t)ch->target_node];
      if (ch->path == GLTF_PATH_TRANSLATION)
        ci.translation = s;
      else if (ch->path == GLTF_PATH_ROTATION)
        ci.rotation = s;
      else if (ch->path == GLTF_PATH_SCALE)
        ci.scale = s;
    }

    ozz::animation::offline::RawAnimation raw_anim;
    raw_anim.duration = duration;
    raw_anim.tracks.resize((size_t)joint_count);

    for (int j = 0; j < joint_count; j++) {
      const int node_idx = g_ctx.joint_to_gltf_node[(size_t)j];
      const GltfNode *node = &model->nodes[node_idx];
      const NodeChannelInfo &ci = node_channels[(size_t)node_idx];

      auto &track = raw_anim.tracks[(size_t)j];

      sanitize_times_and_push_translation(
          ci.translation, duration, node->base_translation, track.translations);
      sanitize_times_and_push_rotation(ci.rotation, duration,
                                       node->base_rotation, track.rotations);
      sanitize_times_and_push_scale(ci.scale, duration, node->base_scale,
                                    track.scales);
    }

    if (!raw_anim.Validate()) {
      fprintf(stderr, "ozz_bridge: raw animation %d invalid\n", anim_idx);
      ozz_bridge_clear_model();
      return false;
    }

    ozz::animation::offline::AnimationBuilder anim_builder;
    auto runtime_anim = anim_builder(raw_anim);
    if (!runtime_anim) {
      fprintf(stderr, "ozz_bridge: failed to build runtime animation %d\n",
              anim_idx);
      ozz_bridge_clear_model();
      return false;
    }

    auto sampling_ctx =
        std::make_unique<ozz::animation::SamplingJob::Context>();
    sampling_ctx->Resize(joint_count);
    g_ctx.animations.push_back(std::move(runtime_anim));
    g_ctx.sampling_contexts.push_back(std::move(sampling_ctx));
  }

  return true;
}

void ozz_bridge_clear_model(void) { g_ctx = OzzModelContext{}; }

bool ozz_bridge_sample_into_model(GltfModel *model, int anim_index,
                                  float time_sec) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton)
    return false;
  if (anim_index < 0 || anim_index >= (int)g_ctx.animations.size())
    return false;

  const auto &anim = g_ctx.animations[(size_t)anim_index];
  if (!anim)
    return false;

  float ratio = 0.0f;
  if (anim->duration() > 1e-6f) {
    float t = time_sec;
    if (t < 0.0f)
      t = 0.0f;
    if (t > anim->duration())
      t = anim->duration();
    ratio = t / anim->duration();
  }

  ozz::animation::SamplingJob job;
  job.animation = anim.get();
  job.context = g_ctx.sampling_contexts[(size_t)anim_index].get();
  job.ratio = ratio;
  job.output = ozz::make_span(g_ctx.locals);
  if (!job.Run()) {
    return false;
  }

  const int joint_count = g_ctx.skeleton->num_joints();
  for (int j = 0; j < joint_count; j++) {
    int node_idx = g_ctx.joint_to_gltf_node[(size_t)j];
    if (node_idx < 0 || node_idx >= model->node_count)
      continue;
    GltfNode *node = &model->nodes[node_idx];
    if (node->is_spring_bone)
      continue;

    int soa = j / 4;
    int lane = j & 3;
    const ozz::math::SoaTransform &st = g_ctx.locals[(size_t)soa];

    float tx[4], ty[4], tz[4];
    float sx[4], sy[4], sz[4];
    float qx[4], qy[4], qz[4], qw[4];

    ozz::math::StorePtrU(st.translation.x, tx);
    ozz::math::StorePtrU(st.translation.y, ty);
    ozz::math::StorePtrU(st.translation.z, tz);
    ozz::math::StorePtrU(st.scale.x, sx);
    ozz::math::StorePtrU(st.scale.y, sy);
    ozz::math::StorePtrU(st.scale.z, sz);
    ozz::math::StorePtrU(st.rotation.x, qx);
    ozz::math::StorePtrU(st.rotation.y, qy);
    ozz::math::StorePtrU(st.rotation.z, qz);
    ozz::math::StorePtrU(st.rotation.w, qw);

    node->translation[0] = tx[lane];
    node->translation[1] = ty[lane];
    node->translation[2] = tz[lane];
    node->scale[0] = sx[lane];
    node->scale[1] = sy[lane];
    node->scale[2] = sz[lane];
    node->rotation[0] = qx[lane];
    node->rotation[1] = qy[lane];
    node->rotation[2] = qz[lane];
    node->rotation[3] = qw[lane];
  }

  apply_global_anim_correction(model, &model->animations[anim_index]);
  return true;
}

// --- Internal helpers for blending ---

static bool
sample_animation_to_buffer(int anim_index, float time_sec,
                           ozz::vector<ozz::math::SoaTransform> &out) {
  if (anim_index < 0 || anim_index >= (int)g_ctx.animations.size())
    return false;
  const auto &anim = g_ctx.animations[(size_t)anim_index];
  if (!anim)
    return false;

  float ratio = 0.0f;
  if (anim->duration() > 1e-6f) {
    float t = time_sec;
    // Wrap time for looping (use fmodf instead of clamping)
    if (t < 0.0f) {
      t = anim->duration() - fmodf(-t, anim->duration());
    } else if (t > anim->duration()) {
      t = fmodf(t, anim->duration());
    }
    ratio = t / anim->duration();
  }

  ozz::animation::SamplingJob job;
  job.animation = anim.get();
  job.context = g_ctx.sampling_contexts[(size_t)anim_index].get();
  job.ratio = ratio;
  job.output = ozz::make_span(out);
  return job.Run();
}

static void
write_locals_to_model(GltfModel *model,
                      const ozz::vector<ozz::math::SoaTransform> &locals,
                      int anim_index) {
  const int joint_count = g_ctx.skeleton->num_joints();
  for (int j = 0; j < joint_count; j++) {
    int node_idx = g_ctx.joint_to_gltf_node[(size_t)j];
    if (node_idx < 0 || node_idx >= model->node_count)
      continue;
    GltfNode *node = &model->nodes[node_idx];
    if (node->is_spring_bone)
      continue;

    int soa = j / 4;
    int lane = j & 3;
    const ozz::math::SoaTransform &st = locals[(size_t)soa];

    float tx[4], ty[4], tz[4];
    float sx[4], sy[4], sz[4];
    float qx[4], qy[4], qz[4], qw[4];

    ozz::math::StorePtrU(st.translation.x, tx);
    ozz::math::StorePtrU(st.translation.y, ty);
    ozz::math::StorePtrU(st.translation.z, tz);
    ozz::math::StorePtrU(st.scale.x, sx);
    ozz::math::StorePtrU(st.scale.y, sy);
    ozz::math::StorePtrU(st.scale.z, sz);
    ozz::math::StorePtrU(st.rotation.x, qx);
    ozz::math::StorePtrU(st.rotation.y, qy);
    ozz::math::StorePtrU(st.rotation.z, qz);
    ozz::math::StorePtrU(st.rotation.w, qw);

    node->translation[0] = tx[lane];
    node->translation[1] = ty[lane];
    node->translation[2] = tz[lane];
    node->scale[0] = sx[lane];
    node->scale[1] = sy[lane];
    node->scale[2] = sz[lane];
    node->rotation[0] = qx[lane];
    node->rotation[1] = qy[lane];
    node->rotation[2] = qz[lane];
    node->rotation[3] = qw[lane];
  }

  if (anim_index >= 0 && anim_index < model->animation_count) {
    apply_global_anim_correction(model, &model->animations[anim_index]);
  }
}

// --- Blending API ---

void ozz_bridge_blend_start(int from_index, float from_time, int to_index,
                            float duration) {
  if (!g_ctx.skeleton || from_index < 0 || to_index < 0)
    return;
  if ((size_t)from_index >= g_ctx.animations.size() ||
      (size_t)to_index >= g_ctx.animations.size())
    return;
  if (duration <= 0.001f) {
    // Instant switch, no blend
    g_ctx.blend.active = false;
    return;
  }

  g_ctx.blend.from_anim = from_index;
  g_ctx.blend.to_anim = to_index;
  g_ctx.blend.from_time = from_time;
  g_ctx.blend.to_time = 0.f;
  g_ctx.blend.blend_time = 0.f;
  g_ctx.blend.blend_duration = duration;
  g_ctx.blend.active = true;
  g_ctx.blend.use_snapshot = false;
}

bool ozz_bridge_blend_tick(GltfModel *model, float dt) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton || !g_ctx.blend.active)
    return false;

  // Advance times
  g_ctx.blend.blend_time += dt;
  g_ctx.blend.from_time += dt;
  g_ctx.blend.to_time += dt;

  // Check if blend is complete
  if (g_ctx.blend.blend_time >= g_ctx.blend.blend_duration) {
    g_ctx.blend.active = false;
    // Sample target animation at final time
    if (!sample_animation_to_buffer(g_ctx.blend.to_anim, g_ctx.blend.to_time,
                                    g_ctx.locals)) {
      return false;
    }
    write_locals_to_model(model, g_ctx.locals, g_ctx.blend.to_anim);
    return false; // Blend complete
  }

  // Sample from animation (or use snapshot)
  if (!g_ctx.blend.use_snapshot) {
    if (!sample_animation_to_buffer(g_ctx.blend.from_anim,
                                    g_ctx.blend.from_time, g_ctx.locals_from)) {
      g_ctx.blend.active = false;
      return false;
    }
  }
  // else: use_snapshot is true, locals_from already has the snapshot

  // Sample to animation
  if (!sample_animation_to_buffer(g_ctx.blend.to_anim, g_ctx.blend.to_time,
                                  g_ctx.locals_to)) {
    g_ctx.blend.active = false;
    return false;
  }

  // Compute blend alpha
  float alpha = g_ctx.blend.blend_time / g_ctx.blend.blend_duration;
  if (alpha < 0.f)
    alpha = 0.f;
  if (alpha > 1.f)
    alpha = 1.f;

  // Setup BlendingJob
  ozz::animation::BlendingJob::Layer layers[2];
  layers[0].transform = ozz::make_span(g_ctx.locals_from);
  layers[0].weight = 1.f - alpha;
  layers[1].transform = ozz::make_span(g_ctx.locals_to);
  layers[1].weight = alpha;

  ozz::animation::BlendingJob blend_job;
  blend_job.layers = layers;
  blend_job.rest_pose = g_ctx.skeleton->joint_rest_poses();
  blend_job.output = ozz::make_span(g_ctx.locals_blended);

  if (!blend_job.Run()) {
    g_ctx.blend.active = false;
    return false;
  }

  // Write blended pose to model
  // Use the target animation for correction (more consistent with end state)
  write_locals_to_model(model, g_ctx.locals_blended, g_ctx.blend.to_anim);
  return true;
}

bool ozz_bridge_blend_active(void) { return g_ctx.blend.active; }

int ozz_bridge_blend_target(void) {
  return g_ctx.blend.active ? g_ctx.blend.to_anim : -1;
}

float ozz_bridge_blend_ratio(void) {
  if (!g_ctx.blend.active || g_ctx.blend.blend_duration <= 0.f)
    return 0.f;
  float ratio = g_ctx.blend.blend_time / g_ctx.blend.blend_duration;
  return ratio < 0.f ? 0.f : (ratio > 1.f ? 1.f : ratio);
}

void ozz_bridge_snapshot_pose(GltfModel *model, int anim_index,
                              float time_sec) {
  (void)time_sec;
  if (!model || model != g_ctx.model || !g_ctx.skeleton)
    return;
  if (anim_index < 0 || anim_index >= model->animation_count)
    return;

  const int num_soa = g_ctx.skeleton->num_soa_joints();
  g_snapshot_locals.resize(num_soa);

  // Capture the currently displayed pose from model->nodes (which already
  // includes any sampling/blending done this frame), then undo the global
  // animation correction to store snapshot locals in ozz-space (pre-correction).
  //
  // This is more robust than re-sampling the clip at (anim_index,time_sec),
  // and avoids one-frame mismatches that look like "snapping" at blend start.
  const float *corr = model->anim_global_correction;
  const bool corr_is_identity =
      (corr[0] == 0.0f && corr[1] == 0.0f && corr[2] == 0.0f && corr[3] == 1.0f);
  float corr_inv[4] = {0, 0, 0, 1};
  if (!corr_is_identity) {
    quat_inv(corr, corr_inv);
  }

  const GltfAnimation *anim = &model->animations[anim_index];
  const int joint_count = g_ctx.skeleton->num_joints();
  for (int soa = 0; soa < num_soa; soa++) {
    float tx[4] = {0, 0, 0, 0};
    float ty[4] = {0, 0, 0, 0};
    float tz[4] = {0, 0, 0, 0};
    float sx[4] = {1, 1, 1, 1};
    float sy[4] = {1, 1, 1, 1};
    float sz[4] = {1, 1, 1, 1};
    float qx[4] = {0, 0, 0, 0};
    float qy[4] = {0, 0, 0, 0};
    float qz[4] = {0, 0, 0, 0};
    float qw[4] = {1, 1, 1, 1};

    for (int lane = 0; lane < 4; lane++) {
      const int j = soa * 4 + lane;
      if (j >= joint_count)
        break;
      const int node_idx = g_ctx.joint_to_gltf_node[(size_t)j];
      if (node_idx < 0 || node_idx >= model->node_count)
        continue;
      const GltfNode *node = &model->nodes[node_idx];

      float t[3] = {node->translation[0], node->translation[1],
                    node->translation[2]};
      float r[4] = {node->rotation[0], node->rotation[1], node->rotation[2],
                    node->rotation[3]};

      if (!corr_is_identity) {
        const uint8_t mask =
            (anim->node_mask && node_idx < anim->node_mask_count)
                ? anim->node_mask[node_idx]
                : 0;
        if (mask & 2) {
          // Invert: r_pre = corr_inv * r_post * corr
          float t1[4], t2[4];
          quat_mul(corr_inv, r, t1);
          quat_mul(t1, corr, t2);
          r[0] = t2[0];
          r[1] = t2[1];
          r[2] = t2[2];
          r[3] = t2[3];
        }
        if (mask & 1) {
          // Invert: t_pre = rotate(corr_inv, t_post)
          float v[3];
          quat_rotate_vec(corr_inv, t, v);
          t[0] = v[0];
          t[1] = v[1];
          t[2] = v[2];
        }
      }

      tx[lane] = t[0];
      ty[lane] = t[1];
      tz[lane] = t[2];
      sx[lane] = node->scale[0];
      sy[lane] = node->scale[1];
      sz[lane] = node->scale[2];
      qx[lane] = r[0];
      qy[lane] = r[1];
      qz[lane] = r[2];
      qw[lane] = r[3];
    }

    ozz::math::SoaTransform &st = g_snapshot_locals[(size_t)soa];
    st.translation.x = ozz::math::simd_float4::LoadPtrU(tx);
    st.translation.y = ozz::math::simd_float4::LoadPtrU(ty);
    st.translation.z = ozz::math::simd_float4::LoadPtrU(tz);
    st.scale.x = ozz::math::simd_float4::LoadPtrU(sx);
    st.scale.y = ozz::math::simd_float4::LoadPtrU(sy);
    st.scale.z = ozz::math::simd_float4::LoadPtrU(sz);
    st.rotation.x = ozz::math::simd_float4::LoadPtrU(qx);
    st.rotation.y = ozz::math::simd_float4::LoadPtrU(qy);
    st.rotation.z = ozz::math::simd_float4::LoadPtrU(qz);
    st.rotation.w = ozz::math::simd_float4::LoadPtrU(qw);
  }

  g_snapshot_valid = true;
}

bool ozz_bridge_snapshot_available(void) { return g_snapshot_valid; }

bool ozz_bridge_blend_from_snapshot(int to_index, float duration) {
  if (!g_ctx.skeleton || to_index < 0)
    return false;
  if ((size_t)to_index >= g_ctx.animations.size())
    return false;
  if (duration <= 0.001f) {
    g_ctx.blend.active = false;
    g_snapshot_valid = false;
    return false;
  }
  if (!g_snapshot_valid) {
    // No snapshot available, can't blend
    return false;
  }

  // Copy snapshot to locals_from (they should be the same size if model
  // unchanged)
  const int num_soa = g_ctx.skeleton->num_soa_joints();
  if ((int)g_snapshot_locals.size() == num_soa) {
    for (int i = 0; i < num_soa; i++) {
      g_ctx.locals_from[i] = g_snapshot_locals[i];
    }
  } else {
    // Model changed, snapshot is stale
    g_snapshot_valid = false;
    return false;
  }

  g_ctx.blend.from_anim = -1; // Not used when use_snapshot is true
  g_ctx.blend.to_anim = to_index;
  g_ctx.blend.from_time = 0.f;
  g_ctx.blend.to_time = 0.f;
  g_ctx.blend.blend_time = 0.f;
  g_ctx.blend.blend_duration = duration;
  g_ctx.blend.active = true;
  g_ctx.blend.use_snapshot = true;
  g_snapshot_valid = false; // Consumed
  return true;
}
