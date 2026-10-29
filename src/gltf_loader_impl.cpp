/*
 * VRM Overlay - glTF/GLB Loader C++ Implementation
 *
 * Uses tinygltf for actual parsing. This file bridges C++ tinygltf
 * to our C interface.
 */

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include <array>
#include <cglm/cglm.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

extern "C" {
#include "gltf_loader.h"
}

static void mat4_to_quat(const float *m, float *q);

/* Helper to copy string safely */
static void copy_name(char *dst, const std::string &src, size_t max_len) {
  size_t len = src.length();
  if (len >= max_len)
    len = max_len - 1;
  memcpy(dst, src.c_str(), len);
  dst[len] = '\0';
}

static void normalize_quat(float *q) {
  float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (len > 0.0001f) {
    q[0] /= len;
    q[1] /= len;
    q[2] /= len;
    q[3] /= len;
  }
}

static float vec3_len(const float *v) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void vec3_norm(float *v) {
  float len = vec3_len(v);
  if (len > 1e-6f) {
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }
}

static float vec3_dot(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_cross(const float *a, const float *b, float *out) {
  float x = a[1] * b[2] - a[2] * b[1];
  float y = a[2] * b[0] - a[0] * b[2];
  float z = a[0] * b[1] - a[1] * b[0];
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

static bool read_vec3_value(const tinygltf::Value &v, float *out) {
  if (v.IsArray() && v.ArrayLen() >= 3) {
    out[0] = (float)v.Get(0).Get<double>();
    out[1] = (float)v.Get(1).Get<double>();
    out[2] = (float)v.Get(2).Get<double>();
    return true;
  }
  if (v.IsObject()) {
    out[0] = v.Has("x") ? (float)v.Get("x").Get<double>() : 0.0f;
    out[1] = v.Has("y") ? (float)v.Get("y").Get<double>() : 0.0f;
    out[2] = v.Has("z") ? (float)v.Get("z").Get<double>() : 0.0f;
    return true;
  }
  return false;
}

static void quat_mul(const float *a, const float *b, float *out) {
  out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void quat_inv(const float *q, float *out) {
  out[0] = -q[0];
  out[1] = -q[1];
  out[2] = -q[2];
  out[3] = q[3];
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

static void mat4_to_quat(const float *m, float *q) {
  float m00 = m[0], m01 = m[4], m02 = m[8];
  float m10 = m[1], m11 = m[5], m12 = m[9];
  float m20 = m[2], m21 = m[6], m22 = m[10];
  float trace = m00 + m11 + m22;
  if (trace > 0.0f) {
    float s = sqrtf(trace + 1.0f) * 2.0f;
    q[3] = 0.25f * s;
    q[0] = (m21 - m12) / s;
    q[1] = (m02 - m20) / s;
    q[2] = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
    q[3] = (m21 - m12) / s;
    q[0] = 0.25f * s;
    q[1] = (m01 + m10) / s;
    q[2] = (m02 + m20) / s;
  } else if (m11 > m22) {
    float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
    q[3] = (m02 - m20) / s;
    q[0] = (m01 + m10) / s;
    q[1] = 0.25f * s;
    q[2] = (m12 + m21) / s;
  } else {
    float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
    q[3] = (m10 - m01) / s;
    q[0] = (m02 + m20) / s;
    q[1] = (m12 + m21) / s;
    q[2] = 0.25f * s;
  }
  normalize_quat(q);
}

static GLenum component_gl_type(int componentType) {
  switch (componentType) {
  case TINYGLTF_COMPONENT_TYPE_BYTE:
    return GL_BYTE;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return GL_UNSIGNED_BYTE;
  case TINYGLTF_COMPONENT_TYPE_SHORT:
    return GL_SHORT;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return GL_UNSIGNED_SHORT;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    return GL_UNSIGNED_INT;
  case TINYGLTF_COMPONENT_TYPE_FLOAT:
    return GL_FLOAT;
  default:
    return GL_FLOAT;
  }
}

static size_t component_type_size(int componentType) {
  switch (componentType) {
  case TINYGLTF_COMPONENT_TYPE_BYTE:
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return 1;
  case TINYGLTF_COMPONENT_TYPE_SHORT:
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    return 2;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
  case TINYGLTF_COMPONENT_TYPE_FLOAT:
    return 4;
  default:
    return 0;
  }
}

static GLuint create_texture(const tinygltf::Image &image,
                             size_t *texture_bytes, size_t *texture_bytes_mip) {
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  GLenum format = GL_RGBA;
  if (image.component == 3)
    format = GL_RGB;

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, format,
               GL_UNSIGNED_BYTE, image.image.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);

  if (texture_bytes) {
    size_t base = (size_t)image.width * (size_t)image.height * 4;
    *texture_bytes += base;
    if (texture_bytes_mip) {
      *texture_bytes_mip += (base * 4) / 3;
    }
  }

  return tex;
}

/* Create OpenGL buffers for a primitive */
static void create_primitive_buffers(GltfPrimitive *prim,
                                     const tinygltf::Model &gltf,
                                     const tinygltf::Primitive &tprim,
                                     size_t *gpu_buffer_bytes) {
  glGenVertexArrays(1, &prim->vao);
  glBindVertexArray(prim->vao);

  auto bind_attribute = [&](int accessor_idx, GLuint *vbo, GLint location,
                            int components, bool integer_attr,
                            bool normalize) -> bool {
    if (accessor_idx < 0)
      return false;
    const auto &accessor = gltf.accessors[accessor_idx];
    if (accessor.bufferView < 0)
      return false;
    const auto &view = gltf.bufferViews[accessor.bufferView];
    const auto &buffer = gltf.buffers[view.buffer];
    const uint8_t *view_data = buffer.data.data() + view.byteOffset;

    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, view.byteLength, view_data, GL_STATIC_DRAW);
    if (gpu_buffer_bytes) {
      *gpu_buffer_bytes += (size_t)view.byteLength;
    }

    size_t stride = accessor.ByteStride(view);
    if (stride == 0) {
      stride = components * component_type_size(accessor.componentType);
    }

    const void *offset = (const void *)(uintptr_t)accessor.byteOffset;
    GLenum gl_type = component_gl_type(accessor.componentType);
    if (integer_attr) {
      glVertexAttribIPointer(location, components, gl_type, (GLsizei)stride,
                             offset);
    } else {
      glVertexAttribPointer(location, components, gl_type,
                            normalize ? GL_TRUE : GL_FALSE, (GLsizei)stride,
                            offset);
    }
    glEnableVertexAttribArray(location);
    return true;
  };

  prim->mode = GL_TRIANGLES;
  prim->vertex_count = 0;
  if (tprim.mode >= 0) {
    switch (tprim.mode) {
    case TINYGLTF_MODE_POINTS:
      prim->mode = GL_POINTS;
      break;
    case TINYGLTF_MODE_LINE:
      prim->mode = GL_LINES;
      break;
    case TINYGLTF_MODE_LINE_LOOP:
      prim->mode = GL_LINE_LOOP;
      break;
    case TINYGLTF_MODE_LINE_STRIP:
      prim->mode = GL_LINE_STRIP;
      break;
    case TINYGLTF_MODE_TRIANGLE_STRIP:
      prim->mode = GL_TRIANGLE_STRIP;
      break;
    case TINYGLTF_MODE_TRIANGLE_FAN:
      prim->mode = GL_TRIANGLE_FAN;
      break;
    case TINYGLTF_MODE_TRIANGLES:
    default:
      prim->mode = GL_TRIANGLES;
      break;
    }
  }

  /* Positions (required) */
  auto it = tprim.attributes.find("POSITION");
  if (it != tprim.attributes.end()) {
    bind_attribute(it->second, &prim->vbo_positions, 0, 3, false, false);
    prim->vertex_count = (uint32_t)gltf.accessors[it->second].count;
  }

  /* Normals */
  it = tprim.attributes.find("NORMAL");
  if (it != tprim.attributes.end()) {
    if (bind_attribute(it->second, &prim->vbo_normals, 1, 3, false, false)) {
      prim->has_normals = true;
    }
  }

  /* Texcoords */
  it = tprim.attributes.find("TEXCOORD_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    if (bind_attribute(it->second, &prim->vbo_texcoords, 2, 2, false,
                       accessor.normalized)) {
      prim->has_texcoords = true;
    }
  }

  /* Joints */
  it = tprim.attributes.find("JOINTS_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    if (accessor.bufferView >= 0) {
      const auto &view = gltf.bufferViews[accessor.bufferView];
      const auto &buffer = gltf.buffers[view.buffer];
      const uint8_t *data =
          buffer.data.data() + view.byteOffset + accessor.byteOffset;
      size_t comp_size = component_type_size(accessor.componentType);
      size_t stride = accessor.ByteStride(view);
      if (stride == 0) {
        stride = 4 * comp_size;
      }
      std::vector<int> palette;
      palette.reserve(GLTF_MAX_PALETTE_BONES);
      std::unordered_map<uint32_t, uint32_t> remap;
      bool palette_overflow = false;

      int count = (int)accessor.count;
      std::vector<uint8_t> remapped;
      remapped.resize((size_t)count * 4 * comp_size);

      auto read_joint = [&](const uint8_t *ptr) -> uint32_t {
        if (comp_size == 1)
          return *ptr;
        if (comp_size == 2) {
          uint16_t v;
          memcpy(&v, ptr, sizeof(v));
          return v;
        }
        uint32_t v;
        memcpy(&v, ptr, sizeof(v));
        return v;
      };
      auto write_joint = [&](uint8_t *ptr, uint32_t v) {
        if (comp_size == 1) {
          *ptr = (uint8_t)v;
        } else if (comp_size == 2) {
          uint16_t out = (uint16_t)v;
          memcpy(ptr, &out, sizeof(out));
        } else {
          memcpy(ptr, &v, sizeof(v));
        }
      };

      for (int i = 0; i < count; i++) {
        const uint8_t *src = data + (size_t)i * stride;
        uint8_t *dst = remapped.data() + (size_t)i * 4 * comp_size;
        for (int c = 0; c < 4; c++) {
          uint32_t joint = read_joint(src + (size_t)c * comp_size);
          auto itp = remap.find(joint);
          uint32_t local_idx = 0;
          if (itp == remap.end()) {
            if ((int)palette.size() < GLTF_MAX_PALETTE_BONES) {
              local_idx = (uint32_t)palette.size();
              remap[joint] = local_idx;
              palette.push_back((int)joint);
            } else {
              palette_overflow = true;
              local_idx = 0;
            }
          } else {
            local_idx = itp->second;
          }
          write_joint(dst + (size_t)c * comp_size, local_idx);
        }
      }

      if (palette_overflow) {
        fprintf(stderr,
                "Warning: primitive uses >%d joints, using bone texture path\n",
                GLTF_MAX_PALETTE_BONES);
        if (bind_attribute(it->second, &prim->vbo_joints, 3, 4, true, false)) {
          prim->has_skinning = true;
        }
        prim->joint_palette_count = 0;
        prim->joint_palette = NULL;
      } else {
        glGenBuffers(1, &prim->vbo_joints);
        glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_joints);
        glBufferData(GL_ARRAY_BUFFER, remapped.size(), remapped.data(),
                     GL_STATIC_DRAW);
        if (gpu_buffer_bytes) {
          *gpu_buffer_bytes += remapped.size();
        }

        prim->joint_palette_count = (int)palette.size();
        prim->joint_palette =
            (int *)malloc((size_t)prim->joint_palette_count * sizeof(int));
        for (int i = 0; i < prim->joint_palette_count; i++) {
          prim->joint_palette[i] = palette[i];
        }

        GLenum gl_type = component_gl_type(accessor.componentType);
        glVertexAttribIPointer(3, 4, gl_type, (GLsizei)(4 * comp_size),
                               (const void *)0);
        glEnableVertexAttribArray(3);
        prim->has_skinning = prim->joint_palette_count > 0;
      }
    }
  }

  /* Weights */
  it = tprim.attributes.find("WEIGHTS_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    bind_attribute(it->second, &prim->vbo_weights, 4, 4, false,
                   accessor.normalized);
  }

  /* Indices */
  if (tprim.indices >= 0) {
    const auto &accessor = gltf.accessors[tprim.indices];
    const auto &view = gltf.bufferViews[accessor.bufferView];
    const auto &buffer = gltf.buffers[view.buffer];
    const uint8_t *data =
        buffer.data.data() + view.byteOffset + accessor.byteOffset;

    glGenBuffers(1, &prim->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prim->ebo);

    size_t elem_size = 2;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
      prim->index_type = GL_UNSIGNED_INT;
      elem_size = 4;
    } else if (accessor.componentType ==
               TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
      prim->index_type = GL_UNSIGNED_BYTE;
      elem_size = 1;
    } else {
      prim->index_type = GL_UNSIGNED_SHORT;
    }

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, accessor.count * elem_size, data,
                 GL_STATIC_DRAW);
    prim->index_count = (uint32_t)accessor.count;
    if (gpu_buffer_bytes) {
      *gpu_buffer_bytes += (size_t)accessor.count * elem_size;
    }
  }

  /* Load morph targets */
  int target_count = (int)tprim.targets.size();
  if (target_count > 0) {
    if (target_count > MAX_MORPH_TARGETS) {
      fprintf(stderr,
              "Warning: primitive has %d morph targets, capping to %d\n",
              target_count, MAX_MORPH_TARGETS);
      prim->morph_gpu_count = MAX_MORPH_TARGETS;
    } else {
      prim->morph_gpu_count = target_count;
    }
    prim->morph_target_count = target_count;
    prim->morph_targets =
        (MorphTarget *)calloc(target_count, sizeof(MorphTarget));

    for (int t = 0; t < target_count; t++) {
      const auto &target = tprim.targets[t];
      MorphTarget *mt = &prim->morph_targets[t];

      /* POSITION deltas */
      auto pos_it = target.find("POSITION");
      if (pos_it != target.end() && pos_it->second >= 0) {
        const auto &accessor = gltf.accessors[pos_it->second];
        if (accessor.bufferView >= 0) {
          const auto &view = gltf.bufferViews[accessor.bufferView];
          const auto &buffer = gltf.buffers[view.buffer];
          const float *data =
              (const float *)(buffer.data.data() + view.byteOffset +
                              accessor.byteOffset);

          size_t count = accessor.count;
          mt->position_deltas = (float *)malloc(count * 3 * sizeof(float));
          memcpy(mt->position_deltas, data, count * 3 * sizeof(float));
        }
      }

      /* NORMAL deltas (optional) */
      auto norm_it = target.find("NORMAL");
      if (norm_it != target.end() && norm_it->second >= 0) {
        const auto &accessor = gltf.accessors[norm_it->second];
        if (accessor.bufferView >= 0) {
          const auto &view = gltf.bufferViews[accessor.bufferView];
          const auto &buffer = gltf.buffers[view.buffer];
          const float *data =
              (const float *)(buffer.data.data() + view.byteOffset +
                              accessor.byteOffset);

          size_t count = accessor.count;
          mt->normal_deltas = (float *)malloc(count * 3 * sizeof(float));
          memcpy(mt->normal_deltas, data, count * 3 * sizeof(float));
          mt->has_normals = true;
        }
      }
    }

    printf("Loaded %d morph targets for primitive (%d for GPU)\n", target_count,
           prim->morph_gpu_count);

    /* Debug: print sample deltas for morph 39 (mouth A) if available */
    if (target_count > 39 && prim->morph_targets[39].position_deltas) {
      float *pd = prim->morph_targets[39].position_deltas;
      float max_delta = 0.0f;
      for (uint32_t v = 0; v < prim->vertex_count; v++) {
        float d =
            fabsf(pd[v * 3]) + fabsf(pd[v * 3 + 1]) + fabsf(pd[v * 3 + 2]);
        if (d > max_delta)
          max_delta = d;
      }
      printf("  Morph 39 (A): max delta magnitude = %.4f\n", max_delta);
    }

    /* Create GPU morph texture if we have targets to put on GPU */
    if (prim->morph_gpu_count > 0 && prim->vertex_count > 0) {
      /*
       * Pack morphs into a 2D RGBA32F texture small enough for GL_MAX_TEXTURE_SIZE.
       * Each vertex uses two texels: [posDelta.xyz, 0] and [normDelta.xyz, 0].
       *
       * Texture layout:
       *   width  = vertsPerRow * 2 texels
       *   height = morphCount * rowsPerMorph
       *   y = morphIndex * rowsPerMorph + rowWithin
       *   x = (vertexWithinRow * 2) [+0 for pos, +1 for norm]
       */
      GLint max_tex = 0;
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
      if (max_tex <= 0) {
        max_tex = 16384;
      }

      int verts_per_row = (int)prim->vertex_count;
      int max_verts_per_row = max_tex / 2;
      if (max_verts_per_row < 1) {
        max_verts_per_row = 1;
      }
      if (verts_per_row > max_verts_per_row) {
        verts_per_row = max_verts_per_row;
      }

      int rows_per_morph =
          (int)((prim->vertex_count + (uint32_t)verts_per_row - 1) /
                (uint32_t)verts_per_row);

      int max_morphs_fit = max_tex / (rows_per_morph > 0 ? rows_per_morph : 1);
      if (max_morphs_fit < prim->morph_gpu_count) {
        fprintf(stderr,
                "Warning: morph texture too tall for GL_MAX_TEXTURE_SIZE=%d; "
                "capping morphs from %d to %d\n",
                max_tex, prim->morph_gpu_count, max_morphs_fit);
        prim->morph_gpu_count = max_morphs_fit;
      }

      if (prim->morph_gpu_count > 0 && rows_per_morph > 0) {
        int tex_width = verts_per_row * 2;
        int tex_height = prim->morph_gpu_count * rows_per_morph;
        float *tex_data = (float *)calloc(
            (size_t)tex_width * (size_t)tex_height * 4, sizeof(float));

        for (int t = 0; t < prim->morph_gpu_count; t++) {
          MorphTarget *mt = &prim->morph_targets[t];
          for (uint32_t v = 0; v < prim->vertex_count; v++) {
            int row_within = (int)(v / (uint32_t)verts_per_row);
            int col = (int)(v - (uint32_t)row_within * (uint32_t)verts_per_row);
            int y = t * rows_per_morph + row_within;
            int x = col * 2;

            float *pos_dest =
                tex_data + (((size_t)y * (size_t)tex_width) + (size_t)x) * 4;
            float *nrm_dest = pos_dest + 4;

            if (mt->position_deltas) {
              pos_dest[0] = mt->position_deltas[v * 3 + 0];
              pos_dest[1] = mt->position_deltas[v * 3 + 1];
              pos_dest[2] = mt->position_deltas[v * 3 + 2];
            }
            if (mt->normal_deltas) {
              nrm_dest[0] = mt->normal_deltas[v * 3 + 0];
              nrm_dest[1] = mt->normal_deltas[v * 3 + 1];
              nrm_dest[2] = mt->normal_deltas[v * 3 + 2];
            }
          }
        }

        prim->morph_tex_verts_per_row = verts_per_row;
        prim->morph_tex_rows_per_morph = rows_per_morph;

        glGenTextures(1, &prim->morph_vbo);
        glBindTexture(GL_TEXTURE_2D, prim->morph_vbo);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tex_width, tex_height, 0,
                     GL_RGBA, GL_FLOAT, tex_data);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);

      free(tex_data);
      printf("Created morph texture %dx%d for primitive\n", tex_width,
             tex_height);
      }
    }
  }

  prim->material_index = tprim.material;

  glBindVertexArray(0);
}

