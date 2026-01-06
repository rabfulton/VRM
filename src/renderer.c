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
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
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
    "uniform mat4 uBones[" STR(RENDERER_MAX_BONES) "];\n"
    "uniform bool uUseSkinning;\n"
    "uniform bool uUseBoneTexture;\n"
    "uniform sampler2D uBoneTex;\n"
    "out vec3 vNormal;\n"
    "out vec2 vTexCoord;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    mat4 skinMatrix;\n"
    "    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {\n"
    "        if (uUseBoneTexture) {\n"
    "            ivec2 row = ivec2(0, aJoints.x);\n"
    "            mat4 b0 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.y);\n"
    "            mat4 b1 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.z);\n"
    "            mat4 b2 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.w);\n"
    "            mat4 b3 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            skinMatrix = aWeights.x * b0 + aWeights.y * b1 + aWeights.z * b2 + aWeights.w * b3;\n"
    "        } else {\n"
    "            skinMatrix = aWeights.x * uBones[aJoints.x]\n"
    "                       + aWeights.y * uBones[aJoints.y]\n"
    "                       + aWeights.z * uBones[aJoints.z]\n"
    "                       + aWeights.w * uBones[aJoints.w];\n"
    "        }\n"
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
    "uniform mat4 uBones[" STR(RENDERER_MAX_BONES) "];\n"
    "uniform bool uUseSkinning;\n"
    "uniform bool uUseBoneTexture;\n"
    "uniform sampler2D uBoneTex;\n"
    "uniform vec3 uShadowOffset;\n"
    "uniform float uShadowScale;\n"
    "void main() {\n"
    "    mat4 skinMatrix;\n"
    "    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {\n"
    "        if (uUseBoneTexture) {\n"
    "            ivec2 row = ivec2(0, aJoints.x);\n"
    "            mat4 b0 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.y);\n"
    "            mat4 b1 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.z);\n"
    "            mat4 b2 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.w);\n"
    "            mat4 b3 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            skinMatrix = aWeights.x * b0 + aWeights.y * b1 + aWeights.z * b2 + aWeights.w * b3;\n"
    "        } else {\n"
    "            skinMatrix = aWeights.x * uBones[aJoints.x]\n"
    "                       + aWeights.y * uBones[aJoints.y]\n"
    "                       + aWeights.z * uBones[aJoints.z]\n"
    "                       + aWeights.w * uBones[aJoints.w];\n"
    "        }\n"
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
    "    FragColor = vec4(a, a, a, a);\n"
    "}\n";

static const char *shadow_blur_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUV;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *shadow_blur_frag_src =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexelSize;\n"
    "uniform vec2 uDirection;\n"
    "void main() {\n"
    "    vec2 off = uDirection * uTexelSize;\n"
    "    vec4 c = texture(uTex, vUV) * 0.227027;\n"
    "    c += texture(uTex, vUV + off * 1.384615) * 0.316216;\n"
    "    c += texture(uTex, vUV - off * 1.384615) * 0.316216;\n"
    "    c += texture(uTex, vUV + off * 3.230769) * 0.070270;\n"
    "    c += texture(uTex, vUV - off * 3.230769) * 0.070270;\n"
    "    FragColor = c;\n"
    "}\n";

