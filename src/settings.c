#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH_LENGTH 256

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

bool settings_load(const char *path, Settings *out_settings) {
    if (!path || !out_settings) return false;
    memset(out_settings, 0, sizeof(*out_settings));
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[MAX_PATH_LENGTH];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);
        if (strcmp(key, "camera_distance") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end && end != val) {
                out_settings->camera_distance = v;
                out_settings->has_camera_distance = true;
                found = true;
            }
        } else if (strcmp(key, "model_pos_x") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end && end != val) {
                out_settings->model_position[0] = v;
                out_settings->has_model_position = true;
                found = true;
            }
        } else if (strcmp(key, "model_pos_y") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end && end != val) {
                out_settings->model_position[1] = v;
                out_settings->has_model_position = true;
                found = true;
            }
        } else if (strcmp(key, "model_pos_z") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end && end != val) {
                out_settings->model_position[2] = v;
                out_settings->has_model_position = true;
                found = true;
            }
        } else if (strcmp(key, "model_scale") == 0) {
            char *end = NULL;
            float v = strtof(val, &end);
            if (end && end != val) {
                out_settings->model_scale = v;
                out_settings->has_model_scale = true;
                found = true;
            }
        } else if (strcmp(key, "window_pos_x") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end && end != val) {
                out_settings->window_pos[0] = (int)v;
                out_settings->has_window_position = true;
                found = true;
            }
        } else if (strcmp(key, "window_pos_y") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end && end != val) {
                out_settings->window_pos[1] = (int)v;
                out_settings->has_window_position = true;
                found = true;
            }
        }
    }
    fclose(f);
    return found;
}

bool settings_save(const char *path, const Settings *settings) {
    if (!path || !settings) return false;
    FILE *f = fopen(path, "w");
    if (!f) return false;
    if (settings->has_camera_distance) {
        fprintf(f, "camera_distance=%.6f\n", settings->camera_distance);
    }
    if (settings->has_model_position) {
        fprintf(f, "model_pos_x=%.6f\n", settings->model_position[0]);
        fprintf(f, "model_pos_y=%.6f\n", settings->model_position[1]);
        fprintf(f, "model_pos_z=%.6f\n", settings->model_position[2]);
    }
    if (settings->has_model_scale) {
        fprintf(f, "model_scale=%.6f\n", settings->model_scale);
    }
    if (settings->has_window_position) {
        fprintf(f, "window_pos_x=%d\n", settings->window_pos[0]);
        fprintf(f, "window_pos_y=%d\n", settings->window_pos[1]);
    }
    fclose(f);
    return true;
}
