/*
 * VRM Overlay - Renderer Implementation
 */

#include "renderer.h"
#include "camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Embedded shaders */
static const char *skinned_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 2) in vec2 aTexCoord;\n"
    "layout(location = 3) in ivec4 aJoints;\n"
    "layout(location = 4) in vec4 aWeights;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uBones[200];\n"
    "uniform bool uUseSkinning;\n"
    "out vec3 vNormal;\n"
    "out vec2 vTexCoord;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    mat4 skinMatrix;\n"
    "    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {\n"
    "        skinMatrix = aWeights.x * uBones[aJoints.x]\n"
    "                   + aWeights.y * uBones[aJoints.y]\n"
    "                   + aWeights.z * uBones[aJoints.z]\n"
    "                   + aWeights.w * uBones[aJoints.w];\n"
    "    } else {\n"
    "        skinMatrix = mat4(1.0);\n"
    "    }\n"
    "    vec4 worldPos = uModel * skinMatrix * vec4(aPos, 1.0);\n"
    "    vWorldPos = worldPos.xyz;\n"
    "    vNormal = mat3(transpose(inverse(uModel * skinMatrix))) * aNormal;\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = uProj * uView * worldPos;\n"
    "}\n";

/* Shadow pass - renders model as dark silhouette offset behind */
static const char *shadow_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 3) in ivec4 aJoints;\n"
    "layout(location = 4) in vec4 aWeights;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uBones[200];\n"
    "uniform bool uUseSkinning;\n"
    "uniform vec3 uShadowOffset;\n"
    "void main() {\n"
    "    mat4 skinMatrix;\n"
    "    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {\n"
    "        skinMatrix = aWeights.x * uBones[aJoints.x]\n"
    "                   + aWeights.y * uBones[aJoints.y]\n"
    "                   + aWeights.z * uBones[aJoints.z]\n"
    "                   + aWeights.w * uBones[aJoints.w];\n"
    "    } else {\n"
    "        skinMatrix = mat4(1.0);\n"
    "    }\n"
    "    vec4 worldPos = uModel * skinMatrix * vec4(aPos, 1.0);\n"
    "    worldPos.xyz += uShadowOffset;\n"
    "    gl_Position = uProj * uView * worldPos;\n"
    "}\n";

static const char *shadow_frag_src =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform float uShadowAlpha;\n"
    "void main() {\n"
    "    float a = uShadowAlpha;\n"
    "    FragColor = vec4(0.0, 0.0, 0.0, a);\n"
    "}\n";

