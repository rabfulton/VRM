/*
 * VRM Overlay - Renderer Module
 */

#ifndef RENDERER_H
#define RENDERER_H

#include "gltf_loader.h"
#include <epoxy/gl.h>
#include <stdbool.h>

typedef struct OrbitCamera OrbitCamera;

#define RENDERER_MAX_BONES GLTF_MAX_PALETTE_BONES

typedef struct {
  GLuint program;
  GLint u_view;
  GLint u_proj;
  GLint u_model;
  GLint u_bones;
  GLint u_use_skinning;
  GLint u_use_bone_texture;
  GLint u_bone_tex;
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
  GLuint shadow_blur_program;
  GLuint shadow_comp_program;
  GLuint outline_program;

  /* Shadow shader uniforms */
  GLint u_shadow_view;
  GLint u_shadow_proj;
  GLint u_shadow_model;
  GLint u_shadow_bones;
  GLint u_shadow_use_skinning;
  GLint u_shadow_use_bone_texture;
  GLint u_shadow_bone_tex;
  GLint u_shadow_offset;
  GLint u_shadow_alpha;
  GLint u_shadow_scale;

  /* Shadow blur/composite uniforms */
  GLint u_shadow_blur_tex;
  GLint u_shadow_blur_texel;
  GLint u_shadow_blur_dir;
  GLint u_shadow_comp_tex;
  GLint u_shadow_comp_screen;
  GLint u_shadow_comp_offset;
  GLint u_shadow_comp_alpha;

  /* Outline shader uniforms */
  GLint u_outline_view;
  GLint u_outline_proj;
  GLint u_outline_model;
  GLint u_outline_bones;
  GLint u_outline_use_skinning;
  GLint u_outline_use_bone_texture;
  GLint u_outline_bone_tex;
  GLint u_outline_width;
  GLint u_outline_color;

  /* Loaded model */
  GltfModel *model;

  /* Bone matrices for skinning */
  float bone_matrices[RENDERER_MAX_BONES * 16];
  GLuint bone_tex;
  int bone_tex_joints;
  float *bone_tex_data;
  bool use_bone_texture;

  /* Model transform (position + rotation) */
  float model_transform[16];

  /* Shadow textures and quad */
  GLuint shadow_fbo;
  GLuint shadow_tex;
  GLuint shadow_blur_fbo;
  GLuint shadow_blur_tex;
  GLuint shadow_quad_vao;
  GLuint shadow_quad_vbo;
  int shadow_tex_w;
  int shadow_tex_h;

  RendererShaderMode shader_mode;
  bool initialized;
} Renderer;

bool renderer_init(Renderer *r);
void renderer_cleanup(Renderer *r);
void renderer_set_model(Renderer *r, GltfModel *model);
void renderer_draw(Renderer *r, const OrbitCamera *camera, int fb_width, int fb_height);
void renderer_set_model_transform(Renderer *r, const float *pos, const float *rot_deg, float scale);
void renderer_toggle_shader(Renderer *r);

#endif
