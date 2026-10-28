#include "app_paths.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char settings[4096], socket[108];
    if (!app_settings_path(settings, sizeof(settings)) || !app_socket_path(socket, sizeof(socket))) return 1;
    if (argc > 1 && !strcmp(argv[1], "--migrate") && !app_migrate_settings(settings)) return 2;
    printf("%s\n%s\n", settings, socket);
    return 0;
}
