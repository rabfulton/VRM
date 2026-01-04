/*
 * VRM Overlay - glTF/GLB Loader Implementation
 *
 * Uses tinygltf (header-only C++ library) for parsing.
 * This is a C wrapper that interfaces with tinygltf.
 */

/*
 * NOTE: tinygltf is C++, so we need a thin C++ wrapper.
 * This file provides the C interface, and gltf_loader_impl.cpp
 * contains the actual tinygltf calls.
 */

#include "gltf_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* These functions are implemented in gltf_loader_impl.cpp */
extern bool gltf_load_impl(GltfModel *model, const char *path);
extern void gltf_unload_impl(GltfModel *model);
extern bool gltf_load_animation_impl(GltfModel *model, const char *anim_path);

bool gltf_load(GltfModel *model, const char *path) {
  memset(model, 0, sizeof(GltfModel));
  return gltf_load_impl(model, path);
}

void gltf_unload(GltfModel *model) {
  if (!model->loaded)
    return;
  gltf_unload_impl(model);
  memset(model, 0, sizeof(GltfModel));
}

bool gltf_load_animation(GltfModel *model, const char *anim_path) {
  if (!model->loaded) return false;
  return gltf_load_animation_impl(model, anim_path);
}

/* Update world matrices by traversing node hierarchy */
static void update_node_transform(GltfModel *model, int node_idx,
                                  const float *parent_matrix) {
  if (node_idx < 0 || node_idx >= model->node_count)
    return;

  GltfNode *node = &model->nodes[node_idx];

  /* Build local matrix from TRS: M = T * R * S */
  float qx = node->rotation[0];
  float qy = node->rotation[1];
  float qz = node->rotation[2];
  float qw = node->rotation[3];

  /* Rotation matrix from quaternion */
  float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  float xy = qx * qy, xz = qx * qz, yz = qy * qz;
  float wx = qw * qx, wy = qw * qy, wz = qw * qz;

  float local[16];
  /* Column-major order for OpenGL */
  local[0] = (1 - 2*(yy + zz)) * node->scale[0];
  local[1] = 2*(xy + wz) * node->scale[0];
  local[2] = 2*(xz - wy) * node->scale[0];
  local[3] = 0;

  local[4] = 2*(xy - wz) * node->scale[1];
  local[5] = (1 - 2*(xx + zz)) * node->scale[1];
  local[6] = 2*(yz + wx) * node->scale[1];
  local[7] = 0;

  local[8] = 2*(xz + wy) * node->scale[2];
  local[9] = 2*(yz - wx) * node->scale[2];
  local[10] = (1 - 2*(xx + yy)) * node->scale[2];
  local[11] = 0;

  local[12] = node->translation[0];
  local[13] = node->translation[1];
  local[14] = node->translation[2];
  local[15] = 1;

  /* world = parent * local (column-major multiplication) */
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        sum += parent_matrix[k * 4 + row] * local[col * 4 + k];
      }
      node->world_matrix[col * 4 + row] = sum;
    }
  }

  /* Recursively update children */
  for (int i = 0; i < node->child_count; i++) {
    update_node_transform(model, node->children[i], node->world_matrix);
  }
}

void gltf_update_transforms(GltfModel *model) {
  /* Identity matrix for roots */
  float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  /* Update all scene root nodes */
  for (int i = 0; i < model->scene_root_count; i++) {
    update_node_transform(model, model->scene_roots[i], identity);
  }
}

int gltf_find_node(const GltfModel *model, const char *name) {
  for (int i = 0; i < model->node_count; i++) {
    if (strcmp(model->nodes[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

void gltf_get_screen_bounds(const GltfModel *model, const float *view, const float *proj,
                            float *out_ndc_w, float *out_ndc_h) {
  float min_x = 1e9f, max_x = -1e9f;
  float min_y = 1e9f, max_y = -1e9f;

  /* Compute view-projection matrix */
  float vp[16];
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        sum += proj[k * 4 + row] * view[col * 4 + k];
      }
      vp[col * 4 + row] = sum;
    }
  }

  /* Project all node positions (skeleton joints) */
  for (int i = 0; i < model->node_count; i++) {
    float *m = model->nodes[i].world_matrix;
    float wx = m[12], wy = m[13], wz = m[14];

    /* Transform to clip space */
    float cx = vp[0]*wx + vp[4]*wy + vp[8]*wz + vp[12];
    float cy = vp[1]*wx + vp[5]*wy + vp[9]*wz + vp[13];
    float cw = vp[3]*wx + vp[7]*wy + vp[11]*wz + vp[15];

    if (cw > 0.001f) {
      float ndcx = cx / cw;
      float ndcy = cy / cw;
      if (ndcx < min_x) min_x = ndcx;
      if (ndcx > max_x) max_x = ndcx;
      if (ndcy < min_y) min_y = ndcy;
      if (ndcy > max_y) max_y = ndcy;
    }
  }

  /* Return NDC range (add margin for mesh beyond bones) */
  *out_ndc_w = (max_x - min_x) * 1.2f;
  *out_ndc_h = (max_y - min_y) * 1.2f;
}

void gltf_get_screen_bounds_px(const GltfModel *model, const float *view, const float *proj,
                               int screen_w, int screen_h,
                               float *out_min_x, float *out_min_y, float *out_max_x, float *out_max_y) {
  float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
  float vp[16];
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) sum += proj[k * 4 + row] * view[col * 4 + k];
      vp[col * 4 + row] = sum;
    }
  }
  for (int i = 0; i < model->node_count; i++) {
    float *m = model->nodes[i].world_matrix;
    float wx = m[12], wy = m[13], wz = m[14];
    float cx = vp[0]*wx + vp[4]*wy + vp[8]*wz + vp[12];
    float cy = vp[1]*wx + vp[5]*wy + vp[9]*wz + vp[13];
    float cw = vp[3]*wx + vp[7]*wy + vp[11]*wz + vp[15];
    if (cw > 0.001f) {
      float ndcx = cx / cw, ndcy = cy / cw;
      if (ndcx < min_x) min_x = ndcx; if (ndcx > max_x) max_x = ndcx;
      if (ndcy < min_y) min_y = ndcy; if (ndcy > max_y) max_y = ndcy;
    }
  }
  /* Add margin and convert NDC to screen pixels (Y flipped) */
  float margin = 0.1f;
  min_x -= margin; max_x += margin; min_y -= margin; max_y += margin;
  *out_min_x = (min_x * 0.5f + 0.5f) * screen_w;
  *out_max_x = (max_x * 0.5f + 0.5f) * screen_w;
  *out_min_y = (1.0f - (max_y * 0.5f + 0.5f)) * screen_h;
  *out_max_y = (1.0f - (min_y * 0.5f + 0.5f)) * screen_h;
}