/* Retain compact per-joint geometry envelopes before source buffers are freed.
 * Each skinned vertex is a weighted point inside these transformed envelopes. */
static bool bounds_attribute(const tinygltf::Model &gltf, int index, size_t vertex,
                             int components, float *values) {
  if (index < 0 || index >= (int)gltf.accessors.size()) return false;
  const auto &accessor = gltf.accessors[index];
  if (accessor.bufferView < 0 || accessor.bufferView >= (int)gltf.bufferViews.size() ||
      vertex >= accessor.count) return false;
  const auto &view = gltf.bufferViews[accessor.bufferView];
  if (view.buffer < 0 || view.buffer >= (int)gltf.buffers.size()) return false;
  const auto &buffer = gltf.buffers[view.buffer].data;
  int size = component_type_size(accessor.componentType);
  int stride = accessor.ByteStride(view);
  if (size <= 0 || stride < components * size) return false;
  size_t offset = view.byteOffset + accessor.byteOffset + vertex * (size_t)stride;
  if (offset > buffer.size() || (size_t)(components * size) > buffer.size()-offset) return false;
  for (int k = 0; k < components; k++) {
    const unsigned char *p = buffer.data()+offset+k*size;
    float value = 0;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) memcpy(&value,p,4);
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) value = *p / (accessor.normalized ? 255.0f : 1.0f);
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) { uint16_t v; memcpy(&v,p,2); value = v / (accessor.normalized ? 65535.0f : 1.0f); }
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) { uint32_t v; memcpy(&v,p,4); value = (float)v; }
    else return false;
    if (!std::isfinite(value)) return false;
    values[k] = value;
  }
  return true;
}

static void build_geometry_bounds(const tinygltf::Model &gltf, GltfModel *model) {
  for (int i = 0; i < model->skin_count; i++) {
    auto &skin = model->skins[i];
    skin.joint_bounds = (GltfBounds *)calloc(skin.joint_count, sizeof(GltfBounds));
  }
  for (int i = 0; i < model->node_count; i++) {
    const auto &node = model->nodes[i];
    if (node.mesh_index < 0 || node.mesh_index >= model->mesh_count) continue;
    auto &mesh = model->meshes[node.mesh_index];
    const auto &source = gltf.meshes[node.mesh_index];
    GltfSkin *skin = node.skin_index >= 0 && node.skin_index < model->skin_count ? &model->skins[node.skin_index] : nullptr;
    for (int j = 0; j < mesh.primitive_count; j++) {
      auto &primitive = mesh.primitives[j];
      const auto &attributes = source.primitives[j].attributes;
      auto position = attributes.find("POSITION"), joints = attributes.find("JOINTS_0"), weights = attributes.find("WEIGHTS_0");
      if (position == attributes.end()) continue;
      for (uint32_t v = 0; v < primitive.vertex_count; v++) {
        float point[3];
        if (!bounds_attribute(gltf,position->second,v,3,point)) continue;
        GltfBounds vertex{};
        gltf_bounds_include(&vertex,point);
        for (int t = 0; t < primitive.morph_gpu_count; t++) {
          const float *deltas = primitive.morph_targets[t].position_deltas;
          if (!deltas) continue;
          for (int k = 0; k < 3; k++) {
            vertex.min[k] += fminf(deltas[v*3+k],0);
            vertex.max[k] += fmaxf(deltas[v*3+k],0);
          }
        }
        gltf_bounds_merge(&primitive.local_bounds,&vertex);
        if (!primitive.has_skinning || !skin || !skin->joint_bounds || !skin->inverse_bind_matrices ||
            joints == attributes.end() || weights == attributes.end()) continue;
        float bone[4], influence[4];
        if (!bounds_attribute(gltf,joints->second,v,4,bone) || !bounds_attribute(gltf,weights->second,v,4,influence)) continue;
        for (int k = 0; k < 4; k++) {
          if (influence[k] > 0 && bone[k] >= 0 && bone[k] < skin->joint_count) {
            int joint = (int)bone[k];
            gltf_bounds_transform(&skin->joint_bounds[joint], &vertex, skin->inverse_bind_matrices+joint*16);
          }
        }
      }
    }
  }
}

