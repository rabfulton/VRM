/*
 * VRM Overlay - IPC Server (POSIX sockets)
 */

#define _GNU_SOURCE
#include "ipc.h"
#include "../deps/cjson/cJSON.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define READ_BUF_SIZE 8192

static void handle_line(IpcServer *ipc, const char *line) {
    cJSON *json = cJSON_Parse(line);
    if (!json) { fprintf(stderr, "IPC: Invalid JSON\n"); return; }
    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    cJSON *action_item = cJSON_GetObjectItem(json, "action");
    int id = id_item ? id_item->valueint : 0;
    const char *action = action_item ? action_item->valuestring : "";
    if (ipc->callback) ipc->callback(action, json, id, ipc->user_data);
    cJSON_Delete(json);
}

bool ipc_init(IpcServer *ipc, const char *socket_path, IpcCommandCallback cb, void *user_data) {
    memset(ipc, 0, sizeof(IpcServer));
    ipc->callback = cb;
    ipc->user_data = user_data;
    ipc->socket_path = strdup(socket_path);
    ipc->client_fd = -1;
    ipc->read_buf = malloc(READ_BUF_SIZE);
    ipc->read_len = 0;

    unlink(socket_path);

    ipc->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc->server_fd < 0) { perror("socket"); return false; }

    int flags = fcntl(ipc->server_fd, F_GETFL, 0);
    fcntl(ipc->server_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(ipc->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(ipc->server_fd); return false;
    }
    if (listen(ipc->server_fd, 1) < 0) {
        perror("listen"); close(ipc->server_fd); return false;
    }

    ipc->running = true;
    printf("IPC: Listening on %s\n", socket_path);
    return true;
}

void ipc_cleanup(IpcServer *ipc) {
    if (!ipc->running) return;
    if (ipc->client_fd >= 0) close(ipc->client_fd);
    if (ipc->server_fd >= 0) close(ipc->server_fd);
    if (ipc->socket_path) { unlink(ipc->socket_path); free(ipc->socket_path); }
    if (ipc->read_buf) free(ipc->read_buf);
    memset(ipc, 0, sizeof(IpcServer));
}

void ipc_poll(IpcServer *ipc) {
    if (!ipc->running) return;

    /* Accept new connection */
    if (ipc->client_fd < 0) {
        int fd = accept(ipc->server_fd, NULL, NULL);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            ipc->client_fd = fd;
            ipc->read_len = 0;
            printf("IPC: Client connected\n");
        }
    }

    /* Read from client */
    if (ipc->client_fd >= 0) {
        int space = READ_BUF_SIZE - ipc->read_len - 1;
        if (space > 0) {
            int n = read(ipc->client_fd, ipc->read_buf + ipc->read_len, space);
            if (n > 0) {
                ipc->read_len += n;
                ipc->read_buf[ipc->read_len] = '\0';
                /* Process complete lines */
                char *line_start = ipc->read_buf;
                char *newline;
                while ((newline = strchr(line_start, '\n')) != NULL) {
                    *newline = '\0';
                    handle_line(ipc, line_start);
                    line_start = newline + 1;
                }
                /* Move remaining data to start */
                int remaining = ipc->read_len - (line_start - ipc->read_buf);
                if (remaining > 0) memmove(ipc->read_buf, line_start, remaining);
                ipc->read_len = remaining;
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                printf("IPC: Client disconnected\n");
                close(ipc->client_fd);
                ipc->client_fd = -1;
                ipc->read_len = 0;
            }
        }
    }
}

void ipc_respond(IpcServer *ipc, int id, bool success, const char *error_msg) {
    if (ipc->client_fd < 0) return;
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddStringToObject(resp, "status", success ? "ok" : "error");
    if (!success && error_msg) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "message", error_msg);
        cJSON_AddItemToObject(resp, "error", err);
    }
    char *str = cJSON_PrintUnformatted(resp);
    write(ipc->client_fd, str, strlen(str));
    write(ipc->client_fd, "\n", 1);
    free(str);
    cJSON_Delete(resp);
}

void ipc_respond_data(IpcServer *ipc, int id, const char *json_data) {
    if (ipc->client_fd < 0) return;
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "{\"id\":%d,\"status\":\"ok\",\"data\":%s}\n", id, json_data);
    write(ipc->client_fd, buf, len);
}
