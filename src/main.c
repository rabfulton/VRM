/*
 * VRM Desktop Overlay Viewer - Main Entry Point (GLFW version)
 */

#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "platform.h"
#include "renderer.h"
#include "camera.h"
#include "gltf_loader.h"
#include "animator.h"
#include "ipc.h"
#include "spring_bones.h"
#include "anim_config.h"
#include "settings.h"
#include "ui_library/ui.h"
#include "debug_ui.h"
#include "../deps/cjson/cJSON.h"

typedef struct {
    PlatformWindow *window;
    PlatformWindow *ui_window;
    Renderer renderer;
    OrbitCamera camera;
    GltfModel model;
    Animator animator;
    IpcServer ipc;
    GLuint msaa_fbo, msaa_color, msaa_depth;
    int msaa_samples, fb_width, fb_height;
    int ui_fb_width, ui_fb_height;
    ui_interface_t *ui;
    int32_t pie_menu_id;
    bool ui_visible;
    bool ui_force_pie_open;
    bool ui_force_pie_open_pending;
    bool ui_pending_close;
    bool visible, always_on_top, model_loaded, auto_resize, running;
    float cached_world_w, cached_world_h;
    double last_time;
    float model_position[3], model_rotation[3];
    float model_scale;
    char current_anim_path[512];
    bool has_current_anim;
} VrmApp;

static VrmApp app = {0};
static const char *SETTINGS_PATH = "settings.cfg";

display_t display = {0};
mouse_t mouse = {0};  /* For UI library */
static int model_mouse_x = 0, model_mouse_y = 0;  /* For model window */
static int model_mouse_buttons[3] = {0};  /* L, R, M */
gl_buffer_t gl_buffer_default = {0};
extern float g_spring_stiffness, g_spring_drag, g_spring_gravity;
extern int g_spring_cloth_mode;
extern float g_spring_cloth_stiffness, g_spring_cloth_drag, g_spring_cloth_gravity, g_spring_cloth_taper;

static void update_ui_window_layout(void);
static void build_view_with_model_transform(float out_view[16]);
static bool model_hit_test(int x, int y);
static void show_ui_window(void);
static void hide_ui_window(void);
static void reset_springs_from_ui(void);
static void save_springs_to_config(void);

static void update_display_metrics_for_window(PlatformWindow *win) {
    int win_w = 0, win_h = 0;
    float sx = 1.0f, sy = 1.0f;
    platform_get_window_size(win, &win_w, &win_h);
    platform_get_content_scale(win, &sx, &sy);
    if (sx <= 0.0f) sx = 1.0f;
    if (sy <= 0.0f) sy = 1.0f;
    display.width = win_w;
    display.height = win_h;
    display.h_dpi = 96.0f * sx;
    display.v_dpi = 96.0f * sy;
    display.d_dpi = 96.0f * (sx > sy ? sx : sy);
}

static void reset_springs_from_ui(void) {
    if (app.model_loaded) spring_bones_reset(&app.model);
}

static void save_springs_to_config(void) {
    if (!app.has_current_anim) return;
    AnimConfig cfg = {0};
    cfg.spring_stiffness = g_spring_stiffness;
    cfg.spring_drag = g_spring_drag;
    cfg.spring_gravity = g_spring_gravity;
    cfg.has_spring_stiffness = true;
    cfg.has_spring_drag = true;
    cfg.has_spring_gravity = true;
    cfg.spring_cloth_mode = (float)g_spring_cloth_mode;
    cfg.spring_cloth_stiffness = g_spring_cloth_stiffness;
    cfg.spring_cloth_drag = g_spring_cloth_drag;
    cfg.spring_cloth_gravity = g_spring_cloth_gravity;
    cfg.spring_cloth_taper = g_spring_cloth_taper;
    cfg.has_spring_cloth_mode = true;
    cfg.has_spring_cloth_stiffness = true;
    cfg.has_spring_cloth_drag = true;
    cfg.has_spring_cloth_gravity = true;
    cfg.has_spring_cloth_taper = true;
    if (!anim_config_save(app.current_anim_path, &cfg)) {
        printf("WARN: Failed to save anim config for %s\n", app.current_anim_path);
    }
}

static void msaa_cleanup(VrmApp *s) {
    if (s->msaa_color) { glDeleteRenderbuffers(1, &s->msaa_color); s->msaa_color = 0; }
    if (s->msaa_depth) { glDeleteRenderbuffers(1, &s->msaa_depth); s->msaa_depth = 0; }
    if (s->msaa_fbo) { glDeleteFramebuffers(1, &s->msaa_fbo); s->msaa_fbo = 0; }
}

static void msaa_resize(VrmApp *s, int w, int h) {
    if (w <= 0 || h <= 0 || s->msaa_samples <= 0) return;
    if (s->fb_width == w && s->fb_height == h && s->msaa_fbo) return;
    msaa_cleanup(s);
    glGenFramebuffers(1, &s->msaa_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->msaa_fbo);
    glGenRenderbuffers(1, &s->msaa_color);
    glBindRenderbuffer(GL_RENDERBUFFER, s->msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, s->msaa_samples, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->msaa_color);
    glGenRenderbuffers(1, &s->msaa_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s->msaa_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, s->msaa_samples, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s->msaa_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "MSAA framebuffer incomplete\n");
        msaa_cleanup(s);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s->fb_width = w; s->fb_height = h;
}

