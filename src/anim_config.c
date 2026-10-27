#define _POSIX_C_SOURCE 200809L
#include "anim_config.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, f) >= 0) {
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
        char *comment = strchr(val, '#');
        if (comment) *comment = '\0';
        trim_spaces(val);
        if (strcmp(key, "loopable") == 0 || strcmp(key, "startup") == 0 || strcmp(key, "is_idle") == 0 || strcmp(key,"floor_contact")==0) {
            bool enabled = strcmp(val, "1") == 0;
            if (strcmp(key, "loopable") == 0) { out_cfg->loopable = enabled; out_cfg->has_loopable = true; }
            if (strcmp(key, "startup") == 0) { out_cfg->startup = enabled; out_cfg->has_startup = true; }
            if (strcmp(key, "is_idle") == 0) { out_cfg->is_idle = enabled; out_cfg->has_is_idle = true; }
            if (strcmp(key,"floor_contact")==0) { out_cfg->floor_contact=enabled; out_cfg->has_floor_contact=true; }
            continue;
        }
        char *end = NULL;
        float fval = strtof(val, &end);
        if (end == val || *end || !isfinite(fval)) continue;
        if (strcmp(key, "spring.stiffness") == 0) {
            out_cfg->spring_stiffness = fval;
            out_cfg->has_spring_stiffness = true;
        } else if (strcmp(key, "spring.drag") == 0) {
            out_cfg->spring_drag = fval;
            out_cfg->has_spring_drag = true;
        } else if (strcmp(key, "spring.gravity") == 0) {
            out_cfg->spring_gravity = fval;
            out_cfg->has_spring_gravity = true;
        } else if (strcmp(key, "spring.cloth_mode") == 0) {
            out_cfg->spring_cloth_mode = fval;
            out_cfg->has_spring_cloth_mode = true;
        } else if (strcmp(key, "spring.cloth_stiffness") == 0) {
            out_cfg->spring_cloth_stiffness = fval;
            out_cfg->has_spring_cloth_stiffness = true;
        } else if (strcmp(key, "spring.cloth_drag") == 0) {
            out_cfg->spring_cloth_drag = fval;
            out_cfg->has_spring_cloth_drag = true;
        } else if (strcmp(key, "spring.cloth_gravity") == 0) {
            out_cfg->spring_cloth_gravity = fval;
            out_cfg->has_spring_cloth_gravity = true;
        } else if (strcmp(key, "spring.cloth_taper") == 0) {
            out_cfg->spring_cloth_taper = fval;
            out_cfg->has_spring_cloth_taper = true;
        } else if (strcmp(key, "anim.seam_blend_time") == 0) {
            out_cfg->anim_seam_blend_time = fval;
            out_cfg->has_anim_seam_blend_time = true;
        } else if (strcmp(key, "bounds.world_w") == 0) {
            out_cfg->bounds_world_w = fval;
            out_cfg->has_bounds_world_w = true;
        } else if (strcmp(key, "bounds.world_h") == 0) {
            out_cfg->bounds_world_h = fval;
            out_cfg->has_bounds_world_h = true;
        }
    }
    bool ok = !ferror(f);
    free(line);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool anim_config_save(const char *anim_path, const AnimConfig *cfg) {
    if (!cfg) return false;
    char cfg_path[PATH_MAX], temp_path[PATH_MAX];
    if (!anim_config_build_path(anim_path, cfg_path, sizeof(cfg_path))) return false;
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.XXXXXX", cfg_path) >= (int)sizeof(temp_path)) return false;
    KvPair updates[15];
    int count = 0;
    const struct { const char *key; float value; bool present; } numbers[] = {
        {"spring.stiffness", cfg->spring_stiffness, cfg->has_spring_stiffness},
        {"spring.drag", cfg->spring_drag, cfg->has_spring_drag},
        {"spring.gravity", cfg->spring_gravity, cfg->has_spring_gravity},
        {"spring.cloth_mode", cfg->spring_cloth_mode, cfg->has_spring_cloth_mode},
        {"spring.cloth_stiffness", cfg->spring_cloth_stiffness, cfg->has_spring_cloth_stiffness},
        {"spring.cloth_drag", cfg->spring_cloth_drag, cfg->has_spring_cloth_drag},
        {"spring.cloth_gravity", cfg->spring_cloth_gravity, cfg->has_spring_cloth_gravity},
        {"spring.cloth_taper", cfg->spring_cloth_taper, cfg->has_spring_cloth_taper},
        {"anim.seam_blend_time", cfg->anim_seam_blend_time, cfg->has_anim_seam_blend_time},
        {"bounds.world_w", cfg->bounds_world_w, cfg->has_bounds_world_w},
        {"bounds.world_h", cfg->bounds_world_h, cfg->has_bounds_world_h},
    };
    for (unsigned i = 0; i < sizeof(numbers)/sizeof(numbers[0]); i++) {
        if (!numbers[i].present) continue;
        if (!isfinite(numbers[i].value)) return false;
        snprintf(updates[count].key, sizeof(updates[count].key), "%s", numbers[i].key);
        snprintf(updates[count].value, sizeof(updates[count].value), "%.3f", numbers[i].value);
        count++;
    }
    const struct { const char *key; bool value; bool present; } flags[] = {
        {"loopable", cfg->loopable, cfg->has_loopable},
        {"startup", cfg->startup, cfg->has_startup},
        {"is_idle", cfg->is_idle, cfg->has_is_idle},
        {"floor_contact",cfg->floor_contact,cfg->has_floor_contact},
    };
    for (unsigned i = 0; i < sizeof(flags)/sizeof(flags[0]); i++) {
        if (!flags[i].present) continue;
        snprintf(updates[count].key, sizeof(updates[count].key), "%s", flags[i].key);
        snprintf(updates[count].value, sizeof(updates[count].value), "%d", flags[i].value);
        count++;
    }
    if (!count) return true;
    FILE *input = fopen(cfg_path, "r");
    if (!input && errno != ENOENT) return false;
    int fd = mkstemp(temp_path);
    if (fd < 0) { if (input) fclose(input); return false; }
    struct stat info;
    if (input && fstat(fileno(input), &info) == 0) {
        if (fchmod(fd, info.st_mode & 0777) != 0) {
            close(fd); fclose(input); unlink(temp_path); return false;
        }
    }
    FILE *output = fdopen(fd, "w");
    if (!output) { close(fd); if (input) fclose(input); unlink(temp_path); return false; }
    bool seen[15] = {0}, ok = true, newline = true;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while (input && (length = getline(&line, &capacity, input)) >= 0) {
        char *copy = strdup(line);
        if (!copy) { ok = false; break; }
        trim_spaces(copy);
        char *eq = copy[0] == '#' ? NULL : strchr(copy, '=');
        int index = -1;
        if (eq) { *eq = '\0'; trim_spaces(copy); index = kv_find(updates, count, copy); }
        if (index >= 0) {
            if (!seen[index]) {
                if (!newline) fputc('\n', output);
                fprintf(output, "%s=%s\n", updates[index].key, updates[index].value);
                newline = true;
                seen[index] = true;
            }
        } else {
            fwrite(line, 1, (size_t)length, output);
            newline = length > 0 && line[length - 1] == '\n';
        }
        free(copy);
    }
    free(line);
    if (input) { if (ferror(input)) ok = false; if (fclose(input) != 0) ok = false; }
    for (int i = 0; i < count; i++) if (!seen[i]) {
        if (!newline) fputc('\n', output);
        fprintf(output, "%s=%s\n", updates[i].key, updates[i].value);
        newline = true;
    }
    if (ferror(output) || fflush(output) != 0 || fsync(fileno(output)) != 0) ok = false;
    if (fclose(output) != 0) ok = false;
    if (ok && rename(temp_path, cfg_path) == 0) return true;
    unlink(temp_path);
    return false;
}
