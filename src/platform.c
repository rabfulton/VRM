/*
 * Platform abstraction - GLFW + X11 implementation
 */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"

#ifdef GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

struct PlatformWindow {
    GLFWwindow *glfw;
    PlatformCallbacks cb;
#ifdef GLFW_EXPOSE_NATIVE_X11
    Display *x_display;
    Window x_window;
#endif
};

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

PlatformWindow *platform_create_window(int width, int height, const char *title, bool transparent, bool decorated) {
    static int glfw_init = 0;
    if (!glfw_init) {
        glfwSetErrorCallback(glfw_error_cb);
        if (!glfwInit()) return NULL;
        glfw_init = 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, transparent ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow *glfw = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!glfw) return NULL;

    PlatformWindow *win = calloc(1, sizeof(PlatformWindow));
    win->glfw = glfw;
    glfwSetWindowUserPointer(glfw, win);
    glfwMakeContextCurrent(glfw);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(glfw, glfw_resize_cb);
    glfwSetCursorPosCallback(glfw, glfw_cursor_cb);
    glfwSetMouseButtonCallback(glfw, glfw_mouse_cb);
    glfwSetScrollCallback(glfw, glfw_scroll_cb);
    glfwSetKeyCallback(glfw, glfw_key_cb);

#ifdef GLFW_EXPOSE_NATIVE_X11
    win->x_display = glfwGetX11Display();
    win->x_window = glfwGetX11Window(glfw);
#endif

    return win;
}

void platform_destroy_window(PlatformWindow *win) {
    if (!win) return;
    glfwDestroyWindow(win->glfw);
    free(win);
}

bool platform_should_close(PlatformWindow *win) {
    return glfwWindowShouldClose(win->glfw);
}

void platform_poll_events(void) {
    glfwPollEvents();
}

void platform_swap_buffers(PlatformWindow *win) {
    glfwSwapBuffers(win->glfw);
}

void platform_get_framebuffer_size(PlatformWindow *win, int *w, int *h) {
    glfwGetFramebufferSize(win->glfw, w, h);
}

void platform_set_callbacks(PlatformWindow *win, PlatformCallbacks *cb) {
    win->cb = *cb;
}

void platform_set_always_on_top(PlatformWindow *win, bool on_top) {
    glfwSetWindowAttrib(win->glfw, GLFW_FLOATING, on_top ? GLFW_TRUE : GLFW_FALSE);
}

void platform_move_window(PlatformWindow *win, int x, int y) {
    glfwSetWindowPos(win->glfw, x, y);
}

void platform_resize_window(PlatformWindow *win, int w, int h) {
    glfwSetWindowSize(win->glfw, w, h);
}

void platform_show_window(PlatformWindow *win) {
    glfwShowWindow(win->glfw);
}

void platform_hide_window(PlatformWindow *win) {
    glfwHideWindow(win->glfw);
}

double platform_get_time(void) {
    return glfwGetTime();
}

/* X11 input region for click-through */
void platform_set_input_region(PlatformWindow *win, int x, int y, int w, int h) {
#ifdef GLFW_EXPOSE_NATIVE_X11
    if (!win->x_display || !win->x_window) return;
    XRectangle rect = { (short)x, (short)y, (unsigned short)w, (unsigned short)h };
    Region region = XCreateRegion();
    XUnionRectWithRegion(&rect, region, region);
    XShapeCombineRegion(win->x_display, win->x_window, ShapeInput, 0, 0, region, ShapeSet);
    XDestroyRegion(region);
#else
    (void)win; (void)x; (void)y; (void)w; (void)h;
#endif
}

void platform_clear_input_region(PlatformWindow *win) {
#ifdef GLFW_EXPOSE_NATIVE_X11
    if (!win->x_display || !win->x_window) return;
    XShapeCombineMask(win->x_display, win->x_window, ShapeInput, 0, 0, None, ShapeSet);
#else
    (void)win;
#endif
}
