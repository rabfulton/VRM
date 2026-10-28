/* Native Wayland overlay: bounded avatar rendering on an output-local canvas. */
#define _GNU_SOURCE
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
#include "xdg-shell-client.h"
#include "platform_backend.h"
#include "input_mask.h"
#include "gem_menu.h"
#include <math.h>
#include <linux/input-event-codes.h>
#include <cairo/cairo.h>
#include <sys/mman.h>
#include <unistd.h>


typedef struct {
    PlatformWindow *owner;
    struct wl_buffer *buffer;
    void *pixels;
    int size,stride;
    bool busy;
} MenuBuffer;
struct PlatformWindow {
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
    int egl_width, egl_height, buffer_scale;
    bool configured, closed, ready;

    PlatformCallbacks cb;
    int scene_width, scene_height;
    bool visible, input_dirty;
    double pointer_x, pointer_y;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct wl_surface *pointer_surface, *menu_surface;
    struct xdg_surface *menu_xdg;
    struct xdg_popup *menu;
    MenuBuffer menu_buffers[2];
    GemMenu menu_model;
    int menu_page,menu_hover;
    double menu_scale,menu_started,menu_last_paint;
    bool menu_ready,menu_dirty;
    uint32_t button_serial, seat_id;
    unsigned char *mask;
    int mask_width, mask_height;
};
static PlatformWindow *active;
static void apply_input(PlatformWindow *p);
static void close_menu(PlatformWindow *p);
static void redraw_menu(PlatformWindow *p);
static void activate_menu(PlatformWindow *p,int hit);
static void cancel_drag(PlatformWindow *p);
static const struct wl_seat_listener seat_listener;
static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
    (void)data; xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    PlatformWindow *p = data;
    if (!strcmp(interface, wl_compositor_interface.name) && version >= 3)
        p->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                        version < 6 ? version : 6);
    else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
        p->shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
    else if (!strcmp(interface, wl_seat_interface.name) && !p->seat) {
        p->seat_id = name;
        p->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(p->seat, &seat_listener, p);
    } else if (!strcmp(interface, wl_shm_interface.name))
        p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        p->wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(p->wm, &wm_listener, p);
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    PlatformWindow *p=data;
    if (name==p->seat_id && p->seat) {
        cancel_drag(p); close_menu(p);
        if (p->pointer) { wl_pointer_destroy(p->pointer); p->pointer=NULL; }
        if (p->keyboard) { wl_keyboard_destroy(p->keyboard); p->keyboard=NULL; }
        wl_seat_destroy(p->seat); p->seat=NULL;
        p->pointer_surface=NULL; p->button_serial=0;
    }
}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height) {
    PlatformWindow *p = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!width || !height || width > 16384 || height > 16384) {
        fprintf(stderr, "Unsupported output size %ux%u\n", width, height);
        p->closed = true;
        return;
    }
    if(p->menu && (p->width!=(int)width || p->height!=(int)height))close_menu(p);
    p->width = (int)width;
    p->height = (int)height;
    if (p->x > p->width - 40) p->x = p->width - p->scene_width;
    if (p->y > p->height - 40) p->y = p->height - p->scene_height;
    p->input_dirty = true;
    p->configured = true;
    fprintf(stderr, "Configured canvas: %dx%d logical pixels\n", p->width, p->height);
}
static void closed(void *data, struct zwlr_layer_surface_v1 *layer) {
    (void)layer;
    ((PlatformWindow *)data)->closed = true;
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
    PlatformWindow *p = data;
    if (scale > 0 && scale <= 8 && scale!=p->scale) {
        if(p->menu)close_menu(p);
        p->scale=scale;
    }
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
    PlatformWindow *p = data;
    wl_callback_destroy(callback);
    p->frame = NULL;
    p->ready = true;
}
static const struct wl_callback_listener frame_listener = {frame_done};

