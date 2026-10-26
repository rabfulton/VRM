/*
 * VRM Overlay - IPC Server (POSIX sockets)
 */

#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include <stddef.h>

/* Includes the terminating newline. Oversized requests disconnect the client. */
#define IPC_MAX_REQUEST_BYTES 65536
#define IPC_MAX_PENDING_BYTES (1024 * 1024)

typedef void (*IpcCommandCallback)(const char *action, void *params, int id, void *user_data);

typedef struct {
    int server_fd;
    int client_fd;
    int lock_fd;
    char *socket_path;
    char *read_buf;
    int read_len;
    char *write_buf;
    size_t write_len;
    size_t write_offset;
    IpcCommandCallback callback;
    void *user_data;
    bool running;
} IpcServer;

bool ipc_init(IpcServer *ipc, const char *socket_path, IpcCommandCallback cb, void *user_data);
void ipc_cleanup(IpcServer *ipc);
void ipc_poll(IpcServer *ipc);
void ipc_respond(IpcServer *ipc, int id, bool success, const char *error_msg);
void ipc_respond_data(IpcServer *ipc, int id, const char *json_data);

#endif
