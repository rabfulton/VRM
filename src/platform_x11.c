/*
 * Platform abstraction - GLFW + X11 implementation
 */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform_backend.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

struct PlatformWindow {
    GLFWwindow *glfw;
    PlatformCallbacks cb;
    bool show_pending;
#ifdef GLFW_EXPOSE_NATIVE_X11
    Display *x_display;
    Window x_window;
    bool input_rect_valid;
    int input_rect[4];
#endif
};

static int glfw_inited = 0;

static void glfw_error_cb(int err, const char *desc) {
    fprintf(stderr, "GLFW error %d: %s\n", err, desc);
}

static void glfw_resize_cb(GLFWwindow *w, int width, int height) {
    PlatformWindow *win = glfwGetWindowUserPointer(w);
    if (win && win->cb.on_resize) win->cb.on_resize(width, height, win->cb.user_data);
}

static void glfw_cursor_cb(GLFWwindow *w, double x, double y) {
    PlatformWindow *win = glfwGetWindowUserPointer(w);
    if (win && win->cb.on_mouse_move) win->cb.on_mouse_move(x, y, win->cb.user_data);
}

static void glfw_mouse_cb(GLFWwindow *w, int button, int action, int mods) {
    (void)mods;
    PlatformWindow *win = glfwGetWindowUserPointer(w);
    if (win && win->cb.on_mouse_button) win->cb.on_mouse_button(button, action == GLFW_PRESS ? 1 : 0, win->cb.user_data);
}

static void glfw_scroll_cb(GLFWwindow *w, double dx, double dy) {
    PlatformWindow *win = glfwGetWindowUserPointer(w);
    if (win && win->cb.on_scroll) win->cb.on_scroll(dx, dy, win->cb.user_data);
}

static void glfw_key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    PlatformWindow *win = glfwGetWindowUserPointer(w);
    if (win && win->cb.on_key && action != GLFW_REPEAT) win->cb.on_key(key, action == GLFW_PRESS ? 1 : 0, win->cb.user_data);
}

PlatformWindow *x11_create_window(int width, int height, const char *title, bool transparent, bool decorated) {
    if (!glfw_inited) {
        glfwSetErrorCallback(glfw_error_cb);
        /* This backend uses Xlib/XShape and global window coordinates. GLFW
         * 3.4+ may otherwise select Wayland and return NULL X11 handles. */
#ifdef GLFW_PLATFORM
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (!glfwInit()) return NULL;
        glfw_inited = 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, transparent ? GLFW_TRUE : GLFW_FALSE);
    /* Set desktop-companion hints before the WM first manages this window. */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    GLFWwindow *glfw = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!glfw) return NULL;

    PlatformWindow *win = calloc(1, sizeof(PlatformWindow));
    if (!win) {
        glfwDestroyWindow(glfw);
        return NULL;
    }
    win->glfw = glfw;
    glfwSetWindowUserPointer(glfw, win);
    glfwMakeContextCurrent(glfw);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(glfw, glfw_resize_cb);
    glfwSetCursorPosCallback(glfw, glfw_cursor_cb);
    glfwSetMouseButtonCallback(glfw, glfw_mouse_cb);
    glfwSetScrollCallback(glfw, glfw_scroll_cb);
    glfwSetKeyCallback(glfw, glfw_key_cb);

    win->x_display = glfwGetX11Display();
    win->x_window = glfwGetX11Window(glfw);
    if (!win->x_display || !win->x_window) {
        fprintf(stderr, "The GLFW overlay backend requires X11/XWayland.\n");
        glfwDestroyWindow(glfw);
        free(win);
        return NULL;
    }

    if (transparent && !decorated) {
        Atom state = XInternAtom(win->x_display, "_NET_WM_STATE", False);
        Atom states[] = {
            XInternAtom(win->x_display, "_NET_WM_STATE_SKIP_TASKBAR", False),
            XInternAtom(win->x_display, "_NET_WM_STATE_SKIP_PAGER", False)
        };
        XChangeProperty(win->x_display, win->x_window, state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)states, 2);
        Atom type = XInternAtom(win->x_display, "_NET_WM_WINDOW_TYPE", False);
        Atom utility = XInternAtom(win->x_display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        XChangeProperty(win->x_display, win->x_window, type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&utility, 1);
        x11_set_input_mask(win,NULL,0,0);
    }
    glfwShowWindow(glfw);

    return win;
}