static void update_window_for_zoom(void) {
    if (!app.auto_resize || !app.model_loaded) return;
    
    /* Get current window position and size */
    int old_x = 0, old_y = 0, old_w = 0, old_h = 0;
    platform_get_window_pos(app.window, &old_x, &old_y);
    platform_get_window_size(app.window, &old_w, &old_h);
    
    /* Calculate new size */
    int base_w = 300;
    int base_h = 550;
    int w = (int)(base_w * app.model_scale);
    int h = (int)(base_h * app.model_scale);
    if (w < 100) w = 100;
    if (h < 100) h = 100;
    
    /* Keep window centered on same point */
    int new_x = old_x + (old_w - w) / 2;
    int new_y = old_y + (old_h - h) / 2;
    
    platform_resize_window(app.window, w, h);
    platform_move_window(app.window, new_x, new_y);
    update_ui_window_layout();
}

static void compute_and_cache_bounds(const char *anim_path) {
    if (!app.model_loaded) return;
    gltf_compute_animation_bounds(&app.model, anim_path, &app.cached_world_w, &app.cached_world_h);
    update_window_for_zoom();
}

static void update_anim_correction_mode(int mode) {
    app.model.anim_correction_mode = mode;
    float *c = app.model.anim_global_correction, *a = app.model.anim_global_auto_correction;
    if (mode == 0) { c[0]=0; c[1]=0; c[2]=0; c[3]=1; printf("Anim correction: none\n"); }
    else if (mode == 1) { c[0]=a[0]; c[1]=a[1]; c[2]=a[2]; c[3]=a[3]; printf("Anim correction: auto\n"); }
    else if (mode == 2) { c[0]=1; c[1]=0; c[2]=0; c[3]=0; printf("Anim correction: rotX 180\n"); }
    else if (mode == 3) { c[0]=0; c[1]=1; c[2]=0; c[3]=0; printf("Anim correction: rotY 180\n"); }
    else if (mode == 4) { c[0]=0; c[1]=0; c[2]=1; c[3]=0; printf("Anim correction: rotZ 180\n"); }
    fflush(stdout);
}

static void update_input_region(void) {
    if (!app.model_loaded) { platform_clear_input_region(app.window); return; }
    /* Project model bounds to screen and set input region */
    camera_update(&app.camera);
    float min_x, min_y, max_x, max_y;
    float view_model[16];
    build_view_with_model_transform(view_model);
    gltf_get_screen_bounds_px(&app.model, view_model, (float*)app.camera.proj,
                              app.fb_width, app.fb_height, &min_x, &min_y, &max_x, &max_y);
    int win_w = 0, win_h = 0;
    platform_get_window_size(app.window, &win_w, &win_h);
    float sx = app.fb_width > 0 ? (float)win_w / (float)app.fb_width : 1.0f;
    float sy = app.fb_height > 0 ? (float)win_h / (float)app.fb_height : 1.0f;
    int x = (int)(min_x * sx);
    int y = (int)(min_y * sy);
    int w = (int)((max_x - min_x) * sx);
    int h = (int)((max_y - min_y) * sy);
    if (w > 0 && h > 0) platform_set_input_region(app.window, x, y, w, h);
    else platform_clear_input_region(app.window);
}

