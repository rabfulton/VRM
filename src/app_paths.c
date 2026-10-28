#define _POSIX_C_SOURCE 200809L
#include "app_paths.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fits(int length, size_t size) {
    return length >= 0 && (size_t)length < size;
}

static const char *home_directory(void) {
    const char *home = getenv("HOME");
    if (home && home[0] == '/') return home;
    struct passwd *entry = getpwuid(getuid());
    return entry ? entry->pw_dir : NULL;
}

static bool make_directories(const char *path) {
    char copy[PATH_MAX];
    if (!fits(snprintf(copy, sizeof(copy), "%s", path), sizeof(copy))) return false;
    for (char *part = copy + 1; ; part++) {
        if (*part && *part != '/') continue;
        char saved = *part;
        *part = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) return false;
        struct stat info;
        if (stat(copy, &info) != 0 || !S_ISDIR(info.st_mode)) return false;
        *part = saved;
        if (!saved) return true;
    }
}

bool app_settings_path(char *path, size_t size) {
    const char *override = getenv("VRM_SETTINGS_PATH");
    if (override && *override) return fits(snprintf(path, size, "%s", override), size);
    const char *config = getenv("XDG_CONFIG_HOME");
    char folder[PATH_MAX];
    if (config && config[0] == '/') {
        if (!fits(snprintf(folder, sizeof(folder), "%s/domovushka", config), sizeof(folder))) return false;
    } else {
        const char *home = home_directory();
        if (!home || !fits(snprintf(folder, sizeof(folder), "%s/.config/domovushka", home), sizeof(folder))) return false;
    }
    return fits(snprintf(path, size, "%s/renderer.cfg", folder), size) && make_directories(folder);
}

static bool private_directory(const char *path, bool create) {
    if (create && mkdir(path, 0700) != 0 && errno != EEXIST) return false;
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode) &&
           info.st_uid == getuid() && !(info.st_mode & 0077);
}

bool app_socket_path(char *path, size_t size) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char folder[PATH_MAX];
    int length;
    if (runtime && runtime[0] == '/' && private_directory(runtime, false))
        length = snprintf(folder, sizeof(folder), "%s/domovushka", runtime);
    else
        length = snprintf(folder, sizeof(folder), "/tmp/domovushka-%lu", (unsigned long)getuid());
    if (!fits(length, sizeof(folder)) || !private_directory(folder, true)) {
        fprintf(stderr, "Cannot create a private runtime directory (owned by you, mode 0700).\n");
        return false;
    }
    length = snprintf(path, size, "%s/renderer.sock", folder);
    if (!fits(length, size) || length >= 108) {
        fprintf(stderr, "Runtime socket path too long; use --socket with a shorter private path.\n");
        return false;
    }
    return true;
}

bool app_migrate_settings(const char *destination) {
    if (access(destination, F_OK) == 0) return true;
    char source[PATH_MAX], marker[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", source, sizeof(source) - 1);
    if (length < 0) return true;
    source[length] = '\0';
    char *slash = strrchr(source, '/');
    if (!slash) return true;
    *slash = '\0';
    if (!fits(snprintf(marker, sizeof(marker), "%s/src/main.c", source), sizeof(marker)) || access(marker, F_OK) != 0)
        return true;
    if (!fits(snprintf(marker, sizeof(marker), "%s/Makefile", source), sizeof(marker)) || access(marker, F_OK) != 0)
        return true;
    if (!fits(snprintf(marker, sizeof(marker), "%s/settings.cfg", source), sizeof(marker))) return false;
    FILE *input = fopen(marker, "rb");
    if (!input) return errno == ENOENT;
    if (!fits(snprintf(source, sizeof(source), "%s.import-XXXXXX", destination), sizeof(source))) { fclose(input); return false; }
    int fd = mkstemp(source);
    FILE *output = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (!output) { if (fd >= 0) { close(fd); unlink(source); } fclose(input); return false; }
    char buffer[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), input)) > 0)
        if (fwrite(buffer, 1, n, output) != n) { ok = false; break; }
    if (ferror(input) || fflush(output) != 0 || fsync(fd) != 0) ok = false;
    fclose(input);
    if (fclose(output) != 0) ok = false;
    if (ok && link(source, destination) != 0 && errno != EEXIST) ok = false;
    unlink(source);
    return ok;
}
