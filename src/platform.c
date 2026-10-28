#define _POSIX_C_SOURCE 200809L
#include "platform_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const PlatformBackend *backend;
static const char *selection = "auto";
bool platform_select_backend(const char *name) {
    if (backend || !name || (strcmp(name,"auto") && strcmp(name,"x11") && strcmp(name,"wayland"))) return false;
    selection = name;
    return true;
}
const char *platform_backend_name(void) { return backend ? backend->name : "none"; }
double platform_get_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}
PlatformWindow *platform_create_window(int width, int height, const char *title, bool transparent, bool decorated) {
    const char *choice = selection;
    const char *environment = getenv("VRM_WINDOW_BACKEND");
    if (!strcmp(choice,"auto") && environment && *environment) choice = environment;
    if (strcmp(choice,"auto") && strcmp(choice,"x11") && strcmp(choice,"wayland")) {
        fprintf(stderr,"Invalid window backend: %s\n",choice); return NULL;
    }
    bool automatic = !strcmp(choice,"auto");
    PlatformWindow *window = NULL;
#ifdef VRM_PLATFORM_WAYLAND
    if (!strcmp(choice,"wayland") || (automatic && getenv("WAYLAND_DISPLAY"))) {
        backend = &platform_wayland_backend;
        window = backend->create_window(width,height,title,transparent,decorated);
        if (window) goto ready;
        backend = NULL;
        if (!automatic) return NULL;
        fprintf(stderr,"Native Wayland overlay unavailable; trying X11/XWayland.\n");
    }
#endif
#ifdef VRM_PLATFORM_X11
    if (automatic || !strcmp(choice,"x11")) {
        backend = &platform_x11_backend;
        window = backend->create_window(width,height,title,transparent,decorated);
        if (window) goto ready;
        backend = NULL;
    }
#endif
    fprintf(stderr,"Cannot create requested %s overlay (backend unavailable or display connection failed).\n",choice);
    return NULL;
ready:
    fprintf(stderr,"Window backend: %s\n",backend->name);
    return window;
}
bool platform_frame_ready(PlatformWindow *win) { return backend->frame_ready(win); }
void platform_present(PlatformWindow *win, unsigned int source, int width, int height) { backend->present(win,source,width,height); }
bool platform_open_menu(PlatformWindow *win) { return backend->open_menu(win); }
void platform_configure_menu(PlatformWindow *win,const GemMenu *menu) { backend->configure_menu(win,menu); }
void platform_destroy_window(PlatformWindow *win) { backend->destroy_window(win); }
bool platform_should_close(PlatformWindow *win) { return backend->should_close(win); }
void platform_poll_events(void) { backend->poll_events(); }
void platform_swap_buffers(PlatformWindow *win) { backend->swap_buffers(win); }
void platform_get_framebuffer_size(PlatformWindow *win, int *w, int *h) { backend->get_framebuffer_size(win,w,h); }
void platform_get_window_size(PlatformWindow *win, int *w, int *h) { backend->get_window_size(win,w,h); }
void platform_get_content_scale(PlatformWindow *win, float *xscale, float *yscale) { backend->get_content_scale(win,xscale,yscale); }
void platform_get_window_pos(PlatformWindow *win, int *x, int *y) { backend->get_window_pos(win,x,y); }
void platform_get_primary_workarea(int *x, int *y, int *w, int *h) { backend->get_primary_workarea(x,y,w,h); }
void platform_get_window_workarea(PlatformWindow *win, int *x, int *y, int *w, int *h) { backend->get_window_workarea(win,x,y,w,h); }
void platform_make_context_current(PlatformWindow *win) { backend->make_context_current(win); }
void platform_get_cursor_pos(PlatformWindow *win, double *x, double *y) { backend->get_cursor_pos(win,x,y); }
void platform_set_callbacks(PlatformWindow *win, PlatformCallbacks *cb) { backend->set_callbacks(win,cb); }
void platform_set_always_on_top(PlatformWindow *win, bool on_top) { backend->set_always_on_top(win,on_top); }
void platform_move_window(PlatformWindow *win, int x, int y) { backend->move_window(win,x,y); }
void platform_resize_window(PlatformWindow *win, int w, int h) { backend->resize_window(win,w,h); }
void platform_set_window_rect(PlatformWindow *win, int x, int y, int w, int h) { backend->set_window_rect(win,x,y,w,h); }
void platform_show_window(PlatformWindow *win) { backend->show_window(win); }
void platform_hide_window(PlatformWindow *win) { backend->hide_window(win); }
void platform_set_input_region(PlatformWindow *win, int x, int y, int w, int h) { backend->set_input_region(win,x,y,w,h); }
void platform_clear_input_region(PlatformWindow *win) { backend->clear_input_region(win); }
void platform_set_input_mask(PlatformWindow *win, const unsigned char *bits, int width, int height) { backend->set_input_mask(win,bits,width,height); }
void platform_set_input_regions(PlatformWindow *win, const PlatformRect *rects, int rect_count) { backend->set_input_regions(win,rects,rect_count); }