/* IPC command handler */
static void on_ipc_command(const char *action, void *params, int id, void *user_data) {
    (void)user_data;
    cJSON *json = (cJSON *)params;
    platform_make_context_current(app.window);

    if (strcmp(action, "ping") == 0) { ipc_respond(&app.ipc, id, true, NULL); }
    else if (strcmp(action, "load") == 0) {
        cJSON *path = cJSON_GetObjectItem(json, "path");
        if (!path || !path->valuestring) { ipc_respond(&app.ipc, id, false, "Missing path"); return; }
        if (app.model_loaded) { gltf_unload(&app.model); app.model_loaded = false; renderer_set_model(&app.renderer, NULL); animator_set_model(&app.animator, NULL); }
        if (gltf_load(&app.model, path->valuestring)) {
            app.model_loaded = true; gltf_update_transforms(&app.model); spring_bones_init(&app.model);
            renderer_set_model(&app.renderer, &app.model);
            app.animator.model = &app.model;
            if (app.animator.current_animation < 0 || app.animator.current_animation >= app.model.animation_count) {
                app.animator.current_animation = app.model.animation_count > 0 ? 0 : -1;
                app.animator.time = 0.0f;
                app.animator.next_animation = -1;
            }
            update_anim_correction_mode(app.model.anim_correction_mode);
            update_ui_window_layout();
            app.last_time = platform_get_time();
            printf("Loaded: %s (%d animations)\n", path->valuestring, app.model.animation_count);
            ipc_respond(&app.ipc, id, true, NULL);
        } else { ipc_respond(&app.ipc, id, false, "Failed to load VRM"); }
    }
    else if (strcmp(action, "unload") == 0) {
        if (app.model_loaded) { gltf_unload(&app.model); app.model_loaded = false; renderer_set_model(&app.renderer, NULL); animator_set_model(&app.animator, NULL); }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_visible") == 0) {
        cJSON *vis = cJSON_GetObjectItem(json, "visible");
        if (vis) {
            app.visible = cJSON_IsTrue(vis);
            if (app.visible) platform_show_window(app.window);
            else platform_hide_window(app.window);
            if (!app.visible) hide_ui_window();
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_window") == 0) {
        cJSON *x = cJSON_GetObjectItem(json, "x"), *y = cJSON_GetObjectItem(json, "y");
        cJSON *w = cJSON_GetObjectItem(json, "width"), *h = cJSON_GetObjectItem(json, "height");
        if (x && y) platform_move_window(app.window, x->valueint, y->valueint);
        if (w && h) platform_resize_window(app.window, w->valueint, h->valueint);
        update_ui_window_layout();
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_camera") == 0) {
        cJSON *az = cJSON_GetObjectItem(json, "azimuth"), *po = cJSON_GetObjectItem(json, "polar");
        cJSON *di = cJSON_GetObjectItem(json, "dist"), *tg = cJSON_GetObjectItem(json, "target");
        if (az) app.camera.azimuth = (float)az->valuedouble;
        if (po) app.camera.polar = (float)po->valuedouble;
        if (di) app.camera.distance = (float)di->valuedouble;
        if (tg && cJSON_IsArray(tg) && cJSON_GetArraySize(tg) >= 3) {
            app.camera.target[0] = (float)cJSON_GetArrayItem(tg, 0)->valuedouble;
            app.camera.target[1] = (float)cJSON_GetArrayItem(tg, 1)->valuedouble;
            app.camera.target[2] = (float)cJSON_GetArrayItem(tg, 2)->valuedouble;
        }
        update_window_for_zoom(); ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_auto_resize") == 0) {
        cJSON *en = cJSON_GetObjectItem(json, "enabled");
        app.auto_resize = en ? cJSON_IsTrue(en) : true;
        update_window_for_zoom(); ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "play") == 0) {
        cJSON *speed = cJSON_GetObjectItem(json, "speed");
        animator_play(&app.animator, speed ? (float)speed->valuedouble : 1.0f);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "load_animation") == 0) {
        cJSON *path = cJSON_GetObjectItem(json, "path");
        if (!path || !path->valuestring) { ipc_respond(&app.ipc, id, false, "Missing path"); return; }
        if (!app.model_loaded) { ipc_respond(&app.ipc, id, false, "No model loaded"); return; }
        if (app.ui) {
            debug_ui_reset_menus();
        }
        if (app.ui_visible) {
            hide_ui_window();
            printf("UI debug: closed UI during animation load\n");
        }
        if (gltf_load_animation(&app.model, path->valuestring)) {
            animator_set_model(&app.animator, &app.model);
            update_anim_correction_mode(app.model.anim_correction_mode);
            app.has_current_anim = false;
            app.current_anim_path[0] = '\0';
            snprintf(app.current_anim_path, sizeof(app.current_anim_path), "%s", path->valuestring);
            app.has_current_anim = true;
            g_spring_stiffness = 1.0f;
            g_spring_drag = 1.0f;
            g_spring_gravity = 1.0f;
            g_spring_cloth_mode = 0;
            g_spring_cloth_stiffness = 0.35f;
            g_spring_cloth_drag = 1.2f;
            g_spring_cloth_gravity = 1.0f;
            g_spring_cloth_taper = 0.6f;
            AnimConfig cfg = {0};
            if (anim_config_load(app.current_anim_path, &cfg)) {
                if (cfg.has_spring_stiffness) g_spring_stiffness = cfg.spring_stiffness;
                if (cfg.has_spring_drag) g_spring_drag = cfg.spring_drag;
                if (cfg.has_spring_gravity) g_spring_gravity = cfg.spring_gravity;
                if (cfg.has_spring_cloth_mode) g_spring_cloth_mode = cfg.spring_cloth_mode >= 0.5f ? 1 : 0;
                if (cfg.has_spring_cloth_stiffness) g_spring_cloth_stiffness = cfg.spring_cloth_stiffness;
                if (cfg.has_spring_cloth_drag) g_spring_cloth_drag = cfg.spring_cloth_drag;
                if (cfg.has_spring_cloth_gravity) g_spring_cloth_gravity = cfg.spring_cloth_gravity;
                if (cfg.has_spring_cloth_taper) g_spring_cloth_taper = cfg.spring_cloth_taper;
            }
            debug_ui_sync_spring_params();
            if (app.auto_resize) compute_and_cache_bounds(path->valuestring);
            if (app.model.animation_count > 0) {
                int loop_remaining = animator_compute_loop_remaining(&app.animator, 0);
                animator_blend_to(&app.animator, 0, g_anim_blend_time_default, loop_remaining);
            }
            app.last_time = platform_get_time();
            ipc_respond(&app.ipc, id, true, NULL);
        } else { ipc_respond(&app.ipc, id, false, "Failed to load animation"); }
    }
    else if (strcmp(action, "pause") == 0) { animator_pause(&app.animator); ipc_respond(&app.ipc, id, true, NULL); }
    else if (strcmp(action, "set_time") == 0) {
        cJSON *t = cJSON_GetObjectItem(json, "t");
        if (t) animator_set_time(&app.animator, (float)t->valuedouble);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_animation") == 0) {
        cJSON *idx = cJSON_GetObjectItem(json, "index");
        if (idx) animator_set_animation(&app.animator, idx->valueint);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "blend_to") == 0) {
        cJSON *idx = cJSON_GetObjectItem(json, "index"), *dur = cJSON_GetObjectItem(json, "duration");
        cJSON *loop = cJSON_GetObjectItem(json, "loop"), *count = cJSON_GetObjectItem(json, "count");
        int loop_remaining = 0;
        bool loop_specified = false;

        if (count && cJSON_IsNumber(count)) {
            int plays = count->valueint;
            loop_remaining = (plays > 1) ? (plays - 1) : 0;
            loop_specified = true;
        }
        if (loop && cJSON_IsBool(loop)) {
            loop_specified = true;
            loop_remaining = cJSON_IsTrue(loop) ? -1 : 0;
        }

        if (idx) {
            if (!loop_specified) {
                loop_remaining = animator_compute_loop_remaining(&app.animator, idx->valueint);
            }
            float blend_time;
            if (dur && cJSON_IsString(dur) && strcmp(dur->valuestring, "auto") == 0) {
                blend_time = animator_compute_auto_blend_time(&app.animator, idx->valueint);
            } else if (dur && cJSON_IsNumber(dur)) {
                blend_time = (float)dur->valuedouble;
            } else {
                blend_time = g_anim_blend_time_default;
            }
            animator_blend_to(&app.animator, idx->valueint, blend_time, loop_remaining);
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_position") == 0) {
        cJSON *x = cJSON_GetObjectItem(json, "x"), *y = cJSON_GetObjectItem(json, "y"), *z = cJSON_GetObjectItem(json, "z");
        if (x) app.model_position[0] = (float)x->valuedouble;
        if (y) app.model_position[1] = (float)y->valuedouble;
        if (z) app.model_position[2] = (float)z->valuedouble;
        renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation, app.model_scale);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_rotation") == 0) {
        cJSON *pitch = cJSON_GetObjectItem(json, "pitch"), *yaw = cJSON_GetObjectItem(json, "yaw"), *roll = cJSON_GetObjectItem(json, "roll");
        if (pitch) app.model_rotation[0] = (float)pitch->valuedouble;
        if (yaw) app.model_rotation[1] = (float)yaw->valuedouble;
        if (roll) app.model_rotation[2] = (float)roll->valuedouble;
        renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation, app.model_scale);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_spring") == 0) {
        extern float g_spring_stiffness, g_spring_drag, g_spring_gravity;
        cJSON *stiff = cJSON_GetObjectItem(json, "stiffness"), *drag = cJSON_GetObjectItem(json, "drag"), *grav = cJSON_GetObjectItem(json, "gravity");
        if (stiff) g_spring_stiffness = (float)stiff->valuedouble;
        if (drag) g_spring_drag = (float)drag->valuedouble;
        if (grav) g_spring_gravity = (float)grav->valuedouble;
        debug_ui_sync_spring_params();
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "reset_spring") == 0) { spring_bones_reset(&app.model); ipc_respond(&app.ipc, id, true, NULL); }
    else if (strcmp(action, "set_anim_correction") == 0) {
        cJSON *m = cJSON_GetObjectItem(json, "mode");
        if (m) update_anim_correction_mode(m->valueint);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "shutdown") == 0) { ipc_respond(&app.ipc, id, true, NULL); app.running = false; }
    else { ipc_respond(&app.ipc, id, false, "Unknown action"); }
}

