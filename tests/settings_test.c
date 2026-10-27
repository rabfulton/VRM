#define _POSIX_C_SOURCE 200809L
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/vrm-settings-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    Settings settings = {0};
    assert(!settings_load(path, &settings));
    assert(!settings.has_always_on_top);
    settings.has_camera_distance = true;
    settings.camera_distance = 3.5f;
    settings.has_window_position = true;
    settings.window_pos[0] = 42; settings.window_pos[1] = 84;
    settings.has_always_on_top = true;
    for (int enabled = 1; enabled >= 0; enabled--) {
        settings.always_on_top = enabled;
        assert(settings_save(path, &settings));
        Settings restored;
        assert(settings_load(path, &restored));
        assert(restored.has_always_on_top && restored.always_on_top == (enabled != 0));
        assert(restored.has_camera_distance && restored.camera_distance == 3.5f);
        assert(restored.has_window_position && restored.window_pos[0] == 42 && restored.window_pos[1] == 84);
    }
    FILE *file = fopen(path, "w"); assert(file);
    fputs("always_on_top=invalid\ncamera_distance=2.5\n", file); fclose(file);
    assert(settings_load(path, &settings));
    assert(!settings.has_always_on_top && settings.camera_distance == 2.5f);
    unlink(path);
    puts("Settings tests passed: remembered on/off state, geometry preservation and invalid-value fallback");
    return 0;
}