extern "C" bool gltf_load_impl(GltfModel *model, const char *path) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model gltf;
  std::string err, warn;
  size_t gpu_texture_bytes = 0;
  size_t gpu_texture_bytes_mip = 0;
  size_t gpu_buffer_bytes = 0;

  bool ret;
  std::string spath(path);
  if (spath.ends_with(".glb") || spath.ends_with(".vrm") ||
      spath.ends_with(".vrma")) {
    ret = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
  } else {
    ret = loader.LoadASCIIFromFile(&gltf, &err, &warn, path);
  }

  if (!warn.empty()) {
    fprintf(stderr, "glTF warning: %s\n", warn.c_str());
  }

  if (!ret) {
    fprintf(stderr, "Failed to load glTF: %s\n", err.c_str());
    return false;
  }

  printf("Loaded glTF: %zu meshes, %zu materials, %zu textures, %zu nodes, %zu "
         "skins\n",
         gltf.meshes.size(), gltf.materials.size(), gltf.textures.size(),
         gltf.nodes.size(), gltf.skins.size());

  /* Count nodes with meshes */
  int mesh_nodes = 0;
  for (size_t i = 0; i < gltf.nodes.size(); i++) {
    if (gltf.nodes[i].mesh >= 0)
      mesh_nodes++;
  }
  printf("Nodes with meshes: %d\n", mesh_nodes);

  /* Load textures */
  model->texture_count = (int)gltf.textures.size();
  if (model->texture_count > 0) {
    model->textures =
        (GltfTexture *)calloc(model->texture_count, sizeof(GltfTexture));
    for (size_t i = 0; i < gltf.textures.size(); i++) {
      int img_idx = gltf.textures[i].source;
      if (img_idx >= 0 && img_idx < (int)gltf.images.size()) {
        model->textures[i].id = create_texture(
            gltf.images[img_idx], &gpu_texture_bytes, &gpu_texture_bytes_mip);
        model->textures[i].width = gltf.images[img_idx].width;
        model->textures[i].height = gltf.images[img_idx].height;
        gltf.images[img_idx].image.clear();
        gltf.images[img_idx].image.shrink_to_fit();
      }
    }
  }

  /* Load materials */
  model->material_count = (int)gltf.materials.size();
  if (model->material_count > 0) {
    model->materials =
        (GltfMaterial *)calloc(model->material_count, sizeof(GltfMaterial));
    for (size_t i = 0; i < gltf.materials.size(); i++) {
      const auto &mat = gltf.materials[i];
      GltfMaterial *m = &model->materials[i];

      copy_name(m->name, mat.name, sizeof(m->name));

      const auto &pbr = mat.pbrMetallicRoughness;
      m->base_color[0] = (float)pbr.baseColorFactor[0];
      m->base_color[1] = (float)pbr.baseColorFactor[1];
      m->base_color[2] = (float)pbr.baseColorFactor[2];
      m->base_color[3] = (float)pbr.baseColorFactor[3];
      m->metallic = (float)pbr.metallicFactor;
      m->roughness = (float)pbr.roughnessFactor;

      m->base_color_texture = pbr.baseColorTexture.index;
      m->metallic_roughness_texture = pbr.metallicRoughnessTexture.index;
      m->normal_texture = mat.normalTexture.index;

      if (mat.alphaMode == "MASK") {
        m->alpha_mode = 1;
        m->alpha_cutoff = (float)mat.alphaCutoff;
      } else if (mat.alphaMode == "BLEND") {
        m->alpha_mode = 2;
      } else {
        m->alpha_mode = 0;
      }

      m->double_sided = mat.doubleSided;
      m->alpha_write_depth = false;
    }
  }

  /* Apply VRM material overrides (MToon) */
  if (gltf.extensions.count("VRM")) {
    const auto &vrm = gltf.extensions.at("VRM");
    if (vrm.Has("materialProperties")) {
      const auto &props = vrm.Get("materialProperties");
      size_t count = props.ArrayLen();
      for (size_t i = 0; i < count; i++) {
        const auto &mp = props.Get(i);
        if (i >= (size_t)model->material_count)
          break;
        GltfMaterial *m = &model->materials[i];
        if (mp.Has("floatProperties")) {
          const auto &fp = mp.Get("floatProperties");
          if (fp.Has("_BlendMode")) {
            int blend_mode = (int)fp.Get("_BlendMode").GetNumberAsDouble();
            if (blend_mode == 3) {
              m->alpha_write_depth = true;
            }
          }
          if (fp.Has("_CullMode")) {
            int cull_mode = (int)fp.Get("_CullMode").GetNumberAsDouble();
            if (cull_mode == 0) {
              m->double_sided = true;
            } else if (cull_mode == 2) {
              m->double_sided = false;
            }
          }
        }
      }
    }
  }

  /* Load meshes */
  model->mesh_count = (int)gltf.meshes.size();
  if (model->mesh_count > 0) {
    model->meshes = (GltfMesh *)calloc(model->mesh_count, sizeof(GltfMesh));
    for (size_t i = 0; i < gltf.meshes.size(); i++) {
      const auto &mesh = gltf.meshes[i];
      GltfMesh *m = &model->meshes[i];

      copy_name(m->name, mesh.name, sizeof(m->name));
      m->primitive_count = (int)mesh.primitives.size();
      m->primitives =
          (GltfPrimitive *)calloc(m->primitive_count, sizeof(GltfPrimitive));

      for (size_t j = 0; j < mesh.primitives.size(); j++) {
        create_primitive_buffers(&m->primitives[j], gltf, mesh.primitives[j],
                                 &gpu_buffer_bytes);
      }

      /* Morph targets - use mesh.weights if available, otherwise use max from
       * primitives */
      m->morph_target_count = (int)mesh.weights.size();
      if (m->morph_target_count == 0) {
        /* Fall back to max morph count from primitives */
        for (int j = 0; j < m->primitive_count; j++) {
          if (m->primitives[j].morph_target_count > m->morph_target_count) {
            m->morph_target_count = m->primitives[j].morph_target_count;
          }
        }
      }
      if (m->morph_target_count > 0) {
        m->morph_weights =
            (float *)calloc(m->morph_target_count, sizeof(float));
        for (size_t j = 0; j < mesh.weights.size(); j++) {
          m->morph_weights[j] = (float)mesh.weights[j];
        }
        printf("Mesh '%s': %d morph targets, weights array allocated\n",
               m->name, m->morph_target_count);
      }
    }
  }

  /* Load nodes */
  model->node_count = (int)gltf.nodes.size();
  if (model->node_count > 0) {
    model->nodes = (GltfNode *)calloc(model->node_count, sizeof(GltfNode));
    for (size_t i = 0; i < gltf.nodes.size(); i++) {
      const auto &node = gltf.nodes[i];
      GltfNode *n = &model->nodes[i];

      copy_name(n->name, node.name, sizeof(n->name));
      n->mesh_index = node.mesh;
      n->skin_index = node.skin;
      n->parent_index = -1; /* Will be set below */

      /* Default transform */
      n->translation[0] = n->translation[1] = n->translation[2] = 0.0f;
      n->rotation[0] = n->rotation[1] = n->rotation[2] = 0.0f;
      n->rotation[3] = 1.0f;
      n->scale[0] = n->scale[1] = n->scale[2] = 1.0f;
      n->base_translation[0] = n->base_translation[1] = n->base_translation[2] =
          0.0f;
      n->base_rotation[0] = n->base_rotation[1] = n->base_rotation[2] = 0.0f;
      n->base_rotation[3] = 1.0f;
      n->base_scale[0] = n->base_scale[1] = n->base_scale[2] = 1.0f;

      if (node.translation.size() == 3) {
        n->translation[0] = (float)node.translation[0];
        n->translation[1] = (float)node.translation[1];
        n->translation[2] = (float)node.translation[2];
      }
      if (node.rotation.size() == 4) {
        n->rotation[0] = (float)node.rotation[0];
        n->rotation[1] = (float)node.rotation[1];
        n->rotation[2] = (float)node.rotation[2];
        n->rotation[3] = (float)node.rotation[3];
        normalize_quat(n->rotation);
      }
      if (node.scale.size() == 3) {
        n->scale[0] = (float)node.scale[0];
        n->scale[1] = (float)node.scale[1];
        n->scale[2] = (float)node.scale[2];
      }

      n->base_translation[0] = n->translation[0];
      n->base_translation[1] = n->translation[1];
      n->base_translation[2] = n->translation[2];
      n->base_rotation[0] = n->rotation[0];
      n->base_rotation[1] = n->rotation[1];
      n->base_rotation[2] = n->rotation[2];
      n->base_rotation[3] = n->rotation[3];
      n->base_scale[0] = n->scale[0];
      n->base_scale[1] = n->scale[1];
      n->base_scale[2] = n->scale[2];

      /* Children */
      n->child_count = (int)node.children.size();
      if (n->child_count > 0) {
        n->children = (int *)malloc(n->child_count * sizeof(int));
        for (size_t j = 0; j < node.children.size(); j++) {
          n->children[j] = node.children[j];
        }
      }
    }

    /* Set parent indices */
    for (int i = 0; i < model->node_count; i++) {
      GltfNode *n = &model->nodes[i];
      for (int j = 0; j < n->child_count; j++) {
        if (n->children[j] >= 0 && n->children[j] < model->node_count) {
          model->nodes[n->children[j]].parent_index = i;
        }
      }
    }
  }

  /* Load skins */
  model->skin_count = (int)gltf.skins.size();
  if (model->skin_count > 0) {
    model->skins = (GltfSkin *)calloc(model->skin_count, sizeof(GltfSkin));
    for (size_t i = 0; i < gltf.skins.size(); i++) {
      const auto &skin = gltf.skins[i];
      GltfSkin *s = &model->skins[i];

      s->joint_count = (int)skin.joints.size();
      s->joints = (int *)malloc(s->joint_count * sizeof(int));
      for (size_t j = 0; j < skin.joints.size(); j++) {
        s->joints[j] = skin.joints[j];
      }

      s->skeleton_root = skin.skeleton;

      /* Inverse bind matrices */
      if (skin.inverseBindMatrices >= 0) {
        const auto &accessor = gltf.accessors[skin.inverseBindMatrices];
        const auto &view = gltf.bufferViews[accessor.bufferView];
        const auto &buffer = gltf.buffers[view.buffer];
        const float *data =
            (const float *)(buffer.data.data() + view.byteOffset +
                            accessor.byteOffset);

        s->inverse_bind_matrices =
            (float *)malloc(s->joint_count * 16 * sizeof(float));
        memcpy(s->inverse_bind_matrices, data,
               s->joint_count * 16 * sizeof(float));
      }
    }
  }

  /* Scene roots */
  if (!gltf.scenes.empty()) {
    const auto &scene =
        gltf.scenes[gltf.defaultScene >= 0 ? gltf.defaultScene : 0];
    model->scene_root_count = (int)scene.nodes.size();
    model->scene_roots = (int *)malloc(model->scene_root_count * sizeof(int));
    for (size_t i = 0; i < scene.nodes.size(); i++) {
      model->scene_roots[i] = scene.nodes[i];
    }
  }

  /* Load animations */
  model->animation_count = (int)gltf.animations.size();
  if (model->animation_count > 0) {
    model->animations =
        (GltfAnimation *)calloc(model->animation_count, sizeof(GltfAnimation));
    for (size_t i = 0; i < gltf.animations.size(); i++) {
      const auto &anim = gltf.animations[i];
      GltfAnimation *a = &model->animations[i];

      copy_name(a->name, anim.name, sizeof(a->name));

      /* Load samplers */
      a->sampler_count = (int)anim.samplers.size();
      a->samplers =
          (GltfAnimSampler *)calloc(a->sampler_count, sizeof(GltfAnimSampler));
      for (size_t j = 0; j < anim.samplers.size(); j++) {
        const auto &sampler = anim.samplers[j];
        GltfAnimSampler *s = &a->samplers[j];

        /* Input (times) */
        const auto &input_acc = gltf.accessors[sampler.input];
        const auto &input_view = gltf.bufferViews[input_acc.bufferView];
        const auto &input_buf = gltf.buffers[input_view.buffer];
        const float *input_data =
            (const float *)(input_buf.data.data() + input_view.byteOffset +
                            input_acc.byteOffset);

        const auto &output_acc = gltf.accessors[sampler.output];
        const auto &output_view = gltf.bufferViews[output_acc.bufferView];
        const auto &output_buf = gltf.buffers[output_view.buffer];
        const float *output_data =
            (const float *)(output_buf.data.data() + output_view.byteOffset +
                            output_acc.byteOffset);

        int input_count = (int)input_acc.count;
        int output_count = (int)output_acc.count;
        int keyframe_count =
            input_count < output_count ? input_count : output_count;
        if (input_count != output_count) {
          fprintf(stderr,
                  "Warning: sampler %zu input/output count mismatch (%d/%d), "
                  "clamping to %d\n",
                  j, input_count, output_count, keyframe_count);
        }

        s->keyframe_count = keyframe_count;
        s->input = (float *)malloc(s->keyframe_count * sizeof(float));
        memcpy(s->input, input_data, s->keyframe_count * sizeof(float));

        /* Track max time for duration */
        if (s->keyframe_count > 0 &&
            s->input[s->keyframe_count - 1] > a->duration) {
          a->duration = s->input[s->keyframe_count - 1];
        }

        /* Output (values) */
        int components = 1;
        if (output_acc.type == TINYGLTF_TYPE_VEC3)
          components = 3;
        else if (output_acc.type == TINYGLTF_TYPE_VEC4)
          components = 4;

        s->output_stride = components;
        s->output =
            (float *)malloc(s->keyframe_count * components * sizeof(float));
        memcpy(s->output, output_data,
               s->keyframe_count * components * sizeof(float));
      }

      /* Load channels */
      a->channel_count = (int)anim.channels.size();
      a->channels =
          (GltfAnimChannel *)calloc(a->channel_count, sizeof(GltfAnimChannel));
      a->node_mask_count = model->node_count;
      a->node_mask = (uint8_t *)calloc(a->node_mask_count, sizeof(uint8_t));
      for (size_t j = 0; j < anim.channels.size(); j++) {
        const auto &channel = anim.channels[j];
        GltfAnimChannel *c = &a->channels[j];

        c->sampler_index = channel.sampler;
        c->target_node = channel.target_node;

        if (channel.target_path == "translation")
          c->path = GLTF_PATH_TRANSLATION;
        else if (channel.target_path == "rotation")
          c->path = GLTF_PATH_ROTATION;
        else if (channel.target_path == "scale")
          c->path = GLTF_PATH_SCALE;
        else if (channel.target_path == "weights")
          c->path = GLTF_PATH_WEIGHTS;

        if (c->sampler_index >= 0 && c->sampler_index < a->sampler_count) {
          int expected = 0;
          if (c->path == GLTF_PATH_TRANSLATION || c->path == GLTF_PATH_SCALE)
            expected = 3;
          else if (c->path == GLTF_PATH_ROTATION)
            expected = 4;
          if (expected > 0) {
            GltfAnimSampler *s = &a->samplers[c->sampler_index];
            if (s->output_stride < expected) {
              fprintf(stderr,
                      "Warning: channel '%s' expects %d components but sampler "
                      "has %d; disabling channel\n",
                      channel.target_path.c_str(), expected, s->output_stride);
              c->target_node = -1;
            }
          }
        }

        if (c->target_node >= 0 && c->target_node < a->node_mask_count) {
          if (c->path == GLTF_PATH_TRANSLATION)
            a->node_mask[c->target_node] |= 1;
          else if (c->path == GLTF_PATH_ROTATION)
            a->node_mask[c->target_node] |= 2;
          else if (c->path == GLTF_PATH_SCALE)
            a->node_mask[c->target_node] |= 4;
        }
      }
    }
    printf("Loaded %d animations\n", model->animation_count);
  }

  bool loaded_vrmc_spring = false;
  if (gltf.extensions.count("VRMC_springBone")) {
    const auto &sb = gltf.extensions.at("VRMC_springBone");

    std::vector<SpringCollider> flat_colliders;
    if (sb.Has("colliders") && sb.Get("colliders").IsArray()) {
      const auto &colliders = sb.Get("colliders");
      flat_colliders.resize(colliders.ArrayLen());
      for (size_t i = 0; i < colliders.ArrayLen(); i++) {
        const auto &c = colliders.Get((int)i);
        SpringCollider col = {};
        col.node = c.Has("node") ? c.Get("node").Get<int>() : -1;
        if (c.Has("shape") && c.Get("shape").IsObject()) {
          const auto &shape = c.Get("shape");
          if (shape.Has("sphere") && shape.Get("sphere").IsObject()) {
            const auto &sphere = shape.Get("sphere");
            col.is_capsule = 0;
            if (sphere.Has("offset")) {
              read_vec3_value(sphere.Get("offset"), col.offset);
            }
            col.radius = sphere.Has("radius")
                             ? (float)sphere.Get("radius").Get<double>()
                             : 0.0f;
          } else if (shape.Has("capsule") && shape.Get("capsule").IsObject()) {
            const auto &cap = shape.Get("capsule");
            col.is_capsule = 1;
            if (cap.Has("offset")) {
              read_vec3_value(cap.Get("offset"), col.offset);
            }
            if (cap.Has("tail")) {
              read_vec3_value(cap.Get("tail"), col.tail);
            }
            col.radius = cap.Has("radius")
                             ? (float)cap.Get("radius").Get<double>()
                             : 0.0f;
          }
        }
        flat_colliders[i] = col;
      }
    }

    if (sb.Has("colliderGroups") && sb.Get("colliderGroups").IsArray()) {
      const auto &cgroups = sb.Get("colliderGroups");
      model->spring_collider_count = (int)cgroups.ArrayLen();
      model->spring_colliders = (SpringColliderGroup *)calloc(
          model->spring_collider_count, sizeof(SpringColliderGroup));
      for (size_t i = 0; i < cgroups.ArrayLen(); i++) {
        const auto &cg = cgroups.Get((int)i);
        SpringColliderGroup *group = &model->spring_colliders[i];
        group->node = -1;
        if (cg.Has("colliders") && cg.Get("colliders").IsArray()) {
          const auto &cols = cg.Get("colliders");
          std::vector<SpringCollider> group_cols;
          group_cols.reserve(cols.ArrayLen());
          for (size_t j = 0; j < cols.ArrayLen(); j++) {
            int idx = cols.Get((int)j).Get<int>();
            if (idx >= 0 && idx < (int)flat_colliders.size()) {
              group_cols.push_back(flat_colliders[idx]);
            }
          }
          group->collider_count = (int)group_cols.size();
          if (group->collider_count > 0) {
            group->colliders = (SpringCollider *)calloc(group->collider_count,
                                                        sizeof(SpringCollider));
            for (int j = 0; j < group->collider_count; j++) {
              group->colliders[j] = group_cols[j];
            }
          }
        }
      }
    }

    if (sb.Has("springs") && sb.Get("springs").IsArray()) {
      const auto &springs = sb.Get("springs");
      model->spring_group_count = (int)springs.ArrayLen();
      model->spring_groups = (SpringBoneGroup *)calloc(
          model->spring_group_count, sizeof(SpringBoneGroup));

      for (size_t i = 0; i < springs.ArrayLen(); i++) {
        const auto &sp = springs.Get((int)i);
        SpringBoneGroup *group = &model->spring_groups[i];
        group->center = sp.Has("center") ? sp.Get("center").Get<int>() : -1;

        if (sp.Has("joints") && sp.Get("joints").IsArray()) {
          const auto &joints = sp.Get("joints");
          group->bone_count = (int)joints.ArrayLen();
          if (group->bone_count > 0) {
            group->bones = (int *)malloc(group->bone_count * sizeof(int));
          }
          if (group->bone_count >= 2) {
            group->joint_count = group->bone_count - 1;
            group->joints =
                (SpringJoint *)calloc(group->joint_count, sizeof(SpringJoint));
          }

          for (int j = 0; j < group->bone_count; j++) {
            const auto &jv = joints.Get(j);
            int node_idx = jv.Has("node") ? jv.Get("node").Get<int>() : -1;
            group->bones[j] = node_idx;
            if (j < group->joint_count) {
              SpringJoint *joint = &group->joints[j];
              joint->hit_radius = jv.Has("hitRadius")
                                      ? (float)jv.Get("hitRadius").Get<double>()
                                      : 0.0f;
              joint->stiffness = jv.Has("stiffness")
                                     ? (float)jv.Get("stiffness").Get<double>()
                                     : 0.0f;
              joint->gravity_power =
                  jv.Has("gravityPower")
                      ? (float)jv.Get("gravityPower").Get<double>()
                      : 0.0f;
              joint->drag_force = jv.Has("dragForce")
                                      ? (float)jv.Get("dragForce").Get<double>()
                                      : 0.0f;
              joint->gravity_dir[0] = 0.0f;
              joint->gravity_dir[1] = -1.0f;
              joint->gravity_dir[2] = 0.0f;
              if (jv.Has("gravityDir")) {
                read_vec3_value(jv.Get("gravityDir"), joint->gravity_dir);
              }
            }
          }
        }

        if (sp.Has("colliderGroups") && sp.Get("colliderGroups").IsArray()) {
          const auto &cgs = sp.Get("colliderGroups");
          group->collider_group_count = (int)cgs.ArrayLen();
          if (group->collider_group_count > 0) {
            group->collider_groups =
                (int *)malloc(group->collider_group_count * sizeof(int));
            for (size_t j = 0; j < cgs.ArrayLen(); j++) {
              group->collider_groups[j] = cgs.Get((int)j).Get<int>();
            }
          }
        }

        for (int j = 0; j < group->bone_count; j++) {
          int node_idx = group->bones[j];
          if (node_idx >= 0 && node_idx < model->node_count) {
            model->nodes[node_idx].is_spring_bone = 1;
          }
        }
      }
      loaded_vrmc_spring = model->spring_group_count > 0;
      if (loaded_vrmc_spring) {
        printf("Loaded %d VRMC spring bone groups, %d collider groups\n",
               model->spring_group_count, model->spring_collider_count);
      }
    }
  }

  /* Load VRM humanoid bone mapping */
  if (gltf.extensions.count("VRM")) {
    const auto &vrm = gltf.extensions.at("VRM");
    if (vrm.Has("humanoid") && vrm.Get("humanoid").Has("humanBones")) {
      const auto &bones = vrm.Get("humanoid").Get("humanBones");
      if (bones.IsArray()) {
        model->humanoid_bone_count = (int)bones.ArrayLen();
        model->humanoid_names =
            (char (*)[32])calloc(model->humanoid_bone_count, 32);
        model->humanoid_bones =
            (int *)malloc(model->humanoid_bone_count * sizeof(int));

        for (size_t i = 0; i < bones.ArrayLen(); i++) {
          const auto &b = bones.Get((int)i);
          if (b.Has("bone") && b.Has("node")) {
            std::string name = b.Get("bone").Get<std::string>();
            int node = b.Get("node").Get<int>();
            strncpy(model->humanoid_names[i], name.c_str(), 31);
            model->humanoid_bones[i] = node;
          }
        }
        printf("Loaded %d humanoid bone mappings\n",
               model->humanoid_bone_count);
      }
    }

    /* Load VRM 0.x blendshape expressions */
    if (vrm.Has("blendShapeMaster") &&
        vrm.Get("blendShapeMaster").Has("blendShapeGroups")) {
      const auto &groups = vrm.Get("blendShapeMaster").Get("blendShapeGroups");
      if (groups.IsArray() && groups.ArrayLen() > 0) {
        int expr_count = (int)groups.ArrayLen();
        if (expr_count > MAX_VRM_EXPRESSIONS) {
          fprintf(stderr, "Warning: VRM has %d expressions, capping to %d\n",
                  expr_count, MAX_VRM_EXPRESSIONS);
          expr_count = MAX_VRM_EXPRESSIONS;
        }

        model->vrm_expressions =
            (VrmExpressionMap *)calloc(1, sizeof(VrmExpressionMap));
        model->vrm_expressions->expressions =
            (VrmExpression *)calloc(expr_count, sizeof(VrmExpression));
        model->vrm_expressions->expression_count = expr_count;

        for (int i = 0; i < expr_count; i++) {
          const auto &g = groups.Get(i);
          VrmExpression *expr = &model->vrm_expressions->expressions[i];

          /* Name */
          if (g.Has("name")) {
            std::string name = g.Get("name").Get<std::string>();
            strncpy(expr->name, name.c_str(), VRM_EXPRESSION_NAME_LEN - 1);
          }

          /* Preset name (e.g., "aa", "blink", "joy") */
          if (g.Has("presetName")) {
            std::string preset = g.Get("presetName").Get<std::string>();
            strncpy(expr->preset_name, preset.c_str(),
                    VRM_EXPRESSION_NAME_LEN - 1);
          }

          /* Is binary */
          if (g.Has("isBinary")) {
            expr->is_binary = g.Get("isBinary").Get<bool>();
          }

          /* Binds */
          if (g.Has("binds") && g.Get("binds").IsArray()) {
            const auto &binds = g.Get("binds");
            int bind_count = (int)binds.ArrayLen();
            if (bind_count > 0) {
              expr->binds = (VrmExpressionBind *)calloc(
                  bind_count, sizeof(VrmExpressionBind));
              expr->bind_count = bind_count;

              for (int b = 0; b < bind_count; b++) {
                const auto &bind = binds.Get(b);
                VrmExpressionBind *eb = &expr->binds[b];
                eb->mesh_index =
                    bind.Has("mesh") ? bind.Get("mesh").Get<int>() : -1;
                eb->morph_index =
                    bind.Has("index") ? bind.Get("index").Get<int>() : -1;
                eb->weight =
                    bind.Has("weight")
                        ? (float)bind.Get("weight").Get<double>() / 100.0f
                        : 1.0f;
              }
            }
          }
        }
        printf("Loaded %d VRM expressions\n", expr_count);
      }
    }

    /* Load VRM spring bones */
    if (!loaded_vrmc_spring && vrm.Has("secondaryAnimation")) {
      const auto &secondary = vrm.Get("secondaryAnimation");

      /* Parse collider groups first */
      if (secondary.Has("colliderGroups") &&
          secondary.Get("colliderGroups").IsArray()) {
        const auto &cgroups = secondary.Get("colliderGroups");
        model->spring_collider_count = (int)cgroups.ArrayLen();
        model->spring_colliders = (SpringColliderGroup *)calloc(
            model->spring_collider_count, sizeof(SpringColliderGroup));

        for (size_t i = 0; i < cgroups.ArrayLen(); i++) {
          const auto &cg = cgroups.Get((int)i);
          SpringColliderGroup *group = &model->spring_colliders[i];
          group->node = cg.Has("node") ? cg.Get("node").Get<int>() : -1;

          if (cg.Has("colliders") && cg.Get("colliders").IsArray()) {
            const auto &cols = cg.Get("colliders");
            group->collider_count = (int)cols.ArrayLen();
            group->colliders = (SpringCollider *)calloc(group->collider_count,
                                                        sizeof(SpringCollider));

            for (size_t j = 0; j < cols.ArrayLen(); j++) {
              const auto &c = cols.Get((int)j);
              SpringCollider *col = &group->colliders[j];
              col->node = group->node;
              col->radius = c.Has("radius")
                                ? (float)c.Get("radius").Get<double>()
                                : 0.05f;
              if (c.Has("offset")) {
                const auto &off = c.Get("offset");
                col->offset[0] =
                    off.Has("x") ? (float)off.Get("x").Get<double>() : 0;
                col->offset[1] =
                    off.Has("y") ? (float)off.Get("y").Get<double>() : 0;
                col->offset[2] =
                    off.Has("z") ? (float)off.Get("z").Get<double>() : 0;
              }
            }
          }
        }
      }

      /* Parse bone groups */
      if (secondary.Has("boneGroups") &&
          secondary.Get("boneGroups").IsArray()) {
        const auto &bgroups = secondary.Get("boneGroups");
        std::vector<SpringBoneGroup> spring_groups_vec;

        for (size_t i = 0; i < bgroups.ArrayLen(); i++) {
          const auto &bg = bgroups.Get((int)i);

          /* Read group-level params (VRM 0.x format) */
          float stiffness = bg.Has("stiffiness")
                                ? (float)bg.Get("stiffiness").Get<double>()
                                : 0.5f;
          float drag = bg.Has("dragForce")
                           ? (float)bg.Get("dragForce").Get<double>()
                           : 0.4f;
          float gravity_power =
              bg.Has("gravityPower")
                  ? (float)bg.Get("gravityPower").Get<double>()
                  : 1.0f;
          float hit_radius = bg.Has("hitRadius")
                                 ? (float)bg.Get("hitRadius").Get<double>()
                                 : 0.02f;
          float gravity_dir[3] = {0, -1, 0};

          if (bg.Has("gravityDir")) {
            const auto &gd = bg.Get("gravityDir");
            gravity_dir[0] = gd.Has("x") ? (float)gd.Get("x").Get<double>() : 0;
            gravity_dir[1] =
                gd.Has("y") ? (float)gd.Get("y").Get<double>() : -1;
            gravity_dir[2] = gd.Has("z") ? (float)gd.Get("z").Get<double>() : 0;
          }

          std::vector<int> collider_groups_vec;
          if (bg.Has("colliderGroups") && bg.Get("colliderGroups").IsArray()) {
            const auto &cgs = bg.Get("colliderGroups");
            collider_groups_vec.reserve(cgs.ArrayLen());
            for (size_t j = 0; j < cgs.ArrayLen(); j++) {
              collider_groups_vec.push_back(cgs.Get((int)j).Get<int>());
            }
          }

          if (bg.Has("bones") && bg.Get("bones").IsArray()) {
            const auto &bones = bg.Get("bones");
            int root_count = (int)bones.ArrayLen();

            for (int root_idx = 0; root_idx < root_count; root_idx++) {
              int root = bones.Get(root_idx).Get<int>();
              if (root < 0 || root >= model->node_count)
                continue;

              std::vector<int> chain;
              chain.push_back(root);
              int current = root;
              while (current >= 0 && current < model->node_count) {
                GltfNode *node = &model->nodes[current];
                if (node->child_count > 0) {
                  current = node->children[0]; /* Follow first child */
                  chain.push_back(current);
                } else {
                  break;
                }
              }

              SpringBoneGroup group = {};
              group.center = -1;
              group.bone_count = (int)chain.size();
              if (group.bone_count > 0) {
                group.bones = (int *)malloc(group.bone_count * sizeof(int));
                for (int j = 0; j < group.bone_count; j++) {
                  group.bones[j] = chain[j];
                }
              }

              if (group.bone_count >= 2) {
                group.joint_count = group.bone_count - 1;
                group.joints = (SpringJoint *)calloc(group.joint_count,
                                                     sizeof(SpringJoint));
                for (int j = 0; j < group.joint_count; j++) {
                  group.joints[j].stiffness = stiffness;
                  group.joints[j].drag_force = drag;
                  group.joints[j].gravity_power = gravity_power;
                  group.joints[j].hit_radius = hit_radius;
                  group.joints[j].gravity_dir[0] = gravity_dir[0];
                  group.joints[j].gravity_dir[1] = gravity_dir[1];
                  group.joints[j].gravity_dir[2] = gravity_dir[2];
                }
              }

              group.collider_group_count = (int)collider_groups_vec.size();
              if (group.collider_group_count > 0) {
                group.collider_groups =
                    (int *)malloc(group.collider_group_count * sizeof(int));
                for (int j = 0; j < group.collider_group_count; j++) {
                  group.collider_groups[j] = collider_groups_vec[j];
                }
              }

              for (int j = 0; j < group.bone_count; j++) {
                int node_idx = group.bones[j];
                if (node_idx >= 0 && node_idx < model->node_count) {
                  model->nodes[node_idx].is_spring_bone = 1;
                }
              }

              spring_groups_vec.push_back(group);
            }
          }
        }

        model->spring_group_count = (int)spring_groups_vec.size();
        if (model->spring_group_count > 0) {
          model->spring_groups = (SpringBoneGroup *)calloc(
              model->spring_group_count, sizeof(SpringBoneGroup));
          for (int i = 0; i < model->spring_group_count; i++) {
            model->spring_groups[i] = spring_groups_vec[i];
          }
        }
      }

      printf("Loaded %d spring bone groups, %d collider groups\n",
             model->spring_group_count, model->spring_collider_count);
    }
  }

  model->loaded = true;
  model->anim_correction_mode = 3;
  model->anim_global_correction[0] = 0.0f;
  model->anim_global_correction[1] = 0.0f;
  model->anim_global_correction[2] = 0.0f;
  model->anim_global_correction[3] = 1.0f;
  model->anim_global_auto_correction[0] = 0.0f;
  model->anim_global_auto_correction[1] = 0.0f;
  model->anim_global_auto_correction[2] = 0.0f;
  model->anim_global_auto_correction[3] = 1.0f;
  gltf_update_transforms(model);

  build_geometry_bounds(gltf, model);

  for (auto &img : gltf.images) {
    img.image.clear();
    img.image.shrink_to_fit();
  }
  for (auto &buf : gltf.buffers) {
    buf.data.clear();
    buf.data.shrink_to_fit();
  }

  if (gpu_texture_bytes > 0 || gpu_buffer_bytes > 0) {
    printf("Approx GPU usage: textures=%.1f MB (base), %.1f MB (with mips), "
           "buffers=%.1f MB\n",
           (double)gpu_texture_bytes / (1024.0 * 1024.0),
           (double)gpu_texture_bytes_mip / (1024.0 * 1024.0),
           (double)gpu_buffer_bytes / (1024.0 * 1024.0));
  }

  return true;
}

