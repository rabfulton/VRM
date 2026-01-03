/*
 * VRM Overlay - Renderer Module
 */

#ifndef RENDERER_H
#define RENDERER_H

#include "gltf_loader.h"
#include <epoxy/gl.h>
#include <stdbool.h>

typedef struct OrbitCamera OrbitCamera;

typedef struct {
  GLuint program;
  GLuint shadow_program;

  /* Main shader uniforms */
  GLint u_view;
  GLint u_proj;
  GLint u_model;
  GLint u_bones;
  GLint u_use_skinning;
  GLint u_base_color;
  GLint u_base_color_tex;
  GLint u_has_base_color_tex;
  GLint u_alpha_mode;
  GLint u_alpha_cutoff;

  /* Shadow shader uniforms */
  GLint u_shadow_view;
  GLint u_shadow_proj;
  GLint u_shadow_model;
  GLint u_shadow_bones;
  GLint u_shadow_use_skinning;
  GLint u_shadow_offset;
  GLint u_shadow_alpha;

  /* Loaded model */
  GltfModel *model;

  /* Bone matrices for skinning */
  float bone_matrices[200 * 16];

  bool initialized;
} Renderer;

bool renderer_init(Renderer *r);
void renderer_cleanup(Renderer *r);
void renderer_set_model(Renderer *r, GltfModel *model);
void renderer_update_bones(Renderer *r, int skin_index, const float *mesh_world);
void renderer_draw(Renderer *r, const OrbitCamera *camera);

#endif