void x11_destroy_window(PlatformWindow *win) {
    if (!win) return;
    glfwDestroyWindow(win->glfw);
    free(win);
}

bool x11_should_close(PlatformWindow *win) {
    return glfwWindowShouldClose(win->glfw);
}

void x11_poll_events(void) {
    glfwPollEvents();
}

void x11_swap_buffers(PlatformWindow *win) {
    glfwSwapBuffers(win->glfw);
    if (win->show_pending) {
        win->show_pending = false;
        glfwShowWindow(win->glfw);
    }
}

void x11_get_framebuffer_size(PlatformWindow *win, int *w, int *h) {
    glfwGetFramebufferSize(win->glfw, w, h);
}

void x11_get_window_size(PlatformWindow *win, int *w, int *h) {
    glfwGetWindowSize(win->glfw, w, h);
}

void x11_get_content_scale(PlatformWindow *win, float *xscale, float *yscale) {
    glfwGetWindowContentScale(win->glfw, xscale, yscale);
}

void x11_get_window_pos(PlatformWindow *win, int *x, int *y) {
    glfwGetWindowPos(win->glfw, x, y);
}

void x11_get_primary_workarea(int *x, int *y, int *w, int *h) {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        *x = 0; *y = 0; *w = 0; *h = 0;
        return;
    }
    glfwGetMonitorWorkarea(monitor, x, y, w, h);
}

void x11_get_window_workarea(PlatformWindow *win, int *x, int *y, int *w, int *h) {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(win->glfw, &wx, &wy);
    glfwGetWindowSize(win->glfw, &ww, &wh);
    int cx = wx + ww / 2;
    int cy = wy + wh / 2;

    int count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; i++) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        if (cx >= mx && cx < mx + mw && cy >= my && cy < my + mh) {
            *x = mx; *y = my; *w = mw; *h = mh;
            return;
        }
    }

    x11_get_primary_workarea(x, y, w, h);
}

void x11_make_context_current(PlatformWindow *win) {
    glfwMakeContextCurrent(win->glfw);
}

void x11_get_cursor_pos(PlatformWindow *win, double *x, double *y) {
    glfwGetCursorPos(win->glfw, x, y);
}

void x11_set_callbacks(PlatformWindow *win, PlatformCallbacks *cb) {
    win->cb = *cb;
}

void x11_set_always_on_top(PlatformWindow *win, bool on_top) {
    glfwSetWindowAttrib(win->glfw, GLFW_FLOATING, on_top ? GLFW_TRUE : GLFW_FALSE);
}

void x11_move_window(PlatformWindow *win, int x, int y) {
    glfwSetWindowPos(win->glfw, x, y);
}

void x11_resize_window(PlatformWindow *win, int w, int h) {
    glfwSetWindowSize(win->glfw, w, h);
}

void x11_set_window_rect(PlatformWindow *win, int x, int y, int w, int h) {
    /* Let the WM apply the new centre and size in one ConfigureRequest. Two
     * requests expose an intermediate, incorrectly positioned surface. */
    XMoveResizeWindow(win->x_display,win->x_window,x,y,(unsigned)w,(unsigned)h);
    XFlush(win->x_display);
}

void x11_show_window(PlatformWindow *win) {
    /* Map only after a fresh frame, avoiding a flash of the old exit pose. */
    if (!glfwGetWindowAttrib(win->glfw, GLFW_VISIBLE)) win->show_pending = true;
}

void x11_hide_window(PlatformWindow *win) {
    win->show_pending = false;
    glfwHideWindow(win->glfw);
}

double x11_get_time(void) {
    return glfwGetTime();
}

