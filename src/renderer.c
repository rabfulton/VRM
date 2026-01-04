/*
 * VRM Overlay - Renderer Implementation
 */

#include "renderer.h"
#include "camera.h"
#include <cglm/cglm.h>
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
    "uniform float uShadowScale;\n"
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
    "    worldPos.xyz = worldPos.xyz * uShadowScale + uShadowOffset;\n"
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
    "    vec3 keyDir = normalize(vec3(0.2, 0.4, -0.9));\n"
    "    float key = max(dot(normal, keyDir), 0.0) * 0.5;\n"
    "    // Fill light (left side, softer)\n"
    "    vec3 fillDir = normalize(vec3(-0.7, 0.3, -0.5));\n"
    "    float fill = max(dot(normal, fillDir), 0.0) * 0.3;\n"
    "    // Ambient\n"
    "    float ambient = 0.4;\n"
    "    color.rgb *= (key + fill + ambient);\n"
    "    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;\n"
    "    FragColor = vec4(color.rgb * color.a, color.a);\n"
    "}\n";

static const char *toon_frag_src =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoord;\n"
    "in vec3 vWorldPos;\n"
    "uniform mat4 uView;\n"
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
    "    vec3 lightDir = normalize(vec3(0.2, 0.4, -0.9));\n"
    "    float ndotl = max(dot(normal, lightDir), 0.0);\n"
    "    float shade = 0.25;\n"
    "    if (ndotl > 0.8) {\n"
    "        shade = 1.0;\n"
    "    } else if (ndotl > 0.5) {\n"
    "        shade = 0.7;\n"
    "    } else if (ndotl > 0.25) {\n"
    "        shade = 0.45;\n"
    "    }\n"
    "    float ambient = 0.2;\n"
    "    vec3 viewPos = (uView * vec4(vWorldPos, 1.0)).xyz;\n"
    "    vec3 viewDir = normalize(-viewPos);\n"
    "    vec3 normalV = normalize((uView * vec4(normal, 0.0)).xyz);\n"
    "    float rim = pow(1.0 - max(dot(normalV, viewDir), 0.0), 2.0);\n"
    "    float rimStrength = 0.35;\n"
    "    color.rgb *= (shade + ambient);\n"
    "    color.rgb = mix(color.rgb, vec3(1.0), rim * rimStrength);\n"
    "    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;\n"
    "    FragColor = vec4(color.rgb * color.a, color.a);\n"
    "}\n";

/* Anime-style cel shader with smooth color bands and specular highlights */
static const char *anime_frag_src =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoord;\n"
    "in vec3 vWorldPos;\n"
    "uniform mat4 uView;\n"
    "uniform vec4 uBaseColor;\n"
    "uniform sampler2D uBaseColorTex;\n"
    "uniform bool uHasBaseColorTex;\n"
    "uniform int uAlphaMode;\n"
    "uniform float uAlphaCutoff;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec4 color = uBaseColor;\n"
    "    if (uHasBaseColorTex) color *= texture(uBaseColorTex, vTexCoord);\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(vec3(0.2, 0.4, -0.9));\n"
    "    float NdotL = dot(N, L);\n"
    /* Two-tone cel shading with soft edge */
    "    float shadow = smoothstep(-0.1, 0.1, NdotL);\n"
    "    vec3 shadowColor = color.rgb * 0.6;\n"
    "    vec3 litColor = color.rgb;\n"
    "    color.rgb = mix(shadowColor, litColor, shadow);\n"
    /* Specular highlight band */
    "    vec3 viewPos = (uView * vec4(vWorldPos, 1.0)).xyz;\n"
    "    vec3 V = normalize(-viewPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, H), 0.0), 40.0);\n"
    "    float specBand = smoothstep(0.4, 0.5, spec);\n"
    "    color.rgb += vec3(0.3) * specBand;\n"
    /* Rim light */
    "    vec3 Nv = normalize((uView * vec4(N, 0.0)).xyz);\n"
    "    float rim = 1.0 - max(dot(Nv, V), 0.0);\n"
    "    rim = smoothstep(0.6, 0.8, rim);\n"
    "    color.rgb += vec3(0.15) * rim;\n"
    "    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;\n"
    "    FragColor = vec4(color.rgb * color.a, color.a);\n"
    "}\n";

