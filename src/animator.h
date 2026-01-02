/*
 * VRM Overlay - Animator
 */

#ifndef ANIMATOR_H
#define ANIMATOR_H

#include "gltf_loader.h"
#include <stdbool.h>

typedef struct {
    GltfModel *model;
    int current_animation;
    float time;
    float speed;
    bool playing;
} Animator;

void animator_init(Animator *anim);
void animator_set_model(Animator *anim, GltfModel *model);
void animator_update(Animator *anim, float dt);
void animator_play(Animator *anim, float speed);
void animator_pause(Animator *anim);
void animator_set_time(Animator *anim, float t);
void animator_set_animation(Animator *anim, int index);

#endif
