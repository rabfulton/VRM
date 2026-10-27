#include "gltf_loader.h"
#include "viseme.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static VisemeAnalyzer analyzer;

typedef struct {
  GltfModel model;
  GltfMesh mesh;
  VrmExpressionMap map;
  VrmExpression expressions[6];
  VrmExpressionBind binds[6];
  float weights[7];
} Fixture;

static void fixture(Fixture *f) {
  memset(f, 0, sizeof(*f));
  f->model.meshes = &f->mesh;
  f->model.mesh_count = 1;
  f->model.vrm_expressions = &f->map;
  f->mesh.morph_weights = f->weights;
  f->mesh.morph_target_count = 7;
  f->map.expressions = f->expressions;
  f->map.expression_count = 6;
  const char *names[] = {"Joy", "A", "Blink", "Custom", "Angry", "ih"};
  const int targets[] = {0, 1, 2, 3, 0, 4};
  for (int i = 0; i < 6; i++) {
    snprintf(f->expressions[i].name, VRM_EXPRESSION_NAME_LEN, "%s", names[i]);
    f->expressions[i].binds = &f->binds[i];
    f->expressions[i].bind_count = 1;
    f->binds[i].morph_index = targets[i];
    f->binds[i].weight = 1;
  }
  f->weights[6] = 1.2f; /* Authored, not owned by any expression. */
}

static void near(float value, float expected) {
  assert(fabsf(value - expected) < 0.0001f);
}

int main(void) {
  Fixture f;
  fixture(&f);
  f.weights[3] = 0.3f; /* An untouched expression target can have an authored default. */
  gltf_set_expression(&f.model, "happy", 0.55f);
  near(f.weights[3], 0.3f);
  gltf_update_expression_balance(&f.model, true, 1);
  near(f.weights[3], 0.3f);
  gltf_update_expression_balance(&f.model, false, 2);
  gltf_set_expression(&f.model, "A", 0.35f);
  gltf_set_expression(&f.model, "Blink", 0.7f);
  gltf_set_expression(&f.model, "Custom", 0.9f);
  near(f.weights[0], 0.55f);
  gltf_update_expression_balance(&f.model, true, 1.0f / 60);
  assert(f.map.emotion_scale > 0.4f && f.map.emotion_scale < 1);
  gltf_update_expression_balance(&f.model, true, 1);
  near(f.weights[0], 0.22f);
  near(f.expressions[0].requested_weight, 0.55f);
  near(f.weights[1], 0.35f);
  near(f.weights[2], 0.7f);
  near(f.weights[3], 0.9f);
  near(f.weights[6], 1.2f);
  /* Setters while speaking use the current scale immediately. */
  gltf_set_expression(&f.model, "Joy", 1);
  near(f.weights[0], 0.4f);
  gltf_update_expression_balance(&f.model, false, 1.0f / 60);
  assert(f.weights[0] > 0.4f && f.weights[0] < 1);
  gltf_update_expression_balance(&f.model, false, 2);
  near(f.weights[0], 1);

  /* Shared morph targets sum independently of setter order. Clearing one
   * input cannot erase another live input or accumulate frame-to-frame. */
  fixture(&f);
  gltf_set_expression(&f.model, "Angry", 0.3f);
  gltf_set_expression(&f.model, "Joy", 0.4f);
  near(f.weights[0], 0.7f);
  gltf_set_expression(&f.model, "Joy", 0);
  near(f.weights[0], 0.3f);
  gltf_set_expression(&f.model, "Joy", 0.9f);
  near(f.weights[0], 1);
  gltf_set_expression(&f.model, "Joy", 0.4f);
  for (int i = 0; i < 100; i++) gltf_set_expression(&f.model, "A", 0.2f);
  near(f.weights[0], 0.7f);
  near(f.weights[1], 0.2f);

  /* Frame-rate-independent attack and release; no input weight destruction. */
  float attack = 0, release = 0;
  const int rates[] = {30, 60, 144};
  for (int r = 0; r < 3; r++) {
    fixture(&f);
    gltf_set_expression(&f.model, "Joy", 0.55f);
    for (int i = 0; i < rates[r]; i++)
      gltf_update_expression_balance(&f.model, true, 1.0f / rates[r]);
    if (r) near(f.weights[0], attack); else attack = f.weights[0];
    for (int i = 0; i < rates[r]; i++)
      gltf_update_expression_balance(&f.model, false, 1.0f / rates[r]);
    if (r) near(f.weights[0], release); else release = f.weights[0];
    near(f.expressions[0].requested_weight, 0.55f);
  }

  /* Real voice analysis keeps its mouth weights while emotions soften. */
  fixture(&f);
  int16_t pcm[2400];
  for (int i = 0; i < 2400; i++)
    pcm[i] = (int16_t)(10000 * sinf(6.2831853f * 500 * i / 24000));
  viseme_init(&analyzer);
  viseme_analyze(&analyzer, pcm, 2400, 0);
  VisemeFrame frame;
  assert(viseme_sample(&analyzer, 1200, &frame));
  gltf_set_expression(&f.model, "A", frame.weights[VISEME_A]);
  gltf_set_expression(&f.model, "I", frame.weights[VISEME_I]);
  gltf_set_expression(&f.model, "Joy", 0.55f);
  gltf_update_expression_balance(&f.model, true, 1);
  near(f.weights[1], frame.weights[VISEME_A]);
  near(f.weights[4], frame.weights[VISEME_I]);
  near(f.weights[0], 0.22f);
  gltf_set_expression(&f.model, "Joy", 0);
  near(f.weights[1], frame.weights[VISEME_A]);
  viseme_free(&analyzer);

  /* Binary expressions stay binary; invalid binds and inputs are harmless. */
  fixture(&f);
  f.expressions[0].is_binary = true;
  gltf_set_expression(&f.model, "Joy", 0.55f);
  near(f.weights[0], 1);
  gltf_update_expression_balance(&f.model, true, 1);
  near(f.weights[0], 0);
  gltf_update_expression_balance(&f.model, false, 2);
  near(f.weights[0], 1);
  f.binds[0].mesh_index = 99;
  f.binds[1].morph_index = -1;
  f.binds[2].weight = NAN;
  gltf_set_expression(&f.model, "Joy", 1);
  gltf_set_expression(&f.model, "A", 1);
  gltf_set_expression(&f.model, "Blink", 1);
  near(f.weights[2], 0);
  gltf_set_expression(&f.model, "Custom", NAN);
  near(f.weights[3], 0);
  gltf_update_expression_balance(&f.model, true, NAN);
  near(f.weights[6], 1.2f);
  puts("Expression tests passed: speech balance, smooth recovery, shared targets, aliases, binary safety and real viseme preservation");
  return 0;
}