/* Platform callbacks */
static double drag_start_x, drag_start_y;
static bool is_dragging = false;
static bool is_window_dragging = false;
static int window_drag_start_x = 0, window_drag_start_y = 0;
static int window_drag_start_pos[2] = {0, 0};

static void on_resize(int w, int h, void *user) {
    (void)user;
    glViewport(0, 0, w, h);
    app.camera.aspect = (float)w / (float)h;
    msaa_resize(&app, w, h);
    app.fb_width = w;
    app.fb_height = h;
    update_display_metrics_for_window(app.window);
    update_ui_window_layout();
    if (app.ui_window) {
        int fbw = 0, fbh = 0;
        platform_get_framebuffer_size(app.ui_window, &fbw, &fbh);
        app.ui_fb_width = fbw;
        app.ui_fb_height = fbh;
    }
}

static void on_mouse_move(double x, double y, void *user) {
    PlatformWindow *win = (PlatformWindow *)user;
    model_mouse_x = (int)x;
    model_mouse_y = (int)y;
    if (is_window_dragging) {
        int wx = 0, wy = 0;
        platform_get_window_pos(win, &wx, &wy);
        int gx = wx + model_mouse_x;
        int gy = wy + model_mouse_y;
        int dx = gx - window_drag_start_x;
        int dy = gy - window_drag_start_y;
        platform_move_window(win, window_drag_start_pos[0] + dx, window_drag_start_pos[1] + dy);
    } else if (is_dragging) {
        app.camera.azimuth += (float)(x - drag_start_x) * 0.01f;
        app.camera.polar += (float)(y - drag_start_y) * 0.01f;
        if (app.camera.polar < 0.1f) app.camera.polar = 0.1f;
        if (app.camera.polar > 3.0f) app.camera.polar = 3.0f;
        drag_start_x = x; drag_start_y = y;
    }
}

