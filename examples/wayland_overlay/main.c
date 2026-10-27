/* Native layer-shell/EGL feasibility probe. No avatar, IPC or input ownership. */
#define _POSIX_C_SOURCE 200809L
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlr-layer-shell-client.h"

static volatile sig_atomic_t interrupted;
static void interrupt_handler(int signal_number) { (void)signal_number; interrupted = 1; }

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wl_callback *frame;
    struct wl_egl_window *window;
    EGLDisplay egl;
    EGLContext context;
    EGLSurface buffer;
    int width, height, scale, x, y;
    bool configured, closed, ready;
    unsigned frames, limit;
} Probe;

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    Probe *p = data;
    if (!strcmp(interface, wl_compositor_interface.name) && version >= 3)
        p->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                        version < 6 ? version : 6);
    else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
        p->shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height) {
    Probe *p = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!width || !height || width > 16384 || height > 16384) {
        fprintf(stderr, "Unsupported output size %ux%u\n", width, height);
        p->closed = true;
        return;
    }
    p->width = (int)width;
    p->height = (int)height;
    p->configured = true;
    fprintf(stderr, "Configured canvas: %dx%d logical pixels\n", p->width, p->height);
}
static void closed(void *data, struct zwlr_layer_surface_v1 *layer) {
    (void)layer;
    ((Probe *)data)->closed = true;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = {configure, closed};
static void enter(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)data; (void)surface; (void)output;
}
static void leave(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)data; (void)surface; (void)output;
}
static void preferred_scale(void *data, struct wl_surface *surface, int32_t scale) {
    (void)surface;
    Probe *p = data;
    if (scale > 0 && scale <= 8) p->scale = scale;
}
static void preferred_transform(void *data, struct wl_surface *surface, uint32_t transform) {
    (void)data; (void)surface; (void)transform;
    /* Normal buffers remain valid; the compositor handles rotation. */
}
static const struct wl_surface_listener surface_listener = {
    .enter = enter, .leave = leave,
    .preferred_buffer_scale = preferred_scale,
    .preferred_buffer_transform = preferred_transform,
};
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    (void)time;
    Probe *p = data;
    wl_callback_destroy(callback);
    p->frame = NULL;
    p->ready = true;
}
static const struct wl_callback_listener frame_listener = {frame_done};

static bool initialize_egl(Probe *p) {
    /* Resolve the client extension before a display exists: libepoxy cannot
     * determine an EGL 1.5 display version at this point. */
    const char *extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!extensions || !strstr(extensions, "EGL_EXT_platform_base") ||
        (!strstr(extensions, "EGL_EXT_platform_wayland") &&
         !strstr(extensions, "EGL_KHR_platform_wayland"))) return false;
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_display) return false;
    p->egl = get_display(EGL_PLATFORM_WAYLAND_EXT, p->display, NULL);
    if (p->egl == EGL_NO_DISPLAY || !eglInitialize(p->egl, NULL, NULL) ||
        !eglBindAPI(EGL_OPENGL_API)) return false;
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(p->egl, attributes, &config, 1, &count) || count != 1) return false;
    const EGLint context_attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE
    };
    p->context = eglCreateContext(p->egl, config, EGL_NO_CONTEXT, context_attributes);
    if (p->context == EGL_NO_CONTEXT) return false;
    p->window = wl_egl_window_create(p->surface, p->width * p->scale, p->height * p->scale);
    if (!p->window) return false;
    p->buffer = eglCreateWindowSurface(p->egl, config, (EGLNativeWindowType)p->window, NULL);
    if (p->buffer == EGL_NO_SURFACE ||
        !eglMakeCurrent(p->egl, p->buffer, p->buffer, p->context)) return false;
    fprintf(stderr, "Native Wayland EGL: %s; GL %s\n", eglQueryString(p->egl, EGL_VENDOR),
            glGetString(GL_VERSION));
    return eglSwapInterval(p->egl, 0);
}

