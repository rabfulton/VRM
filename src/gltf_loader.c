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