static bool initialize_egl(PlatformWindow *p) {
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
        EGL_SAMPLE_BUFFERS, 0,
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
    p->egl_width = p->width * p->scale;
    p->egl_height = p->height * p->scale;
    if (!p->window) return false;
    p->buffer = eglCreateWindowSurface(p->egl, config, (EGLNativeWindowType)p->window, NULL);
    if (p->buffer == EGL_NO_SURFACE ||
        !eglMakeCurrent(p->egl, p->buffer, p->buffer, p->context)) return false;
    fprintf(stderr, "Native Wayland EGL: %s; GL %s\n", eglQueryString(p->egl, EGL_VENDOR),
            glGetString(GL_VERSION));
    return eglSwapInterval(p->egl, 0);
}


/* Surface-local coordinates are translated to the bounded avatar viewport.
 * Existing dragging then operates on an output-local scene, not OS positions. */
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)serial;
    PlatformWindow *p = data;
    p->pointer_surface = surface;
    p->pointer_x = wl_fixed_to_double(x); p->pointer_y = wl_fixed_to_double(y);
    if (p->pointer_surface==p->menu_surface && p->menu) {
        int hit=gem_menu_hit(&p->menu_model,p->menu_page,p->pointer_x/p->menu_scale,p->pointer_y/p->menu_scale);
        if(hit!=p->menu_hover){p->menu_hover=hit;redraw_menu(p);}
    }
}
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    (void)pointer; (void)serial; (void)surface;
    PlatformWindow *p = data;
    p->pointer_surface = NULL;
}
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)time;
    PlatformWindow *p = data;
    p->pointer_x = wl_fixed_to_double(x); p->pointer_y = wl_fixed_to_double(y);
    if(p->pointer_surface==p->menu_surface && p->menu) {
        int hit=gem_menu_hit(&p->menu_model,p->menu_page,p->pointer_x/p->menu_scale,p->pointer_y/p->menu_scale);
        if(hit!=p->menu_hover){p->menu_hover=hit;redraw_menu(p);}
    }
    if (p->pointer_surface == p->surface && p->cb.on_mouse_move)
        p->cb.on_mouse_move(p->pointer_x-p->x,p->pointer_y-p->y,p->cb.user_data);
}
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)time;
    PlatformWindow *p = data;
    p->button_serial = serial;
    if (p->menu) {
        if (button==BTN_RIGHT && state) { close_menu(p); return; }
        if (p->pointer_surface == p->menu_surface && button == BTN_LEFT && !state) {
            int hit=gem_menu_hit(&p->menu_model,p->menu_page,p->pointer_x/p->menu_scale,p->pointer_y/p->menu_scale);
            activate_menu(p,hit);
        }
        return;
    }
    int key = button == BTN_LEFT ? PLATFORM_MOUSE_LEFT : button == BTN_RIGHT ? PLATFORM_MOUSE_RIGHT :
              button == BTN_MIDDLE ? PLATFORM_MOUSE_MIDDLE : -1;
    if (key >= 0 && p->cb.on_mouse_button)
        p->cb.on_mouse_button(key,state == WL_POINTER_BUTTON_STATE_PRESSED,p->cb.user_data);
}
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
    (void)pointer; (void)time;
    PlatformWindow *p = data;
    if (!p->menu && p->cb.on_scroll) {
        double delta = -wl_fixed_to_double(value)/10.0;
        p->cb.on_scroll(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? delta : 0,
                       axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? delta : 0,p->cb.user_data);
    }
}
static void pointer_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void pointer_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void pointer_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void pointer_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t n) { (void)d; (void)p; (void)a; (void)n; }
static const struct wl_pointer_listener pointer_listener = {
    .enter=pointer_enter,.leave=pointer_leave,.motion=pointer_motion,.button=pointer_button,
    .axis=pointer_axis,.frame=pointer_frame,.axis_source=pointer_source,
    .axis_stop=pointer_stop,.axis_discrete=pointer_discrete,
};
static void keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t s) {
    (void)d; (void)k; (void)f; (void)s; close(fd);
}
static void key_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *w, struct wl_array *a) {
    (void)d; (void)k; (void)s; (void)w; (void)a;
}
static void key_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *w) {
    (void)d; (void)k; (void)s; (void)w;
}
static void key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t code, uint32_t state) {
    (void)k; (void)s; (void)t;
    if (code == KEY_ESC && state) close_menu(d);
    PlatformWindow *p=d;
    if(!p->menu || !state)return;
    if(code==KEY_ENTER || code==KEY_SPACE)activate_menu(p,p->menu_hover);
    else if(code==KEY_LEFT || code==KEY_RIGHT)activate_menu(p,code==KEY_LEFT?-3:-4);
    else if(code==KEY_TAB || code==KEY_DOWN || code==KEY_UP) {
        int step=code==KEY_UP?-1:1,index=p->menu_hover<0?(step>0?-1:p->menu_model.count):p->menu_hover;
        for(int i=0;i<p->menu_model.count;i++) {
            index=(index+step+p->menu_model.count)%p->menu_model.count;
            if(p->menu_model.actions[index].page==p->menu_page && p->menu_model.actions[index].enabled){p->menu_hover=index;redraw_menu(p);break;}
        }
    }
}
static void modifiers(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g) {
    (void)d; (void)k; (void)s; (void)a; (void)b; (void)c; (void)g;
}
static void repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t l) { (void)d; (void)k; (void)r; (void)l; }
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap=keymap,.enter=key_enter,.leave=key_leave,.key=key,.modifiers=modifiers,.repeat_info=repeat,
};
static void cancel_drag(PlatformWindow *p) {
    if (p->cb.on_mouse_button) {
        p->cb.on_mouse_button(PLATFORM_MOUSE_LEFT,0,p->cb.user_data);
        p->cb.on_mouse_button(PLATFORM_MOUSE_MIDDLE,0,p->cb.user_data);
    }
}
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    PlatformWindow *p = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !p->pointer) {
        p->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(p->pointer,&pointer_listener,p);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && p->pointer) {
        cancel_drag(p); close_menu(p);
        wl_pointer_destroy(p->pointer); p->pointer=NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p->keyboard) {
        p->keyboard=wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(p->keyboard,&keyboard_listener,p);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && p->keyboard) {
        wl_keyboard_destroy(p->keyboard); p->keyboard=NULL;
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = {seat_capabilities,seat_name};

static void menu_buffer_release(void *data,struct wl_buffer *buffer) {
    (void)buffer;MenuBuffer *b=data;b->busy=false;
    if (b->owner->menu && b->owner->menu_dirty) redraw_menu(b->owner);
}
static const struct wl_buffer_listener menu_buffer_listener={menu_buffer_release};
static bool allocate_menu_buffer(PlatformWindow *p,MenuBuffer *b) {
    int width=(int)ceil(p->menu_model.width*p->menu_scale)*p->scale;
    int height=(int)ceil(p->menu_model.height*p->menu_scale)*p->scale;
    b->owner=p;b->stride=width*4;b->size=b->stride*height;
    int fd=memfd_create("domovushka-gems",MFD_CLOEXEC);
    if(fd<0)return false;
    if(ftruncate(fd,b->size)<0){close(fd);return false;}
    b->pixels=mmap(NULL,b->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(b->pixels==MAP_FAILED){b->pixels=NULL;close(fd);return false;}
    struct wl_shm_pool *pool=wl_shm_create_pool(p->shm,fd,b->size);
    b->buffer=wl_shm_pool_create_buffer(pool,0,width,height,b->stride,WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);close(fd);
    if(!b->buffer)return false;
    wl_buffer_add_listener(b->buffer,&menu_buffer_listener,b);return true;
}
static void close_menu(PlatformWindow *p) {
    bool was_open=p->menu || p->menu_surface;
    if(p->menu)xdg_popup_destroy(p->menu);
    if(p->menu_xdg)xdg_surface_destroy(p->menu_xdg);
    if(p->menu_surface)wl_surface_destroy(p->menu_surface);
    p->menu=NULL;p->menu_xdg=NULL;p->menu_surface=NULL;p->menu_ready=false;
    for(int i=0;i<2;i++) {
        MenuBuffer *b=&p->menu_buffers[i];
        if(b->buffer)wl_buffer_destroy(b->buffer);
        if(b->pixels)munmap(b->pixels,b->size);
        memset(b,0,sizeof(*b));
    }
    if(was_open) {
        p->pointer_surface=NULL;
        if(p->layer){zwlr_layer_surface_v1_set_keyboard_interactivity(p->layer,0);wl_surface_commit(p->surface);}
    }
}
static void redraw_menu(PlatformWindow *p) {
    p->menu_dirty=true;
    if(!p->menu || !p->menu_ready)return;
    MenuBuffer *b=NULL;
    for(int i=0;i<2;i++)if(!p->menu_buffers[i].busy){b=&p->menu_buffers[i];break;}
    if(!b)return;
    int width=b->stride/4,height=b->size/b->stride;
    cairo_surface_t *image=cairo_image_surface_create_for_data(b->pixels,CAIRO_FORMAT_ARGB32,width,height,b->stride);
    cairo_t *cr=cairo_create(image);cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);cairo_paint(cr);
    cairo_set_operator(cr,CAIRO_OPERATOR_OVER);cairo_scale(cr,p->scale*p->menu_scale,p->scale*p->menu_scale);
    double t=fmin(1,(platform_get_time()-p->menu_started)/.15);
    double opening=1-pow(1-t,3);
    gem_menu_draw(&p->menu_model,cr,p->menu_page,p->menu_hover,opening);
    cairo_destroy(cr);cairo_surface_flush(image);cairo_surface_destroy(image);
    b->busy=true;p->menu_dirty=false;p->menu_last_paint=platform_get_time();
    wl_surface_attach(p->menu_surface,b->buffer,0,0);
    wl_surface_damage(p->menu_surface,0,0,(int)ceil(p->menu_model.width*p->menu_scale),(int)ceil(p->menu_model.height*p->menu_scale));
    wl_surface_commit(p->menu_surface);
}
static void activate_menu(PlatformWindow *p,int hit) {
    if(hit==-2) {
        if(!strcmp(p->menu_model.pages[p->menu_page].group,"main")){close_menu(p);return;}
        p->menu_page=gem_menu_target(&p->menu_model,"main");p->menu_hover=-2;redraw_menu(p);return;
    }
    if(hit==-3 || hit==-4) {
        int page=gem_menu_sibling(&p->menu_model,p->menu_page,hit==-3?-1:1);
        if(page>=0){p->menu_page=page;p->menu_hover=-2;redraw_menu(p);}return;
    }
    if(hit<0 || hit>=p->menu_model.count){close_menu(p);return;}
    GemAction *a=&p->menu_model.actions[hit];
    if(!a->enabled)return;
    if(a->target[0]) {
        int page=gem_menu_target(&p->menu_model,a->target);
        if(page>=0){p->menu_page=page;p->menu_hover=-2;redraw_menu(p);}return;
    }
    if(a->close)close_menu(p);
    if(p->cb.on_menu_action)p->cb.on_menu_action(hit,p->cb.user_data);
}
static void menu_configure(void *data,struct xdg_surface *surface,uint32_t serial) {
    PlatformWindow *p=data;xdg_surface_ack_configure(surface,serial);p->menu_ready=true;redraw_menu(p);
}
static const struct xdg_surface_listener menu_surface_listener={menu_configure};
static void popup_configure(void *d,struct xdg_popup *p,int32_t x,int32_t y,int32_t w,int32_t h) {
    (void)d;(void)p;(void)x;(void)y;(void)w;(void)h;
}
static void popup_done(void *d,struct xdg_popup *p){(void)p;close_menu(d);}
static const struct xdg_popup_listener popup_listener={.configure=popup_configure,.popup_done=popup_done};
void wayland_configure_menu(PlatformWindow *p,const GemMenu *model) {
    char page_id[32]="",hover_id[49]="";
    if(p->menu_page>=0 && p->menu_page<p->menu_model.page_count)
        snprintf(page_id,sizeof(page_id),"%s",p->menu_model.pages[p->menu_page].id);
    if(p->menu_hover>=0 && p->menu_hover<p->menu_model.count)
        snprintf(hover_id,sizeof(hover_id),"%s",p->menu_model.actions[p->menu_hover].id);
    if(strcmp(p->menu_model.context,model->context))close_menu(p);
    p->menu_model=*model;
    p->menu_page=0;
    for(int i=0;i<model->page_count;i++)if(!strcmp(model->pages[i].id,page_id))p->menu_page=i;
    if(hover_id[0]) {
        p->menu_hover=-2;
        for(int i=0;i<model->count;i++)if(model->actions[i].page==p->menu_page && !strcmp(model->actions[i].id,hover_id))p->menu_hover=i;
    }
    if(p->menu)redraw_menu(p);
}
bool wayland_open_menu(PlatformWindow *p) {
    if(p->menu){close_menu(p);return true;}
    if(!p->wm || !p->shm || !p->seat || !p->button_serial)return true;
    p->menu_scale=fmin(1,fmin((p->width-16.)/p->menu_model.width,(p->height-16.)/p->menu_model.height));
    if(p->menu_scale<.25)return true;
    if(!allocate_menu_buffer(p,&p->menu_buffers[0]) || !allocate_menu_buffer(p,&p->menu_buffers[1])){close_menu(p);return true;}
    p->menu_page=0;p->menu_hover=-2;p->menu_started=platform_get_time();
    p->menu_surface=wl_compositor_create_surface(p->compositor);
    wl_surface_set_buffer_scale(p->menu_surface,p->scale);
    p->menu_xdg=xdg_wm_base_get_xdg_surface(p->wm,p->menu_surface);
    xdg_surface_add_listener(p->menu_xdg,&menu_surface_listener,p);
    struct xdg_positioner *pos=xdg_wm_base_create_positioner(p->wm);
    xdg_positioner_set_size(pos,(int)ceil(p->menu_model.width*p->menu_scale),(int)ceil(p->menu_model.height*p->menu_scale));
    xdg_positioner_set_anchor_rect(pos,(int)p->pointer_x,(int)p->pointer_y,1,1);
    xdg_positioner_set_anchor(pos,XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(pos,XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_offset(pos,-(int)round(p->menu_model.cx*p->menu_scale),-(int)round(p->menu_model.cy*p->menu_scale));
    xdg_positioner_set_constraint_adjustment(pos,XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X|XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
    p->menu=xdg_surface_get_popup(p->menu_xdg,NULL,pos);xdg_positioner_destroy(pos);
    xdg_popup_add_listener(p->menu,&popup_listener,p);
    zwlr_layer_surface_v1_get_popup(p->layer,p->menu);
    if(wl_proxy_get_version((struct wl_proxy *)p->shell)>=4)zwlr_layer_surface_v1_set_keyboard_interactivity(p->layer,2);
    wl_surface_commit(p->surface);xdg_popup_grab(p->menu,p->seat,p->button_serial);wl_surface_commit(p->menu_surface);
    return true;
}


static bool dispatch(PlatformWindow *p, int timeout) {
    while (wl_display_prepare_read(p->display)!=0)
        if (wl_display_dispatch_pending(p->display)<0) return false;
    int flushed=wl_display_flush(p->display);
    if (flushed<0 && errno!=EAGAIN) { wl_display_cancel_read(p->display); return false; }
    struct pollfd fd={wl_display_get_fd(p->display),POLLIN,0};
    if (flushed<0) fd.events|=POLLOUT;
    int result=poll(&fd,1,timeout);
    if (result>0 && (fd.revents&POLLIN)) {
        if (wl_display_read_events(p->display)<0) return false;
    } else wl_display_cancel_read(p->display);
    if (result<0 && errno!=EINTR) return false;
    if (fd.revents&(POLLERR|POLLHUP|POLLNVAL)) return false;
    return wl_display_dispatch_pending(p->display)>=0;
}

PlatformWindow *wayland_create_window(int width,int height,const char *title,bool transparent,bool decorated) {
    (void)title; (void)transparent; (void)decorated;
    PlatformWindow *p=calloc(1,sizeof(*p));
    if (!p) return NULL;
    gem_menu_default(&p->menu_model);
    p->scale=1; p->scene_width=width; p->scene_height=height;
    p->x=100; p->y=100; p->visible=true; p->input_dirty=true;
    p->display=wl_display_connect(NULL);
    if (!p->display) goto fail;
    p->registry=wl_display_get_registry(p->display);
    wl_registry_add_listener(p->registry,&registry_listener,p);
    if (wl_display_roundtrip(p->display)<0 || !p->compositor || !p->shell) {
        fprintf(stderr,"Wayland overlay requires wl_compositor v3 and wlr-layer-shell.\n"); goto fail;
    }
    p->surface=wl_compositor_create_surface(p->compositor);
    wl_surface_add_listener(p->surface,&surface_listener,p);
    p->layer=zwlr_layer_shell_v1_get_layer_surface(p->shell,p->surface,NULL,ZWLR_LAYER_SHELL_V1_LAYER_TOP,"vrm-overlay");
    zwlr_layer_surface_v1_add_listener(p->layer,&layer_listener,p);
    zwlr_layer_surface_v1_set_anchor(p->layer,ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(p->layer,0,0);
    zwlr_layer_surface_v1_set_exclusive_zone(p->layer,-1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(p->layer,0);
    struct wl_region *empty=wl_compositor_create_region(p->compositor);
    wl_surface_set_input_region(p->surface,empty);
    wl_surface_set_opaque_region(p->surface,empty);
    wl_region_destroy(empty);
    wl_surface_commit(p->surface);
    double deadline=platform_get_time()+3;
    while (!p->configured && !p->closed && platform_get_time()<deadline)
        if (!dispatch(p,20)) goto fail;
    if (!p->configured || p->closed || !initialize_egl(p)) goto fail;
    p->ready=true; active=p;
    return p;
fail:
    fprintf(stderr,"Native Wayland overlay initialization failed.\n");
    wayland_destroy_window(p);
    return NULL;
}
void wayland_destroy_window(PlatformWindow *p) {
    if (!p) return;
    close_menu(p);
    if (p->egl!=EGL_NO_DISPLAY) {
        eglMakeCurrent(p->egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if (p->buffer!=EGL_NO_SURFACE) eglDestroySurface(p->egl,p->buffer);
        if (p->context!=EGL_NO_CONTEXT) eglDestroyContext(p->egl,p->context);
        eglTerminate(p->egl);
    }
    if (p->window) wl_egl_window_destroy(p->window);
    if (p->frame) wl_callback_destroy(p->frame);
    if (p->layer) zwlr_layer_surface_v1_destroy(p->layer);
    if (p->surface) wl_surface_destroy(p->surface);
    if (p->shell) wl_proxy_destroy((struct wl_proxy *)p->shell);
    if (p->pointer) wl_pointer_destroy(p->pointer);
    if (p->keyboard) wl_keyboard_destroy(p->keyboard);
    if (p->seat) wl_seat_destroy(p->seat);
    if (p->shm) wl_shm_destroy(p->shm);
    if (p->wm) xdg_wm_base_destroy(p->wm);
    if (p->compositor) wl_compositor_destroy(p->compositor);
    if (p->registry) wl_registry_destroy(p->registry);
    if (p->display) wl_display_disconnect(p->display);
    if (active==p) active=NULL;
    free(p->mask); free(p);
}
bool wayland_should_close(PlatformWindow *p) { return p->closed; }
void wayland_poll_events(void) {
    if (active && !dispatch(active,active->ready && active->visible ? 0 : 8)) active->closed=true;
    if(active && active->menu && platform_get_time()-active->menu_started<.17 && platform_get_time()-active->menu_last_paint>=1./60)
        redraw_menu(active);
}
bool wayland_frame_ready(PlatformWindow *p) { return p->ready && p->visible && !p->closed; }
void wayland_make_context_current(PlatformWindow *p) { eglMakeCurrent(p->egl,p->buffer,p->buffer,p->context); }
void wayland_swap_buffers(PlatformWindow *p) {
    p->ready=false;
    p->frame=wl_surface_frame(p->surface);
    wl_callback_add_listener(p->frame,&frame_listener,p);
    if (!eglSwapBuffers(p->egl,p->buffer)) p->closed=true;
}
void wayland_present(PlatformWindow *p,unsigned int source,int width,int height) {
    int w=p->width*p->scale,h=p->height*p->scale;
    if (p->buffer_scale != p->scale) {
        wl_surface_set_buffer_scale(p->surface,p->scale);
        p->buffer_scale = p->scale;
    }
    if (p->egl_width != w || p->egl_height != h) {
        wl_egl_window_resize(p->window,w,h,0,0);
        p->egl_width = w; p->egl_height = h;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    int x=p->x*p->scale,y=(p->height-p->y-p->scene_height)*p->scale;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,source);
    glBlitFramebuffer(0,0,width,height,x,y,x+width,y+height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    apply_input(p);
}
void wayland_get_framebuffer_size(PlatformWindow *p,int *w,int *h) { *w=p->scene_width*p->scale; *h=p->scene_height*p->scale; }
void wayland_get_window_size(PlatformWindow *p,int *w,int *h) { *w=p->scene_width; *h=p->scene_height; }
void wayland_get_content_scale(PlatformWindow *p,float *x,float *y) { *x=*y=p->scale; }
void wayland_get_window_pos(PlatformWindow *p,int *x,int *y) { *x=p->x; *y=p->y; }
void wayland_get_primary_workarea(int *x,int *y,int *w,int *h) { *x=*y=0; *w=active?active->width:0; *h=active?active->height:0; }
void wayland_get_window_workarea(PlatformWindow *p,int *x,int *y,int *w,int *h) { *x=*y=0; *w=p->width; *h=p->height; }
void wayland_get_cursor_pos(PlatformWindow *p,double *x,double *y) { *x=p->pointer_x-p->x; *y=p->pointer_y-p->y; }
void wayland_set_callbacks(PlatformWindow *p,PlatformCallbacks *cb) { p->cb=*cb; }
void wayland_set_always_on_top(PlatformWindow *p,bool on_top) {
    if (wl_proxy_get_version((struct wl_proxy *)p->shell)>=2) {
        zwlr_layer_surface_v1_set_layer(p->layer,on_top?ZWLR_LAYER_SHELL_V1_LAYER_TOP:ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
        wl_surface_commit(p->surface);
    }
}
void wayland_move_window(PlatformWindow *p,int x,int y) {
    /* Keep a reachable part of the viewport on the selected output. */
    p->x=x < 40-p->scene_width ? 40-p->scene_width : x > p->width-40 ? p->width-40 : x;
    p->y=y < 40-p->scene_height ? 40-p->scene_height : y > p->height-40 ? p->height-40 : y;
    p->input_dirty=true;
}
void wayland_resize_window(PlatformWindow *p,int w,int h) {
    if (w>0 && h>0 && w<=4096 && h<=4096) { p->scene_width=w; p->scene_height=h; }
}
void wayland_set_window_rect(PlatformWindow *p,int x,int y,int w,int h) { wayland_resize_window(p,w,h); wayland_move_window(p,x,y); }
void wayland_show_window(PlatformWindow *p) { p->visible=true; p->input_dirty=true; if (!p->frame) p->ready=true; }
void wayland_hide_window(PlatformWindow *p) {
    close_menu(p); cancel_drag(p); p->visible=false; p->input_dirty=true;
    if (p->frame) { wl_callback_destroy(p->frame); p->frame=NULL; }
    wayland_make_context_current(p);
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    apply_input(p); eglSwapBuffers(p->egl,p->buffer);
}

/* Convert logical bitmap runs into exact regions; matching runs in adjacent
 * rows are merged. Transparent holes and shadow-only pixels never receive input. */
typedef struct { struct wl_region *region; int x,y; } RegionTarget;
static void emit_rectangle(int x,int y,int w,int h,void *data) {
    RegionTarget *target=data;
    wl_region_add(target->region,target->x+x,target->y+y,w,h);
}
static void apply_input(PlatformWindow *p) {
    if (!p->input_dirty) return;
    p->input_dirty=false;
    struct wl_region *region=wl_compositor_create_region(p->compositor);
    if (p->visible && p->mask) {
        InputMask mask={p->mask,p->mask_width,p->mask_height,(p->mask_width+7)/8};
        RegionTarget target={region,p->x,p->y};
        input_mask_rectangles(&mask,emit_rectangle,&target);
    }
    wl_surface_set_input_region(p->surface,region);
    wl_region_destroy(region);
}
void wayland_set_input_mask(PlatformWindow *p,const unsigned char *bits,int width,int height) {
    p->input_dirty=true;
    free(p->mask); p->mask=NULL; p->mask_width=p->mask_height=0;
    if (bits && width>0 && height>0 && width<=4096 && height<=4096) {
        size_t size=(size_t)((width+7)/8)*height;
        p->mask=malloc(size);
        if (p->mask) { memcpy(p->mask,bits,size); p->mask_width=width; p->mask_height=height; }
    }
}
void wayland_set_input_region(PlatformWindow *p,int x,int y,int w,int h) {
    PlatformRect rect={x,y,w,h}; wayland_set_input_regions(p,&rect,1);
}
void wayland_clear_input_region(PlatformWindow *p) {
    PlatformRect rect={0,0,p->scene_width,p->scene_height}; wayland_set_input_regions(p,&rect,1);
}
void wayland_set_input_regions(PlatformWindow *p,const PlatformRect *rects,int count) {
    int w=p->scene_width,h=p->scene_height,stride=(w+7)/8;
    unsigned char *bits=calloc((size_t)stride,h);
    if (!bits) return;
    for(int i=0;i<count;i++) for(int y=0;y<h;y++) for(int x=0;x<w;x++)
        if(x>=rects[i].x && y>=rects[i].y && x-rects[i].x<rects[i].w && y-rects[i].y<rects[i].h)
            bits[y*stride+x/8]|=1u<<(x%8);
    wayland_set_input_mask(p,bits,w,h); free(bits);
}

const PlatformBackend platform_wayland_backend = {
    .name = "wayland",
    .frame_ready = wayland_frame_ready,
    .present = wayland_present,
    .open_menu = wayland_open_menu,
    .configure_menu = wayland_configure_menu,
    .create_window = wayland_create_window,
    .destroy_window = wayland_destroy_window,
    .should_close = wayland_should_close,
    .poll_events = wayland_poll_events,
    .swap_buffers = wayland_swap_buffers,
    .get_framebuffer_size = wayland_get_framebuffer_size,
    .get_window_size = wayland_get_window_size,
    .get_content_scale = wayland_get_content_scale,
    .get_window_pos = wayland_get_window_pos,
    .get_primary_workarea = wayland_get_primary_workarea,
    .get_window_workarea = wayland_get_window_workarea,
    .make_context_current = wayland_make_context_current,
    .get_cursor_pos = wayland_get_cursor_pos,
    .set_callbacks = wayland_set_callbacks,
    .set_always_on_top = wayland_set_always_on_top,
    .move_window = wayland_move_window,
    .resize_window = wayland_resize_window,
    .set_window_rect = wayland_set_window_rect,
    .show_window = wayland_show_window,
    .hide_window = wayland_hide_window,
    .set_input_region = wayland_set_input_region,
    .clear_input_region = wayland_clear_input_region,
    .set_input_mask = wayland_set_input_mask,
    .set_input_regions = wayland_set_input_regions,
};
