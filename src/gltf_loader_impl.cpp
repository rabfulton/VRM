/*
 * VRM Overlay - glTF/GLB Loader C++ Implementation
 *
 * Uses tinygltf for actual parsing. This file bridges C++ tinygltf
 * to our C interface.
 */

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "tiny_gltf.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "gltf_loader.h"
}

/* Helper to copy string safely */
static void copy_name(char *dst, const std::string &src, size_t max_len) {
  size_t len = src.length();
  if (len >= max_len)
    len = max_len - 1;
  memcpy(dst, src.c_str(), len);
  dst[len] = '\0';
}

/* Create OpenGL texture from glTF image */
static GLuint create_texture(const tinygltf::Image &image) {
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

  return tex;
}

/* Create OpenGL buffers for a primitive */
static void create_primitive_buffers(GltfPrimitive *prim,
                                     const tinygltf::Model &gltf,
                                     const tinygltf::Primitive &tprim) {
  glGenVertexArrays(1, &prim->vao);
  glBindVertexArray(prim->vao);

  /* Helper lambda to get accessor data */
  auto get_buffer = [&](int accessor_idx) -> const uint8_t * {
    if (accessor_idx < 0)
      return nullptr;
    const auto &accessor = gltf.accessors[accessor_idx];
    const auto &view = gltf.bufferViews[accessor.bufferView];
    const auto &buffer = gltf.buffers[view.buffer];
    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
  };

  /* Positions (required) */
  auto it = tprim.attributes.find("POSITION");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    const uint8_t *data = get_buffer(it->second);

    glGenBuffers(1, &prim->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, accessor.count * 3 * sizeof(float), data,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
  }

  /* Normals */
  it = tprim.attributes.find("NORMAL");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    const uint8_t *data = get_buffer(it->second);

    glGenBuffers(1, &prim->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER, accessor.count * 3 * sizeof(float), data,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);
    prim->has_normals = true;
  }

  /* Texcoords */
  it = tprim.attributes.find("TEXCOORD_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    const uint8_t *data = get_buffer(it->second);

    glGenBuffers(1, &prim->vbo_texcoords);
    glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_texcoords);
    glBufferData(GL_ARRAY_BUFFER, accessor.count * 2 * sizeof(float), data,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(2);
    prim->has_texcoords = true;
  }

  /* Joints */
  it = tprim.attributes.find("JOINTS_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    const uint8_t *data = get_buffer(it->second);

    glGenBuffers(1, &prim->vbo_joints);
    glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_joints);

    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
      glBufferData(GL_ARRAY_BUFFER, accessor.count * 4, data, GL_STATIC_DRAW);
      glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, 0, nullptr);
    } else {
      glBufferData(GL_ARRAY_BUFFER, accessor.count * 4 * sizeof(uint16_t), data,
                   GL_STATIC_DRAW);
      glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, 0, nullptr);
    }
    glEnableVertexAttribArray(3);
    prim->has_skinning = true;
  }

  /* Weights */
  it = tprim.attributes.find("WEIGHTS_0");
  if (it != tprim.attributes.end()) {
    const auto &accessor = gltf.accessors[it->second];
    const uint8_t *data = get_buffer(it->second);

    glGenBuffers(1, &prim->vbo_weights);
    glBindBuffer(GL_ARRAY_BUFFER, prim->vbo_weights);
    glBufferData(GL_ARRAY_BUFFER, accessor.count * 4 * sizeof(float), data,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(4);
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
  }

  prim->material_index = tprim.material;

  glBindVertexArray(0);
}

