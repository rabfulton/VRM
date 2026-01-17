#ifndef ANIM_CONFIG_H
#define ANIM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float spring_stiffness;
    float spring_drag;
    float spring_gravity;
    float spring_cloth_mode;
    float spring_cloth_stiffness;
    float spring_cloth_drag;
    float spring_cloth_gravity;
    float spring_cloth_taper;
    float anim_seam_blend_time;
    float bounds_world_w;
    float bounds_world_h;
    bool has_spring_stiffness;
    bool has_spring_drag;
    bool has_spring_gravity;
    bool has_spring_cloth_mode;
    bool has_spring_cloth_stiffness;
    bool has_spring_cloth_drag;
    bool has_spring_cloth_gravity;
    bool has_spring_cloth_taper;
    bool has_anim_seam_blend_time;
    bool has_bounds_world_w;
    bool has_bounds_world_h;
} AnimConfig;

bool anim_config_build_path(const char *anim_path, char *out_path, size_t out_size);
bool anim_config_load(const char *anim_path, AnimConfig *out_cfg);
bool anim_config_save(const char *anim_path, const AnimConfig *cfg);

#endif
