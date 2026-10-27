#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

typedef struct {
    bool has_always_on_top;
    bool always_on_top;
    bool has_camera_distance;
    float camera_distance;
    bool has_model_position;
    float model_position[3];
    bool has_model_scale;
    float model_scale;
    bool has_window_position;
    int window_pos[2];
    bool has_window_size;
    int window_size[2];
} Settings;

bool settings_load(const char *path, Settings *out_settings);
bool settings_save(const char *path, const Settings *settings);

#endif