static const char *pbr_frag_src =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoord;\n"
    "in vec3 vWorldPos;\n"
    "uniform vec4 uBaseColor;\n"
    "uniform sampler2D uBaseColorTex;\n"
    "uniform bool uHasBaseColorTex;\n"
    "uniform int uAlphaMode;\n"
    "uniform float uAlphaCutoff;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec4 color = uBaseColor;\n"
    "    if (uHasBaseColorTex) {\n"
    "        color *= texture(uBaseColorTex, vTexCoord);\n"
    "    }\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    // Key light (upper right front)\n"
    "    vec3 keyDir = normalize(vec3(0.5, 1.0, 0.8));\n"
    "    float key = max(dot(normal, keyDir), 0.0) * 0.7;\n"
    "    // Fill light (left side, softer)\n"
    "    vec3 fillDir = normalize(vec3(-0.7, 0.3, 0.5));\n"
    "    float fill = max(dot(normal, fillDir), 0.0) * 0.4;\n"
    "    // Ambient\n"
    "    float ambient = 0.5;\n"
    "    color.rgb *= (key + fill + ambient);\n"
    "    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;\n"
    "    FragColor = vec4(color.rgb * color.a, color.a);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "Shader error: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint link_program(GLuint vert, GLuint frag) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    fprintf(stderr, "Link error: %s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

bool renderer_init(Renderer *r) {
  memset(r, 0, sizeof(Renderer));

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDisable(GL_CULL_FACE);  /* Disable culling by default */

  /* Main shader */
  GLuint vert = compile_shader(GL_VERTEX_SHADER, skinned_vert_src);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, pbr_frag_src);
  if (!vert || !frag) {
    if (vert) glDeleteShader(vert);
    if (frag) glDeleteShader(frag);
    return false;
  }

  r->program = link_program(vert, frag);
  glDeleteShader(vert);
  glDeleteShader(frag);
  if (!r->program) return false;

  r->u_view = glGetUniformLocation(r->program, "uView");
  r->u_proj = glGetUniformLocation(r->program, "uProj");
  r->u_model = glGetUniformLocation(r->program, "uModel");
  r->u_bones = glGetUniformLocation(r->program, "uBones");
  r->u_use_skinning = glGetUniformLocation(r->program, "uUseSkinning");
  r->u_base_color = glGetUniformLocation(r->program, "uBaseColor");
  r->u_base_color_tex = glGetUniformLocation(r->program, "uBaseColorTex");
  r->u_has_base_color_tex = glGetUniformLocation(r->program, "uHasBaseColorTex");
  r->u_alpha_mode = glGetUniformLocation(r->program, "uAlphaMode");
  r->u_alpha_cutoff = glGetUniformLocation(r->program, "uAlphaCutoff");

  /* Shadow shader */
  GLuint shadow_vert = compile_shader(GL_VERTEX_SHADER, shadow_vert_src);
  GLuint shadow_frag = compile_shader(GL_FRAGMENT_SHADER, shadow_frag_src);
  if (shadow_vert && shadow_frag) {
    r->shadow_program = link_program(shadow_vert, shadow_frag);
    if (r->shadow_program) {
      r->u_shadow_view = glGetUniformLocation(r->shadow_program, "uView");
      r->u_shadow_proj = glGetUniformLocation(r->shadow_program, "uProj");
      r->u_shadow_model = glGetUniformLocation(r->shadow_program, "uModel");
      r->u_shadow_bones = glGetUniformLocation(r->shadow_program, "uBones");
      r->u_shadow_use_skinning = glGetUniformLocation(r->shadow_program, "uUseSkinning");
      r->u_shadow_offset = glGetUniformLocation(r->shadow_program, "uShadowOffset");
      r->u_shadow_alpha = glGetUniformLocation(r->shadow_program, "uShadowAlpha");
    }
  }
  if (shadow_vert) glDeleteShader(shadow_vert);
  if (shadow_frag) glDeleteShader(shadow_frag);

  /* Initialize bone matrices to identity */
  for (int i = 0; i < 200; i++) {
    float *m = &r->bone_matrices[i * 16];
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
  }

  r->initialized = true;
  printf("Renderer initialized\n");
  return true;
}

void renderer_cleanup(Renderer *r) {
  if (!r->initialized) return;
  if (r->program) glDeleteProgram(r->program);
  if (r->shadow_program) glDeleteProgram(r->shadow_program);
  memset(r, 0, sizeof(Renderer));
}

void renderer_set_model(Renderer *r, GltfModel *model) {
  r->model = model;
  if (model) {
    printf("Renderer: model set with %d meshes, %d nodes, %d skins\n",
           model->mesh_count, model->node_count, model->skin_count);
  }
}

void renderer_update_bones(Renderer *r, int skin_index) {
  if (!r->model || skin_index < 0 || skin_index >= r->model->skin_count) return;
  if (!r->model->skins[skin_index].inverse_bind_matrices) return;

  GltfSkin *skin = &r->model->skins[skin_index];

  for (int i = 0; i < skin->joint_count && i < 200; i++) {
    int node_idx = skin->joints[i];
    if (node_idx < 0 || node_idx >= r->model->node_count) continue;

    GltfNode *node = &r->model->nodes[node_idx];
    float *ibm = &skin->inverse_bind_matrices[i * 16];
    float *out = &r->bone_matrices[i * 16];

    /* bone_matrix = world_matrix * inverse_bind_matrix (column-major) */
    for (int col = 0; col < 4; col++) {
      for (int row = 0; row < 4; row++) {
        float sum = 0;
        for (int k = 0; k < 4; k++) {
          sum += node->world_matrix[k * 4 + row] * ibm[col * 4 + k];
        }
        out[col * 4 + row] = sum;
      }
    }
  }
}