static bool draw(Probe *p) {
    int width = p->width * p->scale, height = p->height * p->scale;
    wl_surface_set_buffer_scale(p->surface, p->scale);
    wl_egl_window_resize(p->window, width, height, 0, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Premultiplied translucent marker, positioned inside the output canvas.
     * No full-output MSAA/depth/readback allocation is needed for this probe. */
    glEnable(GL_SCISSOR_TEST);
    glScissor(p->x * p->scale, (p->height - p->y - 160) * p->scale,
              160 * p->scale, 160 * p->scale);
    glClearColor(0.08f, 0.42f, 0.56f, 0.7f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    p->ready = false;
    p->frame = wl_surface_frame(p->surface);
    if (!p->frame) return false;
    wl_callback_add_listener(p->frame, &frame_listener, p);
    if (!eglSwapBuffers(p->egl, p->buffer)) return false;
    p->frames++;
    return true;
}

/* Interruptible, bounded wait, including while the compositor withholds frames.
 * Dispatch and flush before polling; handle socket backpressure without spinning. */
static bool dispatch(Probe *p) {
    while (wl_display_prepare_read(p->display) != 0)
        if (wl_display_dispatch_pending(p->display) < 0) return false;
    int flushed = wl_display_flush(p->display);
    if (flushed < 0 && errno != EAGAIN) {
        wl_display_cancel_read(p->display);
        return false;
    }
    struct pollfd fd = {wl_display_get_fd(p->display), POLLIN, 0};
    if (flushed < 0) fd.events |= POLLOUT;
    int result = poll(&fd, 1, 100);
    if (result > 0 && (fd.revents & POLLIN)) {
        if (wl_display_read_events(p->display) < 0) return false;
    } else {
        wl_display_cancel_read(p->display);
    }
    if (result < 0 && errno != EINTR) return false;
    if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
    return wl_display_dispatch_pending(p->display) >= 0;
}

static void cleanup(Probe *p) {
    if (p->egl != EGL_NO_DISPLAY) {
        eglMakeCurrent(p->egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (p->buffer != EGL_NO_SURFACE) eglDestroySurface(p->egl, p->buffer);
        if (p->context != EGL_NO_CONTEXT) eglDestroyContext(p->egl, p->context);
        eglTerminate(p->egl);
    }
    if (p->window) wl_egl_window_destroy(p->window);
    if (p->frame) wl_callback_destroy(p->frame);
    if (p->layer) zwlr_layer_surface_v1_destroy(p->layer);
    if (p->surface) wl_surface_destroy(p->surface);
    /* Version 1 of layer-shell has no destructor request. */
    if (p->shell) wl_proxy_destroy((struct wl_proxy *)p->shell);
    if (p->compositor) wl_compositor_destroy(p->compositor);
    if (p->registry) wl_registry_destroy(p->registry);
    if (p->display) wl_display_disconnect(p->display);
}

int main(int argc, char **argv) {
    Probe p = {.scale = 1, .x = 100, .y = 100, .limit = 240};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") && strcmp(argv[i], "--x") && strcmp(argv[i], "--y")) {
            fprintf(stderr, "Usage: %s [--frames N] [--x N] [--y N]\n"
                    "Frames 0 runs until Ctrl+C; the entire surface passes input through.\n", argv[0]);
            return 2;
        }
        const char *option = argv[i];
        if (++i == argc) return 2;
        char *end;
        errno = 0;
        long value = strtol(argv[i], &end, 10);
        if (errno || end == argv[i] || *end || value < 0 || value > 16384) return 2;
        if (!strcmp(option, "--x")) p.x = (int)value;
        else if (!strcmp(option, "--y")) p.y = (int)value;
        else p.limit = (unsigned)value;
    }
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);
    int result = 1;
    p.display = wl_display_connect(NULL);
    if (!p.display) { fprintf(stderr, "Cannot connect to Wayland\n"); goto done; }
    p.registry = wl_display_get_registry(p.display);
    wl_registry_add_listener(p.registry, &registry_listener, &p);
    if (wl_display_roundtrip(p.display) < 0 || !p.compositor || !p.shell) {
        fprintf(stderr, "A Wayland compositor with wlr-layer-shell is required\n"); goto done;
    }
    p.surface = wl_compositor_create_surface(p.compositor);
    wl_surface_add_listener(p.surface, &surface_listener, &p);
    p.layer = zwlr_layer_shell_v1_get_layer_surface(p.shell, p.surface, NULL,
                ZWLR_LAYER_SHELL_V1_LAYER_TOP, "vrm-overlay-probe");
    zwlr_layer_surface_v1_add_listener(p.layer, &layer_listener, &p);
    zwlr_layer_surface_v1_set_anchor(p.layer,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(p.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(p.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(p.layer, 0);
    struct wl_region *empty = wl_compositor_create_region(p.compositor);
    wl_surface_set_input_region(p.surface, empty);
    wl_surface_set_opaque_region(p.surface, empty);
    wl_region_destroy(empty);
    wl_surface_commit(p.surface); /* No buffer before the first configure. */
    while (!p.configured && !p.closed && !interrupted)
        if (!dispatch(&p)) goto done;
    if (p.closed || interrupted) { result = 0; goto done; }
    if (!initialize_egl(&p)) {
        fprintf(stderr, "Cannot create desktop OpenGL 3.3 EGL context (0x%x)\n", eglGetError());
        goto done;
    }
    p.ready = true;
    while (!p.closed && !interrupted && (!p.limit || p.frames < p.limit)) {
        if (p.ready && !draw(&p)) goto done;
        if (!dispatch(&p)) goto done;
    }
    result = 0;
done:
    fprintf(stderr, "Presented %u frames; exit %d\n", p.frames, result);
    cleanup(&p);
    return result;
}
