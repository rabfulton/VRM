#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* Default paths create their parent directories. Explicit paths remain caller-owned. */
bool app_settings_path(char *path, size_t size);
bool app_socket_path(char *path, size_t size);
/* Import only a sibling checkout's settings, never the current working directory. */
bool app_migrate_settings(const char *destination);

#endif
