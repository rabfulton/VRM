/*
 * VRM Overlay - IPC Server
 */

#ifndef IPC_H
#define IPC_H

#include <gio/gio.h>
#include <stdbool.h>

/* Command callback type */
typedef void (*IpcCommandCallback)(const char *action, void *params, int id, void *user_data);

typedef struct {
    GSocketService *service;
    GSocketConnection *client;
    GDataInputStream *input;
    GOutputStream *output;
    char *socket_path;
    IpcCommandCallback callback;
    void *user_data;
    bool running;
} IpcServer;

/* Initialize IPC server on given socket path */
bool ipc_init(IpcServer *ipc, const char *socket_path, IpcCommandCallback cb, void *user_data);

/* Cleanup IPC server */
void ipc_cleanup(IpcServer *ipc);

/* Send response to client */
void ipc_respond(IpcServer *ipc, int id, bool success, const char *error_msg);

/* Send response with data */
void ipc_respond_data(IpcServer *ipc, int id, const char *json_data);

#endif