extern "C" bool gltf_load_impl(GltfModel *model, const char *path) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model gltf;
  std::string err, warn;

  bool ret;
  std::string spath(path);
  if (spath.ends_with(".glb") || spath.ends_with(".vrm") || spath.ends_with(".vrma")) {
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

  printf("Loaded glTF: %zu meshes, %zu materials, %zu textures, %zu nodes, %zu skins\n",
         gltf.meshes.size(), gltf.materials.size(), gltf.textures.size(),
         gltf.nodes.size(), gltf.skins.size());

  /* Count nodes with meshes */
  int mesh_nodes = 0;
  for (size_t i = 0; i < gltf.nodes.size(); i++) {
    if (gltf.nodes[i].mesh >= 0) mesh_nodes++;
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
        model->textures[i].id = create_texture(gltf.images[img_idx]);
        model->textures[i].width = gltf.images[img_idx].width;
        model->textures[i].height = gltf.images[img_idx].height;
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
        create_primitive_buffers(&m->primitives[j], gltf, mesh.primitives[j]);
      }

      /* Morph targets */
      m->morph_target_count = (int)mesh.weights.size();
      if (m->morph_target_count > 0) {
        m->morph_weights =
            (float *)calloc(m->morph_target_count, sizeof(float));
        for (size_t j = 0; j < mesh.weights.size(); j++) {
          m->morph_weights[j] = (float)mesh.weights[j];
        }
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
      }
      if (node.scale.size() == 3) {
        n->scale[0] = (float)node.scale[0];
        n->scale[1] = (float)node.scale[1];
        n->scale[2] = (float)node.scale[2];
      }

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
    model->animations = (GltfAnimation *)calloc(model->animation_count, sizeof(GltfAnimation));
    for (size_t i = 0; i < gltf.animations.size(); i++) {
      const auto &anim = gltf.animations[i];
      GltfAnimation *a = &model->animations[i];

      copy_name(a->name, anim.name, sizeof(a->name));

      /* Load samplers */
      a->sampler_count = (int)anim.samplers.size();
      a->samplers = (GltfAnimSampler *)calloc(a->sampler_count, sizeof(GltfAnimSampler));
      for (size_t j = 0; j < anim.samplers.size(); j++) {
        const auto &sampler = anim.samplers[j];
        GltfAnimSampler *s = &a->samplers[j];

        /* Input (times) */
        const auto &input_acc = gltf.accessors[sampler.input];
        const auto &input_view = gltf.bufferViews[input_acc.bufferView];
        const auto &input_buf = gltf.buffers[input_view.buffer];
        const float *input_data = (const float *)(input_buf.data.data() + input_view.byteOffset + input_acc.byteOffset);

        s->keyframe_count = (int)input_acc.count;
        s->input = (float *)malloc(s->keyframe_count * sizeof(float));
        memcpy(s->input, input_data, s->keyframe_count * sizeof(float));

        /* Track max time for duration */
        if (s->keyframe_count > 0 && s->input[s->keyframe_count - 1] > a->duration) {
          a->duration = s->input[s->keyframe_count - 1];
        }

        /* Output (values) */
        const auto &output_acc = gltf.accessors[sampler.output];
        const auto &output_view = gltf.bufferViews[output_acc.bufferView];
        const auto &output_buf = gltf.buffers[output_view.buffer];
        const float *output_data = (const float *)(output_buf.data.data() + output_view.byteOffset + output_acc.byteOffset);

        int components = 1;
        if (output_acc.type == TINYGLTF_TYPE_VEC3) components = 3;
        else if (output_acc.type == TINYGLTF_TYPE_VEC4) components = 4;

        s->output_stride = components;
        s->output = (float *)malloc(s->keyframe_count * components * sizeof(float));
        memcpy(s->output, output_data, s->keyframe_count * components * sizeof(float));
      }

      /* Load channels */
      a->channel_count = (int)anim.channels.size();
      a->channels = (GltfAnimChannel *)calloc(a->channel_count, sizeof(GltfAnimChannel));
      for (size_t j = 0; j < anim.channels.size(); j++) {
        const auto &channel = anim.channels[j];
        GltfAnimChannel *c = &a->channels[j];

        c->sampler_index = channel.sampler;
        c->target_node = channel.target_node;

        if (channel.target_path == "translation") c->path = GLTF_PATH_TRANSLATION;
        else if (channel.target_path == "rotation") c->path = GLTF_PATH_ROTATION;
        else if (channel.target_path == "scale") c->path = GLTF_PATH_SCALE;
        else if (channel.target_path == "weights") c->path = GLTF_PATH_WEIGHTS;
      }
    }
    printf("Loaded %d animations\n", model->animation_count);
  }

  /* Load VRM humanoid bone mapping */
  if (gltf.extensions.count("VRM")) {
    const auto &vrm = gltf.extensions.at("VRM");
    if (vrm.Has("humanoid") && vrm.Get("humanoid").Has("humanBones")) {
      const auto &bones = vrm.Get("humanoid").Get("humanBones");
      if (bones.IsArray()) {
        model->humanoid_bone_count = (int)bones.ArrayLen();
        model->humanoid_names = (char(*)[32])calloc(model->humanoid_bone_count, 32);
        model->humanoid_bones = (int *)malloc(model->humanoid_bone_count * sizeof(int));
        
        for (size_t i = 0; i < bones.ArrayLen(); i++) {
          const auto &b = bones.Get((int)i);
          if (b.Has("bone") && b.Has("node")) {
            std::string name = b.Get("bone").Get<std::string>();
            int node = b.Get("node").Get<int>();
            strncpy(model->humanoid_names[i], name.c_str(), 31);
            model->humanoid_bones[i] = node;
          }
        }
        printf("Loaded %d humanoid bone mappings\n", model->humanoid_bone_count);
      }
    }
  }

  model->loaded = true;
  gltf_update_transforms(model);

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
  }
  free(model->animations);

  /* Free humanoid mapping */
  free(model->humanoid_names);
  free(model->humanoid_bones);

  free(model->scene_roots);
}


