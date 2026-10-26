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
#include "transition.h"

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"
#include "ozz/base/span.h"

namespace {

// State for local-pose inertial transitions
struct BlendState {
  int to_anim = -1;
  float blend_time = 0.f;
  float blend_duration = 0.f;
  bool active = false;
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
  ozz::vector<ozz::math::SoaTransform> locals_to;


  // Humanoid groups select per-joint transition duration multipliers.
  std::vector<uint8_t> joint_group; // BlendGroup per joint

  std::vector<PoseTransform> history;
  std::vector<PoseVelocity> velocity;
  std::vector<PoseTransition> transitions;
  std::vector<float> transition_durations;
  BlendState blend;
};

OzzModelContext g_ctx;

// Persistent snapshot buffer (survives ozz_bridge_set_model calls)
// Stores corrected local poses and their velocities across clip reloads.
static std::vector<PoseTransform> g_snapshot_pose;
static std::vector<PoseVelocity> g_snapshot_velocity;
static std::vector<int> g_snapshot_mapping;
static GltfNode *g_snapshot_nodes = nullptr;
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
  g_ctx.locals_to.resize(num_soa_joints);
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

// --- Velocity-preserving transitions ---
static PoseTransform node_pose(const GltfNode &node) {
  PoseTransform pose;
  memcpy(pose.translation, node.translation, sizeof(pose.translation));
  memcpy(pose.rotation, node.rotation, sizeof(pose.rotation));
  memcpy(pose.scale, node.scale, sizeof(pose.scale));
  return pose;
}

static PoseTransform buffer_pose(const ozz::vector<ozz::math::SoaTransform> &buffer, int joint) {
  const auto &t = buffer[(size_t)(joint/4)];
  float value[4];
  PoseTransform pose;
  const int lane = joint & 3;
  const ozz::math::SimdFloat4 *translation[] = {&t.translation.x, &t.translation.y, &t.translation.z};
  const ozz::math::SimdFloat4 *rotation[] = {&t.rotation.x, &t.rotation.y, &t.rotation.z, &t.rotation.w};
  const ozz::math::SimdFloat4 *scale[] = {&t.scale.x, &t.scale.y, &t.scale.z};
  for (int i=0;i<3;i++) {
    ozz::math::StorePtrU(*translation[i], value); pose.translation[i] = value[lane];
    ozz::math::StorePtrU(*scale[i], value); pose.scale[i] = value[lane];
  }
  for (int i=0;i<4;i++) { ozz::math::StorePtrU(*rotation[i], value); pose.rotation[i] = value[lane]; }
  return pose;
}

void ozz_bridge_record_pose(GltfModel *model, float dt) {
  if (!model || model != g_ctx.model || !g_ctx.skeleton) return;
  const size_t count = g_ctx.joint_to_gltf_node.size();
  bool has_history = g_ctx.history.size() == count;
  g_ctx.history.resize(count);
  g_ctx.velocity.resize(count);
  for (size_t j=0;j<count;j++) {
    PoseTransform pose = node_pose(model->nodes[g_ctx.joint_to_gltf_node[j]]);
    if (has_history) pose_velocity(&g_ctx.history[j], &pose, dt, &g_ctx.velocity[j]);
    else g_ctx.velocity[j] = PoseVelocity{};
    g_ctx.history[j] = pose;
  }
}

void ozz_bridge_reset_velocity(void) {
  for (auto &velocity : g_ctx.velocity) velocity = PoseVelocity{};
}

static std::vector<PoseTransform> transition_source(const GltfModel *model) {
  /* IK is applied after this pose was recorded. Starting from model->nodes
   * instead feeds its pelvis/leg correction back into the animation layer. */
  if (g_ctx.history.size() == g_ctx.joint_to_gltf_node.size()) return g_ctx.history;
  std::vector<PoseTransform> source;
  for (int node : g_ctx.joint_to_gltf_node) source.push_back(node_pose(model->nodes[node]));
  return source;
}

static bool begin_transition(const std::vector<PoseTransform> &source,
                             const std::vector<PoseVelocity> &velocity,
                             int to_index, float to_time, float duration, float speed) {
  if (!g_ctx.skeleton || duration <= 0.001f || source.size() != g_ctx.joint_to_gltf_node.size()) return false;
  if (!sample_animation_to_buffer_clamped(to_index, to_time, g_ctx.locals_to) ||
      !sample_animation_to_buffer_clamped(to_index, to_time + 0.001f*speed, g_ctx.locals)) return false;
  g_ctx.transitions.resize(source.size());
  g_ctx.transition_durations.resize(source.size());
  float maximum_duration = 0;
  for (size_t j=0;j<source.size();j++) {
    PoseTransform target = buffer_pose(g_ctx.locals_to, (int)j);
    PoseTransform next = buffer_pose(g_ctx.locals, (int)j);
    PoseVelocity target_velocity;
    pose_velocity(&target, &next, 0.001f, &target_velocity);
    PoseVelocity source_velocity = j < velocity.size() ? velocity[j] : PoseVelocity{};
    pose_transition_init(&g_ctx.transitions[j], &source[j], &source_velocity, &target, &target_velocity);
    int group = g_ctx.joint_group[j];
    float factor = group < BLEND_GROUP_COUNT ? g_anim_blend_group_factors[group] : 1;
    g_ctx.transition_durations[j] = duration * clampf(factor, 0, 2);
    if (g_ctx.transition_durations[j] > maximum_duration) maximum_duration = g_ctx.transition_durations[j];
  }
  g_ctx.blend.to_anim = to_index;
  g_ctx.blend.blend_time = 0;
  g_ctx.blend.blend_duration = maximum_duration;
  g_ctx.blend.active = maximum_duration > 0.001f;
  return g_ctx.blend.active;
}

void ozz_bridge_blend_start(int from_index, float from_time, int to_index,
                           float to_time, float duration, float speed) {
  (void)from_index; (void)from_time;
  if (!g_ctx.model || !g_ctx.skeleton) return;
  std::vector<PoseTransform> source = transition_source(g_ctx.model);
  begin_transition(source, g_ctx.velocity, to_index, to_time, duration, speed);
}

bool ozz_bridge_blend_tick(GltfModel *model, float dt, float target_time) {
  if (!model || model != g_ctx.model || !g_ctx.blend.active) return false;
  if (!ozz_bridge_sample_into_model(model, g_ctx.blend.to_anim, target_time)) {
    g_ctx.blend.active = false;
    return false;
  }
  return ozz_bridge_blend_apply(model, dt);
}

bool ozz_bridge_blend_apply(GltfModel *model, float dt) {
  if (!model || model != g_ctx.model || !g_ctx.blend.active) return false;
  g_ctx.blend.blend_time += fmaxf(0, dt);
  if (g_ctx.blend.blend_time + 1e-6f >= g_ctx.blend.blend_duration) {
    g_ctx.blend.active = false;
    return false;
  }
  for (size_t j=0;j<g_ctx.joint_to_gltf_node.size();j++) {
    GltfNode &node = model->nodes[g_ctx.joint_to_gltf_node[j]];
    if (node.is_spring_bone) continue;
    PoseTransform target = node_pose(node), output;
    pose_transition_sample(&g_ctx.transitions[j], &target, g_ctx.blend.blend_time,
                           g_ctx.transition_durations[j], &output);
    memcpy(node.translation, output.translation, sizeof(output.translation));
    memcpy(node.rotation, output.rotation, sizeof(output.rotation));
    memcpy(node.scale, output.scale, sizeof(output.scale));
  }
  return true;
}

void ozz_bridge_blend_cancel(void) { g_ctx.blend.active = false; }
bool ozz_bridge_blend_active(void) { return g_ctx.blend.active; }
int ozz_bridge_blend_target(void) { return g_ctx.blend.active ? g_ctx.blend.to_anim : -1; }
float ozz_bridge_blend_ratio(void) {
  return g_ctx.blend.active ? clampf(g_ctx.blend.blend_time/g_ctx.blend.blend_duration, 0, 1) : 0;
}

void ozz_bridge_snapshot_pose(GltfModel *model, int anim_index, float time_sec) {
  (void)anim_index; (void)time_sec;
  if (!model || model != g_ctx.model || !g_ctx.skeleton) return;
  g_snapshot_pose = transition_source(model);
  g_snapshot_velocity = g_ctx.velocity;
  g_snapshot_mapping = g_ctx.joint_to_gltf_node;
  g_snapshot_nodes = model->nodes;
  g_snapshot_valid = true;
}

bool ozz_bridge_snapshot_available(void) { return g_snapshot_valid; }
void ozz_bridge_snapshot_invalidate(void) {
  g_snapshot_valid = false;
  g_snapshot_pose.clear(); g_snapshot_velocity.clear(); g_snapshot_mapping.clear();
  g_snapshot_nodes = nullptr;
}

bool ozz_bridge_blend_from_snapshot(int to_index, float to_time, float duration, float speed) {
  if (!g_snapshot_valid || !g_ctx.model || g_snapshot_nodes != g_ctx.model->nodes ||
      g_snapshot_mapping != g_ctx.joint_to_gltf_node) {
    ozz_bridge_snapshot_invalidate();
    return false;
  }
  bool result = begin_transition(g_snapshot_pose, g_snapshot_velocity, to_index, to_time, duration, speed);
  ozz_bridge_snapshot_invalidate();
  return result;
}