static void on_mouse_button(int button, int action, void *user) {
    PlatformWindow *win = (PlatformWindow *)user;
    double cx = 0.0, cy = 0.0;
    platform_get_cursor_pos(win, &cx, &cy);
    model_mouse_x = (int)cx;
    model_mouse_y = (int)cy;
    if (button == PLATFORM_MOUSE_LEFT) {
        model_mouse_buttons[0] = action;
        if (action && model_hit_test(model_mouse_x, model_mouse_y)) {
            int wx = 0, wy = 0;
            platform_get_window_pos(win, &wx, &wy);
            window_drag_start_x = wx + model_mouse_x;
            window_drag_start_y = wy + model_mouse_y;
            window_drag_start_pos[0] = wx;
            window_drag_start_pos[1] = wy;
            is_window_dragging = true;
        }
        if (!action) {
            is_window_dragging = false;
        }
    } else if (button == PLATFORM_MOUSE_RIGHT) {
        model_mouse_buttons[1] = action;
        if (action && model_hit_test(model_mouse_x, model_mouse_y)) {
            show_ui_window();
        }
    } else if (button == PLATFORM_MOUSE_MIDDLE) {
        model_mouse_buttons[2] = action;
        is_dragging = action;
        if (action) { drag_start_x = model_mouse_x; drag_start_y = model_mouse_y; }
    }
}

static void on_ui_resize(int w, int h, void *user) {
    (void)user;
    app.ui_fb_width = w;
    app.ui_fb_height = h;
    update_display_metrics_for_window(app.ui_window);
}

static void on_ui_mouse_move(double x, double y, void *user) {
    (void)user;
    mouse.x = (int32_t)x;
    mouse.y = (int32_t)y;
}

static void on_ui_mouse_button(int button, int action, void *user) {
    PlatformWindow *win = (PlatformWindow *)user;
    double cx = 0.0, cy = 0.0;
    platform_get_cursor_pos(win, &cx, &cy);
    mouse.x = (int32_t)cx;
    mouse.y = (int32_t)cy;
    if (button == PLATFORM_MOUSE_LEFT) {
        mouse.button_l = action;
    } else if (button == PLATFORM_MOUSE_RIGHT) {
        mouse.button_r = action;
    } else if (button == PLATFORM_MOUSE_MIDDLE) {
        mouse.button_m = action;
    }
    if (app.ui && app.ui_visible) {
        int hit = ui_get_element_id(app.ui, mouse.x, mouse.y);
        printf("UI debug: ui_mouse_button btn=%d action=%d hit=%d\n", button, action, hit);
    }
    if (!action && app.ui_visible && button == PLATFORM_MOUSE_LEFT) {
        app.ui_pending_close = true;
    }
}

static void on_ui_scroll(double dx, double dy, void *user) {
    (void)user; (void)dx; (void)dy;
}

static void on_ui_key(int key, int action, void *user) {
    (void)key; (void)action; (void)user;
}

static void on_scroll(double dx, double dy, void *user) {
    (void)user; (void)dx;
    if (dy > 0) app.model_scale *= 1.1f;
    else if (dy < 0) app.model_scale *= 0.9f;
    if (app.model_scale < 0.1f) app.model_scale = 0.1f;
    if (app.model_scale > 10.0f) app.model_scale = 10.0f;
    
    /* Scale camera distance and target to keep model in view */
    app.camera.distance = 3.0f * app.model_scale;
    app.camera.target[1] = 0.8f * app.model_scale;
    
    renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation, app.model_scale);
    update_window_for_zoom();
}

static void on_key(int key, int action, void *user) {
    (void)user;
    if (!action) return;
    switch (key) {
        case PLATFORM_KEY_ESCAPE: case PLATFORM_KEY_Q: app.running = false; break;
        case PLATFORM_KEY_T:
            app.always_on_top = !app.always_on_top;
            platform_set_always_on_top(app.window, app.always_on_top);
            if (app.ui_window) platform_set_always_on_top(app.ui_window, app.always_on_top);
            break;
        case PLATFORM_KEY_C: renderer_toggle_shader(&app.renderer); break;
        case PLATFORM_KEY_B:
            g_spring_cloth_mode = !g_spring_cloth_mode;
            printf("Spring cloth mode %s\n", g_spring_cloth_mode ? "enabled" : "disabled");
            if (app.model_loaded) spring_bones_reset(&app.model);
            debug_ui_sync_spring_params();
            break;
        case PLATFORM_KEY_1: update_anim_correction_mode(0); break;
        case PLATFORM_KEY_2: update_anim_correction_mode(1); break;
        case PLATFORM_KEY_3: update_anim_correction_mode(2); break;
        case PLATFORM_KEY_4: update_anim_correction_mode(3); break;
        case PLATFORM_KEY_5: update_anim_correction_mode(4); break;
    }
}