extern "C" void gltf_unload_impl(GltfModel *model) {
  /* Free textures */
  for (int i = 0; i < model->texture_count; i++) {
    if (model->textures[i].id) {
      glDeleteTextures(1, &model->textures[i].id);
    }
  }
  free(model->textures);

  /* Free meshes */
  for (int i = 0; i < model->mesh_count; i++) {
    GltfMesh *m = &model->meshes[i];
    for (int j = 0; j < m->primitive_count; j++) {
      GltfPrimitive *p = &m->primitives[j];
      if (p->vao)
        glDeleteVertexArrays(1, &p->vao);
      if (p->vbo_positions)
        glDeleteBuffers(1, &p->vbo_positions);
      if (p->vbo_normals)
        glDeleteBuffers(1, &p->vbo_normals);
      if (p->vbo_texcoords)
        glDeleteBuffers(1, &p->vbo_texcoords);
      if (p->vbo_joints)
        glDeleteBuffers(1, &p->vbo_joints);
      if (p->vbo_weights)
        glDeleteBuffers(1, &p->vbo_weights);
      if (p->ebo)
        glDeleteBuffers(1, &p->ebo);
      if (p->morph_vbo)
        glDeleteTextures(1, &p->morph_vbo);
      free(p->joint_palette);

      /* Free morph targets */
      for (int k = 0; k < p->morph_target_count; k++) {
        free(p->morph_targets[k].position_deltas);
        free(p->morph_targets[k].normal_deltas);
      }
      free(p->morph_targets);
    }
    free(m->primitives);
    free(m->morph_weights);
  }
  free(model->meshes);

  free(model->materials);

  /* Free nodes */
  for (int i = 0; i < model->node_count; i++) {
    free(model->nodes[i].children);
  }
  free(model->nodes);

  /* Free skins */
  for (int i = 0; i < model->skin_count; i++) {
    free(model->skins[i].joints);
    free(model->skins[i].inverse_bind_matrices);
    free(model->skins[i].joint_bounds);
  }
  free(model->skins);

  /* Free animations */
  for (int i = 0; i < model->animation_count; i++) {
    GltfAnimation *a = &model->animations[i];
    for (int j = 0; j < a->sampler_count; j++) {
      free(a->samplers[j].input);
      free(a->samplers[j].output);
    }
    free(a->samplers);
    free(a->channels);
    free(a->node_mask);
  }
  free(model->animations);

  /* Free humanoid mapping */
  free(model->humanoid_names);
  free(model->humanoid_bones);

  /* Free VRM expressions */
  if (model->vrm_expressions) {
    for (int i = 0; i < model->vrm_expressions->expression_count; i++) {
      free(model->vrm_expressions->expressions[i].binds);
    }
    free(model->vrm_expressions->expressions);
    free(model->vrm_expressions);
  }

  free(model->scene_roots);
}

