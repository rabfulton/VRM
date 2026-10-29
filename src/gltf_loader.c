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
#include "spring_bones.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These functions are implemented in gltf_loader_impl.cpp */
extern bool gltf_load_impl(GltfModel *model, const char *path);
extern void gltf_unload_impl(GltfModel *model);
extern bool gltf_load_animation_impl(GltfModel *model, const char *anim_path, bool neutral_facing);

bool gltf_load(GltfModel *model, const char *path) {
  memset(model, 0, sizeof(GltfModel));
  return gltf_load_impl(model, path);
}

void gltf_unload(GltfModel *model) {
  if (!model->loaded)
    return;
  spring_bones_cleanup(model);
  gltf_unload_impl(model);
  memset(model, 0, sizeof(GltfModel));
}

bool gltf_load_animation(GltfModel *model, const char *anim_path) {
  if (!model->loaded)
    return false;
  return gltf_load_animation_impl(model, anim_path, false);
}

bool gltf_load_animation_neutral(GltfModel *model, const char *anim_path) {
  return model->loaded && gltf_load_animation_impl(model, anim_path, true);
}


int gltf_find_node(const GltfModel *model, const char *name) {
  for (int i = 0; i < model->node_count; i++) {
    if (strcmp(model->nodes[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

void gltf_get_screen_bounds(const GltfModel *model, const float *view,
                            const float *proj, float *out_ndc_w,
                            float *out_ndc_h) {
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
    float cx = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
    float cy = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
    float cw = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];

    if (cw > 0.001f) {
      float ndcx = cx / cw;
      float ndcy = cy / cw;
      if (ndcx < min_x)
        min_x = ndcx;
      if (ndcx > max_x)
        max_x = ndcx;
      if (ndcy < min_y)
        min_y = ndcy;
      if (ndcy > max_y)
        max_y = ndcy;
    }
  }

  /* Return NDC range (add margin for mesh beyond bones) */
  *out_ndc_w = (max_x - min_x) * 1.2f;
  *out_ndc_h = (max_y - min_y) * 1.2f;
}

void gltf_get_screen_bounds_px(const GltfModel *model, const float *view,
                               const float *proj, int screen_w, int screen_h,
                               float *out_min_x, float *out_min_y,
                               float *out_max_x, float *out_max_y) {
  float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
  float vp[16];
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++)
        sum += proj[k * 4 + row] * view[col * 4 + k];
      vp[col * 4 + row] = sum;
    }
  }
  for (int i = 0; i < model->node_count; i++) {
    float *m = model->nodes[i].world_matrix;
    float wx = m[12], wy = m[13], wz = m[14];
    float cx = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
    float cy = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
    float cw = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];
    if (cw > 0.001f) {
      float ndcx = cx / cw, ndcy = cy / cw;
      if (ndcx < min_x)
        min_x = ndcx;
      if (ndcx > max_x)
        max_x = ndcx;
      if (ndcy < min_y)
        min_y = ndcy;
      if (ndcy > max_y)
        max_y = ndcy;
    }
  }
  /* Add margin and convert NDC to screen pixels (Y flipped) */
  float margin = 0.1f;
  min_x -= margin;
  max_x += margin;
  min_y -= margin;
  max_y += margin;
  *out_min_x = (min_x * 0.5f + 0.5f) * screen_w;
  *out_max_x = (max_x * 0.5f + 0.5f) * screen_w;
  *out_min_y = (1.0f - (max_y * 0.5f + 0.5f)) * screen_h;
  *out_max_y = (1.0f - (min_y * 0.5f + 0.5f)) * screen_h;
}
