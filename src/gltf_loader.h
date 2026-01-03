/*
 * VRM Overlay - glTF/GLB Loader
 *
 * Wrapper around tinygltf for loading VRM (.vrm) and glTF/GLB files.
 */

#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <epoxy/gl.h>
#include <stdbool.h>
#include <stdint.h>

/* Maximum limits */
#define MAX_MESHES 256
#define MAX_PRIMITIVES 512
#define MAX_MATERIALS 128
#define MAX_TEXTURES 128
#define MAX_NODES 512
#define MAX_JOINTS 256
#define MAX_ANIMATIONS 64

/* Primitive (submesh) */
typedef struct {
  GLuint vao;
  GLuint vbo_positions;
  GLuint vbo_normals;
  GLuint vbo_texcoords;
  GLuint vbo_joints;
  GLuint vbo_weights;
  GLuint ebo;

  uint32_t index_count;
  GLenum index_type;
  int material_index;

  bool has_normals;
  bool has_texcoords;
  bool has_skinning;
} GltfPrimitive;

/* Mesh */
typedef struct {
  char name[64];
  GltfPrimitive *primitives;
  int primitive_count;

  /* Morph targets (blendshapes) */
  int morph_target_count;
  float *morph_weights; /* Current weights */
} GltfMesh;

/* Material */
typedef struct {
  char name[64];

  /* PBR metallic-roughness */
  float base_color[4];
  float metallic;
  float roughness;

  int base_color_texture;
  int metallic_roughness_texture;
  int normal_texture;

  /* Alpha mode: 0=OPAQUE, 1=MASK, 2=BLEND */
  int alpha_mode;
  float alpha_cutoff;

  bool double_sided;
} GltfMaterial;

/* Texture */
typedef struct {
  GLuint id;
  int width, height;
} GltfTexture;

/* Node */
typedef struct {
  char name[64];
  int mesh_index;   /* -1 if no mesh */
  int skin_index;   /* -1 if no skin */
  int parent_index; /* -1 if root */
  int *children;
  int child_count;

  /* Local transform */
  float translation[3];
  float rotation[4]; /* Quaternion (x,y,z,w) */
  float scale[3];

  /* Computed world matrix */
  float world_matrix[16];
} GltfNode;

/* Skin (skeleton) */
typedef struct {
  int *joints;
  int joint_count;
  float *inverse_bind_matrices; /* 16 * joint_count floats */
  int skeleton_root;
} GltfSkin;

/* Animation channel target */
typedef enum {
  GLTF_PATH_TRANSLATION = 0,
  GLTF_PATH_ROTATION,
  GLTF_PATH_SCALE,
  GLTF_PATH_WEIGHTS
} GltfAnimPath;

/* Animation sampler */
typedef struct {
  float *input;      /* Keyframe times */
  float *output;     /* Keyframe values */
  int keyframe_count;
  int output_stride; /* Components per keyframe (3 for vec3, 4 for quat) */
} GltfAnimSampler;

/* Animation channel */
typedef struct {
  int sampler_index;
  int target_node;
  GltfAnimPath path;
} GltfAnimChannel;

/* Animation */
typedef struct {
  char name[64];
  GltfAnimSampler *samplers;
  int sampler_count;
  GltfAnimChannel *channels;
  int channel_count;
  float duration;
} GltfAnimation;

/* Complete glTF model */
typedef struct {
  GltfMesh *meshes;
  int mesh_count;

  GltfMaterial *materials;
  int material_count;

  GltfTexture *textures;
  int texture_count;

  GltfNode *nodes;
  int node_count;

  GltfSkin *skins;
  int skin_count;

  /* Scene root nodes */
  int *scene_roots;
  int scene_root_count;

  /* Animations */
  GltfAnimation *animations;
  int animation_count;
  bool anim_needs_coord_flip;  /* True if animation has flipped coordinate system */
  bool anim_is_mixamo;         /* True if animation uses Mixamo skeleton */

  /* VRM humanoid bone mapping */
  char (*humanoid_names)[32];  /* Array of bone names */
  int *humanoid_bones;         /* Array of node indices */
  int humanoid_bone_count;

  bool loaded;
} GltfModel;

/* Load a glTF/GLB/VRM file */
bool gltf_load(GltfModel *model, const char *path);

/* Unload and free all resources */
void gltf_unload(GltfModel *model);

/* Update node world matrices (call after changing transforms) */
void gltf_update_transforms(GltfModel *model);

/* Load animation from separate file and apply to current model */
bool gltf_load_animation(GltfModel *model, const char *anim_path);

/* Get a node by name (returns -1 if not found) */
int gltf_find_node(const GltfModel *model, const char *name);

#endif /* GLTF_LOADER_H */