/* Outline vertex shader - extrudes along normals */
static const char *outline_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 3) in ivec4 aJoints;\n"
    "layout(location = 4) in vec4 aWeights;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uBones[200];\n"
    "uniform bool uUseSkinning;\n"
    "uniform float uOutlineWidth;\n"
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
    "    vec3 normal = mat3(transpose(inverse(uModel * skinMatrix))) * aNormal;\n"
    "    vec4 worldPos = uModel * skinMatrix * vec4(aPos, 1.0);\n"
    "    worldPos.xyz += normalize(normal) * uOutlineWidth;\n"
    "    gl_Position = uProj * uView * worldPos;\n"
    "}\n";

static const char *outline_frag_src =
    "#version 330 core\n"
    "uniform vec3 uOutlineColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(uOutlineColor, 1.0);\n"
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

static bool init_shader_program(ShaderProgram *program, const char *frag_src) {
  GLuint vert = compile_shader(GL_VERTEX_SHADER, skinned_vert_src);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
  if (!vert || !frag) {
    if (vert) glDeleteShader(vert);
    if (frag) glDeleteShader(frag);
    return false;
  }

  program->program = link_program(vert, frag);
  glDeleteShader(vert);
  glDeleteShader(frag);
  if (!program->program) return false;

  program->u_view = glGetUniformLocation(program->program, "uView");
  program->u_proj = glGetUniformLocation(program->program, "uProj");
  program->u_model = glGetUniformLocation(program->program, "uModel");
  program->u_bones = glGetUniformLocation(program->program, "uBones");
  program->u_use_skinning = glGetUniformLocation(program->program, "uUseSkinning");
  program->u_base_color = glGetUniformLocation(program->program, "uBaseColor");
  program->u_base_color_tex = glGetUniformLocation(program->program, "uBaseColorTex");
  program->u_has_base_color_tex = glGetUniformLocation(program->program, "uHasBaseColorTex");
  program->u_alpha_mode = glGetUniformLocation(program->program, "uAlphaMode");
  program->u_alpha_cutoff = glGetUniformLocation(program->program, "uAlphaCutoff");

  return true;
}

bool renderer_init(Renderer *r) {
  memset(r, 0, sizeof(Renderer));

  /* Identity model transform */
  r->model_transform[0] = r->model_transform[5] = r->model_transform[10] = r->model_transform[15] = 1.0f;

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_CULL_FACE);  /* Disable culling by default */

  /* Main shader */
  if (!init_shader_program(&r->pbr, pbr_frag_src)) {
    return false;
  }

  if (!init_shader_program(&r->toon, toon_frag_src)) {
    fprintf(stderr, "Failed to initialize toon shader, falling back to PBR\n");
    r->toon.program = 0;
  }

  if (!init_shader_program(&r->anime, anime_frag_src)) {
    fprintf(stderr, "Failed to initialize anime shader\n");
    r->anime.program = 0;
  }

  r->shader_mode = RENDERER_SHADER_ANIME;
  r->active = &r->anime;

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
      r->u_shadow_scale = glGetUniformLocation(r->shadow_program, "uShadowScale");
    }
  }
  if (shadow_vert) glDeleteShader(shadow_vert);
  if (shadow_frag) glDeleteShader(shadow_frag);

  /* Outline shader */
  GLuint outline_vert = compile_shader(GL_VERTEX_SHADER, outline_vert_src);
  GLuint outline_frag = compile_shader(GL_FRAGMENT_SHADER, outline_frag_src);
  if (outline_vert && outline_frag) {
    r->outline_program = link_program(outline_vert, outline_frag);
    if (r->outline_program) {
      r->u_outline_view = glGetUniformLocation(r->outline_program, "uView");
      r->u_outline_proj = glGetUniformLocation(r->outline_program, "uProj");
      r->u_outline_model = glGetUniformLocation(r->outline_program, "uModel");
      r->u_outline_bones = glGetUniformLocation(r->outline_program, "uBones");
      r->u_outline_use_skinning = glGetUniformLocation(r->outline_program, "uUseSkinning");
      r->u_outline_width = glGetUniformLocation(r->outline_program, "uOutlineWidth");
      r->u_outline_color = glGetUniformLocation(r->outline_program, "uOutlineColor");
    }
  }
  if (outline_vert) glDeleteShader(outline_vert);
  if (outline_frag) glDeleteShader(outline_frag);

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
  if (r->pbr.program) glDeleteProgram(r->pbr.program);
  if (r->toon.program) glDeleteProgram(r->toon.program);
  if (r->anime.program) glDeleteProgram(r->anime.program);
  if (r->shadow_program) glDeleteProgram(r->shadow_program);
  if (r->outline_program) glDeleteProgram(r->outline_program);
  memset(r, 0, sizeof(Renderer));
}

void renderer_set_model(Renderer *r, GltfModel *model) {
  r->model = model;
  if (model) {
    printf("Renderer: model set with %d meshes, %d nodes, %d skins\n",
           model->mesh_count, model->node_count, model->skin_count);
  }
}