/* X11 input region for click-through */
void x11_set_input_region(PlatformWindow *win, int x, int y, int w, int h) {
    if (!win->x_display || !win->x_window) return;
    if (win->input_rect_valid && win->input_rect[0] == x && win->input_rect[1] == y &&
        win->input_rect[2] == w && win->input_rect[3] == h) return;
    win->input_rect_valid = true;
    win->input_rect[0] = x; win->input_rect[1] = y;
    win->input_rect[2] = w; win->input_rect[3] = h;
    XRectangle rect = { (short)x, (short)y, (unsigned short)w, (unsigned short)h };
    Region region = XCreateRegion();
    XUnionRectWithRegion(&rect, region, region);
    XShapeCombineRegion(win->x_display, win->x_window, ShapeInput, 0, 0, region, ShapeSet);
    XDestroyRegion(region);
}

void x11_clear_input_region(PlatformWindow *win) {
    if (!win->x_display || !win->x_window) return;
    XShapeCombineMask(win->x_display, win->x_window, ShapeInput, 0, 0, None, ShapeSet);
    win->input_rect_valid = false;
}

void x11_set_input_mask(PlatformWindow *win, const unsigned char *bits, int width, int height) {
    if (!win->x_display || !win->x_window) return;
    static const unsigned char empty=0;
    if (!bits || width<=0 || height<=0) { bits=&empty; width=height=1; }
    Pixmap mask=XCreateBitmapFromData(win->x_display,win->x_window,(const char *)bits,width,height);
    if (!mask) return;
    XShapeCombineMask(win->x_display,win->x_window,ShapeInput,0,0,mask,ShapeSet);
    XFreePixmap(win->x_display,mask);
    win->input_rect_valid=false;
}

void x11_set_input_regions(PlatformWindow *win, const PlatformRect *rects, int rect_count) {
    if (!win->x_display || !win->x_window) return;
    if (!rects || rect_count <= 0) {
        x11_clear_input_region(win);
        return;
    }
    win->input_rect_valid = false;
    Region region = XCreateRegion();
    for (int i = 0; i < rect_count; i++) {
        XRectangle rect = {
            (short)rects[i].x,
            (short)rects[i].y,
            (unsigned short)rects[i].w,
            (unsigned short)rects[i].h
        };
        XUnionRectWithRegion(&rect, region, region);
    }
    XShapeCombineRegion(win->x_display, win->x_window, ShapeInput, 0, 0, region, ShapeSet);
    XDestroyRegion(region);
}

bool x11_frame_ready(PlatformWindow *win) { (void)win; return true; }
bool x11_open_menu(PlatformWindow *win) { (void)win; return false; }
void x11_configure_menu(PlatformWindow *win,const GemMenu *menu) { (void)win;(void)menu; }
void x11_present(PlatformWindow *win, unsigned int source, int width, int height) {
    (void)win;
    if (!source) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const PlatformBackend platform_x11_backend = {
    .name = "x11",
    .frame_ready = x11_frame_ready,
    .present = x11_present,
    .open_menu = x11_open_menu,
    .configure_menu = x11_configure_menu,
    .create_window = x11_create_window,
    .destroy_window = x11_destroy_window,
    .should_close = x11_should_close,
    .poll_events = x11_poll_events,
    .swap_buffers = x11_swap_buffers,
    .get_framebuffer_size = x11_get_framebuffer_size,
    .get_window_size = x11_get_window_size,
    .get_content_scale = x11_get_content_scale,
    .get_window_pos = x11_get_window_pos,
    .get_primary_workarea = x11_get_primary_workarea,
    .get_window_workarea = x11_get_window_workarea,
    .make_context_current = x11_make_context_current,
    .get_cursor_pos = x11_get_cursor_pos,
    .set_callbacks = x11_set_callbacks,
    .set_always_on_top = x11_set_always_on_top,
    .move_window = x11_move_window,
    .resize_window = x11_resize_window,
    .set_window_rect = x11_set_window_rect,
    .show_window = x11_show_window,
    .hide_window = x11_hide_window,
    .set_input_region = x11_set_input_region,
    .clear_input_region = x11_clear_input_region,
    .set_input_mask = x11_set_input_mask,
    .set_input_regions = x11_set_input_regions,
};
