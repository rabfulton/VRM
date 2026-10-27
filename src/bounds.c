/* Conservative geometry bounds and pixel-stable transparent viewport sizing. */
#include "gltf_loader.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void gltf_bounds_include(GltfBounds *bounds, const float p[3]) {
  if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) return;
  for (int k = 0; k < 3; k++) {
    if (!bounds->valid || p[k] < bounds->min[k]) bounds->min[k] = p[k];
    if (!bounds->valid || p[k] > bounds->max[k]) bounds->max[k] = p[k];
  }
  bounds->valid = true;
}

void gltf_bounds_merge(GltfBounds *bounds, const GltfBounds *other) {
  if (!other->valid) return;
  gltf_bounds_include(bounds, other->min);
  gltf_bounds_include(bounds, other->max);
}

void gltf_bounds_transform(GltfBounds *out, const GltfBounds *bounds, const float m[16]) {
  if (!bounds->valid) return;
  for (int corner = 0; corner < 8; corner++) {
    float p[3], result[3];
    for (int k = 0; k < 3; k++) p[k] = (corner & (1 << k)) ? bounds->max[k] : bounds->min[k];
    for (int k = 0; k < 3; k++) result[k] = m[k]*p[0] + m[4+k]*p[1] + m[8+k]*p[2] + m[12+k];
    gltf_bounds_include(out, result);
  }
}

void gltf_get_world_bounds(const GltfModel *model, GltfBounds *bounds) {
  memset(bounds, 0, sizeof(*bounds));
  if (!model) return;
  for (int i = 0; i < model->node_count; i++) {
    const GltfNode *node = &model->nodes[i];
    if (node->mesh_index < 0 || node->mesh_index >= model->mesh_count) continue;
    const GltfMesh *mesh = &model->meshes[node->mesh_index];
    bool skinned = false;
    if (node->skin_index >= 0 && node->skin_index < model->skin_count) {
      const GltfSkin *skin = &model->skins[node->skin_index];
      if (skin->joint_bounds) {
        for (int j = 0; j < skin->joint_count; j++) {
          int bone = skin->joints[j];
          if (bone >= 0 && bone < model->node_count && skin->joint_bounds[j].valid) {
            gltf_bounds_transform(bounds, &skin->joint_bounds[j], model->nodes[bone].world_matrix);
            skinned = true;
          }
        }
      }
    }
    for (int j = 0; j < mesh->primitive_count; j++) {
      const GltfPrimitive *primitive = &mesh->primitives[j];
      if (!skinned || !primitive->has_skinning)
        gltf_bounds_transform(bounds, &primitive->local_bounds, node->world_matrix);
    }
  }
  /* Synthetic rigs/unsupported geometry still get a conservative joint fallback. */
  if (!bounds->valid) {
    for (int i = 0; i < model->node_count; i++) gltf_bounds_include(bounds, &model->nodes[i].world_matrix[12]);
    if (bounds->valid) for (int k = 0; k < 3; k++) { bounds->min[k] -= 0.2f; bounds->max[k] += 0.2f; }
  }
}

extern void animator_sample_frame(GltfModel *model, int index, float time);

void gltf_compute_animation_bounds(GltfModel *model, GltfBounds *bounds) {
  gltf_get_world_bounds(model, bounds);
  if (!model || !model->node_count || !model->animation_count) return;
  /* No cross-avatar disk cache: bounds depend on geometry, retargeting and axis
   * correction, not only the animation file's timestamp. */
  float *saved = malloc((size_t)model->node_count * 10 * sizeof(float));
  if (!saved) return;
  for (int i = 0; i < model->node_count; i++) {
    memcpy(saved+i*10, model->nodes[i].translation, 3*sizeof(float));
    memcpy(saved+i*10+3, model->nodes[i].rotation, 4*sizeof(float));
    memcpy(saved+i*10+7, model->nodes[i].scale, 3*sizeof(float));
  }
  memset(bounds, 0, sizeof(*bounds));
  for (int clip = 0; clip < model->animation_count; clip++) {
    float duration = model->animations[clip].duration;
    if (!isfinite(duration) || duration < 0) continue;
    int samples = (int)fminf(300, fmaxf(10, duration * 30));
    for (int i = 0; i <= samples; i++) {
      animator_sample_frame(model, clip, duration * i / samples);
      gltf_update_transforms(model);
      GltfBounds frame;
      gltf_get_world_bounds(model, &frame);
      gltf_bounds_merge(bounds, &frame);
    }
  }
  for (int i = 0; i < model->node_count; i++) {
    memcpy(model->nodes[i].translation, saved+i*10, 3*sizeof(float));
    memcpy(model->nodes[i].rotation, saved+i*10+3, 4*sizeof(float));
    memcpy(model->nodes[i].scale, saved+i*10+7, 3*sizeof(float));
  }
  free(saved);
  gltf_update_transforms(model);
}

bool gltf_fit_viewport(const GltfBounds *bounds, const float m[16], float fov,
                       int base_w, int base_h, int max_w, int max_h,
                       int *width, int *height, float *zoom) {
  if (!bounds->valid || base_w <= 0 || base_h <= 0 || max_w < 100 || max_h < 100 || !isfinite(fov) || fov <= 0 || fov >= 3.1f) return false;
  float half_x = 0, half_y = 0;
  const float focal = base_h / (2 * tanf(fov / 2));
  for (int corner = 0; corner < 8; corner++) {
    float p[3], camera[3];
    for (int k = 0; k < 3; k++) p[k] = (corner & (1 << k)) ? bounds->max[k] : bounds->min[k];
    for (int k = 0; k < 3; k++) camera[k] = m[k]*p[0] + m[4+k]*p[1] + m[8+k]*p[2] + m[12+k];
    if (!isfinite(camera[2]) || camera[2] >= -0.1f) return false;
    half_x = fmaxf(half_x, fabsf(focal * camera[0] / camera[2]));
    half_y = fmaxf(half_y, fabsf(focal * camera[1] / camera[2]));
  }
  /* The silhouette shadow is offset 100 px horizontally in renderer.c; reserve
   * its blur/outline too, not just the solid mesh. Keep the optical centre fixed. */
  const float padding_x = fminf(128.0f,max_w*0.2f), padding_y = 24.0f;
  *zoom = fminf(1, fminf((max_w - 2*padding_x) / fmaxf(2*half_x, 1), (max_h - 2*padding_y) / fmaxf(2*half_y, 1)));
  *width = (int)ceilf(fminf(max_w, fmaxf(fminf(base_w, max_w), 2*half_x*(*zoom)+2*padding_x)));
  *height = (int)ceilf(fminf(max_h, fmaxf(fminf(base_h, max_h), 2*half_y*(*zoom)+2*padding_y)));
  return true;
}