static void mat4_mul(const float *a, const float *b, float *out) {
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += a[k * 4 + row] * b[col * 4 + k];
      }
      out[col * 4 + row] = sum;
    }
  }
}

void renderer_update_bones(Renderer *r, int skin_index, const float *mesh_world) {
  if (!r->model || skin_index < 0 || skin_index >= r->model->skin_count) return;
  if (!r->model->skins[skin_index].inverse_bind_matrices) return;
  if (!mesh_world) return;

  mat4 mesh;
  mat4 mesh_inv;
  memcpy(mesh, mesh_world, sizeof(mat4));
  glm_mat4_inv(mesh, mesh_inv);

  GltfSkin *skin = &r->model->skins[skin_index];

  for (int i = 0; i < skin->joint_count && i < 200; i++) {
    int node_idx = skin->joints[i];
    if (node_idx < 0 || node_idx >= r->model->node_count) continue;

    GltfNode *node = &r->model->nodes[node_idx];
    float *ibm = &skin->inverse_bind_matrices[i * 16];
    float *out = &r->bone_matrices[i * 16];

    /* bone_matrix = inverse(mesh_world) * joint_world * inverse_bind */
    float joint_ibm[16];
    mat4_mul(node->world_matrix, ibm, joint_ibm);
    mat4_mul((float *)mesh_inv, joint_ibm, out);
  }
}

static void draw_primitive(Renderer *r, GltfPrimitive *prim) {
  ShaderProgram *program = r->active;
  GltfMaterial *mat = NULL;
  if (prim->material_index >= 0 && prim->material_index < r->model->material_count) {
    mat = &r->model->materials[prim->material_index];
  }

  if (mat) {
    glUniform4fv(program->u_base_color, 1, mat->base_color);
    glUniform1i(program->u_alpha_mode, mat->alpha_mode);
    glUniform1f(program->u_alpha_cutoff, mat->alpha_cutoff);

    if (mat->base_color_texture >= 0 && mat->base_color_texture < r->model->texture_count) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, r->model->textures[mat->base_color_texture].id);
      glUniform1i(program->u_base_color_tex, 0);
      glUniform1i(program->u_has_base_color_tex, 1);
    } else {
      glUniform1i(program->u_has_base_color_tex, 0);
    }

    if (mat->double_sided) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
    }
  } else {
    float white[4] = {1, 1, 1, 1};
    glUniform4fv(program->u_base_color, 1, white);
    glUniform1i(program->u_alpha_mode, 0);
    glUniform1i(program->u_has_base_color_tex, 0);
    glDisable(GL_CULL_FACE);
  }

  glUniform1i(program->u_use_skinning, prim->has_skinning ? 1 : 0);

  glBindVertexArray(prim->vao);
  if (prim->ebo && prim->index_count > 0) {
    glDrawElements(GL_TRIANGLES, prim->index_count, prim->index_type, NULL);
  }
  glBindVertexArray(0);
}

