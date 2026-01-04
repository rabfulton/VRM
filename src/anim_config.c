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

static bool build_cfg_path(const char *anim_path, char *out_path, size_t out_size) {
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

bool anim_config_load(const char *anim_path, AnimConfig *out_cfg) {
    if (!out_cfg) return false;
    memset(out_cfg, 0, sizeof(*out_cfg));

    char cfg_path[PATH_MAX];
    if (!build_cfg_path(anim_path, cfg_path, sizeof(cfg_path))) return false;

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
        }
    }
    fclose(f);
    return true;
}

bool anim_config_save(const char *anim_path, const AnimConfig *cfg) {
    if (!cfg) return false;
    char cfg_path[PATH_MAX];
    if (!build_cfg_path(anim_path, cfg_path, sizeof(cfg_path))) return false;

    FILE *f = fopen(cfg_path, "w");
    if (!f) return false;

    fprintf(f, "spring.stiffness=%.3f\n", cfg->spring_stiffness);
    fprintf(f, "spring.drag=%.3f\n", cfg->spring_drag);
    fprintf(f, "spring.gravity=%.3f\n", cfg->spring_gravity);
    fclose(f);
    return true;
}