/* Load animation from VRMA file and apply to existing model */
extern "C" bool gltf_load_animation_impl(GltfModel *model, const char *anim_path) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model anim_gltf;
  std::string err, warn;

  std::string spath(anim_path);
  bool ret;
  if (spath.ends_with(".glb") || spath.ends_with(".vrm") || spath.ends_with(".vrma")) {
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

  /* Free existing animations */
  for (int i = 0; i < model->animation_count; i++) {
    GltfAnimation *a = &model->animations[i];
    for (int j = 0; j < a->sampler_count; j++) {
      free(a->samplers[j].input);
      free(a->samplers[j].output);
    }
    free(a->samplers);
    free(a->channels);
  }
  free(model->animations);
  model->animations = NULL;
  model->animation_count = 0;

  /* Build mapping from animation node index to model node index */
  std::unordered_map<int, int> anim_to_model_node;
  
  /* Method 1: Direct node name matching (for animations with same node names) */
  std::unordered_map<std::string, int> model_node_map;
  for (int i = 0; i < model->node_count; i++) {
    if (model->nodes[i].name[0]) {
      model_node_map[model->nodes[i].name] = i;
    }
  }
  
  /* Method 2: VRM humanoid bone name mapping (model's humanoid -> model node) */
  std::unordered_map<std::string, int> model_humanoid_map;
  if (model->humanoid_names && model->humanoid_bones && model->humanoid_bone_count > 0) {
    for (int i = 0; i < model->humanoid_bone_count; i++) {
      if (model->humanoid_names[i][0]) {
        model_humanoid_map[model->humanoid_names[i]] = model->humanoid_bones[i];
      }
    }
  }
  
  /* Check for VRMC_vrm_animation extension (VRMA files) */
  std::unordered_map<int, std::array<float,4>> anim_local_rest;  /* Local rest rotation */
  bool needs_coord_flip = false;  /* Detect if VRMA uses different coordinate system */
  
  if (anim_gltf.extensions.count("VRMC_vrm_animation")) {
    const auto &vrma_ext = anim_gltf.extensions.at("VRMC_vrm_animation");
    if (vrma_ext.Has("humanoid") && vrma_ext.Get("humanoid").Has("humanBones")) {
      const auto &hbones = vrma_ext.Get("humanoid").Get("humanBones");
      
      /* Check if coordinate system is flipped by looking at leftUpperLeg position */
      if (hbones.Has("leftUpperLeg")) {
        int node_idx = hbones.Get("leftUpperLeg").Get("node").GetNumberAsInt();
        const auto &node = anim_gltf.nodes[node_idx];
        if (node.translation.size() >= 1 && node.translation[0] > 0) {
          needs_coord_flip = true;  /* Left leg has positive X = flipped */
          printf("VRMA: detected flipped coordinate system\n");
        }
      }
      
      for (const auto &key : hbones.Keys()) {
        if (hbones.Get(key).Has("node")) {
          int anim_node = hbones.Get(key).Get("node").GetNumberAsInt();
          auto it = model_humanoid_map.find(key);
          if (it != model_humanoid_map.end()) {
            anim_to_model_node[anim_node] = it->second;
            
            /* Store rest rotation for normalization */
            const auto &node = anim_gltf.nodes[anim_node];
            std::array<float,4> local_rot = {0,0,0,1};
            if (node.rotation.size() == 4) {
              local_rot = {(float)node.rotation[0], (float)node.rotation[1], 
                          (float)node.rotation[2], (float)node.rotation[3]};
            }
            anim_local_rest[anim_node] = local_rot;
          }
        }
      }
      printf("VRMA: mapped %zu humanoid bones, coord_flip=%d\n", anim_to_model_node.size(), needs_coord_flip);
    }
  }
  
  /* Store coord flip flag in model */
  model->anim_needs_coord_flip = needs_coord_flip;
  
  /* Mixamo bone name to VRM humanoid name mapping */
  static const std::pair<const char*, const char*> mixamo_to_vrm[] = {
    {"Hips", "hips"}, {"Spine", "spine"}, {"Spine1", "chest"}, {"Spine2", "upperChest"},
    {"Neck", "neck"}, {"Head", "head"},
    {"LeftShoulder", "leftShoulder"}, {"LeftArm", "leftUpperArm"}, 
    {"LeftForeArm", "leftLowerArm"}, {"LeftHand", "leftHand"},
    {"RightShoulder", "rightShoulder"}, {"RightArm", "rightUpperArm"},
    {"RightForeArm", "rightLowerArm"}, {"RightHand", "rightHand"},
    {"LeftUpLeg", "leftUpperLeg"}, {"LeftLeg", "leftLowerLeg"},
    {"LeftFoot", "leftFoot"}, {"LeftToeBase", "leftToes"},
    {"RightUpLeg", "rightUpperLeg"}, {"RightLeg", "rightLowerLeg"},
    {"RightFoot", "rightFoot"}, {"RightToeBase", "rightToes"},
  };
  
  /* Compute world rest rotations for animation nodes */
  std::unordered_map<int, std::array<float,4>> anim_world_rest;
  std::function<void(int, const float*)> compute_world_rot = [&](int node_idx, const float* parent_world) {
    const auto &node = anim_gltf.nodes[node_idx];
    float local[4] = {0,0,0,1};
    if (node.rotation.size() == 4) {
      local[0] = (float)node.rotation[0]; local[1] = (float)node.rotation[1];
      local[2] = (float)node.rotation[2]; local[3] = (float)node.rotation[3];
    }
    float world[4];
    /* world = parent_world * local */
    world[0] = parent_world[3]*local[0] + parent_world[0]*local[3] + parent_world[1]*local[2] - parent_world[2]*local[1];
    world[1] = parent_world[3]*local[1] - parent_world[0]*local[2] + parent_world[1]*local[3] + parent_world[2]*local[0];
    world[2] = parent_world[3]*local[2] + parent_world[0]*local[1] - parent_world[1]*local[0] + parent_world[2]*local[3];
    world[3] = parent_world[3]*local[3] - parent_world[0]*local[0] - parent_world[1]*local[1] - parent_world[2]*local[2];
    anim_world_rest[node_idx] = {world[0], world[1], world[2], world[3]};
    for (int child : node.children) compute_world_rot(child, world);
  };
  for (int scene_idx : anim_gltf.scenes[0].nodes) {
    float identity[4] = {0,0,0,1};
    compute_world_rot(scene_idx, identity);
  }
  
  /* Fallback: Mixamo name mapping, then direct node name matching */
  bool is_mixamo = false;
  for (size_t i = 0; i < anim_gltf.nodes.size(); i++) {
    if (anim_to_model_node.count(i)) continue;
    const std::string &name = anim_gltf.nodes[i].name;
    if (name.empty()) continue;
    
    /* Try Mixamo to VRM mapping */
    for (const auto& [mixamo, vrm] : mixamo_to_vrm) {
      if (name == mixamo) {
        auto it = model_humanoid_map.find(vrm);
        if (it != model_humanoid_map.end()) {
          anim_to_model_node[i] = it->second;
          is_mixamo = true;
          /* Store rest pose from node rotation */
          const auto &node = anim_gltf.nodes[i];
          if (node.rotation.size() == 4) {
            anim_local_rest[i] = {(float)node.rotation[0], (float)node.rotation[1],
                                  (float)node.rotation[2], (float)node.rotation[3]};
          }
        }
        break;
      }
    }
    
    /* Try direct name match */
    if (!anim_to_model_node.count(i)) {
      auto it = model_node_map.find(name);
      if (it != model_node_map.end()) {
        anim_to_model_node[i] = it->second;
      }
    }
  }
  
  model->anim_needs_coord_flip = needs_coord_flip && !is_mixamo;
  model->anim_is_mixamo = is_mixamo;
  printf("Animation: mapped %zu nodes, coord_flip=%d, mixamo=%d\n", anim_to_model_node.size(), model->anim_needs_coord_flip, is_mixamo);
  
  /* Quaternion helpers */
  auto qmul = [](const float* a, const float* b, float* out) {
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
  };
  auto qinv = [](const float* q, float* out) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
  };

  /* Load animations, remapping node indices */
  model->animation_count = (int)anim_gltf.animations.size();
  model->animations = (GltfAnimation *)calloc(model->animation_count, sizeof(GltfAnimation));

  for (size_t i = 0; i < anim_gltf.animations.size(); i++) {
    const auto &anim = anim_gltf.animations[i];
    GltfAnimation *a = &model->animations[i];

    copy_name(a->name, anim.name, sizeof(a->name));

    /* Load samplers */
    a->sampler_count = (int)anim.samplers.size();
    a->samplers = (GltfAnimSampler *)calloc(a->sampler_count, sizeof(GltfAnimSampler));
    for (size_t j = 0; j < anim.samplers.size(); j++) {
      const auto &sampler = anim.samplers[j];
      GltfAnimSampler *s = &a->samplers[j];

      const auto &input_acc = anim_gltf.accessors[sampler.input];
      const auto &input_view = anim_gltf.bufferViews[input_acc.bufferView];
      const auto &input_buf = anim_gltf.buffers[input_view.buffer];
      const float *input_data = (const float *)(input_buf.data.data() + input_view.byteOffset + input_acc.byteOffset);

      s->keyframe_count = (int)input_acc.count;
      s->input = (float *)malloc(s->keyframe_count * sizeof(float));
      memcpy(s->input, input_data, s->keyframe_count * sizeof(float));

      if (s->keyframe_count > 0 && s->input[s->keyframe_count - 1] > a->duration) {
        a->duration = s->input[s->keyframe_count - 1];
      }

      const auto &output_acc = anim_gltf.accessors[sampler.output];
      const auto &output_view = anim_gltf.bufferViews[output_acc.bufferView];
      const auto &output_buf = anim_gltf.buffers[output_view.buffer];
      const float *output_data = (const float *)(output_buf.data.data() + output_view.byteOffset + output_acc.byteOffset);

      int components = 1;
      if (output_acc.type == TINYGLTF_TYPE_VEC3) components = 3;
      else if (output_acc.type == TINYGLTF_TYPE_VEC4) components = 4;

      s->output_stride = components;
      s->output = (float *)malloc(s->keyframe_count * components * sizeof(float));
      memcpy(s->output, output_data, s->keyframe_count * components * sizeof(float));
    }

    /* Load channels, remapping node indices */
    a->channel_count = (int)anim.channels.size();
    a->channels = (GltfAnimChannel *)calloc(a->channel_count, sizeof(GltfAnimChannel));
    int matched = 0;
    for (size_t j = 0; j < anim.channels.size(); j++) {
      const auto &channel = anim.channels[j];
      GltfAnimChannel *c = &a->channels[j];

      c->sampler_index = channel.sampler;

      int anim_node_idx = channel.target_node;
      auto it = anim_to_model_node.find(anim_node_idx);
      if (it != anim_to_model_node.end()) {
        c->target_node = it->second;
        matched++;
        
        /* Process rotation keyframes */
        if (channel.target_path == "rotation") {
          GltfAnimSampler *s = &a->samplers[c->sampler_index];
          
          /* Normalize: L^-1 * anim */
          auto local_it = anim_local_rest.find(anim_node_idx);
          if (local_it != anim_local_rest.end()) {
            const auto &L = local_it->second;
            if (std::abs(L[0]) > 0.001f || std::abs(L[1]) > 0.001f || 
                std::abs(L[2]) > 0.001f || std::abs(L[3] - 1.0f) > 0.001f) {
              float L_inv[4];
              qinv(L.data(), L_inv);
              for (int k = 0; k < s->keyframe_count; k++) {
                float *q = &s->output[k * 4];
                float tmp[4];
                qmul(L_inv, q, tmp);  /* L^-1 * anim */
                q[0] = tmp[0]; q[1] = tmp[1]; q[2] = tmp[2]; q[3] = tmp[3];
              }
            }
          }
        }
      } else {
        c->target_node = -1;
      }

      if (channel.target_path == "translation") c->path = GLTF_PATH_TRANSLATION;
      else if (channel.target_path == "rotation") c->path = GLTF_PATH_ROTATION;
      else if (channel.target_path == "scale") c->path = GLTF_PATH_SCALE;
      else if (channel.target_path == "weights") c->path = GLTF_PATH_WEIGHTS;
    }
    printf("Animation '%s': %d/%d channels mapped\n", a->name, matched, a->channel_count);
  }

  printf("Loaded %d animations from %s (duration: %.2fs)\n", 
         model->animation_count, anim_path, 
         model->animation_count > 0 ? model->animations[0].duration : 0.0f);

  return true;
}
