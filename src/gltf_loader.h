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
#define GLTF_MAX_PALETTE_BONES 128
#define MAX_MORPH_TARGETS 64
#define MAX_VRM_EXPRESSIONS 64
#define VRM_EXPRESSION_NAME_LEN 32

/* Morph target (per primitive) */
typedef struct {
  float *position_deltas; /* 3 floats per vertex */
  float *normal_deltas;   /* 3 floats per vertex, optional */
  bool has_normals;
} MorphTarget;

/* Primitive (submesh) */
typedef struct {
  GLuint vao;
  GLuint vbo_positions;
  GLuint vbo_normals;
  GLuint vbo_texcoords;
  GLuint vbo_joints;
  GLuint vbo_weights;
  GLuint ebo;

  GLenum mode;
  uint32_t vertex_count;

  uint32_t index_count;
  GLenum index_type;
  int material_index;

  bool has_normals;
  bool has_texcoords;
  bool has_skinning;

  int joint_palette_count;
  int *joint_palette; /* Node indices used by this primitive */

  /* Morph targets */
  MorphTarget *morph_targets;
  int morph_target_count;
  GLuint morph_vbo;    /* Packed morph deltas for GPU */
  int morph_gpu_count; /* Capped count for shader (max MAX_MORPH_TARGETS) */
  int morph_tex_verts_per_row;
  int morph_tex_rows_per_morph;
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
  bool alpha_write_depth;
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
  float base_translation[3];
  float base_rotation[4];
  float base_scale[3];

  int is_spring_bone; /* 1 if this is a spring bone (don't reset in animator) */

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
  float *input;  /* Keyframe times */
  float *output; /* Keyframe values */
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
  uint8_t *node_mask; /* Per-node mask of animated paths */
  int node_mask_count;
} GltfAnimation;

/* Spring bone collider (supports sphere and capsule per spec) */
typedef struct {
  int node;        /* Node index the collider is attached to */
  float offset[3]; /* Local offset from node */
  float radius;    /* Collider radius */
  int is_capsule;  /* 0=sphere, 1=capsule */
  float tail[3];   /* Capsule tail in local coords (if capsule) */
} SpringCollider;

/* Spring bone collider group */
typedef struct {
  int node; /* Node the group is attached to */
  SpringCollider *colliders;
  int collider_count;
} SpringColliderGroup;

/* Spring bone joint state (per VRMC_springBone-1.0 spec) */
typedef struct {
  int node;                        /* Node index */
  float prev_tail[3];              /* Previous tail position (world) */
  float current_tail[3];           /* Current tail position (world) */
  float bone_axis[3];              /* Direction to child in local rest pose */
  float bone_length;               /* Length to child in world units */
  float initial_local_rotation[4]; /* Rest orientation (quaternion) */
  float initial_local_matrix[16];  /* Rest local matrix */
  /* Per-joint parameters (spec requires these per joint) */
  float hit_radius;
  float stiffness;
  float gravity_power;
  float gravity_dir[3];
  float drag_force;
} SpringJoint;

/* Spring bone chain (SpringChain in spec) */
typedef struct {
  int *bones; /* Node indices of joints in chain */
  int bone_count;
  int *collider_groups; /* Indices into collider groups array */
  int collider_group_count;
  int center;            /* Center space node index (-1 for world) */
  SpringJoint *joints;   /* Runtime joint state (bone_count - 1 pairs) */
  int joint_count;       /* Number of active joint pairs */
  float avg_bone_length; /* Average bone length in chain */
} SpringBoneGroup;

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
  bool anim_needs_coord_flip; /* True if animation has flipped coordinate system
                               */
  bool anim_is_mixamo;        /* True if animation uses Mixamo skeleton */
  int anim_correction_mode;   /* 0: none, 1: auto, 2: rotX, 3: rotY, 4: rotZ
                                NOTE: Default is rotY 180 to match legacy
                                VRMA/poses.glb   data in this repo. Do not remove
                                without validating   against VRMA_01-07 and
                                poses.glb. */
  float anim_global_correction[4];
  float anim_global_auto_correction[4];

  /* VRM humanoid bone mapping */
  char (*humanoid_names)[32]; /* Array of bone names */
  int *humanoid_bones;        /* Array of node indices */
  int humanoid_bone_count;

  /* VRM spring bones */
  SpringBoneGroup *spring_groups;
  int spring_group_count;
  SpringColliderGroup *spring_colliders;
  int spring_collider_count;
  struct SpringRuntime *spring_runtime;

  /* VRM expressions (blendshapes) - parsed from VRM 0.x/1.0 */
  struct VrmExpressionMap *vrm_expressions;

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

/* Compute screen-space bounding box from skeleton (returns NDC extents) */
void gltf_get_screen_bounds(const GltfModel *model, const float *view,
                            const float *proj, float *out_ndc_w,
                            float *out_ndc_h);

/* Compute screen-space bounding box in pixels (for input regions) */
void gltf_get_screen_bounds_px(const GltfModel *model, const float *view,
                               const float *proj, int screen_w, int screen_h,
                               float *out_min_x, float *out_min_y,
                               float *out_max_x, float *out_max_y);

/* Compute max bounds over entire animation, with caching */
void gltf_compute_animation_bounds(GltfModel *model, const char *anim_path,
                                   float *out_world_w, float *out_world_h);

/* VRM expression bind (morph target reference within an expression) */
typedef struct {
  int mesh_index;  /* Which mesh */
  int morph_index; /* Which morph target in that mesh */
  float weight;    /* Weight multiplier for this bind */
} VrmExpressionBind;

/* VRM expression (named blendshape group) */
typedef struct {
  char name[VRM_EXPRESSION_NAME_LEN];
  char preset_name[VRM_EXPRESSION_NAME_LEN]; /* VRM preset like "aa", "blink",
                                                etc. */
  VrmExpressionBind *binds;
  int bind_count;
  bool is_binary; /* If true, weight should be 0 or 1 */
} VrmExpression;

/* VRM expression map (all expressions for a model) */
typedef struct VrmExpressionMap {
  VrmExpression *expressions;
  int expression_count;
} VrmExpressionMap;

/* Set a VRM expression weight by name (uses expressions or fallback to morph
 * names) */
void gltf_set_expression(GltfModel *model, const char *name, float weight);

/* Get list of available expression names */
int gltf_get_expression_names(const GltfModel *model,
                              char names[][VRM_EXPRESSION_NAME_LEN],
                              int max_names);

#endif /* GLTF_LOADER_H */
