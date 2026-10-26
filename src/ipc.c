/* Bounded NDJSON transport. Poll and callbacks run on the application thread. */
#define _GNU_SOURCE
#include "ipc.h"
#include "../deps/cjson/cJSON.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

static void disconnect_client(IpcServer *ipc) {
    if (ipc->client_fd >= 0) close(ipc->client_fd);
    ipc->client_fd = -1;
    ipc->read_len = 0;
    free(ipc->write_buf);
    ipc->write_buf = NULL;
    ipc->write_len = ipc->write_offset = 0;
}

static void flush_output(IpcServer *ipc) {
    while (ipc->client_fd >= 0 && ipc->write_offset < ipc->write_len) {
        ssize_t n = send(ipc->client_fd, ipc->write_buf + ipc->write_offset,
                         ipc->write_len - ipc->write_offset, MSG_NOSIGNAL);
        if (n > 0) ipc->write_offset += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        else { disconnect_client(ipc); return; }
    }
    free(ipc->write_buf);
    ipc->write_buf = NULL;
    ipc->write_len = ipc->write_offset = 0;
}

static void queue_response(IpcServer *ipc, cJSON *response) {
    if (ipc->client_fd < 0) return;
    char *str = cJSON_PrintUnformatted(response);
    if (!str) { disconnect_client(ipc); return; }
    size_t len = strlen(str);
    size_t pending = ipc->write_len - ipc->write_offset;
    if (len + 1 > IPC_MAX_PENDING_BYTES - pending) {
        free(str);
        disconnect_client(ipc);
        return;
    }
    if (pending) memmove(ipc->write_buf, ipc->write_buf + ipc->write_offset, pending);
    char *buf = realloc(ipc->write_buf, pending + len + 1);
    if (!buf) { free(str); disconnect_client(ipc); return; }
    ipc->write_buf = buf;
    memcpy(buf + pending, str, len);
    buf[pending + len] = '\n';
    ipc->write_len = pending + len + 1;
    ipc->write_offset = 0;
    free(str);
    flush_output(ipc);
}

static void handle_line(IpcServer *ipc, const char *line) {
    cJSON *json = cJSON_ParseWithOpts(line, NULL, true);
    if (!json) { ipc_respond(ipc, 0, false, "Invalid JSON"); return; }
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(json, "id");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
    int id = 0;
    if (id_item) {
        double number = id_item->valuedouble;
        if (!cJSON_IsNumber(id_item) || !isfinite(number) ||
            number < INT_MIN || number > INT_MAX || number != id_item->valueint) {
            ipc_respond(ipc, 0, false, "id must be a signed 32-bit integer");
            cJSON_Delete(json);
            return;
        }
        id = id_item->valueint;
    }
    if (!cJSON_IsObject(json) || !cJSON_IsString(action) || !action->valuestring[0]) {
        ipc_respond(ipc, id, false, "action must be a nonempty string");
    } else if (ipc->callback) {
        ipc->callback(action->valuestring, json, id, ipc->user_data);
    }
    cJSON_Delete(json);
}

