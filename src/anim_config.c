#include "anim_config.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define PATH_MAX 256

static void trim_spaces(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

bool anim_config_build_path(const char *anim_path, char *out_path, size_t out_size) {
    if (!anim_path || !out_path || out_size == 0) return false;
    const char *slash = strrchr(anim_path, '/');
    const char *dot = strrchr(anim_path, '.');
    if (dot && (!slash || dot > slash)) {
        size_t base_len = (size_t)(dot - anim_path);
        if (base_len + 4 >= out_size) return false;
        memcpy(out_path, anim_path, base_len);
        memcpy(out_path + base_len, ".cfg", 5);
        return true;
    }
    if (strlen(anim_path) + 4 >= out_size) return false;
    snprintf(out_path, out_size, "%s.cfg", anim_path);
    return true;
}

typedef struct {
    char key[128];
    char value[128];
} KvPair;

static int kv_find(KvPair *pairs, int count, const char *key) {
    for (int i = 0; i < count; i++) {
        if (strcmp(pairs[i].key, key) == 0) return i;
    }
    return -1;
}

bool anim_config_load(const char *anim_path, AnimConfig *out_cfg) {
    if (!out_cfg) return false;
    memset(out_cfg, 0, sizeof(*out_cfg));

    char cfg_path[PATH_MAX];
    if (!anim_config_build_path(anim_path, cfg_path, sizeof(cfg_path))) return false;

    FILE *f = fopen(cfg_path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        trim_spaces(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_spaces(key);
        trim_spaces(val);
        if (key[0] == '\0' || val[0] == '\0') continue;
        char *end = NULL;
        float fval = strtof(val, &end);
        if (end == val) continue;
        if (strcmp(key, "spring.stiffness") == 0) {
            out_cfg->spring_stiffness = fval;
            out_cfg->has_spring_stiffness = true;
        } else if (strcmp(key, "spring.drag") == 0) {
            out_cfg->spring_drag = fval;
            out_cfg->has_spring_drag = true;
        } else if (strcmp(key, "spring.gravity") == 0) {
            out_cfg->spring_gravity = fval;
            out_cfg->has_spring_gravity = true;
        } else if (strcmp(key, "bounds.world_w") == 0) {
            out_cfg->bounds_world_w = fval;
            out_cfg->has_bounds_world_w = true;
        } else if (strcmp(key, "bounds.world_h") == 0) {
            out_cfg->bounds_world_h = fval;
            out_cfg->has_bounds_world_h = true;
        }
    }
    fclose(f);
    return true;
}

bool anim_config_save(const char *anim_path, const AnimConfig *cfg) {
    if (!cfg) return false;
    char cfg_path[PATH_MAX];
    if (!anim_config_build_path(anim_path, cfg_path, sizeof(cfg_path))) return false;

    KvPair pairs[128];
    int pair_count = 0;

    FILE *f = fopen(cfg_path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            trim_spaces(line);
            if (line[0] == '\0' || line[0] == '#') continue;
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            trim_spaces(key);
            trim_spaces(val);
            if (key[0] == '\0' || val[0] == '\0') continue;
            if (pair_count >= (int)(sizeof(pairs) / sizeof(pairs[0]))) break;
            if (strlen(key) >= sizeof(pairs[pair_count].key)) {
                fprintf(stderr, "WARN: Skipping long config key: %s\n", key);
                continue;
            }
            if (strlen(val) >= sizeof(pairs[pair_count].value)) {
                fprintf(stderr, "WARN: Skipping long config value for key %s\n", key);
                continue;
            }
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", key);
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
        fclose(f);
    }

    if (cfg->has_spring_stiffness) {
        char val[64];
        snprintf(val, sizeof(val), "%.3f", cfg->spring_stiffness);
        int idx = kv_find(pairs, pair_count, "spring.stiffness");
        if (idx >= 0) snprintf(pairs[idx].value, sizeof(pairs[idx].value), "%s", val);
        else if (pair_count < (int)(sizeof(pairs) / sizeof(pairs[0]))) {
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", "spring.stiffness");
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
    }
    if (cfg->has_spring_drag) {
        char val[64];
        snprintf(val, sizeof(val), "%.3f", cfg->spring_drag);
        int idx = kv_find(pairs, pair_count, "spring.drag");
        if (idx >= 0) snprintf(pairs[idx].value, sizeof(pairs[idx].value), "%s", val);
        else if (pair_count < (int)(sizeof(pairs) / sizeof(pairs[0]))) {
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", "spring.drag");
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
    }
    if (cfg->has_spring_gravity) {
        char val[64];
        snprintf(val, sizeof(val), "%.3f", cfg->spring_gravity);
        int idx = kv_find(pairs, pair_count, "spring.gravity");
        if (idx >= 0) snprintf(pairs[idx].value, sizeof(pairs[idx].value), "%s", val);
        else if (pair_count < (int)(sizeof(pairs) / sizeof(pairs[0]))) {
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", "spring.gravity");
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
    }
    if (cfg->has_bounds_world_w) {
        char val[64];
        snprintf(val, sizeof(val), "%.3f", cfg->bounds_world_w);
        int idx = kv_find(pairs, pair_count, "bounds.world_w");
        if (idx >= 0) snprintf(pairs[idx].value, sizeof(pairs[idx].value), "%s", val);
        else if (pair_count < (int)(sizeof(pairs) / sizeof(pairs[0]))) {
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", "bounds.world_w");
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
    }
    if (cfg->has_bounds_world_h) {
        char val[64];
        snprintf(val, sizeof(val), "%.3f", cfg->bounds_world_h);
        int idx = kv_find(pairs, pair_count, "bounds.world_h");
        if (idx >= 0) snprintf(pairs[idx].value, sizeof(pairs[idx].value), "%s", val);
        else if (pair_count < (int)(sizeof(pairs) / sizeof(pairs[0]))) {
            snprintf(pairs[pair_count].key, sizeof(pairs[pair_count].key), "%s", "bounds.world_h");
            snprintf(pairs[pair_count].value, sizeof(pairs[pair_count].value), "%s", val);
            pair_count++;
        }
    }

    f = fopen(cfg_path, "w");
    if (!f) return false;
    for (int i = 0; i < pair_count; i++) {
        fprintf(f, "%s=%s\n", pairs[i].key, pairs[i].value);
    }
    fclose(f);
    return true;
}