static void draw_primitive(Renderer *r, GltfPrimitive *prim) {
  GltfMaterial *mat = NULL;
  if (prim->material_index >= 0 && prim->material_index < r->model->material_count) {
    mat = &r->model->materials[prim->material_index];
  }

  if (mat) {
    glUniform4fv(r->u_base_color, 1, mat->base_color);
    glUniform1i(r->u_alpha_mode, mat->alpha_mode);
    glUniform1f(r->u_alpha_cutoff, mat->alpha_cutoff);

    if (mat->base_color_texture >= 0 && mat->base_color_texture < r->model->texture_count) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, r->model->textures[mat->base_color_texture].id);
      glUniform1i(r->u_base_color_tex, 0);
      glUniform1i(r->u_has_base_color_tex, 1);
    } else {
      glUniform1i(r->u_has_base_color_tex, 0);
    }

    if (mat->double_sided) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
    }
  } else {
    float white[4] = {1, 1, 1, 1};
    glUniform4fv(r->u_base_color, 1, white);
    glUniform1i(r->u_alpha_mode, 0);
    glUniform1i(r->u_has_base_color_tex, 0);
    glDisable(GL_CULL_FACE);
  }

  glUniform1i(r->u_use_skinning, prim->has_skinning ? 1 : 0);

  glBindVertexArray(prim->vao);
  if (prim->ebo && prim->index_count > 0) {
    glDrawElements(GL_TRIANGLES, prim->index_count, prim->index_type, NULL);
  }
  glBindVertexArray(0);
}

void renderer_draw(Renderer *r, const OrbitCamera *camera) {
  if (!r->initialized) return;

  if (!r->model || !r->model->loaded) return;

  /* === Draw shadow pass first (behind model) === */
  if (r->shadow_program) {
    glUseProgram(r->shadow_program);
    glUniformMatrix4fv(r->u_shadow_view, 1, GL_FALSE, camera->view[0]);
    glUniformMatrix4fv(r->u_shadow_proj, 1, GL_FALSE, camera->proj[0]);
    
    /* Shadow offset: slightly back and down-right */
    float shadow_offset[3] = {0.08f, -0.05f, -0.15f};
    glUniform3fv(r->u_shadow_offset, 1, shadow_offset);
    glUniform1f(r->u_shadow_alpha, 0.3f);

    glDepthMask(GL_FALSE);

    for (int i = 0; i < r->model->node_count; i++) {
      GltfNode *node = &r->model->nodes[i];
      if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

      if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
        renderer_update_bones(r, node->skin_index);
        glUniformMatrix4fv(r->u_shadow_bones, 200, GL_FALSE, r->bone_matrices);
      }
      glUniform1i(r->u_shadow_use_skinning, node->skin_index >= 0 ? 1 : 0);
      glUniformMatrix4fv(r->u_shadow_model, 1, GL_FALSE, node->world_matrix);

      GltfMesh *mesh = &r->model->meshes[node->mesh_index];
      for (int j = 0; j < mesh->primitive_count; j++) {
        GltfPrimitive *prim = &mesh->primitives[j];
        glBindVertexArray(prim->vao);
        if (prim->ebo && prim->index_count > 0) {
          glDrawElements(GL_TRIANGLES, prim->index_count, prim->index_type, NULL);
        }
        glBindVertexArray(0);
      }
    }
    glDepthMask(GL_TRUE);
  }

  /* === Draw main model === */
  glUseProgram(r->program);
  glUniformMatrix4fv(r->u_view, 1, GL_FALSE, camera->view[0]);
  glUniformMatrix4fv(r->u_proj, 1, GL_FALSE, camera->proj[0]);

  for (int i = 0; i < r->model->node_count; i++) {
    GltfNode *node = &r->model->nodes[i];
    if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

    if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
      renderer_update_bones(r, node->skin_index);
      glUniformMatrix4fv(r->u_bones, 200, GL_FALSE, r->bone_matrices);
    }

    glUniformMatrix4fv(r->u_model, 1, GL_FALSE, node->world_matrix);

    GltfMesh *mesh = &r->model->meshes[node->mesh_index];

    /* Draw opaque first */
    for (int j = 0; j < mesh->primitive_count; j++) {
      GltfPrimitive *prim = &mesh->primitives[j];
      GltfMaterial *mat = (prim->material_index >= 0 && prim->material_index < r->model->material_count) 
                          ? &r->model->materials[prim->material_index] : NULL;
      if (!mat || mat->alpha_mode != 2) {
        glDepthMask(GL_TRUE);
        draw_primitive(r, prim);
      }
    }

    /* Draw transparent */
    for (int j = 0; j < mesh->primitive_count; j++) {
      GltfPrimitive *prim = &mesh->primitives[j];
      GltfMaterial *mat = (prim->material_index >= 0 && prim->material_index < r->model->material_count) 
                          ? &r->model->materials[prim->material_index] : NULL;
      if (mat && mat->alpha_mode == 2) {
        glDepthMask(GL_FALSE);
        draw_primitive(r, prim);
      }
    }
  }

  glDepthMask(GL_TRUE);
}
