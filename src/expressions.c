/* Named facial inputs are composed together, independent of setter order. */
#include "gltf_loader.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int name_equal(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a >= 'A' && *a <= 'Z' ? *a + ('a' - 'A') : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? *b + ('a' - 'A') : *b;
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == *b;
}

static const char *canonical(const char *name) {
  static const char *aliases[][3] = {
      {"a", "aa", "aa"}, {"i", "ih", "ih"}, {"u", "ou", "ou"},
      {"e", "ee", "ee"}, {"o", "oh", "oh"}, {"joy", "happy", "happy"},
      {"sorrow", "sad", "sad"}, {"fun", "relaxed", "relaxed"}};
  for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
    if (name_equal(name, aliases[i][0]) || name_equal(name, aliases[i][1]))
      return aliases[i][2];
  return name;
}

static bool is_emotion(const VrmExpression *expression) {
  static const char *names[] = {"happy", "angry", "sad", "relaxed", "surprised"};
  const char *preset = canonical(expression->preset_name);
  const char *name = canonical(expression->name);
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (name_equal(preset, names[i]) || name_equal(name, names[i])) return true;
  return false;
}

static float *target(GltfModel *model, const VrmExpressionBind *bind) {
  if (bind->mesh_index < 0 || bind->mesh_index >= model->mesh_count) return NULL;
  GltfMesh *mesh = &model->meshes[bind->mesh_index];
  if (!mesh->morph_weights || bind->morph_index < 0 ||
      bind->morph_index >= mesh->morph_target_count) return NULL;
  return &mesh->morph_weights[bind->morph_index];
}

static void initialize_balance(VrmExpressionMap *map) {
  if (!map->balance_initialized) {
    map->emotion_scale = 1.0f;
    map->balance_initialized = true;
  }
}

static void compose(GltfModel *model) {
  VrmExpressionMap *map = model->vrm_expressions;
  initialize_balance(map);
  /* Clear only expression-owned targets; leave unrelated authored morphs alone. */
  for (int i = 0; i < map->expression_count; i++) {
    const VrmExpression *expression = &map->expressions[i];
    if (!expression->has_input) continue;
    for (int b = 0; b < expression->bind_count; b++) {
      float *weight = target(model, &expression->binds[b]);
      if (weight) *weight = 0.0f;
    }
  }
  for (int i = 0; i < map->expression_count; i++) {
    const VrmExpression *expression = &map->expressions[i];
    if (!expression->has_input) continue;
    float input = expression->requested_weight;
    if (expression->is_binary) input = input >= 0.5f ? 1.0f : 0.0f;
    float effective = input * (is_emotion(expression) ? map->emotion_scale : 1.0f);
    /* Binary assets cannot be faded; keep their output binary when ducked. */
    if (expression->is_binary) effective = effective >= 0.5f ? 1.0f : 0.0f;
    for (int b = 0; b < expression->bind_count; b++) {
      const VrmExpressionBind *bind = &expression->binds[b];
      float *weight = target(model, bind);
      if (weight && isfinite(bind->weight) && bind->weight > 0.0f)
        *weight += effective * bind->weight;
    }
  }
  for (int i = 0; i < map->expression_count; i++) {
    const VrmExpression *expression = &map->expressions[i];
    if (!expression->has_input) continue;
    for (int b = 0; b < expression->bind_count; b++) {
      float *weight = target(model, &expression->binds[b]);
      if (weight && *weight > 1.0f) *weight = 1.0f;
    }
  }
}

void gltf_set_expression(GltfModel *model, const char *name, float weight) {
  if (!model || !name || !model->vrm_expressions || !isfinite(weight)) return;
  weight = fmaxf(0.0f, fminf(1.0f, weight));
  VrmExpressionMap *map = model->vrm_expressions;
  for (int i = 0; i < map->expression_count; i++) {
    VrmExpression *expression = &map->expressions[i];
    if (name_equal(canonical(expression->name), canonical(name)) ||
        name_equal(canonical(expression->preset_name), canonical(name))) {
      expression->requested_weight = weight;
      expression->has_input = true;
      compose(model);
      return;
    }
  }
}

void gltf_update_expression_balance(GltfModel *model, bool speaking, float dt) {
  if (!model || !model->vrm_expressions) return;
  VrmExpressionMap *map = model->vrm_expressions;
  initialize_balance(map);
  if (!isfinite(dt) || dt <= 0.0f) return;
  const float goal = speaking ? 0.4f : 1.0f;
  const float tau = speaking ? 0.06f : 0.15f;
  float next = goal + (map->emotion_scale - goal) * expf(-dt / tau);
  if (fabsf(next - goal) < 0.0001f) next = goal;
  if (next != map->emotion_scale) {
    map->emotion_scale = next;
    compose(model);
  }
}

int gltf_get_expression_names(const GltfModel *model,
                              char names[][VRM_EXPRESSION_NAME_LEN], int max_names) {
  if (!model || !names || max_names <= 0 || !model->vrm_expressions) return 0;
  const VrmExpressionMap *map = model->vrm_expressions;
  int count = 0;
  for (int i = 0; i < map->expression_count && count < max_names; i++) {
    snprintf(names[count], VRM_EXPRESSION_NAME_LEN, "%s", map->expressions[i].name);
    count++;
  }
  return count;
}
