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
} ShaderProgram;

typedef enum {
  RENDERER_SHADER_PBR = 0,
  RENDERER_SHADER_TOON = 1,
  RENDERER_SHADER_ANIME = 2
} RendererShaderMode;

typedef struct {
  ShaderProgram pbr;
  ShaderProgram toon;
  ShaderProgram anime;
  ShaderProgram *active;
  GLuint shadow_program;
  GLuint outline_program;

  /* Shadow shader uniforms */
  GLint u_shadow_view;
  GLint u_shadow_proj;
  GLint u_shadow_model;
  GLint u_shadow_bones;
  GLint u_shadow_use_skinning;
  GLint u_shadow_offset;
  GLint u_shadow_alpha;
  GLint u_shadow_scale;

  /* Outline shader uniforms */
  GLint u_outline_view;
  GLint u_outline_proj;
  GLint u_outline_model;
  GLint u_outline_bones;
  GLint u_outline_use_skinning;
  GLint u_outline_width;
  GLint u_outline_color;

  /* Loaded model */
  GltfModel *model;

  /* Bone matrices for skinning */
  float bone_matrices[200 * 16];

  RendererShaderMode shader_mode;
  bool initialized;
} Renderer;

bool renderer_init(Renderer *r);
void renderer_cleanup(Renderer *r);
void renderer_set_model(Renderer *r, GltfModel *model);
void renderer_update_bones(Renderer *r, int skin_index, const float *mesh_world);
void renderer_draw(Renderer *r, const OrbitCamera *camera);
void renderer_toggle_shader(Renderer *r);

#endif
