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
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "animator.h"

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
  std::vector<int> gltf_node_to_joint; // gltf node index -> runtime joint index

  // Single-animation sampling buffer
  ozz::vector<ozz::math::SoaTransform> locals;

  // Blending buffers
  ozz::vector<ozz::math::SoaTransform> locals_from;
  ozz::vector<ozz::math::SoaTransform> locals_to;
  ozz::vector<ozz::math::SoaTransform> locals_blended;

  // Pose matching scratch buffer
  ozz::vector<ozz::math::SoaTransform> locals_match_from;

  // Per-joint blending mask based on humanoid groups.
  std::vector<uint8_t> joint_group; // BlendGroup per joint
  ozz::vector<ozz::math::SimdFloat4> joint_weights_to; // SoA weights

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

static void apply_correction_to_translation_track(
    const float *corr, const GltfAnimation *anim, int node_idx,
    ozz::animation::offline::RawAnimation::JointTrack::Translations &track) {
  if (!anim || !anim->node_mask || node_idx < 0 || node_idx >= anim->node_mask_count)
    return;
  const uint8_t mask = anim->node_mask[node_idx];
  if (!(mask & 1))
    return;
  for (auto &k : track) {
    float v[3] = {k.value.x, k.value.y, k.value.z};
    float out[3];
    quat_rotate_vec(corr, v, out);
    k.value.x = out[0];
    k.value.y = out[1];
    k.value.z = out[2];
  }
}