static float compute_model_radius_px(void) {
    float dpi = display.d_dpi > 0.0f ? display.d_dpi : 96.0f;
    float fallback = 120.0f * (dpi / 96.0f);
    if (!app.model_loaded || app.fb_width <= 0 || app.fb_height <= 0) return fallback;

    float min_x, min_y, max_x, max_y;
    camera_update(&app.camera);
    float view_model[16];
    build_view_with_model_transform(view_model);
    gltf_get_screen_bounds_px(&app.model, view_model, (float*)app.camera.proj,
                              app.fb_width, app.fb_height, &min_x, &min_y, &max_x, &max_y);
    int win_w = 0, win_h = 0;
    platform_get_window_size(app.window, &win_w, &win_h);
    float sx = app.fb_width > 0 ? (float)win_w / (float)app.fb_width : 1.0f;
    float sy = app.fb_height > 0 ? (float)win_h / (float)app.fb_height : 1.0f;
    float w = (max_x - min_x) * sx;
    float h = (max_y - min_y) * sy;
    float radius = 0.5f * (w > h ? w : h);
    if (radius < fallback) radius = fallback;
    return radius * 0.75f;
}

static void update_ui_window_layout(void) {
    if (!app.ui_window || !app.ui_visible) return;

    int wx = 0, wy = 0, ww = 0, wh = 0;
    platform_get_window_workarea(app.window, &wx, &wy, &ww, &wh);
    int ui_w = 0, ui_h = 0, uix = 0, uiy = 0;
    if (ww > 0 && wh > 0) {
        ui_w = ww;
        ui_h = wh;
        uix = wx;
        uiy = wy;
    } else {
        int mw = 0, mh = 0, mx = 0, my = 0;
        platform_get_window_size(app.window, &mw, &mh);
        platform_get_window_pos(app.window, &mx, &my);
        ui_w = mw * 2;
        ui_h = mh * 2;
        uix = mx - mw / 2;
        uiy = my - mh / 2;
    }

    platform_resize_window(app.ui_window, ui_w, ui_h);
    platform_move_window(app.ui_window, uix, uiy);

    platform_get_framebuffer_size(app.ui_window, &app.ui_fb_width, &app.ui_fb_height);
    update_display_metrics_for_window(app.ui_window);
}

static void update_ui_input_region(void) {
    if (!app.ui_window || !app.ui || !app.ui_visible) return;

    PlatformRect rects[8];
    int rect_count = 0;

    int32_t panel_ids[4];
    int panel_count = debug_ui_get_panel_ids(panel_ids, 4);
    for (int i = 0; i < panel_count && rect_count < (int)(sizeof(rects) / sizeof(rects[0])); i++) {
        int x = 0, y = 0;
        if (ui_get_element_position(app.ui, (uint32_t)panel_ids[i], &x, &y) == 0) {
            int w = ui_get_element_width(app.ui, (uint32_t)panel_ids[i]);
            int h = ui_get_element_height(app.ui, (uint32_t)panel_ids[i]);
            if (w > 0 && h > 0) {
                rects[rect_count++] = (PlatformRect){ x, y, w, h };
            }
        }
    }

    if (app.pie_menu_id >= 0 && rect_count < (int)(sizeof(rects) / sizeof(rects[0]))) {
        int x = 0, y = 0;
        if (ui_get_element_position(app.ui, (uint32_t)app.pie_menu_id, &x, &y) == 0) {
            int w = ui_get_element_width(app.ui, (uint32_t)app.pie_menu_id);
            int h = ui_get_element_height(app.ui, (uint32_t)app.pie_menu_id);
            if (w > 0 && h > 0) {
                rects[rect_count++] = (PlatformRect){ x, y, w, h };
            }
        }
    }

    if (rect_count > 0) {
        platform_set_input_regions(app.ui_window, rects, rect_count);
    } else {
        platform_clear_input_region(app.ui_window);
    }
}

static void build_view_with_model_transform(float out_view[16]) {
    float *view = (float *)app.camera.view;
    float *model = app.renderer.model_transform;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += view[k * 4 + row] * model[col * 4 + k];
            }
            out_view[col * 4 + row] = sum;
        }
    }
}

static bool model_hit_test(int x, int y) {
    if (!app.model_loaded || app.fb_width <= 0 || app.fb_height <= 0) return false;
    float min_x, min_y, max_x, max_y;
    camera_update(&app.camera);
    float view_model[16];
    build_view_with_model_transform(view_model);
    gltf_get_screen_bounds_px(&app.model, view_model, (float*)app.camera.proj,
                              app.fb_width, app.fb_height, &min_x, &min_y, &max_x, &max_y);
    int win_w = 0, win_h = 0;
    platform_get_window_size(app.window, &win_w, &win_h);
    float sx = app.fb_width > 0 ? (float)win_w / (float)app.fb_width : 1.0f;
    float sy = app.fb_height > 0 ? (float)win_h / (float)app.fb_height : 1.0f;
    int bx0 = (int)(min_x * sx);
    int by0 = (int)(min_y * sy);
    int bx1 = (int)(max_x * sx);
    int by1 = (int)(max_y * sy);
    return x >= bx0 && x <= bx1 && y >= by0 && y <= by1;
}