/* Load animation from VRMA file and apply to existing model */
extern "C" bool gltf_load_animation_impl(GltfModel *model,
                                         const char *anim_path, bool neutral_facing) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model anim_gltf;
  std::string err, warn;

  std::string spath(anim_path);
  bool ret;
  if (spath.ends_with(".glb") || spath.ends_with(".vrm") ||
      spath.ends_with(".vrma")) {
    ret = loader.LoadBinaryFromFile(&anim_gltf, &err, &warn, anim_path);
  } else {
    ret = loader.LoadASCIIFromFile(&anim_gltf, &err, &warn, anim_path);
  }

  if (!ret) {
    fprintf(stderr, "Failed to load animation: %s\n", err.c_str());
    return false;
  }

  if (anim_gltf.animations.empty()) {
    fprintf(stderr, "No animations in file\n");
    return false;
  }

  float facing_inverse[4] = {0, 0, 0, 1};
  if (neutral_facing) {
    const auto &extras = anim_gltf.extras;
    if (!extras.IsObject() || !extras.Has("vrmOverlay")) return false;
    const auto &meta = extras.Get("vrmOverlay");
    if (!meta.IsObject() || !meta.Has("normalizedHips") ||
        !meta.Get("normalizedHips").IsBool() || !meta.Get("normalizedHips").Get<bool>() ||
        !meta.Has("facingYaw") || !meta.Get("facingYaw").IsNumber()) return false;
    double yaw = meta.Get("facingYaw").GetNumberAsDouble();
    if (!std::isfinite(yaw) || std::abs(yaw) > 180) return false;
    double angle = -yaw * 3.14159265358979323846 / 360;
    facing_inverse[1] = (float)sin(angle);
    facing_inverse[3] = (float)cos(angle);
  }

  /* Free existing animations */
  for (int i = 0; i < model->animation_count; i++) {
    GltfAnimation *a = &model->animations[i];
    for (int j = 0; j < a->sampler_count; j++) {
      free(a->samplers[j].input);
      free(a->samplers[j].output);
    }
    free(a->samplers);
    free(a->channels);
    free(a->node_mask);
  }
  free(model->animations);
  model->animations = NULL;
  model->animation_count = 0;

  /* Build mapping from animation node index to model node index */
  std::unordered_map<int, int> anim_to_model_node;

  /* Method 1: Direct node name matching (for animations with same node names)
   */
  std::unordered_map<std::string, int> model_node_map;
  for (int i = 0; i < model->node_count; i++) {
    if (model->nodes[i].name[0]) {
      model_node_map[model->nodes[i].name] = i;
    }
  }

  /* Method 2: VRM humanoid bone name mapping (model's humanoid -> model node)
   */
  std::unordered_map<std::string, int> model_humanoid_map;
  if (model->humanoid_names && model->humanoid_bones &&
      model->humanoid_bone_count > 0) {
    for (int i = 0; i < model->humanoid_bone_count; i++) {
      if (model->humanoid_names[i][0]) {
        model_humanoid_map[model->humanoid_names[i]] = model->humanoid_bones[i];
      }
    }
  }

  /* Compute target model's rest rotations (L_B, W_B) for step 2 of spec
   * conversion */
  std::unordered_map<int, std::array<float, 4>> model_local_rest;
  std::unordered_map<int, std::array<float, 4>> model_world_rest;
  std::unordered_map<int, std::array<float, 4>> model_world_bind;

  std::function<void(int, const float *)> compute_model_world =
      [&](int node_idx, const float *parent_world) {
        if (node_idx < 0 || node_idx >= model->node_count)
          return;
        GltfNode *node = &model->nodes[node_idx];
        float *L = node->base_rotation;
        model_local_rest[node_idx] = {L[0], L[1], L[2], L[3]};

        float W[4];
        W[0] = parent_world[3] * L[0] + parent_world[0] * L[3] +
               parent_world[1] * L[2] - parent_world[2] * L[1];
        W[1] = parent_world[3] * L[1] - parent_world[0] * L[2] +
               parent_world[1] * L[3] + parent_world[2] * L[0];
        W[2] = parent_world[3] * L[2] + parent_world[0] * L[1] -
               parent_world[1] * L[0] + parent_world[2] * L[3];
        W[3] = parent_world[3] * L[3] - parent_world[0] * L[0] -
               parent_world[1] * L[1] - parent_world[2] * L[2];
        normalize_quat(W);
        model_world_rest[node_idx] = {W[0], W[1], W[2], W[3]};

        for (int i = 0; i < node->child_count; i++) {
          compute_model_world(node->children[i], W);
        }
      };
  /* Find root nodes and traverse */
  for (int i = 0; i < model->node_count; i++) {
    if (model->nodes[i].parent_index < 0) {
      float identity[4] = {0, 0, 0, 1};
      compute_model_world(i, identity);
    }
  }

  /* Override rest rotations using skin inverse bind matrices when available. */
  if (model->skin_count > 0) {
    for (int skin_idx = 0; skin_idx < model->skin_count; skin_idx++) {
      GltfSkin *skin = &model->skins[skin_idx];
      if (!skin->inverse_bind_matrices)
        continue;

      const float *mesh_world = NULL;
      for (int n = 0; n < model->node_count; n++) {
        if (model->nodes[n].skin_index == skin_idx) {
          mesh_world = model->nodes[n].world_matrix;
          break;
        }
      }
      float mesh_world_local[16];
      if (!mesh_world) {
        memset(mesh_world_local, 0, sizeof(mesh_world_local));
        mesh_world_local[0] = mesh_world_local[5] = mesh_world_local[10] =
            mesh_world_local[15] = 1.0f;
        mesh_world = mesh_world_local;
      }

      for (int j = 0; j < skin->joint_count; j++) {
        int node_idx = skin->joints[j];
        if (node_idx < 0 || node_idx >= model->node_count)
          continue;

        const float *ibm = &skin->inverse_bind_matrices[j * 16];
        float bind[16];
        /* bind = inverse(ibm) */
        float inv[16];
        memcpy(inv, ibm, sizeof(inv));
        /* Invert using cglm-friendly layout */
        mat4 inv_mat;
        mat4 bind_mat;
        memcpy(inv_mat, inv, sizeof(mat4));
        glm_mat4_inv(inv_mat, bind_mat);
        memcpy(bind, bind_mat, sizeof(bind));

        float joint_world[16];
        mat4_mul(mesh_world, bind, joint_world);

        float q[4];
        mat4_to_quat(joint_world, q);
        model_world_bind[node_idx] = {q[0], q[1], q[2], q[3]};
      }
    }
  }

  /* Recompute local/world rest from bind pose when available. */
  if (!model_world_bind.empty()) {
    for (const auto &pair : model_world_bind) {
      model_world_rest[pair.first] = pair.second;
    }
    for (int i = 0; i < model->node_count; i++) {
      auto it = model_world_rest.find(i);
      if (it == model_world_rest.end())
        continue;
      int parent = model->nodes[i].parent_index;
      if (parent >= 0) {
        auto pit = model_world_rest.find(parent);
        if (pit != model_world_rest.end()) {
          float parent_inv[4];
          quat_inv(pit->second.data(), parent_inv);
          float local_q[4];
          quat_mul(parent_inv, it->second.data(), local_q);
          normalize_quat(local_q);
          model_local_rest[i] = {local_q[0], local_q[1], local_q[2],
                                 local_q[3]};
        }
      } else {
        model_local_rest[i] = it->second;
      }
    }
  }

  /* Check for VRMC_vrm_animation extension (VRMA files) */
  std::unordered_map<int, std::array<float, 4>>
      anim_local_rest; /* Local rest rotation */

  if (anim_gltf.extensions.count("VRMC_vrm_animation")) {
    const auto &vrma_ext = anim_gltf.extensions.at("VRMC_vrm_animation");
    if (vrma_ext.Has("humanoid") &&
        vrma_ext.Get("humanoid").Has("humanBones")) {
      const auto &hbones = vrma_ext.Get("humanoid").Get("humanBones");

      for (const auto &key : hbones.Keys()) {
        if (hbones.Get(key).Has("node")) {
          int anim_node = hbones.Get(key).Get("node").GetNumberAsInt();
          /* VRMA uses VRM 1 thumb names; VRM 0 used Proximal/Intermediate. */
          std::string target_key=key;
          for (const char *side : {"left", "right"}) {
            std::string prefix=std::string(side)+"Thumb";
            if (model_humanoid_map.count(prefix+"Intermediate") && hbones.Has(prefix+"Metacarpal")) {
              if (key==prefix+"Metacarpal") target_key=prefix+"Proximal";
              else if (key==prefix+"Proximal") target_key=prefix+"Intermediate";
            }
          }
          auto it = model_humanoid_map.find(target_key);
          if (it != model_humanoid_map.end()) {
            anim_to_model_node[anim_node] = it->second;
            /* Store rest rotation for normalization */
            const auto &node = anim_gltf.nodes[anim_node];
            std::array<float, 4> local_rot = {0, 0, 0, 1};
            if (node.rotation.size() == 4) {
              local_rot = {(float)node.rotation[0], (float)node.rotation[1],
                           (float)node.rotation[2], (float)node.rotation[3]};
            }
            normalize_quat(local_rot.data());
            anim_local_rest[anim_node] = local_rot;
          }
        }
      }

      printf("VRMA: mapped %zu humanoid bones\n", anim_to_model_node.size());
    }
  }
  model->anim_needs_coord_flip = false;

  /* Mixamo bone name to VRM humanoid name mapping */
  static const std::pair<const char *, const char *> mixamo_to_vrm[] = {
      {"Hips", "hips"},
      {"Spine", "spine"},
      {"Spine1", "chest"},
      {"Spine2", "upperChest"},
      {"Neck", "neck"},
      {"Head", "head"},
      {"LeftShoulder", "leftShoulder"},
      {"LeftArm", "leftUpperArm"},
      {"LeftForeArm", "leftLowerArm"},
      {"LeftHand", "leftHand"},
      {"RightShoulder", "rightShoulder"},
      {"RightArm", "rightUpperArm"},
      {"RightForeArm", "rightLowerArm"},
      {"RightHand", "rightHand"},
      {"LeftUpLeg", "leftUpperLeg"},
      {"LeftLeg", "leftLowerLeg"},
      {"LeftFoot", "leftFoot"},
      {"LeftToeBase", "leftToes"},
      {"RightUpLeg", "rightUpperLeg"},
      {"RightLeg", "rightLowerLeg"},
      {"RightFoot", "rightFoot"},
      {"RightToeBase", "rightToes"},
  };

  /* Compute world rest rotations for animation nodes */
  std::unordered_map<int, std::array<float, 4>> anim_world_rest;
  std::function<void(int, const float *)> compute_world_rot =
      [&](int node_idx, const float *parent_world) {
        const auto &node = anim_gltf.nodes[node_idx];
        float local[4] = {0, 0, 0, 1};
        if (node.rotation.size() == 4) {
          local[0] = (float)node.rotation[0];
          local[1] = (float)node.rotation[1];
          local[2] = (float)node.rotation[2];
          local[3] = (float)node.rotation[3];
          normalize_quat(local);
        }
        float world[4];
        /* world = parent_world * local */
        world[0] = parent_world[3] * local[0] + parent_world[0] * local[3] +
                   parent_world[1] * local[2] - parent_world[2] * local[1];
        world[1] = parent_world[3] * local[1] - parent_world[0] * local[2] +
                   parent_world[1] * local[3] + parent_world[2] * local[0];
        world[2] = parent_world[3] * local[2] + parent_world[0] * local[1] -
                   parent_world[1] * local[0] + parent_world[2] * local[3];
        world[3] = parent_world[3] * local[3] - parent_world[0] * local[0] -
                   parent_world[1] * local[1] - parent_world[2] * local[2];
        normalize_quat(world);
        anim_world_rest[node_idx] = {world[0], world[1], world[2], world[3]};
        for (int child : node.children)
          compute_world_rot(child, world);
      };
  if (!anim_gltf.scenes.empty() && !anim_gltf.scenes[0].nodes.empty()) {
    for (int scene_idx : anim_gltf.scenes[0].nodes) {
      float identity[4] = {0, 0, 0, 1};
      compute_world_rot(scene_idx, identity);
    }
  }

  auto get_source_dir = [&](int node_idx, float *out) -> bool {
    if (node_idx < 0 || node_idx >= (int)anim_gltf.nodes.size())
      return false;
    const auto &node = anim_gltf.nodes[node_idx];
    if (node.children.empty())
      return false;
    float best_len = 0.0f;
    bool found = false;
    for (int child_idx : node.children) {
      if (child_idx < 0 || child_idx >= (int)anim_gltf.nodes.size())
        continue;
      const auto &child = anim_gltf.nodes[child_idx];
      if (child.translation.size() != 3)
        continue;
      float v[3] = {(float)child.translation[0], (float)child.translation[1],
                    (float)child.translation[2]};
      float len = vec3_len(v);
      if (len > best_len) {
        best_len = len;
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
        found = true;
      }
    }
    return found && best_len > 1e-6f;
  };
  auto get_target_dir = [&](int node_idx, float *out) -> bool {
    if (node_idx < 0 || node_idx >= model->node_count)
      return false;
    const auto &node = model->nodes[node_idx];
    if (node.child_count <= 0)
      return false;
    float best_len = 0.0f;
    bool found = false;
    for (int i = 0; i < node.child_count; i++) {
      int child_idx = node.children[i];
      if (child_idx < 0 || child_idx >= model->node_count)
        continue;
      const auto &child = model->nodes[child_idx];
      float v[3] = {child.base_translation[0], child.base_translation[1],
                    child.base_translation[2]};
      float len = vec3_len(v);
      if (len > best_len) {
        best_len = len;
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
        found = true;
      }
    }
    return found && best_len > 1e-6f;
  };
  auto candidate_rotations = []() {
    std::vector<std::array<float, 9>> mats;
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                    {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    auto perm_sign = [](const int *p) {
      int inv = 0;
      for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
          if (p[i] > p[j])
            inv++;
        }
      }
      return (inv % 2 == 0) ? 1 : -1;
    };
    for (int pi = 0; pi < 6; pi++) {
      const int *p = perms[pi];
      int ps = perm_sign(p);
      for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
          for (int sz = -1; sz <= 1; sz += 2) {
            int det = ps * sx * sy * sz;
            if (det != 1)
              continue;
            float m[9] = {0};
            float axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            const int signs[3] = {sx, sy, sz};
            for (int col = 0; col < 3; col++) {
              int axis = p[col];
              m[col * 3 + 0] = axes[axis][0] * signs[col];
              m[col * 3 + 1] = axes[axis][1] * signs[col];
              m[col * 3 + 2] = axes[axis][2] * signs[col];
            }
            mats.push_back(
                {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]});
          }
        }
      }
    }
    return mats;
  };
  std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> dir_pairs;
  for (const auto &pair : anim_to_model_node) {
    int anim_node = pair.first;
    int model_node = pair.second;
    float src_dir[3];
    float dst_dir[3];
    if (get_source_dir(anim_node, src_dir) &&
        get_target_dir(model_node, dst_dir)) {
      vec3_norm(src_dir);
      vec3_norm(dst_dir);
      dir_pairs.push_back({{src_dir[0], src_dir[1], src_dir[2]},
                           {dst_dir[0], dst_dir[1], dst_dir[2]}});
    }
  }
  float best_corr_q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  if (!dir_pairs.empty()) {
    float best_score = -1e9f;
    auto mats = candidate_rotations();
    for (const auto &m : mats) {
      float score = 0.0f;
      for (const auto &pair : dir_pairs) {
        const float *s = pair.first.data();
        const float *d = pair.second.data();
        float v[3] = {m[0] * s[0] + m[3] * s[1] + m[6] * s[2],
                      m[1] * s[0] + m[4] * s[1] + m[7] * s[2],
                      m[2] * s[0] + m[5] * s[1] + m[8] * s[2]};
        score += vec3_dot(v, d);
      }
      if (score > best_score) {
        best_score = score;
        float m4[16] = {m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f,
                        m[6], m[7], m[8], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        mat4_to_quat(m4, best_corr_q);
        normalize_quat(best_corr_q);
      }
    }
  }
  model->anim_global_auto_correction[0] = best_corr_q[0];
  model->anim_global_auto_correction[1] = best_corr_q[1];
  model->anim_global_auto_correction[2] = best_corr_q[2];
  model->anim_global_auto_correction[3] = best_corr_q[3];
  printf("Animation: auto_correction %.4f %.4f %.4f %.4f (pairs=%zu)\n",
         best_corr_q[0], best_corr_q[1], best_corr_q[2], best_corr_q[3],
         dir_pairs.size());
  if (model->anim_correction_mode == 1) {
    model->anim_global_correction[0] = model->anim_global_auto_correction[0];
    model->anim_global_correction[1] = model->anim_global_auto_correction[1];
    model->anim_global_correction[2] = model->anim_global_auto_correction[2];
    model->anim_global_correction[3] = model->anim_global_auto_correction[3];
  }

  /* Fallback: Mixamo name mapping, then direct node name matching */
  bool is_mixamo = false;
  for (size_t i = 0; i < anim_gltf.nodes.size(); i++) {
    if (anim_to_model_node.count(i))
      continue;
    const std::string &name = anim_gltf.nodes[i].name;
    if (name.empty())
      continue;

    /* Try Mixamo to VRM mapping */
    for (const auto &[mixamo, vrm] : mixamo_to_vrm) {
      if (name == mixamo) {
        auto it = model_humanoid_map.find(vrm);
        if (it != model_humanoid_map.end()) {
          anim_to_model_node[i] = it->second;
          is_mixamo = true;
          /* Store rest pose from node rotation */
          const auto &node = anim_gltf.nodes[i];
          if (node.rotation.size() == 4) {
            anim_local_rest[i] = {
                (float)node.rotation[0], (float)node.rotation[1],
                (float)node.rotation[2], (float)node.rotation[3]};
            normalize_quat(anim_local_rest[i].data());
          }
        }
        break;
      }
    }

    /* Try VRM humanoid name directly (animation uses humanoid names as node
     * names) */
    if (!anim_to_model_node.count(i)) {
      auto it = model_humanoid_map.find(name);
      if (it != model_humanoid_map.end()) {
        anim_to_model_node[i] = it->second;
        const auto &node = anim_gltf.nodes[i];
        if (node.rotation.size() == 4) {
          anim_local_rest[i] = {
              (float)node.rotation[0], (float)node.rotation[1],
              (float)node.rotation[2], (float)node.rotation[3]};
          normalize_quat(anim_local_rest[i].data());
        } else {
          anim_local_rest[i] = {0.0f, 0.0f, 0.0f, 1.0f};
        }
      }
    }

    /* Try direct name match */
    if (!anim_to_model_node.count(i)) {
      auto it = model_node_map.find(name);
      if (it != model_node_map.end()) {
        anim_to_model_node[i] = it->second;
        const auto &node = anim_gltf.nodes[i];
        if (node.rotation.size() == 4) {
          anim_local_rest[i] = {
              (float)node.rotation[0], (float)node.rotation[1],
              (float)node.rotation[2], (float)node.rotation[3]};
          normalize_quat(anim_local_rest[i].data());
        } else {
          anim_local_rest[i] = {0.0f, 0.0f, 0.0f, 1.0f};
        }
      }
    }
  }

  model->anim_needs_coord_flip = false;
  model->anim_is_mixamo = is_mixamo;
  printf("Animation: mapped %zu nodes, coord_flip=%d, mixamo=%d\n",
         anim_to_model_node.size(), model->anim_needs_coord_flip, is_mixamo);

  /* Quaternion helpers */
  auto qmul = quat_mul;
  auto qinv = quat_inv;

  /* Load animations, remapping node indices */
  model->animation_count = (int)anim_gltf.animations.size();
  model->animations =
      (GltfAnimation *)calloc(model->animation_count, sizeof(GltfAnimation));

  for (size_t i = 0; i < anim_gltf.animations.size(); i++) {
    const auto &anim = anim_gltf.animations[i];
    GltfAnimation *a = &model->animations[i];

    copy_name(a->name, anim.name, sizeof(a->name));

    /* Load samplers */
    a->sampler_count = (int)anim.samplers.size();
    a->samplers =
        (GltfAnimSampler *)calloc(a->sampler_count, sizeof(GltfAnimSampler));
    for (size_t j = 0; j < anim.samplers.size(); j++) {
      const auto &sampler = anim.samplers[j];
      GltfAnimSampler *s = &a->samplers[j];

      const auto &input_acc = anim_gltf.accessors[sampler.input];
      const auto &input_view = anim_gltf.bufferViews[input_acc.bufferView];
      const auto &input_buf = anim_gltf.buffers[input_view.buffer];
      const float *input_data =
          (const float *)(input_buf.data.data() + input_view.byteOffset +
                          input_acc.byteOffset);

      const auto &output_acc = anim_gltf.accessors[sampler.output];
      const auto &output_view = anim_gltf.bufferViews[output_acc.bufferView];
      const auto &output_buf = anim_gltf.buffers[output_view.buffer];
      const float *output_data =
          (const float *)(output_buf.data.data() + output_view.byteOffset +
                          output_acc.byteOffset);

      int input_count = (int)input_acc.count;
      int output_count = (int)output_acc.count;
      int keyframe_count =
          input_count < output_count ? input_count : output_count;
      if (input_count != output_count) {
        fprintf(stderr,
                "Warning: sampler %zu input/output count mismatch (%d/%d), "
                "clamping to %d\n",
                j, input_count, output_count, keyframe_count);
      }

      s->keyframe_count = keyframe_count;
      s->input = (float *)malloc(s->keyframe_count * sizeof(float));
      memcpy(s->input, input_data, s->keyframe_count * sizeof(float));

      if (s->keyframe_count > 0 &&
          s->input[s->keyframe_count - 1] > a->duration) {
        a->duration = s->input[s->keyframe_count - 1];
      }

      int components = 1;
      if (output_acc.type == TINYGLTF_TYPE_VEC3)
        components = 3;
      else if (output_acc.type == TINYGLTF_TYPE_VEC4)
        components = 4;

      s->output_stride = components;
      s->output =
          (float *)malloc(s->keyframe_count * components * sizeof(float));
      memcpy(s->output, output_data,
             s->keyframe_count * components * sizeof(float));
    }

    /* Load channels, remapping node indices */
    a->channel_count = (int)anim.channels.size();
    a->channels =
        (GltfAnimChannel *)calloc(a->channel_count, sizeof(GltfAnimChannel));
    int matched = 0;
    a->node_mask_count = model->node_count;
    a->node_mask = (uint8_t *)calloc(a->node_mask_count, sizeof(uint8_t));
    for (size_t j = 0; j < anim.channels.size(); j++) {
      const auto &channel = anim.channels[j];
      GltfAnimChannel *c = &a->channels[j];

      c->sampler_index = channel.sampler;
      bool sampler_valid =
          c->sampler_index >= 0 && c->sampler_index < a->sampler_count;
      if (!sampler_valid) {
        fprintf(stderr,
                "Warning: channel %zu has invalid sampler index %d "
                "(samplers=%d); disabling channel\n",
                j, c->sampler_index, a->sampler_count);
        c->target_node = -1;
      }

      int anim_node_idx = channel.target_node;
      auto it = anim_to_model_node.find(anim_node_idx);
      /* Undo only the canonical hips' baked facing, in source space, before
       * rest-pose retargeting. Both samplers then contain a forward gait and
       * inertial blending never has a second yaw turn to compose with travel. */
      auto hips = model_humanoid_map.find("hips");
      if (neutral_facing && sampler_valid && it != anim_to_model_node.end() &&
          hips != model_humanoid_map.end() && it->second == hips->second) {
        auto &s = a->samplers[c->sampler_index];
        if (channel.target_path == "rotation" && s.output_stride == 4) {
          for (int k = 0; k < s.keyframe_count; k++) {
            float rotated[4];
            qmul(facing_inverse, &s.output[k*4], rotated);
            memcpy(&s.output[k*4], rotated, sizeof(rotated));
          }
        } else if (channel.target_path == "translation" && s.output_stride == 3) {
          const auto &origin = anim_gltf.nodes[anim_node_idx].translation;
          if (origin.size() == 3) for (int k = 0; k < s.keyframe_count; k++) {
            vec3 delta, rotated;
            for (int axis = 0; axis < 3; axis++) delta[axis] = s.output[k*3+axis] - (float)origin[axis];
            glm_quat_rotatev(facing_inverse, delta, rotated);
            for (int axis = 0; axis < 3; axis++) s.output[k*3+axis] = (float)origin[axis] + rotated[axis];
          }
        }
      }
      if (it != anim_to_model_node.end()) {
        if (sampler_valid) {
          c->target_node = it->second;
        }
        matched++;

        /* Process rotation keyframes */
        if (channel.target_path == "rotation" && sampler_valid) {
          GltfAnimSampler *s = &a->samplers[c->sampler_index];
          if (s->output_stride != 4) {
            fprintf(stderr,
                    "Warning: rotation channel sampler stride %d, expected 4; "
                    "disabling channel\n",
                    s->output_stride);
            c->target_node = -1;
          } else {
            int model_node = c->target_node;

            /* Get source rest rotations (A) */
            auto src_local_it = anim_local_rest.find(anim_node_idx);
            auto src_world_it = anim_world_rest.find(anim_node_idx);

            /* Get target rest rotations (B) */
            auto tgt_local_it = model_local_rest.find(model_node);
            auto tgt_world_it = model_world_rest.find(model_node);

            if (src_local_it != anim_local_rest.end() &&
                src_world_it != anim_world_rest.end() &&
                tgt_local_it != model_local_rest.end() &&
                tgt_world_it != model_world_rest.end()) {
              const auto &L_A = src_local_it->second;
              const auto &W_A = src_world_it->second;
              const auto &L_B = tgt_local_it->second;
              const auto &W_B = tgt_world_it->second;

              bool src_non_identity =
                  std::abs(L_A[0]) > 0.001f || std::abs(L_A[1]) > 0.001f ||
                  std::abs(L_A[2]) > 0.001f ||
                  std::abs(L_A[3] - 1.0f) > 0.001f ||
                  std::abs(W_A[0]) > 0.001f || std::abs(W_A[1]) > 0.001f ||
                  std::abs(W_A[2]) > 0.001f || std::abs(W_A[3] - 1.0f) > 0.001f;
              bool tgt_non_identity =
                  std::abs(L_B[0]) > 0.001f || std::abs(L_B[1]) > 0.001f ||
                  std::abs(L_B[2]) > 0.001f ||
                  std::abs(L_B[3] - 1.0f) > 0.001f ||
                  std::abs(W_B[0]) > 0.001f || std::abs(W_B[1]) > 0.001f ||
                  std::abs(W_B[2]) > 0.001f || std::abs(W_B[3] - 1.0f) > 0.001f;

              bool tgt_identity = !tgt_non_identity;

              if ((src_non_identity || tgt_non_identity) && !tgt_identity) {
                float L_A_inv[4], W_A_inv[4], W_B_inv[4];
                qinv(L_A.data(), L_A_inv);
                qinv(W_A.data(), W_A_inv);
                qinv(W_B.data(), W_B_inv);

                for (int k = 0; k < s->keyframe_count; k++) {
                  float *q = &s->output[k * 4];
                  float t1[4], t2[4], normalized[4], t3[4], t4[4];

                  /* Step 1: Normalized = W_A * L_A^-1 * anim * W_A^-1 */
                  qmul(L_A_inv, q, t1);
                  qmul(W_A.data(), t1, t2);
                  qmul(t2, W_A_inv, normalized);

                  /* Step 2: B.Local = L_B * W_B^-1 * Normalized * W_B */
                  qmul(W_B_inv, normalized, t3);
                  qmul(L_B.data(), t3, t4);
                  qmul(t4, W_B.data(), q);
                  float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] +
                                    q[3] * q[3]);
                  if (len > 0.0001f) {
                    q[0] /= len;
                    q[1] /= len;
                    q[2] /= len;
                    q[3] /= len;
                  }
                }
              }

              if (tgt_identity && src_non_identity) {
                float L_A_inv[4], W_A_inv[4];
                qinv(L_A.data(), L_A_inv);
                qinv(W_A.data(), W_A_inv);
                for (int k = 0; k < s->keyframe_count; k++) {
                  float *q = &s->output[k * 4];
                  float t1[4], t2[4], normalized[4];
                  qmul(L_A_inv, q, t1);
                  qmul(W_A.data(), t1, t2);
                  qmul(t2, W_A_inv, normalized);
                  q[0] = normalized[0];
                  q[1] = normalized[1];
                  q[2] = normalized[2];
                  q[3] = normalized[3];
                  normalize_quat(q);
                }
              }

              (void)tgt_identity;
            }
          }
        }
      } else {
        c->target_node = -1;
      }

      if (channel.target_path == "translation")
        c->path = GLTF_PATH_TRANSLATION;
      else if (channel.target_path == "rotation")
        c->path = GLTF_PATH_ROTATION;
      else if (channel.target_path == "scale")
        c->path = GLTF_PATH_SCALE;
      else if (channel.target_path == "weights")
        c->path = GLTF_PATH_WEIGHTS;

      /* Starter VRMA clips use a canonical hips height of one metre. Adapt
       * pose displacement to the target VRM's hips, preserving target offsets.
       * This marker deliberately leaves existing unmarked clips unchanged. */
      if (c->path==GLTF_PATH_TRANSLATION && c->target_node>=0 && sampler_valid &&
          anim_gltf.extras.IsObject() && anim_gltf.extras.Has("vrmOverlay")) {
        const auto &extra=anim_gltf.extras.Get("vrmOverlay");
        auto hips=model_humanoid_map.find("hips");
        if (extra.IsObject() && extra.Has("normalizedHips") && extra.Get("normalizedHips").IsBool() &&
            extra.Get("normalizedHips").Get<bool>() && hips!=model_humanoid_map.end() && c->target_node==hips->second) {
          const auto &src=anim_gltf.nodes[anim_node_idx];
          auto &sampler=a->samplers[c->sampler_index];
          const auto &target=model->nodes[c->target_node];
          if (src.translation.size()==3 && src.translation[1]>0.1 && sampler.output_stride==3 && target.base_translation[1]>0) {
            float scale=target.base_translation[1]/(float)src.translation[1];
            for(int k=0;k<sampler.keyframe_count;k++) for(int axis=0;axis<3;axis++)
              sampler.output[k*3+axis]=target.base_translation[axis]+(sampler.output[k*3+axis]-(float)src.translation[axis])*scale;
          }
        }
      }

      if (sampler_valid) {
        int expected = 0;
        if (c->path == GLTF_PATH_TRANSLATION || c->path == GLTF_PATH_SCALE)
          expected = 3;
        else if (c->path == GLTF_PATH_ROTATION)
          expected = 4;
        if (expected > 0) {
          GltfAnimSampler *s = &a->samplers[c->sampler_index];
          if (s->output_stride < expected) {
            fprintf(stderr,
                    "Warning: channel '%s' expects %d components but sampler "
                    "has %d; disabling channel\n",
                    channel.target_path.c_str(), expected, s->output_stride);
            c->target_node = -1;
          }
        }
      }

      /* Skip root (hips) translation if it looks like world-space or wrong
       * scale */
      if (c->path == GLTF_PATH_TRANSLATION && c->target_node >= 0) {
        int hips_node = -1;
        for (int h = 0; h < model->humanoid_bone_count; h++) {
          if (strcmp(model->humanoid_names[h], "hips") == 0) {
            hips_node = model->humanoid_bones[h];
            break;
          }
        }
        if (c->target_node == hips_node && sampler_valid) {
          /* Check first translation value */
          GltfAnimSampler *s = &a->samplers[c->sampler_index];
          if (s->output && s->keyframe_count > 0 && s->output_stride >= 2) {
            float ty = s->output[1]; /* Y component */
            float model_hips_y = model->nodes[hips_node].base_translation[1];
            /* If animation hips Y is way off (negative or >2x model), skip
             * translation */
            if (ty < -1.0f || fabsf(ty - model_hips_y) > 2.0f) {
              printf("Skipping root translation (anim Y=%.2f, model Y=%.2f)\n",
                     ty, model_hips_y);
              c->target_node = -1; /* Disable this channel */
            }
          }
        }
      }

      if (c->target_node >= 0 && c->target_node < a->node_mask_count) {
        if (c->path == GLTF_PATH_TRANSLATION)
          a->node_mask[c->target_node] |= 1;
        else if (c->path == GLTF_PATH_ROTATION)
          a->node_mask[c->target_node] |= 2;
        else if (c->path == GLTF_PATH_SCALE)
          a->node_mask[c->target_node] |= 4;
      }
    }
    printf("Animation '%s': %d/%d channels mapped\n", a->name, matched,
           a->channel_count);
  }

  printf("Loaded %d animations from %s (duration: %.2fs)\n",
         model->animation_count, anim_path,
         model->animation_count > 0 ? model->animations[0].duration : 0.0f);

  return true;
}
