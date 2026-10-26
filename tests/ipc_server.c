/* Headless fixture exercising the production transport without OpenGL/audio. */
#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "cJSON.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static volatile sig_atomic_t running = 1;
static void stop(int signal_number) { (void)signal_number; running = 0; }

static void command(const char *action, void *params, int id, void *ctx) {
    IpcServer *ipc = ctx;
    if (!strcmp(action, "large") || !strcmp(action, "overflow")) {
        int small = 4096;
        setsockopt(ipc->client_fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
        char *payload = malloc(400003);
        if (!payload) abort();
        payload[0] = '"';
        memset(payload + 1, 'x', 400000);
        payload[400001] = '"';
        payload[400002] = 0;
        int repeats = !strcmp(action, "overflow") ? 8 : 1;
        for (int i = 0; i < repeats; i++) ipc_respond_data(ipc, id, payload);
        free(payload);
    } else {
        char *payload = cJSON_PrintUnformatted(params);
        ipc_respond_data(ipc, id, payload);
        free(payload);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    IpcServer ipc;
    if (!ipc_init(&ipc, argv[1], command, &ipc)) return 1;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    struct timespec delay = {0, 1000000};
    while (running) { ipc_poll(&ipc); nanosleep(&delay, NULL); }
    ipc_cleanup(&ipc);
    return 0;
}