static void show_ui_window(void) {
    if (!app.ui_window) return;
    app.ui_visible = true;
    app.ui_force_pie_open = false;
    app.ui_force_pie_open_pending = true;
    
    /* Translate mouse coordinates from model window to UI window */
    int mx = 0, my = 0, ux = 0, uy = 0;
    platform_get_window_pos(app.window, &mx, &my);
    platform_get_window_pos(app.ui_window, &ux, &uy);
    int translated_x = model_mouse_x + (mx - ux);
    int translated_y = model_mouse_y + (my - uy);
    
    /* Set UI mouse to translated coords */
    mouse.x = translated_x;
    mouse.y = translated_y;
    
    app.pie_menu_id = debug_ui_build_pie_menu(app.ui, app.pie_menu_id, compute_model_radius_px());
    
    /* Explicitly position pie menu at translated coordinates */
    ui_position_element(app.ui, app.pie_menu_id, translated_x, translated_y);
    
    update_ui_window_layout();
    platform_show_window(app.ui_window);
    mouse.button_l = 0;
    mouse.button_r = 0;
    mouse.button_m = 0;
}

static void hide_ui_window(void) {
    if (!app.ui_window) return;
    printf("UI debug: hide_ui_window\n");
    app.ui_visible = false;
    app.ui_force_pie_open = false;
    app.ui_force_pie_open_pending = false;
    app.ui_pending_close = false;
    mouse.button_l = 0;
    mouse.button_r = 0;
    if (app.ui) {
        ui_reset_interaction(app.ui);
    }
    platform_clear_input_region(app.ui_window);
    platform_hide_window(app.ui_window);
}

