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
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/span.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/memory/unique_ptr.h"

namespace {

struct OzzModelContext {
  GltfModel *model = nullptr;
  ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
  std::vector<ozz::unique_ptr<ozz::animation::Animation>> animations;
  std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>>
      sampling_contexts;
  std::vector<int> joint_to_gltf_node; // runtime joint index -> gltf node index
  ozz::vector<ozz::math::SoaTransform> locals;
};

OzzModelContext g_ctx;

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

static void build_raw_skeleton_joint(GltfModel *model, int node_index,
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
    float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
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

  g_ctx.locals.resize(g_ctx.skeleton->num_soa_joints());

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
      if (ch->sampler_index < 0 || ch->sampler_index >= gltf_anim->sampler_count)
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

      sanitize_times_and_push_translation(ci.translation, duration,
                                          node->base_translation,
                                          track.translations);
      sanitize_times_and_push_rotation(ci.rotation, duration, node->base_rotation,
                                       track.rotations);
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

    auto sampling_ctx = std::make_unique<ozz::animation::SamplingJob::Context>();
    sampling_ctx->Resize(joint_count);
    g_ctx.animations.push_back(std::move(runtime_anim));
    g_ctx.sampling_contexts.push_back(std::move(sampling_ctx));
  }

  return true;
}

void ozz_bridge_clear_model(void) {
  g_ctx = OzzModelContext{};
}

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
