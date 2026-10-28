#define _POSIX_C_SOURCE 200809L
#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

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
        if (strcmp(key, "always_on_top") == 0) {
            if (strcmp(val, "0") == 0 || strcmp(val, "1") == 0) {
                out_settings->always_on_top = strcmp(val, "1") == 0;
                out_settings->has_always_on_top = true;
                found = true;
            }
        } else if (strcmp(key, "camera_distance") == 0) {
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
        } else if (strcmp(key, "wayland_pos_x") == 0 || strcmp(key, "wayland_pos_y") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end && end != val && !*end && v >= -16384 && v <= 16384) {
                out_settings->wayland_pos[strcmp(key,"wayland_pos_y")==0] = (int)v;
                out_settings->has_wayland_position = true;
                found = true;
            }
        } else if (strcmp(key, "window_size_w") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end && end != val) {
                out_settings->window_size[0] = (int)v;
                out_settings->has_window_size = true;
                found = true;
            }
        } else if (strcmp(key, "window_size_h") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end && end != val) {
                out_settings->window_size[1] = (int)v;
                out_settings->has_window_size = true;
                found = true;
            }
        }
    }
    fclose(f);
    return found;
}

bool settings_save(const char *path, const Settings *settings) {
    if (!path || !settings) return false;
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp-XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return false;
    int fd = mkstemp(temporary);
    if (fd < 0) return false;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temporary); return false; }
    FILE *old = fopen(path, "r");
    if (old) {
        char *line = NULL;
        size_t capacity = 0;
        const char *keys[] = {"always_on_top", "camera_distance", "model_pos_x", "model_pos_y", "model_pos_z",
                             "model_scale", "window_pos_x", "window_pos_y", "wayland_pos_x", "wayland_pos_y",
                             "window_size_w", "window_size_h"};
        while (getline(&line, &capacity, old) >= 0) {
            char *copy = strdup(line);
            if (!copy) { free(line); fclose(old); fclose(f); unlink(temporary); return false; }
            char *equal = strchr(copy, '=');
            bool known = false;
            if (equal) {
                *equal = '\0';
                char *key = trim(copy);
                for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                    if (strcmp(key, keys[i]) == 0) { known = true; break; }
            }
            if (!known) {
                fputs(line, f);
                if (*line && line[strlen(line) - 1] != '\n') fputc('\n', f);
            }
            free(copy);
        }
        bool failed = ferror(old);
        free(line);
        fclose(old);
        if (failed) { fclose(f); unlink(temporary); return false; }
    }
    if (settings->has_always_on_top) {
        fprintf(f, "always_on_top=%d\n", settings->always_on_top ? 1 : 0);
    }
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
    if (settings->has_wayland_position) {
        fprintf(f, "wayland_pos_x=%d\nwayland_pos_y=%d\n", settings->wayland_pos[0],settings->wayland_pos[1]);
    }
    if (settings->has_window_size) {
        fprintf(f, "window_size_w=%d\n", settings->window_size[0]);
        fprintf(f, "window_size_h=%d\n", settings->window_size[1]);
    }
    bool ok = !ferror(f);
    if (fflush(f) != 0 || fsync(fd) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (ok && rename(temporary, path) == 0) return true;
    unlink(temporary);
    return false;
}
