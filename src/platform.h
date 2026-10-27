/*
 * Platform abstraction layer for windowing/input
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

typedef struct PlatformWindow PlatformWindow;

typedef struct {
    void (*on_resize)(int width, int height, void *user);
    void (*on_mouse_move)(double x, double y, void *user);
    void (*on_mouse_button)(int button, int action, void *user);
    void (*on_scroll)(double dx, double dy, void *user);
    void (*on_key)(int key, int action, void *user);
    void (*on_menu_action)(int action, void *user); /* 0 Chat, 1 Studio, 2 Quit */
    void *user_data;
} PlatformCallbacks;

/* Selection must precede window creation. auto prefers a usable Wayland layer. */
bool platform_select_backend(const char *name);
const char *platform_backend_name(void);
bool platform_frame_ready(PlatformWindow *win);
/* Resolve the bounded avatar FBO into the backend's presentation surface. */
void platform_present(PlatformWindow *win, unsigned int source, int width, int height);
/* Returns false for X11, whose menu is owned by the Qt controller. */
bool platform_open_menu(PlatformWindow *win);

/* Geometry describes the bounded avatar viewport. On X11 it is a native
 * window rectangle; on Wayland it is output-local placement within the canvas.
 * Pointer coordinates are viewport-local logical units on both backends. */
/* Window management */
PlatformWindow *platform_create_window(int width, int height, const char *title, bool transparent, bool decorated);
void platform_destroy_window(PlatformWindow *win);
bool platform_should_close(PlatformWindow *win);
void platform_poll_events(void);
void platform_swap_buffers(PlatformWindow *win);
void platform_get_framebuffer_size(PlatformWindow *win, int *w, int *h);
void platform_get_window_size(PlatformWindow *win, int *w, int *h);
void platform_get_content_scale(PlatformWindow *win, float *xscale, float *yscale);
void platform_get_window_pos(PlatformWindow *win, int *x, int *y);
void platform_get_primary_workarea(int *x, int *y, int *w, int *h);
void platform_get_window_workarea(PlatformWindow *win, int *x, int *y, int *w, int *h);
void platform_make_context_current(PlatformWindow *win);
void platform_get_cursor_pos(PlatformWindow *win, double *x, double *y);
void platform_set_callbacks(PlatformWindow *win, PlatformCallbacks *cb);

/* Window properties */
void platform_set_always_on_top(PlatformWindow *win, bool on_top);
void platform_move_window(PlatformWindow *win, int x, int y);
void platform_resize_window(PlatformWindow *win, int w, int h);
void platform_set_window_rect(PlatformWindow *win, int x, int y, int w, int h);
void platform_show_window(PlatformWindow *win);
void platform_hide_window(PlatformWindow *win);

/* Input region (click-through) */
void platform_set_input_region(PlatformWindow *win, int x, int y, int w, int h);
void platform_clear_input_region(PlatformWindow *win);
/* NULL/empty mask makes the entire overlay click-through. */
void platform_set_input_mask(PlatformWindow *win, const unsigned char *bits, int width, int height);

typedef struct {
    int x;
    int y;
    int w;
    int h;
} PlatformRect;

void platform_set_input_regions(PlatformWindow *win, const PlatformRect *rects, int rect_count);

/* Time */
double platform_get_time(void);

/* Key codes (subset) */
#define PLATFORM_KEY_ESCAPE 256
#define PLATFORM_KEY_Q      81
#define PLATFORM_KEY_T      84
#define PLATFORM_KEY_C      67
#define PLATFORM_KEY_B      66
#define PLATFORM_KEY_1      49
#define PLATFORM_KEY_2      50
#define PLATFORM_KEY_3      51
#define PLATFORM_KEY_4      52
#define PLATFORM_KEY_5      53

#define PLATFORM_PRESS   1
#define PLATFORM_RELEASE 0

#define PLATFORM_MOUSE_LEFT   0
#define PLATFORM_MOUSE_RIGHT  1
#define PLATFORM_MOUSE_MIDDLE 2

#endif /* PLATFORM_H */
