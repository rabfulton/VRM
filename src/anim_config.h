#ifndef ANIM_CONFIG_H
#define ANIM_CONFIG_H

#include <stdbool.h>

typedef struct {
    float spring_stiffness;
    float spring_drag;
    float spring_gravity;
    bool has_spring_stiffness;
    bool has_spring_drag;
    bool has_spring_gravity;
} AnimConfig;

bool anim_config_load(const char *anim_path, AnimConfig *out_cfg);
bool anim_config_save(const char *anim_path, const AnimConfig *cfg);

#endif