static void ui_init_overlay(VrmApp *s) {
    if (s->ui) return;
    s->ui = ui_init(NULL);
    if (!s->ui) return;
    ui_load_font(s->ui, "src/ui_library/fonts/BeVietnamPro-Regular.ttf", 18);
    debug_ui_set_interface(s->ui);
    debug_ui_set_spring_reset(reset_springs_from_ui);
    debug_ui_set_spring_save(save_springs_to_config);

    s->pie_menu_id = -1;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (!ipc_init(&app.ipc, "/tmp/vrm-overlay.sock", on_ipc_command, NULL))
        fprintf(stderr, "Failed to start IPC server\n");

    app.auto_resize = true;

    app.window = platform_create_window(400, 600, "VRM Overlay", true, false);
    if (!app.window) { fprintf(stderr, "Failed to create window\n"); return 1; }

    app.ui_window = platform_create_shared_window(800, 1200, "VRM Overlay UI", true, false, app.window);
    if (!app.ui_window) { fprintf(stderr, "Failed to create UI window\n"); return 1; }
    app.ui_visible = false;
    app.ui_pending_close = false;
    platform_hide_window(app.ui_window);

    platform_make_context_current(app.window);

    printf("OpenGL: %s\n", glGetString(GL_VERSION));
    if (!renderer_init(&app.renderer)) { fprintf(stderr, "Failed to init renderer\n"); return 1; }

    GLint max_samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    app.msaa_samples = max_samples >= 4 ? 4 : max_samples;
    printf("MSAA samples: %d\n", app.msaa_samples);

    int w, h;
    platform_get_framebuffer_size(app.window, &w, &h);
    msaa_resize(&app, w, h);
    app.fb_width = w;
    app.fb_height = h;
    update_display_metrics_for_window(app.window);
    update_ui_window_layout();

    camera_init(&app.camera);
    app.camera.distance = 3.0f; app.camera.target[1] = 0.8f;
    app.model_scale = 1.0f;
    {
        Settings settings = {0};
        if (settings_load(SETTINGS_PATH, &settings)) {
            if (settings.has_camera_distance) {
                float saved_distance = settings.camera_distance;
                if (saved_distance < 0.5f) saved_distance = 0.5f;
                if (saved_distance > 10.0f) saved_distance = 10.0f;
                app.camera.distance = saved_distance;
            }
            if (settings.has_model_scale) {
                float saved_scale = settings.model_scale;
                if (saved_scale < 0.1f) saved_scale = 0.1f;
                if (saved_scale > 10.0f) saved_scale = 10.0f;
                app.model_scale = saved_scale;
            }
            if (settings.has_model_position) {
                app.model_position[0] = settings.model_position[0];
                app.model_position[1] = settings.model_position[1];
                app.model_position[2] = settings.model_position[2];
            }
            if (settings.has_window_position) {
                platform_move_window(app.window, settings.window_pos[0], settings.window_pos[1]);
            }
            if (settings.has_window_size) {
                platform_resize_window(app.window, settings.window_size[0], settings.window_size[1]);
            }
            if (settings.has_model_position || settings.has_model_scale) {
                renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation, app.model_scale);
                /* Update camera to match scale */
                app.camera.distance = 3.0f * app.model_scale;
                app.camera.target[1] = 0.8f * app.model_scale;
            }
        }
    }
    app.camera.polar = 1.57f; app.camera.azimuth = 3.14f;
    app.camera.aspect = (float)w / (float)h;

    animator_init(&app.animator);
    platform_make_context_current(app.ui_window);
    update_display_metrics_for_window(app.ui_window);
    ui_init_overlay(&app);
    platform_make_context_current(app.window);

    PlatformCallbacks cb = { on_resize, on_mouse_move, on_mouse_button, on_scroll, on_key, app.window };
    PlatformCallbacks ui_cb = { on_ui_resize, on_ui_mouse_move, on_ui_mouse_button, on_ui_scroll, on_ui_key, app.ui_window };
    platform_set_callbacks(app.window, &cb);
    platform_set_callbacks(app.ui_window, &ui_cb);

    app.visible = true; app.running = true;
    app.last_time = platform_get_time();

    printf("VRM Overlay started. Press 'q' to quit, 't' for always-on-top\n");
    printf("Press 'c' to toggle cel shading\n");
    printf("Press 'b' to toggle cloth spring mode\n");
    printf("IPC socket: /tmp/vrm-overlay.sock\n");
    fflush(stdout);

    while (app.running && !platform_should_close(app.window)) {
        platform_make_context_current(app.window);
        platform_poll_events();
        ipc_poll(&app.ipc);

        double now = platform_get_time();
        float dt = (float)(now - app.last_time);
        app.last_time = now;
        if (dt > 0.0f) display.frame_rate = 1.0f / dt;

        animator_update(&app.animator, dt);
        if (app.model_loaded) {
            gltf_update_transforms(&app.model);
            spring_bones_update(&app.model, dt);
            gltf_update_transforms(&app.model);
        }

        update_input_region();

        platform_make_context_current(app.window);
        if (app.msaa_fbo) glBindFramebuffer(GL_FRAMEBUFFER, app.msaa_fbo);
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Ensure proper GL state for rendering */
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glCullFace(GL_BACK);

        camera_update(&app.camera);
        renderer_draw(&app.renderer, &app.camera, app.fb_width, app.fb_height);

        if (app.msaa_fbo) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, app.msaa_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, app.fb_width, app.fb_height, 0, 0, app.fb_width, app.fb_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        platform_swap_buffers(app.window);

        if (app.ui_window && app.ui && app.ui_visible) {
            platform_make_context_current(app.ui_window);
            platform_get_framebuffer_size(app.ui_window, &app.ui_fb_width, &app.ui_fb_height);
            glViewport(0, 0, app.ui_fb_width, app.ui_fb_height);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            update_display_metrics_for_window(app.ui_window);
            if (app.ui_force_pie_open_pending) {
                ui_render(app.ui);
                app.ui_force_pie_open_pending = false;
                app.ui_force_pie_open = true;
            } else if (app.ui_force_pie_open) {
                mouse.button_r = 1;
                ui_render(app.ui);
                mouse.button_r = 0;
                app.ui_force_pie_open = false;
            } else {
                ui_render(app.ui);
            }
            update_ui_input_region();
            if (app.ui_pending_close) {
                app.ui_pending_close = false;
                if (!debug_ui_has_open_panels()) {
                    printf("UI debug: auto-closing UI (no panels)\n");
                    hide_ui_window();
                }
            }
            if (debug_ui_consume_close_request()) {
                debug_ui_prepare_for_close();
                mouse.button_l = 0;
                mouse.button_r = 0;
                mouse.button_m = 0;
                ui_render(app.ui);
                if (debug_ui_consume_force_close_request() || !debug_ui_has_open_panels()) {
                    printf("UI debug: close request consumed\n");
                    hide_ui_window();
                } else {
                    printf("UI debug: close request ignored (panels still open)\n");
                }
            }
            platform_swap_buffers(app.ui_window);
        }
    }

    Settings settings = {0};
    settings.has_camera_distance = true;
    settings.camera_distance = app.camera.distance;
    settings.has_model_position = true;
    settings.model_position[0] = app.model_position[0];
    settings.model_position[1] = app.model_position[1];
    settings.model_position[2] = app.model_position[2];
    settings.has_model_scale = true;
    settings.model_scale = app.model_scale;
    {
        int wx = 0, wy = 0, ww = 0, wh = 0;
        platform_get_window_pos(app.window, &wx, &wy);
        platform_get_window_size(app.window, &ww, &wh);
        settings.has_window_position = true;
        settings.window_pos[0] = wx;
        settings.window_pos[1] = wy;
        settings.has_window_size = true;
        settings.window_size[0] = ww;
        settings.window_size[1] = wh;
    }
    settings_save(SETTINGS_PATH, &settings);

    gltf_unload(&app.model);
    renderer_cleanup(&app.renderer);
    ipc_cleanup(&app.ipc);
    if (app.ui_window) platform_destroy_window(app.ui_window);
    platform_destroy_window(app.window);
    return 0;
}