/* Forward declaration for animation sampling */
extern void animator_sample_frame(GltfModel *model, int anim_idx, float time);

/* Compute world-space bounding box for animation (camera-independent) */
static void compute_world_bounds(GltfModel *model, float *out_min, float *out_max) {
  out_min[0] = out_min[1] = out_min[2] = 1e9f;
  out_max[0] = out_max[1] = out_max[2] = -1e9f;
  
  for (int i = 0; i < model->node_count; i++) {
    float *m = model->nodes[i].world_matrix;
    float x = m[12], y = m[13], z = m[14];
    if (x < out_min[0]) out_min[0] = x;
    if (y < out_min[1]) out_min[1] = y;
    if (z < out_min[2]) out_min[2] = z;
    if (x > out_max[0]) out_max[0] = x;
    if (y > out_max[1]) out_max[1] = y;
    if (z > out_max[2]) out_max[2] = z;
  }
}

void gltf_compute_animation_bounds(GltfModel *model, const char *anim_path,
                                   const float *view, const float *proj,
                                   float *out_ndc_w, float *out_ndc_h) {
  (void)view; (void)proj; /* Now computing world bounds, not NDC */
  *out_ndc_w = 0.5f;
  *out_ndc_h = 1.0f;
  
  if (!model || !anim_path || model->animation_count == 0) return;
  
  /* Build cache filename */
  char cache_path[512];
  snprintf(cache_path, sizeof(cache_path), "%s.bounds", anim_path);
  
  float world_w, world_h;
  
  /* Check if cache is valid */
  struct stat anim_stat, cache_stat;
  if (stat(anim_path, &anim_stat) == 0 && stat(cache_path, &cache_stat) == 0) {
    if (cache_stat.st_mtime >= anim_stat.st_mtime) {
      FILE *f = fopen(cache_path, "r");
      if (f) {
        if (fscanf(f, "%f %f", &world_w, &world_h) == 2) {
          fclose(f);
          *out_ndc_w = world_w;
          *out_ndc_h = world_h;
          return;
        }
        fclose(f);
      }
    }
  }
  
  /* Sample animation and compute world-space bounds */
  GltfAnimation *anim = &model->animations[0];
  float duration = anim->duration > 0 ? anim->duration : 1.0f;
  int samples = (int)(duration * 30);
  if (samples < 10) samples = 10;
  if (samples > 300) samples = 300;
  
  float global_min[3] = {1e9f, 1e9f, 1e9f};
  float global_max[3] = {-1e9f, -1e9f, -1e9f};
  
  for (int i = 0; i <= samples; i++) {
    float t = (float)i / samples * duration;
    animator_sample_frame(model, 0, t);
    gltf_update_transforms(model);
    
    float frame_min[3], frame_max[3];
    compute_world_bounds(model, frame_min, frame_max);
    
    for (int j = 0; j < 3; j++) {
      if (frame_min[j] < global_min[j]) global_min[j] = frame_min[j];
      if (frame_max[j] > global_max[j]) global_max[j] = frame_max[j];
    }
  }
  
  /* Store world-space extents (add margin for mesh beyond bones) */
  world_w = (global_max[0] - global_min[0]) * 1.3f;
  world_h = (global_max[1] - global_min[1]) * 1.3f;
  
  *out_ndc_w = world_w;
  *out_ndc_h = world_h;
  
  /* Write cache */
  FILE *f = fopen(cache_path, "w");
  if (f) {
    fprintf(f, "%f %f\n", world_w, world_h);
    fclose(f);
  }
}