void renderer_draw(Renderer *r, const OrbitCamera *camera) {
  if (!r->initialized) return;

  if (!r->model || !r->model->loaded) return;

  mat4 mt;
  memcpy(mt, r->model_transform, sizeof(mat4));

  /* === Draw outline pass for anime mode (back faces extruded) === */
  if (r->shader_mode == RENDERER_SHADER_ANIME && r->outline_program) {
    glUseProgram(r->outline_program);
    glUniformMatrix4fv(r->u_outline_view, 1, GL_FALSE, camera->view[0]);
    glUniformMatrix4fv(r->u_outline_proj, 1, GL_FALSE, camera->proj[0]);
    glUniform1f(r->u_outline_width, 0.002f);
    float outline_color[3] = {0.1f, 0.08f, 0.12f};
    glUniform3fv(r->u_outline_color, 1, outline_color);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    for (int i = 0; i < r->model->node_count; i++) {
      GltfNode *node = &r->model->nodes[i];
      if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

      mat4 combined;
      glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

      if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
        renderer_update_bones(r, node->skin_index, node->world_matrix);
        glUniformMatrix4fv(r->u_outline_bones, 200, GL_FALSE, r->bone_matrices);
      }
      glUniform1i(r->u_outline_use_skinning, node->skin_index >= 0 ? 1 : 0);
      glUniformMatrix4fv(r->u_outline_model, 1, GL_FALSE, (float*)combined);

      GltfMesh *mesh = &r->model->meshes[node->mesh_index];
      for (int j = 0; j < mesh->primitive_count; j++) {
        GltfPrimitive *prim = &mesh->primitives[j];
        GltfMaterial *mat = (prim->material_index >= 0 && prim->material_index < r->model->material_count)
                            ? &r->model->materials[prim->material_index] : NULL;
        if (mat && mat->alpha_mode == 2) continue; /* Skip transparent */
        glBindVertexArray(prim->vao);
        if (prim->ebo && prim->index_count > 0) {
          glDrawElements(GL_TRIANGLES, prim->index_count, prim->index_type, NULL);
        }
        glBindVertexArray(0);
      }
    }
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
  }

  /* === Draw shadow pass (behind model) === */
  if (r->shadow_program) {
    glUseProgram(r->shadow_program);
    glUniformMatrix4fv(r->u_shadow_view, 1, GL_FALSE, camera->view[0]);
    glUniformMatrix4fv(r->u_shadow_proj, 1, GL_FALSE, camera->proj[0]);
    
    /* Shadow offset: slightly back and down-right */
    float shadow_offset[3] = {0.08f, 0.02f, -0.15f};
    glUniform3fv(r->u_shadow_offset, 1, shadow_offset);
    glUniform1f(r->u_shadow_alpha, 0.15f);
    glUniform1f(r->u_shadow_scale, 0.9f);

    glDepthMask(GL_FALSE);

    for (int i = 0; i < r->model->node_count; i++) {
      GltfNode *node = &r->model->nodes[i];
      if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

      mat4 combined;
      glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

      if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
        renderer_update_bones(r, node->skin_index, node->world_matrix);
        glUniformMatrix4fv(r->u_shadow_bones, 200, GL_FALSE, r->bone_matrices);
      }
      glUniform1i(r->u_shadow_use_skinning, node->skin_index >= 0 ? 1 : 0);
      glUniformMatrix4fv(r->u_shadow_model, 1, GL_FALSE, (float*)combined);

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
  ShaderProgram *program = r->active;
  glUseProgram(program->program);
  glUniformMatrix4fv(program->u_view, 1, GL_FALSE, camera->view[0]);
  glUniformMatrix4fv(program->u_proj, 1, GL_FALSE, camera->proj[0]);

  for (int i = 0; i < r->model->node_count; i++) {
    GltfNode *node = &r->model->nodes[i];
    if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

    mat4 combined;
    glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

    if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
      renderer_update_bones(r, node->skin_index, node->world_matrix);
      glUniformMatrix4fv(program->u_bones, 200, GL_FALSE, r->bone_matrices);
    }

    glUniformMatrix4fv(program->u_model, 1, GL_FALSE, (float*)combined);

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

void renderer_toggle_shader(Renderer *r) {
  if (!r->initialized) return;

  if (r->shader_mode == RENDERER_SHADER_PBR) {
    if (r->toon.program) {
      r->shader_mode = RENDERER_SHADER_TOON;
      r->active = &r->toon;
      printf("Shader: toon\n");
    }
  } else if (r->shader_mode == RENDERER_SHADER_TOON) {
    if (r->anime.program) {
      r->shader_mode = RENDERER_SHADER_ANIME;
      r->active = &r->anime;
      printf("Shader: anime\n");
    } else {
      r->shader_mode = RENDERER_SHADER_ANIME;
      r->active = &r->anime;
      printf("Shader: PBR\n");
    }
  } else {
    r->shader_mode = RENDERER_SHADER_ANIME;
    r->active = &r->anime;
    printf("Shader: PBR\n");
  }
}

void renderer_set_model_transform(Renderer *r, const float *pos, const float *rot_deg) {
  /* Build transform: T * Ry * Rx * Rz (yaw, pitch, roll order) */
  float cx = cosf(rot_deg[0] * 0.0174533f), sx = sinf(rot_deg[0] * 0.0174533f);
  float cy = cosf(rot_deg[1] * 0.0174533f), sy = sinf(rot_deg[1] * 0.0174533f);
  float cz = cosf(rot_deg[2] * 0.0174533f), sz = sinf(rot_deg[2] * 0.0174533f);
  
  float *m = r->model_transform;
  m[0] = cy*cz + sy*sx*sz;  m[4] = -cy*sz + sy*sx*cz; m[8]  = sy*cx; m[12] = pos[0];
  m[1] = cx*sz;             m[5] = cx*cz;             m[9]  = -sx;   m[13] = pos[1];
  m[2] = -sy*cz + cy*sx*sz; m[6] = sy*sz + cy*sx*cz;  m[10] = cy*cx; m[14] = pos[2];
  m[3] = 0;                 m[7] = 0;                 m[11] = 0;     m[15] = 1;
}
