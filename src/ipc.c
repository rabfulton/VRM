/*
 * VRM Overlay - IPC Server Implementation
 * NDJSON over UNIX domain socket
 */

#include "ipc.h"
#include "../deps/cjson/cJSON.h"
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void handle_line(IpcServer *ipc, const char *line) {
    cJSON *json = cJSON_Parse(line);
    if (!json) {
        fprintf(stderr, "IPC: Invalid JSON\n");
        return;
    }

    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    cJSON *action_item = cJSON_GetObjectItem(json, "action");

    int id = id_item ? id_item->valueint : 0;
    const char *action = action_item ? action_item->valuestring : "";

    if (ipc->callback) {
        ipc->callback(action, json, id, ipc->user_data);
    }

    cJSON_Delete(json);
}

static void read_line_async(IpcServer *ipc);

static void on_line_read(GObject *source, GAsyncResult *result, gpointer user_data) {
    IpcServer *ipc = (IpcServer *)user_data;
    GError *error = NULL;
    gsize length;

    char *line = g_data_input_stream_read_line_finish(
        G_DATA_INPUT_STREAM(source), result, &length, &error);

    if (error) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            fprintf(stderr, "IPC read error: %s\n", error->message);
        }
        g_error_free(error);
        return;
    }

    if (line) {
        handle_line(ipc, line);
        g_free(line);
        read_line_async(ipc);
    } else {
        /* Client disconnected */
        printf("IPC: Client disconnected\n");
        if (ipc->client) {
            g_object_unref(ipc->client);
            ipc->client = NULL;
        }
        ipc->input = NULL;
        ipc->output = NULL;
    }
}

static void read_line_async(IpcServer *ipc) {
    if (!ipc->input) return;
    g_data_input_stream_read_line_async(
        ipc->input, G_PRIORITY_DEFAULT, NULL, on_line_read, ipc);
}

static gboolean on_incoming(GSocketService *service, GSocketConnection *conn,
                            GObject *source, gpointer user_data) {
    (void)service; (void)source;
    IpcServer *ipc = (IpcServer *)user_data;

    /* Only one client at a time */
    if (ipc->client) {
        printf("IPC: Rejecting connection (already connected)\n");
        return FALSE;
    }

    printf("IPC: Client connected\n");
    ipc->client = g_object_ref(conn);

    GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    ipc->input = g_data_input_stream_new(in);
    ipc->output = g_io_stream_get_output_stream(G_IO_STREAM(conn));

    read_line_async(ipc);
    return TRUE;
}

bool ipc_init(IpcServer *ipc, const char *socket_path, IpcCommandCallback cb, void *user_data) {
    memset(ipc, 0, sizeof(IpcServer));
    ipc->callback = cb;
    ipc->user_data = user_data;
    ipc->socket_path = g_strdup(socket_path);

    /* Remove stale socket */
    unlink(socket_path);

    GError *error = NULL;
    ipc->service = g_socket_service_new();

    GSocketAddress *addr = g_unix_socket_address_new(socket_path);
    if (!g_socket_listener_add_address(G_SOCKET_LISTENER(ipc->service),
            addr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
            NULL, NULL, &error)) {
        fprintf(stderr, "IPC: Failed to bind: %s\n", error->message);
        g_error_free(error);
        g_object_unref(addr);
        return false;
    }
    g_object_unref(addr);

    g_signal_connect(ipc->service, "incoming", G_CALLBACK(on_incoming), ipc);
    g_socket_service_start(ipc->service);

    ipc->running = true;
    printf("IPC: Listening on %s\n", socket_path);
    return true;
}

void ipc_cleanup(IpcServer *ipc) {
    if (!ipc->running) return;

    if (ipc->service) {
        g_socket_service_stop(ipc->service);
        g_object_unref(ipc->service);
    }
    if (ipc->client) {
        g_object_unref(ipc->client);
    }
    if (ipc->input) {
        g_object_unref(ipc->input);
    }
    if (ipc->socket_path) {
        unlink(ipc->socket_path);
        g_free(ipc->socket_path);
    }

    memset(ipc, 0, sizeof(IpcServer));
}

void ipc_respond(IpcServer *ipc, int id, bool success, const char *error_msg) {
    if (!ipc->output) return;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddStringToObject(resp, "status", success ? "ok" : "error");
    if (!success && error_msg) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "message", error_msg);
        cJSON_AddItemToObject(resp, "error", err);
    }

    char *str = cJSON_PrintUnformatted(resp);
    gsize len = strlen(str);
    char *line = g_strdup_printf("%s\n", str);

    GError *error = NULL;
    g_output_stream_write_all(ipc->output, line, len + 1, NULL, NULL, &error);
    if (error) {
        fprintf(stderr, "IPC write error: %s\n", error->message);
        g_error_free(error);
    }

    g_free(line);
    free(str);
    cJSON_Delete(resp);
}

void ipc_respond_data(IpcServer *ipc, int id, const char *json_data) {
    if (!ipc->output) return;

    char *line = g_strdup_printf("{\"id\":%d,\"status\":\"ok\",\"data\":%s}\n", id, json_data);

    GError *error = NULL;
    g_output_stream_write_all(ipc->output, line, strlen(line), NULL, NULL, &error);
    if (error) {
        fprintf(stderr, "IPC write error: %s\n", error->message);
        g_error_free(error);
    }

    g_free(line);
}