bool ipc_init(IpcServer *ipc, const char *socket_path, IpcCommandCallback cb, void *user_data) {
    memset(ipc, 0, sizeof(*ipc));
    ipc->server_fd = ipc->client_fd = ipc->lock_fd = -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (!socket_path || !*socket_path || strlen(socket_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "IPC: socket path is empty or too long\n");
        return false;
    }
    strcpy(addr.sun_path, socket_path);
    ipc->callback = cb;
    ipc->user_data = user_data;
    ipc->socket_path = strdup(socket_path);
    ipc->read_buf = malloc(IPC_MAX_REQUEST_BYTES + 1);
    char *lock_path = malloc(strlen(socket_path) + sizeof(".lock"));
    if (!ipc->socket_path || !ipc->read_buf || !lock_path) {
        free(lock_path);
        goto fail;
    }
    sprintf(lock_path, "%s.lock", socket_path);
    /* Keep the lock file: unlinking it creates a race between future instances. */
    ipc->lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    free(lock_path);
    if (ipc->lock_fd < 0 || flock(ipc->lock_fd, LOCK_EX | LOCK_NB) < 0) {
        fprintf(stderr, "IPC: socket is owned by another instance: %s\n", socket_path);
        goto fail;
    }
    struct stat st;
    if (lstat(socket_path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            fprintf(stderr, "IPC: refusing to replace a non-socket: %s\n", socket_path);
            goto fail;
        }
        /* Also respect older servers which do not use our lock file. */
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (probe < 0) goto fail;
        int result = connect(probe, (struct sockaddr *)&addr, sizeof(addr));
        int connect_error = errno;
        close(probe);
        if (result == 0 || connect_error != ECONNREFUSED) {
            fprintf(stderr, "IPC: socket is already in use: %s\n", socket_path);
            goto fail;
        }
        if (unlink(socket_path) < 0) goto fail;
    } else if (errno != ENOENT) goto fail;

    ipc->server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (ipc->server_fd < 0) goto fail;
    if (bind(ipc->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) goto fail;
    ipc->running = true; /* Cleanup now owns the bound socket path. */
    if (chmod(socket_path, 0600) < 0 || listen(ipc->server_fd, 4) < 0) goto fail;
    printf("IPC: Listening on %s\n", socket_path);
    return true;
fail:
    ipc_cleanup(ipc);
    return false;
}

void ipc_cleanup(IpcServer *ipc) {
    disconnect_client(ipc);
    if (ipc->server_fd >= 0) close(ipc->server_fd);
    if (ipc->running && ipc->socket_path) unlink(ipc->socket_path);
    if (ipc->lock_fd >= 0) close(ipc->lock_fd);
    free(ipc->socket_path);
    free(ipc->read_buf);
    memset(ipc, 0, sizeof(*ipc));
    ipc->server_fd = ipc->client_fd = ipc->lock_fd = -1;
}

void ipc_poll(IpcServer *ipc) {
    if (!ipc->running) return;
    if (ipc->client_fd < 0) {
        ipc->client_fd = accept4(ipc->server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    }
    if (ipc->client_fd < 0) return;
    flush_output(ipc);
    if (ipc->client_fd < 0) return;
    /* One bounded read per frame prevents a chatty client monopolizing rendering. */
    ssize_t n = recv(ipc->client_fd, ipc->read_buf + ipc->read_len,
                     IPC_MAX_REQUEST_BYTES - ipc->read_len, 0);
    if (n == 0) { disconnect_client(ipc); return; }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) disconnect_client(ipc);
        return;
    }
    ipc->read_len += (int)n;
    char *start = ipc->read_buf;
    char *end = start + ipc->read_len;
    char *newline;
    while (ipc->client_fd >= 0 && (newline = memchr(start, '\n', (size_t)(end - start)))) {
        *newline = '\0';
        if (memchr(start, '\0', (size_t)(newline - start)))
            ipc_respond(ipc, 0, false, "NUL bytes are not allowed");
        else if (newline != start) handle_line(ipc, start);
        start = newline + 1;
    }
    if (ipc->client_fd < 0) return;
    ipc->read_len = (int)(end - start);
    memmove(ipc->read_buf, start, (size_t)ipc->read_len);
    if (ipc->read_len == IPC_MAX_REQUEST_BYTES) disconnect_client(ipc);
}

void ipc_respond(IpcServer *ipc, int id, bool success, const char *error_msg) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddStringToObject(resp, "status", success ? "ok" : "error");
    if (!success && error_msg) {
        cJSON *err = cJSON_AddObjectToObject(resp, "error");
        cJSON_AddStringToObject(err, "message", error_msg);
    }
    queue_response(ipc, resp);
    cJSON_Delete(resp);
}

void ipc_respond_data(IpcServer *ipc, int id, const char *json_data) {
    cJSON *data = cJSON_ParseWithOpts(json_data, NULL, true);
    if (!data) { ipc_respond(ipc, id, false, "Invalid response data"); return; }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "data", data);
    queue_response(ipc, resp);
    cJSON_Delete(resp);
}