static void apply_correction_to_rotation_track(
    const float *corr, const float *corr_inv, const GltfAnimation *anim,
    int node_idx, ozz::animation::offline::RawAnimation::JointTrack::Rotations &track) {
  if (!anim || !anim->node_mask || node_idx < 0 || node_idx >= anim->node_mask_count)
    return;
  const uint8_t mask = anim->node_mask[node_idx];
  if (!(mask & 2))
    return;
  for (auto &k : track) {
    float q[4] = {k.value.x, k.value.y, k.value.z, k.value.w};
    float t1[4], t2[4];
    quat_mul(corr, q, t1);
    quat_mul(t1, corr_inv, t2);
    float len = std::sqrt(t2[0] * t2[0] + t2[1] * t2[1] + t2[2] * t2[2] +
                          t2[3] * t2[3]);
    if (len > 1e-6f) {
      t2[0] /= len;
      t2[1] /= len;
      t2[2] /= len;
      t2[3] /= len;
    } else {
      t2[0] = 0.f;
      t2[1] = 0.f;
      t2[2] = 0.f;
      t2[3] = 1.f;
    }
    k.value.x = t2[0];
    k.value.y = t2[1];
    k.value.z = t2[2];
    k.value.w = t2[3];
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

static void build_raw_skeleton_joint_filtered(
    GltfModel *model, int node_index, const std::vector<uint8_t> &included,
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
  for (int i = 0; i < node->child_count; i++) {
    const int child_index = node->children[i];
    if (child_index < 0 || child_index >= model->node_count)
      continue;
    if (!included[(size_t)child_index])
      continue;
    out.children.emplace_back();
    build_raw_skeleton_joint_filtered(model, child_index, included,
                                     out.children.back(), joint_to_gltf_node);
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

static BlendGroup humanoid_name_to_blend_group(std::string_view n);
static void update_joint_weights_to(void);

bool ozz_bridge_set_model(GltfModel *model) {
  ozz_bridge_clear_model();
  if (!model || !model->loaded || model->node_count <= 0 ||
      model->animation_count <= 0) {
    return false;
  }

  g_ctx.model = model;

  ozz::animation::offline::RawSkeleton raw_skeleton;

  std::vector<int> joint_to_gltf_node;
  joint_to_gltf_node.reserve((size_t)model->node_count);

  // Prefer building an ozz skeleton from the model's skin joint set (plus any
  // required ancestors), instead of animating all scene nodes.
  std::vector<uint8_t> included((size_t)model->node_count, 0);
  int included_count = 0;
  if (model->skin_count > 0) {
    for (int s = 0; s < model->skin_count; s++) {
      const GltfSkin *skin = &model->skins[s];
      for (int j = 0; j < skin->joint_count; j++) {
        const int node_idx = skin->joints[j];
        if (node_idx < 0 || node_idx >= model->node_count)
          continue;
        if (!included[(size_t)node_idx]) {
          included[(size_t)node_idx] = 1;
          included_count++;
        }
      }
    }
    // Ensure included nodes form a valid hierarchy by pulling in all ancestors
    // up to the root.
    for (int n = 0; n < model->node_count; n++) {
      if (!included[(size_t)n])
        continue;
      int p = model->nodes[n].parent_index;
      while (p >= 0 && p < model->node_count) {
        if (included[(size_t)p])
          break;
        included[(size_t)p] = 1;
        included_count++;
        p = model->nodes[p].parent_index;
      }
    }
  }

  if (included_count > 0) {
    // Roots are included nodes whose parent is not included.
    std::vector<int> roots;
    roots.reserve((size_t)included_count);
    for (int n = 0; n < model->node_count; n++) {
      if (!included[(size_t)n])
        continue;
      const int p = model->nodes[n].parent_index;
      if (p < 0 || p >= model->node_count || !included[(size_t)p]) {
        roots.push_back(n);
      }
    }

    raw_skeleton.roots.reserve(roots.size());
    for (int root_index : roots) {
      raw_skeleton.roots.emplace_back();
      build_raw_skeleton_joint_filtered(model, root_index, included,
                                       raw_skeleton.roots.back(),
                                       joint_to_gltf_node);
    }
  } else {
    // Fallback: no skin joints available, keep legacy behavior (all scene
    // nodes).
    raw_skeleton.roots.reserve((size_t)model->scene_root_count);
    for (int i = 0; i < model->scene_root_count; i++) {
      int root_index = model->scene_roots[i];
      raw_skeleton.roots.emplace_back();
      build_raw_skeleton_joint(model, root_index, raw_skeleton.roots.back(),
                               joint_to_gltf_node);
    }
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
  g_ctx.gltf_node_to_joint.assign((size_t)model->node_count, -1);
  for (int j = 0; j < (int)g_ctx.joint_to_gltf_node.size(); j++) {
    const int node_idx = g_ctx.joint_to_gltf_node[(size_t)j];
    if (node_idx >= 0 && node_idx < model->node_count)
      g_ctx.gltf_node_to_joint[(size_t)node_idx] = j;
  }

  const int num_soa_joints = g_ctx.skeleton->num_soa_joints();
  g_ctx.locals.resize(num_soa_joints);
  g_ctx.locals_from.resize(num_soa_joints);
  g_ctx.locals_to.resize(num_soa_joints);
  g_ctx.locals_blended.resize(num_soa_joints);
  g_ctx.locals_match_from.resize(num_soa_joints);
  g_ctx.blend = BlendState{};

  // Build per-joint blend groups from humanoid mapping (if available).
  g_ctx.joint_group.assign((size_t)joint_count, (uint8_t)BLEND_GROUP_OTHER);
  if (model->humanoid_names && model->humanoid_bones &&
      model->humanoid_bone_count > 0) {
    for (int i = 0; i < model->humanoid_bone_count; i++) {
      const int node_idx = model->humanoid_bones[i];
      if (node_idx < 0 || node_idx >= model->node_count)
        continue;
      const int j = g_ctx.gltf_node_to_joint[(size_t)node_idx];
      if (j < 0 || j >= joint_count)
        continue;
      const std::string_view name(model->humanoid_names[i]);
      g_ctx.joint_group[(size_t)j] = (uint8_t)humanoid_name_to_blend_group(name);
    }
  }
  update_joint_weights_to();

  g_ctx.animations.clear();
  g_ctx.sampling_contexts.clear();
  g_ctx.animations.reserve((size_t)model->animation_count);
  g_ctx.sampling_contexts.reserve((size_t)model->animation_count);

  const float *corr = model->anim_global_correction;
  const bool corr_is_identity =
      (corr[0] == 0.0f && corr[1] == 0.0f && corr[2] == 0.0f && corr[3] == 1.0f);
  float corr_inv[4] = {0, 0, 0, 1};
  if (!corr_is_identity) {
    quat_inv(corr, corr_inv);
  }

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

      // Bake global correction into the ozz clip at conversion time so runtime
      // sampling/blending stays in one consistent space.
      if (!corr_is_identity) {
        apply_correction_to_translation_track(corr, gltf_anim, node_idx,
                                             track.translations);
        apply_correction_to_rotation_track(corr, corr_inv, gltf_anim, node_idx,
                                           track.rotations);
      }
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

static bool
sample_animation_to_buffer_clamped(int anim_index, float time_sec,
                                   ozz::vector<ozz::math::SoaTransform> &out) {
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
  job.output = ozz::make_span(out);
  return job.Run();
}

static void capture_model_pose_to_buffer(
    GltfModel *model, ozz::vector<ozz::math::SoaTransform> &out) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton)
    return;
  const int num_soa = g_ctx.skeleton->num_soa_joints();
  out.resize(num_soa);

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

      tx[lane] = node->translation[0];
      ty[lane] = node->translation[1];
      tz[lane] = node->translation[2];
      sx[lane] = node->scale[0];
      sy[lane] = node->scale[1];
      sz[lane] = node->scale[2];
      qx[lane] = node->rotation[0];
      qy[lane] = node->rotation[1];
      qz[lane] = node->rotation[2];
      qw[lane] = node->rotation[3];
    }

    ozz::math::SoaTransform &st = out[(size_t)soa];
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
}

static void extract_joint_quat(
    const ozz::vector<ozz::math::SoaTransform> &locals, int joint_index,
    float out_q[4]) {
  if (joint_index < 0)
    return;
  const int soa = joint_index / 4;
  const int lane = joint_index & 3;
  const ozz::math::SoaTransform &st = locals[(size_t)soa];
  float qx[4], qy[4], qz[4], qw[4];
  ozz::math::StorePtrU(st.rotation.x, qx);
  ozz::math::StorePtrU(st.rotation.y, qy);
  ozz::math::StorePtrU(st.rotation.z, qz);
  ozz::math::StorePtrU(st.rotation.w, qw);
  out_q[0] = qx[lane];
  out_q[1] = qy[lane];
  out_q[2] = qz[lane];
  out_q[3] = qw[lane];
}

static float quat_abs_dot(const float a[4], const float b[4]) {
  float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  return d < 0.f ? -d : d;
}

static BlendGroup humanoid_name_to_blend_group(std::string_view n) {
  if (n == "hips" || n == "root")
    return BLEND_GROUP_ROOT;

  if (n == "spine" || n == "chest" || n == "upperChest")
    return BLEND_GROUP_SPINE;

  if (n == "neck" || n == "head" || n == "jaw")
    return BLEND_GROUP_HEAD;

  if (n == "leftShoulder" || n == "rightShoulder" || n == "leftUpperArm" ||
      n == "rightUpperArm" || n == "leftLowerArm" || n == "rightLowerArm")
    return BLEND_GROUP_ARMS;

  if (n == "leftHand" || n == "rightHand" ||
      n.find("Thumb") != std::string_view::npos ||
      n.find("Index") != std::string_view::npos ||
      n.find("Middle") != std::string_view::npos ||
      n.find("Ring") != std::string_view::npos ||
      n.find("Little") != std::string_view::npos)
    return BLEND_GROUP_HANDS;

  if (n == "leftUpperLeg" || n == "rightUpperLeg" || n == "leftLowerLeg" ||
      n == "rightLowerLeg")
    return BLEND_GROUP_LEGS;

  if (n == "leftFoot" || n == "rightFoot" || n == "leftToes" || n == "rightToes")
    return BLEND_GROUP_FEET;

  return BLEND_GROUP_OTHER;
}

static void update_joint_weights_to(void) {
  if (!g_ctx.skeleton)
    return;
  const int joint_count = g_ctx.skeleton->num_joints();
  const int num_soa = g_ctx.skeleton->num_soa_joints();
  if ((int)g_ctx.joint_group.size() != joint_count)
    return;
  g_ctx.joint_weights_to.resize(num_soa);

  for (int soa = 0; soa < num_soa; soa++) {
    float w[4] = {1.f, 1.f, 1.f, 1.f};
    for (int lane = 0; lane < 4; lane++) {
      const int j = soa * 4 + lane;
      if (j >= joint_count) {
        w[lane] = 0.f;
        continue;
      }
      const int group = (int)g_ctx.joint_group[(size_t)j];
      float f = 1.f;
      if (group >= 0 && group < BLEND_GROUP_COUNT) {
        f = g_anim_blend_group_factors[group];
      }
      // Keep sane: factors are intended in [0,1].
      if (f < 0.f)
        f = 0.f;
      if (f > 1.f)
        f = 1.f;
      w[lane] = f;
    }
    g_ctx.joint_weights_to[(size_t)soa] = ozz::math::simd_float4::LoadPtrU(w);
  }
}

static std::vector<int> pick_match_joints(GltfModel *model) {
  std::vector<int> joints;
  if (!model || !g_ctx.skeleton)
    return joints;

  auto add_humanoid = [&](const char *name) {
    if (!model->humanoid_names || !model->humanoid_bones)
      return;
    for (int i = 0; i < model->humanoid_bone_count; i++) {
      if (strcmp(model->humanoid_names[i], name) != 0)
        continue;
      const int node_idx = model->humanoid_bones[i];
      if (node_idx < 0 || node_idx >= (int)g_ctx.gltf_node_to_joint.size())
        return;
      const int j = g_ctx.gltf_node_to_joint[(size_t)node_idx];
      if (j >= 0)
        joints.push_back(j);
      return;
    }
  };

  // Keep this conservative: hips/head are high-signal and avoid matching
  // against extreme limb twists that can produce odd blends.
  add_humanoid("hips");
  add_humanoid("head");

  if (joints.empty() && g_ctx.skeleton->num_joints() > 0)
    joints.push_back(0);

  return joints;
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

  (void)anim_index;
}

// --- Blending API ---

void ozz_bridge_blend_start(int from_index, float from_time, int to_index,
                            float to_time, float duration) {
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
  g_ctx.blend.to_time = to_time;
  g_ctx.blend.blend_time = 0.f;
  g_ctx.blend.blend_duration = duration;
  g_ctx.blend.active = true;
  g_ctx.blend.use_snapshot = false;
}

bool ozz_bridge_blend_tick(GltfModel *model, float dt) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton || !g_ctx.blend.active)
    return false;

  // Allow live tuning of group factors from UI/IPC.
  update_joint_weights_to();

  // Advance times
  g_ctx.blend.blend_time += dt;
  g_ctx.blend.from_time += dt;
  g_ctx.blend.to_time += dt;

  // Check if blend is complete
  if (g_ctx.blend.blend_time >= g_ctx.blend.blend_duration) {
    g_ctx.blend.active = false;
    // Sample target animation at final time
    // Clamp to avoid wrapping through the loop seam during the blend.
    if (!sample_animation_to_buffer_clamped(g_ctx.blend.to_anim,
                                            g_ctx.blend.to_time, g_ctx.locals)) {
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
  // Clamp to avoid wrapping through the loop seam during the blend.
  if (!sample_animation_to_buffer_clamped(g_ctx.blend.to_anim, g_ctx.blend.to_time,
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
  if (!g_ctx.joint_weights_to.empty()) {
    layers[1].joint_weights = ozz::make_span(g_ctx.joint_weights_to);
  }

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

  capture_model_pose_to_buffer(model, g_snapshot_locals);

  g_snapshot_valid = true;
}

bool ozz_bridge_snapshot_available(void) { return g_snapshot_valid; }

void ozz_bridge_snapshot_invalidate(void) { g_snapshot_valid = false; }

bool ozz_bridge_blend_from_snapshot(int to_index, float to_time, float duration) {
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
  g_ctx.blend.to_time = to_time;
  g_ctx.blend.blend_time = 0.f;
  g_ctx.blend.blend_duration = duration;
  g_ctx.blend.active = true;
  g_ctx.blend.use_snapshot = true;
  g_snapshot_valid = false; // Consumed
  return true;
}

float ozz_bridge_match_start_time(GltfModel *model, int to_index,
                                  float blend_duration, int samples) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton)
    return 0.f;
  if (to_index < 0 || to_index >= (int)g_ctx.animations.size())
    return 0.f;
  const auto &anim = g_ctx.animations[(size_t)to_index];
  if (!anim || anim->duration() <= 1e-6f)
    return 0.f;

  const float dur = anim->duration();
  // If the clip is too short relative to the blend, don't try to start mid-clip
  // (it will hit the loop seam during the blend and look worse).
  if (blend_duration <= 0.f || dur <= (blend_duration * 1.25f) || dur < 0.35f)
    return 0.f;

  const float max_start = dur - blend_duration;
  if (max_start <= 1e-4f)
    return 0.f;

  if (samples < 4)
    samples = 4;
  if (samples > 128)
    samples = 128;

  capture_model_pose_to_buffer(model, g_ctx.locals_match_from);
  const std::vector<int> match_joints = pick_match_joints(model);

  float best_time = 0.f;
  float best_err = INFINITY;

  for (int i = 0; i < samples; i++) {
    const float ratio = (float)i / (float)samples;
    const float t = ratio * max_start;
    if (!sample_animation_to_buffer_clamped(to_index, t, g_ctx.locals_to))
      continue;

    float err = 0.f;
    for (int j : match_joints) {
      float qa[4] = {0, 0, 0, 1};
      float qb[4] = {0, 0, 0, 1};
      extract_joint_quat(g_ctx.locals_match_from, j, qa);
      extract_joint_quat(g_ctx.locals_to, j, qb);
      const float d = quat_abs_dot(qa, qb);
      err += (1.f - d);
    }

    if (err < best_err) {
      best_err = err;
      best_time = t;
    }
  }

  if (!std::isfinite(best_err))
    return 0.f;
  if (best_time < 0.f)
    best_time = 0.f;
  if (best_time > max_start)
    best_time = max_start;
  return best_time;
}