static const char *shadow_comp_frag_src =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uScreenSize;\n"
    "uniform vec2 uOffsetPx;\n"
    "uniform float uShadowAlpha;\n"
    "void main() {\n"
    "    vec2 uv = vUV + uOffsetPx / uScreenSize;\n"
    "    float a = texture(uTex, uv).r * uShadowAlpha;\n"
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
    "    vec3 normal = normalize(gl_FrontFacing ? vNormal : -vNormal);\n"
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
    "    FragColor = color;\n"
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
    "    vec3 normal = normalize(gl_FrontFacing ? vNormal : -vNormal);\n"
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
    "    FragColor = color;\n"
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
    "    vec3 N = normalize(gl_FrontFacing ? vNormal : -vNormal);\n"
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
    "    FragColor = color;\n"
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
    "uniform mat4 uBones[" STR(RENDERER_MAX_BONES) "];\n"
    "uniform bool uUseSkinning;\n"
    "uniform bool uUseBoneTexture;\n"
    "uniform sampler2D uBoneTex;\n"
    "uniform float uOutlineWidth;\n"
    "void main() {\n"
    "    mat4 skinMatrix;\n"
    "    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {\n"
    "        if (uUseBoneTexture) {\n"
    "            ivec2 row = ivec2(0, aJoints.x);\n"
    "            mat4 b0 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.y);\n"
    "            mat4 b1 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.z);\n"
    "            mat4 b2 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            row = ivec2(0, aJoints.w);\n"
    "            mat4 b3 = mat4(texelFetch(uBoneTex, row, 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(1,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(2,0), 0),\n"
    "                          texelFetch(uBoneTex, row + ivec2(3,0), 0));\n"
    "            skinMatrix = aWeights.x * b0 + aWeights.y * b1 + aWeights.z * b2 + aWeights.w * b3;\n"
    "        } else {\n"
    "            skinMatrix = aWeights.x * uBones[aJoints.x]\n"
    "                       + aWeights.y * uBones[aJoints.y]\n"
    "                       + aWeights.z * uBones[aJoints.z]\n"
    "                       + aWeights.w * uBones[aJoints.w];\n"
    "        }\n"
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

static void init_shadow_quad(Renderer *r) {
  if (r->shadow_quad_vao) return;
  float quad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f
  };
  glGenVertexArrays(1, &r->shadow_quad_vao);
  glGenBuffers(1, &r->shadow_quad_vbo);
  glBindVertexArray(r->shadow_quad_vao);
  glBindBuffer(GL_ARRAY_BUFFER, r->shadow_quad_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

static void ensure_shadow_targets(Renderer *r, int w, int h) {
  int tw = w / 2;
  int th = h / 2;
  if (tw < 128) tw = 128;
  if (th < 128) th = 128;
  if (tw > 1024) tw = 1024;
  if (th > 1024) th = 1024;

  if (r->shadow_tex_w == tw && r->shadow_tex_h == th && r->shadow_tex && r->shadow_blur_tex) {
    return;
  }

  r->shadow_tex_w = tw;
  r->shadow_tex_h = th;

  if (!r->shadow_tex) glGenTextures(1, &r->shadow_tex);
  glBindTexture(GL_TEXTURE_2D, r->shadow_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (!r->shadow_blur_tex) glGenTextures(1, &r->shadow_blur_tex);
  glBindTexture(GL_TEXTURE_2D, r->shadow_blur_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!r->shadow_fbo) glGenFramebuffers(1, &r->shadow_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->shadow_tex, 0);

  if (!r->shadow_blur_fbo) glGenFramebuffers(1, &r->shadow_blur_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_blur_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->shadow_blur_tex, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
  program->u_use_bone_texture = glGetUniformLocation(program->program, "uUseBoneTexture");
  program->u_bone_tex = glGetUniformLocation(program->program, "uBoneTex");
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
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
      r->u_shadow_use_bone_texture = glGetUniformLocation(r->shadow_program, "uUseBoneTexture");
      r->u_shadow_bone_tex = glGetUniformLocation(r->shadow_program, "uBoneTex");
      r->u_shadow_offset = glGetUniformLocation(r->shadow_program, "uShadowOffset");
      r->u_shadow_alpha = glGetUniformLocation(r->shadow_program, "uShadowAlpha");
      r->u_shadow_scale = glGetUniformLocation(r->shadow_program, "uShadowScale");
    }
  }
  if (shadow_vert) glDeleteShader(shadow_vert);
  if (shadow_frag) glDeleteShader(shadow_frag);

  /* Shadow blur/composite shaders */
  {
    GLuint blur_vert = compile_shader(GL_VERTEX_SHADER, shadow_blur_vert_src);
    GLuint blur_frag = compile_shader(GL_FRAGMENT_SHADER, shadow_blur_frag_src);
    if (blur_vert && blur_frag) {
      r->shadow_blur_program = link_program(blur_vert, blur_frag);
      if (r->shadow_blur_program) {
        r->u_shadow_blur_tex = glGetUniformLocation(r->shadow_blur_program, "uTex");
        r->u_shadow_blur_texel = glGetUniformLocation(r->shadow_blur_program, "uTexelSize");
        r->u_shadow_blur_dir = glGetUniformLocation(r->shadow_blur_program, "uDirection");
      }
    }
    if (blur_vert) glDeleteShader(blur_vert);
    if (blur_frag) glDeleteShader(blur_frag);
  }
  {
    GLuint comp_vert = compile_shader(GL_VERTEX_SHADER, shadow_blur_vert_src);
    GLuint comp_frag = compile_shader(GL_FRAGMENT_SHADER, shadow_comp_frag_src);
    if (comp_vert && comp_frag) {
      r->shadow_comp_program = link_program(comp_vert, comp_frag);
      if (r->shadow_comp_program) {
        r->u_shadow_comp_tex = glGetUniformLocation(r->shadow_comp_program, "uTex");
        r->u_shadow_comp_screen = glGetUniformLocation(r->shadow_comp_program, "uScreenSize");
        r->u_shadow_comp_offset = glGetUniformLocation(r->shadow_comp_program, "uOffsetPx");
        r->u_shadow_comp_alpha = glGetUniformLocation(r->shadow_comp_program, "uShadowAlpha");
      }
    }
    if (comp_vert) glDeleteShader(comp_vert);
    if (comp_frag) glDeleteShader(comp_frag);
  }

  init_shadow_quad(r);

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
      r->u_outline_use_bone_texture = glGetUniformLocation(r->outline_program, "uUseBoneTexture");
      r->u_outline_bone_tex = glGetUniformLocation(r->outline_program, "uBoneTex");
      r->u_outline_width = glGetUniformLocation(r->outline_program, "uOutlineWidth");
      r->u_outline_color = glGetUniformLocation(r->outline_program, "uOutlineColor");
    }
  }
  if (outline_vert) glDeleteShader(outline_vert);
  if (outline_frag) glDeleteShader(outline_frag);

  /* Initialize bone matrices to identity */
  for (int i = 0; i < RENDERER_MAX_BONES; i++) {
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
  if (r->shadow_blur_program) glDeleteProgram(r->shadow_blur_program);
  if (r->shadow_comp_program) glDeleteProgram(r->shadow_comp_program);
  if (r->outline_program) glDeleteProgram(r->outline_program);
  if (r->bone_tex) glDeleteTextures(1, &r->bone_tex);
  if (r->shadow_tex) glDeleteTextures(1, &r->shadow_tex);
  if (r->shadow_blur_tex) glDeleteTextures(1, &r->shadow_blur_tex);
  if (r->shadow_fbo) glDeleteFramebuffers(1, &r->shadow_fbo);
  if (r->shadow_blur_fbo) glDeleteFramebuffers(1, &r->shadow_blur_fbo);
  if (r->shadow_quad_vao) glDeleteVertexArrays(1, &r->shadow_quad_vao);
  if (r->shadow_quad_vbo) glDeleteBuffers(1, &r->shadow_quad_vbo);
  free(r->bone_tex_data);
  memset(r, 0, sizeof(Renderer));
}

void renderer_set_model(Renderer *r, GltfModel *model) {
  r->model = model;
  if (model) {
    printf("Renderer: model set with %d meshes, %d nodes, %d skins\n",
           model->mesh_count, model->node_count, model->skin_count);
    int max_joints = 0;
    for (int i = 0; i < model->skin_count; i++) {
      if (model->skins[i].joint_count > max_joints) {
        max_joints = model->skins[i].joint_count;
      }
    }
    if (max_joints > RENDERER_MAX_BONES) {
      r->use_bone_texture = true;
      r->bone_tex_joints = max_joints;
      free(r->bone_tex_data);
      r->bone_tex_data = (float *)malloc((size_t)r->bone_tex_joints * 16 * sizeof(float));
      if (!r->bone_tex) {
        glGenTextures(1, &r->bone_tex);
      }
      glBindTexture(GL_TEXTURE_2D, r->bone_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, r->bone_tex_joints, 0,
                   GL_RGBA, GL_FLOAT, NULL);
      glBindTexture(GL_TEXTURE_2D, 0);
      printf("Renderer: bone texture enabled (%d joints)\n", r->bone_tex_joints);
    } else {
      r->use_bone_texture = false;
      r->bone_tex_joints = 0;
      if (r->bone_tex) {
        glDeleteTextures(1, &r->bone_tex);
        r->bone_tex = 0;
      }
      free(r->bone_tex_data);
      r->bone_tex_data = NULL;
    }
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

static void renderer_update_bone_texture(Renderer *r, int skin_index,
                                         const float *mesh_world) {
  if (!r->model || skin_index < 0 || skin_index >= r->model->skin_count) return;
  if (!r->model->skins[skin_index].inverse_bind_matrices) return;
  if (!mesh_world || !r->use_bone_texture || !r->bone_tex_data) return;

  mat4 mesh;
  mat4 mesh_inv;
  memcpy(mesh, mesh_world, sizeof(mat4));
  glm_mat4_inv(mesh, mesh_inv);

  GltfSkin *skin = &r->model->skins[skin_index];
  int joint_count = skin->joint_count;
  if (joint_count > r->bone_tex_joints) {
    joint_count = r->bone_tex_joints;
  }

  for (int i = 0; i < joint_count; i++) {
    int node_idx = skin->joints[i];
    if (node_idx < 0 || node_idx >= r->model->node_count) {
      float *m = &r->bone_tex_data[i * 16];
      memset(m, 0, 16 * sizeof(float));
      m[0] = m[5] = m[10] = m[15] = 1.0f;
      continue;
    }

    GltfNode *node = &r->model->nodes[node_idx];
    float *ibm = &skin->inverse_bind_matrices[i * 16];
    float *out = &r->bone_tex_data[i * 16];

    float joint_ibm[16];
    mat4_mul(node->world_matrix, ibm, joint_ibm);
    mat4_mul((float *)mesh_inv, joint_ibm, out);
  }

  glBindTexture(GL_TEXTURE_2D, r->bone_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, joint_count, GL_RGBA, GL_FLOAT,
                  r->bone_tex_data);
}

static void renderer_update_bones(Renderer *r, int skin_index,
                                  const float *mesh_world,
                                  const GltfPrimitive *prim) {
  if (!r->model || skin_index < 0 || skin_index >= r->model->skin_count) return;
  if (!r->model->skins[skin_index].inverse_bind_matrices) return;
  if (!mesh_world || !prim || prim->joint_palette_count <= 0) return;

  mat4 mesh;
  mat4 mesh_inv;
  memcpy(mesh, mesh_world, sizeof(mat4));
  glm_mat4_inv(mesh, mesh_inv);

  GltfSkin *skin = &r->model->skins[skin_index];

  for (int i = 0; i < RENDERER_MAX_BONES; i++) {
    float *m = &r->bone_matrices[i * 16];
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
  }

  int palette_count = prim->joint_palette_count;
  if (palette_count > RENDERER_MAX_BONES) {
    palette_count = RENDERER_MAX_BONES;
  }

  for (int i = 0; i < palette_count; i++) {
    int joint_idx = prim->joint_palette[i];
    if (joint_idx < 0 || joint_idx >= skin->joint_count) continue;
    int node_idx = skin->joints[joint_idx];
    if (node_idx < 0 || node_idx >= r->model->node_count) continue;

    GltfNode *node = &r->model->nodes[node_idx];
    float *ibm = &skin->inverse_bind_matrices[joint_idx * 16];
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

    if (mat->double_sided || mat->alpha_mode == 2) {
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

  glBindVertexArray(prim->vao);
  if (prim->ebo && prim->index_count > 0) {
    glDrawElements(prim->mode, prim->index_count, prim->index_type, NULL);
  } else if (prim->vertex_count > 0) {
    glDrawArrays(prim->mode, 0, (GLsizei)prim->vertex_count);
  }
  glBindVertexArray(0);
}

void renderer_draw(Renderer *r, const OrbitCamera *camera, int fb_width, int fb_height) {
  if (!r->initialized) return;

  if (!r->model || !r->model->loaded) return;

  mat4 mt;
  memcpy(mt, r->model_transform, sizeof(mat4));

  /* === Render silhouette to texture + blur + composite === */
  if (r->shadow_program && r->shadow_blur_program && r->shadow_comp_program && fb_width > 0 && fb_height > 0) {
    ensure_shadow_targets(r, fb_width, fb_height);
    if (r->shadow_tex && r->shadow_blur_tex && r->shadow_fbo && r->shadow_blur_fbo) {
      GLint prev_fbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

      GLboolean had_depth = glIsEnabled(GL_DEPTH_TEST);
      GLboolean had_blend = glIsEnabled(GL_BLEND);

      glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
      glViewport(0, 0, r->shadow_tex_w, r->shadow_tex_h);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);

      glUseProgram(r->shadow_program);
      glUniformMatrix4fv(r->u_shadow_view, 1, GL_FALSE, camera->view[0]);
      glUniformMatrix4fv(r->u_shadow_proj, 1, GL_FALSE, camera->proj[0]);
      if (r->use_bone_texture && r->bone_tex && r->u_shadow_bone_tex >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->bone_tex);
        glUniform1i(r->u_shadow_bone_tex, 1);
      }
      float shadow_offset[3] = {0.0f, 0.0f, 0.0f};
      glUniform3fv(r->u_shadow_offset, 1, shadow_offset);
      glUniform1f(r->u_shadow_alpha, 1.0f);
      glUniform1f(r->u_shadow_scale, 1.0f);

      for (int i = 0; i < r->model->node_count; i++) {
        GltfNode *node = &r->model->nodes[i];
        if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

        mat4 combined;
        glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

        if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
          if (r->use_bone_texture) {
            renderer_update_bone_texture(r, node->skin_index, node->world_matrix);
          }
        }
        glUniform1i(r->u_shadow_use_skinning, 0);
        glUniform1i(r->u_shadow_use_bone_texture, 0);
        glUniformMatrix4fv(r->u_shadow_model, 1, GL_FALSE, (float*)combined);

        GltfMesh *mesh = &r->model->meshes[node->mesh_index];
        for (int j = 0; j < mesh->primitive_count; j++) {
          GltfPrimitive *prim = &mesh->primitives[j];
          if (node->skin_index >= 0 && node->skin_index < r->model->skin_count && prim->has_skinning) {
            if (prim->joint_palette_count > 0) {
              renderer_update_bones(r, node->skin_index, node->world_matrix, prim);
              glUniformMatrix4fv(r->u_shadow_bones, RENDERER_MAX_BONES, GL_FALSE, r->bone_matrices);
              glUniform1i(r->u_shadow_use_skinning, 1);
              glUniform1i(r->u_shadow_use_bone_texture, 0);
            } else if (r->use_bone_texture) {
              glUniform1i(r->u_shadow_use_skinning, 1);
              glUniform1i(r->u_shadow_use_bone_texture, 1);
            } else {
              glUniform1i(r->u_shadow_use_skinning, 0);
              glUniform1i(r->u_shadow_use_bone_texture, 0);
            }
          } else {
            glUniform1i(r->u_shadow_use_skinning, 0);
            glUniform1i(r->u_shadow_use_bone_texture, 0);
          }
          glBindVertexArray(prim->vao);
          if (prim->ebo && prim->index_count > 0) {
            glDrawElements(prim->mode, prim->index_count, prim->index_type, NULL);
          } else if (prim->vertex_count > 0) {
            glDrawArrays(prim->mode, 0, (GLsizei)prim->vertex_count);
          }
          glBindVertexArray(0);
        }
      }

      glUseProgram(r->shadow_blur_program);
      glBindVertexArray(r->shadow_quad_vao);
      glUniform1i(r->u_shadow_blur_tex, 0);
      glUniform2f(r->u_shadow_blur_texel, 1.0f / (float)r->shadow_tex_w, 1.0f / (float)r->shadow_tex_h);

      glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_blur_fbo);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, r->shadow_tex);
      glUniform2f(r->u_shadow_blur_dir, 1.0f, 0.0f);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
      glBindTexture(GL_TEXTURE_2D, r->shadow_blur_tex);
      glUniform2f(r->u_shadow_blur_dir, 0.0f, 1.0f);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      glBindVertexArray(0);

      glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
      glViewport(0, 0, fb_width, fb_height);

      glUseProgram(r->shadow_comp_program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, r->shadow_tex);
      glUniform1i(r->u_shadow_comp_tex, 0);
      glUniform2f(r->u_shadow_comp_screen, (float)fb_width, (float)fb_height);
      glUniform2f(r->u_shadow_comp_offset, -25.0f, -5.0f);
      glUniform1f(r->u_shadow_comp_alpha, 0.4f);

      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      glBindVertexArray(r->shadow_quad_vao);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      glBindVertexArray(0);

      if (had_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      if (had_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    }
  }

  /* === Draw outline pass for anime mode (back faces extruded) === */
  if (r->shader_mode == RENDERER_SHADER_ANIME && r->outline_program) {
    glUseProgram(r->outline_program);
    glUniformMatrix4fv(r->u_outline_view, 1, GL_FALSE, camera->view[0]);
    glUniformMatrix4fv(r->u_outline_proj, 1, GL_FALSE, camera->proj[0]);
    glUniform1f(r->u_outline_width, 0.002f);
    float outline_color[3] = {0.1f, 0.08f, 0.12f};
    glUniform3fv(r->u_outline_color, 1, outline_color);
    if (r->use_bone_texture && r->bone_tex && r->u_outline_bone_tex >= 0) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, r->bone_tex);
      glUniform1i(r->u_outline_bone_tex, 1);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    for (int i = 0; i < r->model->node_count; i++) {
      GltfNode *node = &r->model->nodes[i];
      if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

      mat4 combined;
      glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

      if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
        if (r->use_bone_texture) {
          renderer_update_bone_texture(r, node->skin_index, node->world_matrix);
        }
      }
      glUniform1i(r->u_outline_use_skinning, 0);
      glUniform1i(r->u_outline_use_bone_texture, 0);
      glUniformMatrix4fv(r->u_outline_model, 1, GL_FALSE, (float*)combined);

      GltfMesh *mesh = &r->model->meshes[node->mesh_index];
      for (int j = 0; j < mesh->primitive_count; j++) {
        GltfPrimitive *prim = &mesh->primitives[j];
        GltfMaterial *mat = (prim->material_index >= 0 && prim->material_index < r->model->material_count)
                            ? &r->model->materials[prim->material_index] : NULL;
        if (mat && mat->alpha_mode == 2) continue; /* Skip transparent */
        if (node->skin_index >= 0 && node->skin_index < r->model->skin_count && prim->has_skinning) {
          if (prim->joint_palette_count > 0) {
            renderer_update_bones(r, node->skin_index, node->world_matrix, prim);
            glUniformMatrix4fv(r->u_outline_bones, RENDERER_MAX_BONES, GL_FALSE, r->bone_matrices);
            glUniform1i(r->u_outline_use_skinning, 1);
            glUniform1i(r->u_outline_use_bone_texture, 0);
          } else if (r->use_bone_texture) {
            glUniform1i(r->u_outline_use_skinning, 1);
            glUniform1i(r->u_outline_use_bone_texture, 1);
          } else {
            glUniform1i(r->u_outline_use_skinning, 0);
            glUniform1i(r->u_outline_use_bone_texture, 0);
          }
        } else {
          glUniform1i(r->u_outline_use_skinning, 0);
          glUniform1i(r->u_outline_use_bone_texture, 0);
        }
        glBindVertexArray(prim->vao);
        if (prim->ebo && prim->index_count > 0) {
          glDrawElements(prim->mode, prim->index_count, prim->index_type, NULL);
        } else if (prim->vertex_count > 0) {
          glDrawArrays(prim->mode, 0, (GLsizei)prim->vertex_count);
        }
        glBindVertexArray(0);
      }
    }
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
  }

  /* === Draw main model === */
  ShaderProgram *program = r->active;
  glUseProgram(program->program);
  glUniformMatrix4fv(program->u_view, 1, GL_FALSE, camera->view[0]);
  glUniformMatrix4fv(program->u_proj, 1, GL_FALSE, camera->proj[0]);
  if (r->use_bone_texture && r->bone_tex && program->u_bone_tex >= 0) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->bone_tex);
    glUniform1i(program->u_bone_tex, 1);
  }

  for (int i = 0; i < r->model->node_count; i++) {
    GltfNode *node = &r->model->nodes[i];
    if (node->mesh_index < 0 || node->mesh_index >= r->model->mesh_count) continue;

    mat4 combined;
    glm_mat4_mul(mt, (vec4*)node->world_matrix, combined);

    if (node->skin_index >= 0 && node->skin_index < r->model->skin_count) {
      if (r->use_bone_texture) {
        renderer_update_bone_texture(r, node->skin_index, node->world_matrix);
      }
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
        if (node->skin_index >= 0 && node->skin_index < r->model->skin_count && prim->has_skinning) {
          if (prim->joint_palette_count > 0) {
            renderer_update_bones(r, node->skin_index, node->world_matrix, prim);
            glUniformMatrix4fv(program->u_bones, RENDERER_MAX_BONES, GL_FALSE, r->bone_matrices);
            glUniform1i(program->u_use_skinning, 1);
            glUniform1i(program->u_use_bone_texture, 0);
          } else if (r->use_bone_texture) {
            glUniform1i(program->u_use_skinning, 1);
            glUniform1i(program->u_use_bone_texture, 1);
          } else {
            glUniform1i(program->u_use_skinning, 0);
            glUniform1i(program->u_use_bone_texture, 0);
          }
        } else {
          glUniform1i(program->u_use_skinning, 0);
          glUniform1i(program->u_use_bone_texture, 0);
        }
        draw_primitive(r, prim);
      }
    }

    /* Draw transparent */
    for (int j = 0; j < mesh->primitive_count; j++) {
      GltfPrimitive *prim = &mesh->primitives[j];
      GltfMaterial *mat = (prim->material_index >= 0 && prim->material_index < r->model->material_count) 
                          ? &r->model->materials[prim->material_index] : NULL;
      if (mat && mat->alpha_mode == 2) {
        glDepthMask(mat->alpha_write_depth ? GL_TRUE : GL_FALSE);
        if (node->skin_index >= 0 && node->skin_index < r->model->skin_count && prim->has_skinning) {
          if (prim->joint_palette_count > 0) {
            renderer_update_bones(r, node->skin_index, node->world_matrix, prim);
            glUniformMatrix4fv(program->u_bones, RENDERER_MAX_BONES, GL_FALSE, r->bone_matrices);
            glUniform1i(program->u_use_skinning, 1);
            glUniform1i(program->u_use_bone_texture, 0);
          } else if (r->use_bone_texture) {
            glUniform1i(program->u_use_skinning, 1);
            glUniform1i(program->u_use_bone_texture, 1);
          } else {
            glUniform1i(program->u_use_skinning, 0);
            glUniform1i(program->u_use_bone_texture, 0);
          }
        } else {
          glUniform1i(program->u_use_skinning, 0);
          glUniform1i(program->u_use_bone_texture, 0);
        }
        draw_primitive(r, prim);
      }
    }
  }

  glDepthMask(GL_TRUE);
}

void renderer_toggle_shader(Renderer *r) {
  if (!r->initialized) return;

  if (r->shader_mode == RENDERER_SHADER_ANIME) {
    if (r->toon.program) {
      r->shader_mode = RENDERER_SHADER_TOON;
      r->active = &r->toon;
      printf("Shader: toon\n");
    }
  } else if (r->shader_mode == RENDERER_SHADER_TOON) {
    if (r->pbr.program) {
      r->shader_mode = RENDERER_SHADER_PBR;
      r->active = &r->pbr;
      printf("Shader: PBR\n");
    } else {
      r->shader_mode = RENDERER_SHADER_ANIME;
      r->active = &r->anime;
      printf("Shader: anime\n");
    }
  } else {
    r->shader_mode = RENDERER_SHADER_ANIME;
    r->active = &r->anime;
    printf("Shader: anime\n");
  }
}

void renderer_set_model_transform(Renderer *r, const float *pos, const float *rot_deg, float scale) {
  /* Build transform: T * Ry * Rx * Rz (yaw, pitch, roll order) */
  float cx = cosf(rot_deg[0] * 0.0174533f), sx = sinf(rot_deg[0] * 0.0174533f);
  float cy = cosf(rot_deg[1] * 0.0174533f), sy = sinf(rot_deg[1] * 0.0174533f);
  float cz = cosf(rot_deg[2] * 0.0174533f), sz = sinf(rot_deg[2] * 0.0174533f);
  
  float *m = r->model_transform;
  m[0] = (cy*cz + sy*sx*sz) * scale;  m[4] = (-cy*sz + sy*sx*cz) * scale; m[8]  = (sy*cx) * scale; m[12] = pos[0];
  m[1] = (cx*sz) * scale;             m[5] = (cx*cz) * scale;             m[9]  = (-sx) * scale;   m[13] = pos[1];
  m[2] = (-sy*cz + cy*sx*sz) * scale; m[6] = (sy*sz + cy*sx*cz) * scale;  m[10] = (cy*cx) * scale; m[14] = pos[2];
  m[3] = 0;                 m[7] = 0;                 m[11] = 0;     m[15] = 1;
}
