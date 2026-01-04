/*
 * VRM Desktop Overlay Viewer - Main Entry Point
 */

#include <gtk/gtk.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "renderer.h"
#include "camera.h"
#include "gltf_loader.h"
#include "animator.h"
#include "ipc.h"
#include "spring_bones.h"
#include "ui_library/ui.h"
#include "../deps/cjson/cJSON.h"

typedef struct {
    GtkWidget *window;
    GtkWidget *gl_area;
    Renderer renderer;
    OrbitCamera camera;
    GltfModel model;
    Animator animator;
    IpcServer ipc;
    GLuint msaa_fbo;
    GLuint msaa_color;
    GLuint msaa_depth;
    int msaa_samples;
    int fb_width;
    int fb_height;
    ui_interface_t *ui;
    int32_t pie_menu_id;
    gboolean visible;
    gboolean always_on_top;
    gboolean model_loaded;
    gboolean auto_resize;
    float cached_world_w, cached_world_h; /* World-space bounds */
    gint64 last_time;
    float model_position[3];  /* x, y, z offset */
    float model_rotation[3];  /* pitch, yaw, roll in degrees */
} VrmApp;

static VrmApp app = {0};

display_t display = {0};
mouse_t mouse = {0};
gl_buffer_t gl_buffer_default = {0};

static void ui_update_display_metrics(GtkWidget *widget) {
    GdkScreen *screen = gtk_widget_get_screen(widget);
    double dpi = gdk_screen_get_resolution(screen);
    int scale = gtk_widget_get_scale_factor(widget);
    if (dpi <= 0.0) {
        dpi = 96.0 * (double)scale;
    }
    display.h_dpi = (float)dpi;
    display.v_dpi = (float)dpi;
    display.d_dpi = (float)dpi;
    display.width = gtk_widget_get_allocated_width(widget);
    display.height = gtk_widget_get_allocated_height(widget);
}

static void on_pie_select(int idx) {
    printf("UI pie selection: %d\n", idx);
}

static void ui_init_overlay(VrmApp *state) {
    if (state->ui) return;
    state->ui = ui_init(NULL);
    if (!state->ui) return;
    ui_load_font(state->ui, "src/ui_library/fonts/BeVietnamPro-Regular.ttf", 18);

    float dpi = display.d_dpi > 0.0f ? display.d_dpi : 96.0f;
    float radius = 120.0f * (dpi / 96.0f);
    element_data items[4];
    items[0].b = (ui_button_t){.str = "One", .w = 70, .h = 30, .state = 0};
    items[1].b = (ui_button_t){.str = "Two", .w = 70, .h = 30, .state = 0};
    items[2].b = (ui_button_t){.str = "Three", .w = 70, .h = 30, .state = 0};
    items[3].b = (ui_button_t){.str = "Four", .w = 70, .h = 30, .state = 0};

    ui_pie_menu_t pie = {
        .type = UI_BUTTON_ROUND,
        .count = 4,
        .radius = radius,
        .elements = items
    };
    state->pie_menu_id = ui_element_init(state->ui, &pie, 0, UI_PIE_MENU);
    ui_set_callback(state->ui, state->pie_menu_id, on_pie_select);
}

static void update_window_for_zoom(void) {
    if (!app.auto_resize || !app.model_loaded) return;
    if (app.cached_world_w <= 0 || app.cached_world_h <= 0) return;
    
    /* Convert world size to screen pixels based on camera distance and FOV */
    /* Screen size ≈ world_size / distance * focal_length * base_pixels */
    float fov = 0.785f; /* 45 degrees in radians */
    float scale = 1.0f / (app.camera.distance * tanf(fov * 0.5f));
    
    int base = 300; /* Base pixels per unit at scale=1 */
    int w = (int)(app.cached_world_w * scale * base);
    int h = (int)(app.cached_world_h * scale * base);
    
    if (w < 100) w = 100;
    if (h < 100) h = 100;
    if (w > 2000) w = 2000;
    if (h > 3000) h = 3000;
    gtk_window_resize(GTK_WINDOW(app.window), w, h);
}

static void compute_and_cache_bounds(const char *anim_path) {
    if (!app.model_loaded) return;
    camera_update(&app.camera);
    gltf_compute_animation_bounds(&app.model, anim_path,
                                  (float*)app.camera.view, (float*)app.camera.proj,
                                  &app.cached_world_w, &app.cached_world_h);
    update_window_for_zoom();
}

static void msaa_cleanup(VrmApp *state) {
    if (state->msaa_color) {
        glDeleteRenderbuffers(1, &state->msaa_color);
        state->msaa_color = 0;
    }
    if (state->msaa_depth) {
        glDeleteRenderbuffers(1, &state->msaa_depth);
        state->msaa_depth = 0;
    }
    if (state->msaa_fbo) {
        glDeleteFramebuffers(1, &state->msaa_fbo);
        state->msaa_fbo = 0;
    }
}

static void msaa_resize(VrmApp *state, int width, int height) {
    if (width <= 0 || height <= 0 || state->msaa_samples <= 0) return;
    if (state->fb_width == width && state->fb_height == height && state->msaa_fbo) {
        return;
    }

    msaa_cleanup(state);

    glGenFramebuffers(1, &state->msaa_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, state->msaa_fbo);

    glGenRenderbuffers(1, &state->msaa_color);
    glBindRenderbuffer(GL_RENDERBUFFER, state->msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, state->msaa_samples,
                                     GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, state->msaa_color);

    glGenRenderbuffers(1, &state->msaa_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, state->msaa_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, state->msaa_samples,
                                     GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, state->msaa_depth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "MSAA framebuffer incomplete (status 0x%x)\n", status);
        msaa_cleanup(state);
        return;
    }

    state->fb_width = width;
    state->fb_height = height;
}

static void update_anim_correction_mode(int mode) {
    /* VRMA/poses.glb in this repo require a global Y-180 correction.
       Keep the hotkeys for testing, but default is rotY 180. */
    app.model.anim_correction_mode = mode;
    if (mode == 0) {
        /* No correction - identity quaternion but still process */
        app.model.anim_global_correction[0] = 0.0f;
        app.model.anim_global_correction[1] = 0.0f;
        app.model.anim_global_correction[2] = 0.0f;
        app.model.anim_global_correction[3] = 1.0f;
        printf("Anim correction: none\n");
    } else if (mode == 1) {
        app.model.anim_global_correction[0] = app.model.anim_global_auto_correction[0];
        app.model.anim_global_correction[1] = app.model.anim_global_auto_correction[1];
        app.model.anim_global_correction[2] = app.model.anim_global_auto_correction[2];
        app.model.anim_global_correction[3] = app.model.anim_global_auto_correction[3];
        printf("Anim correction: auto (%.2f,%.2f,%.2f,%.2f)\n",
               app.model.anim_global_correction[0], app.model.anim_global_correction[1],
               app.model.anim_global_correction[2], app.model.anim_global_correction[3]);
    } else if (mode == 2) {
        app.model.anim_global_correction[0] = 1.0f;
        app.model.anim_global_correction[1] = 0.0f;
        app.model.anim_global_correction[2] = 0.0f;
        app.model.anim_global_correction[3] = 0.0f;
        printf("Anim correction: rotX 180\n");
    } else if (mode == 3) {
        app.model.anim_global_correction[0] = 0.0f;
        app.model.anim_global_correction[1] = 1.0f;
        app.model.anim_global_correction[2] = 0.0f;
        app.model.anim_global_correction[3] = 0.0f;
        printf("Anim correction: rotY 180\n");
    } else if (mode == 4) {
        app.model.anim_global_correction[0] = 0.0f;
        app.model.anim_global_correction[1] = 0.0f;
        app.model.anim_global_correction[2] = 1.0f;
        app.model.anim_global_correction[3] = 0.0f;
        printf("Anim correction: rotZ 180\n");
    }
    fflush(stdout);
}

/* IPC command handler */
static void on_ipc_command(const char *action, void *params, int id, void *user_data) {
    (void)user_data;
    cJSON *json = (cJSON *)params;

    if (strcmp(action, "ping") == 0) {
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "load") == 0) {
        cJSON *path = cJSON_GetObjectItem(json, "path");
        if (!path || !path->valuestring) {
            ipc_respond(&app.ipc, id, false, "Missing path");
            return;
        }

        /* Make GL context current for buffer creation */
        gtk_gl_area_make_current(GTK_GL_AREA(app.gl_area));

        if (app.model_loaded) {
            gltf_unload(&app.model);
            app.model_loaded = false;
            renderer_set_model(&app.renderer, NULL);
            animator_set_model(&app.animator, NULL);
        }

        if (gltf_load(&app.model, path->valuestring)) {
            app.model_loaded = true;
            gltf_update_transforms(&app.model); /* Update transforms before spring init */
            spring_bones_init(&app.model);
            renderer_set_model(&app.renderer, &app.model);
            animator_set_model(&app.animator, &app.model);
            update_anim_correction_mode(app.model.anim_correction_mode);
            printf("Loaded: %s (%d animations)\n", path->valuestring, app.model.animation_count);
            ipc_respond(&app.ipc, id, true, NULL);
        } else {
            ipc_respond(&app.ipc, id, false, "Failed to load VRM");
        }
    }
    else if (strcmp(action, "unload") == 0) {
        if (app.model_loaded) {
            gltf_unload(&app.model);
            app.model_loaded = false;
            renderer_set_model(&app.renderer, NULL);
            animator_set_model(&app.animator, NULL);
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_visible") == 0) {
        cJSON *vis = cJSON_GetObjectItem(json, "visible");
        if (vis) {
            app.visible = cJSON_IsTrue(vis);
            if (app.visible) {
                gtk_widget_show(app.window);
            } else {
                gtk_widget_hide(app.window);
            }
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_window") == 0) {
        cJSON *x = cJSON_GetObjectItem(json, "x");
        cJSON *y = cJSON_GetObjectItem(json, "y");
        cJSON *w = cJSON_GetObjectItem(json, "width");
        cJSON *h = cJSON_GetObjectItem(json, "height");

        if (x && y) {
            gtk_window_move(GTK_WINDOW(app.window), x->valueint, y->valueint);
        }
        if (w && h) {
            gtk_window_resize(GTK_WINDOW(app.window), w->valueint, h->valueint);
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_camera") == 0) {
        cJSON *az = cJSON_GetObjectItem(json, "azimuth");
        cJSON *po = cJSON_GetObjectItem(json, "polar");
        cJSON *di = cJSON_GetObjectItem(json, "dist");
        cJSON *tg = cJSON_GetObjectItem(json, "target");

        if (az) app.camera.azimuth = (float)az->valuedouble;
        if (po) app.camera.polar = (float)po->valuedouble;
        if (di) app.camera.distance = (float)di->valuedouble;
        if (tg && cJSON_IsArray(tg) && cJSON_GetArraySize(tg) >= 3) {
            app.camera.target[0] = (float)cJSON_GetArrayItem(tg, 0)->valuedouble;
            app.camera.target[1] = (float)cJSON_GetArrayItem(tg, 1)->valuedouble;
            app.camera.target[2] = (float)cJSON_GetArrayItem(tg, 2)->valuedouble;
        }
        update_window_for_zoom();
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_auto_resize") == 0) {
        cJSON *en = cJSON_GetObjectItem(json, "enabled");
        app.auto_resize = en ? cJSON_IsTrue(en) : TRUE;
        update_window_for_zoom();
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_blendshape") == 0) {
        cJSON *name = cJSON_GetObjectItem(json, "name");
        cJSON *value = cJSON_GetObjectItem(json, "value");
        /* TODO: Implement blendshape control */
        (void)name; (void)value;
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "play") == 0) {
        cJSON *speed = cJSON_GetObjectItem(json, "speed");
        float s = speed ? (float)speed->valuedouble : 1.0f;
        animator_play(&app.animator, s);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "load_animation") == 0) {
        cJSON *path = cJSON_GetObjectItem(json, "path");
        if (!path || !path->valuestring) {
            ipc_respond(&app.ipc, id, false, "Missing path");
            return;
        }
        if (!app.model_loaded) {
            ipc_respond(&app.ipc, id, false, "No model loaded");
            return;
        }
        if (gltf_load_animation(&app.model, path->valuestring)) {
            animator_set_model(&app.animator, &app.model);
            update_anim_correction_mode(app.model.anim_correction_mode);
            spring_bones_reset(&app.model);
            if (app.auto_resize) compute_and_cache_bounds(path->valuestring);
            ipc_respond(&app.ipc, id, true, NULL);
        } else {
            ipc_respond(&app.ipc, id, false, "Failed to load animation");
        }
    }
    else if (strcmp(action, "set_debug_rot") == 0) {
        ipc_respond(&app.ipc, id, false, "Debug rotation overrides removed");
    }
    else if (strcmp(action, "pause") == 0) {
        animator_pause(&app.animator);
        ipc_respond(&app.ipc, id, true, NULL);
    }
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
        cJSON *idx = cJSON_GetObjectItem(json, "index");
        cJSON *dur = cJSON_GetObjectItem(json, "duration");
        if (idx) {
            float d = dur ? (float)dur->valuedouble : 0.3f;
            animator_blend_to(&app.animator, idx->valueint, d);
        }
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_loop_blend") == 0) {
        cJSON *dur = cJSON_GetObjectItem(json, "duration");
        float d = dur ? (float)dur->valuedouble : 0.0f;
        animator_set_loop_blend(&app.animator, d);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_smooth_loop") == 0) {
        cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
        animator_set_smooth_loop(&app.animator, enabled && cJSON_IsTrue(enabled));
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_position") == 0) {
        cJSON *x = cJSON_GetObjectItem(json, "x");
        cJSON *y = cJSON_GetObjectItem(json, "y");
        cJSON *z = cJSON_GetObjectItem(json, "z");
        if (x) app.model_position[0] = (float)x->valuedouble;
        if (y) app.model_position[1] = (float)y->valuedouble;
        if (z) app.model_position[2] = (float)z->valuedouble;
        renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_rotation") == 0) {
        cJSON *pitch = cJSON_GetObjectItem(json, "pitch");
        cJSON *yaw = cJSON_GetObjectItem(json, "yaw");
        cJSON *roll = cJSON_GetObjectItem(json, "roll");
        if (pitch) app.model_rotation[0] = (float)pitch->valuedouble;
        if (yaw) app.model_rotation[1] = (float)yaw->valuedouble;
        if (roll) app.model_rotation[2] = (float)roll->valuedouble;
        renderer_set_model_transform(&app.renderer, app.model_position, app.model_rotation);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_spring") == 0) {
        /* Tune spring bone global multipliers */
        extern float g_spring_stiffness, g_spring_drag, g_spring_gravity;
        cJSON *stiff = cJSON_GetObjectItem(json, "stiffness");
        cJSON *drag = cJSON_GetObjectItem(json, "drag");
        cJSON *grav = cJSON_GetObjectItem(json, "gravity");
        
        if (stiff) g_spring_stiffness = (float)stiff->valuedouble;
        if (drag) g_spring_drag = (float)drag->valuedouble;
        if (grav) g_spring_gravity = (float)grav->valuedouble;
        
        printf("Spring params: stiffness=%.2f drag=%.2f gravity=%.2f\n",
               g_spring_stiffness, g_spring_drag, g_spring_gravity);
        fflush(stdout);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "reset_spring") == 0) {
        spring_bones_reset(&app.model);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "set_anim_correction") == 0) {
        cJSON *m = cJSON_GetObjectItem(json, "mode");
        if (m) update_anim_correction_mode(m->valueint);
        ipc_respond(&app.ipc, id, true, NULL);
    }
    else if (strcmp(action, "quit") == 0) {
        ipc_respond(&app.ipc, id, true, NULL);
        gtk_main_quit();
    }
    else {
        ipc_respond(&app.ipc, id, false, "Unknown action");
    }
}

static void on_realize(GtkGLArea *area, gpointer user_data) {
    (void)user_data;
    gtk_gl_area_make_current(area);

    if (gtk_gl_area_get_error(area) != NULL) {
        fprintf(stderr, "Failed to initialize OpenGL context\n");
        return;
    }

    printf("OpenGL: %s\n", glGetString(GL_VERSION));

    if (!renderer_init(&app.renderer)) {
        fprintf(stderr, "Failed to initialize renderer\n");
        return;
    }

    GLint max_samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    app.msaa_samples = max_samples >= 4 ? 4 : max_samples;
    printf("MSAA samples (requested): %d\n", app.msaa_samples);
    msaa_resize(&app, gtk_widget_get_allocated_width(GTK_WIDGET(area)),
                gtk_widget_get_allocated_height(GTK_WIDGET(area)));

    camera_init(&app.camera);
    app.camera.distance = 3.0f;
    app.camera.target[1] = 0.8f;  /* Look at chest height */
    app.camera.polar = 1.57f;     /* Look from front */
    app.camera.azimuth = 3.14f;   /* Face the model */

    animator_init(&app.animator);

    ui_update_display_metrics(GTK_WIDGET(area));
    ui_init_overlay(&app);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data) {
    (void)area; (void)context; (void)user_data;
    GLint target_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target_fbo);

    if (app.msaa_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, app.msaa_fbo);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target_fbo);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_update(&app.camera);
    renderer_draw(&app.renderer, &app.camera);

    if (app.msaa_fbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, app.msaa_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)target_fbo);
        glBlitFramebuffer(0, 0, app.fb_width, app.fb_height,
                          0, 0, app.fb_width, app.fb_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target_fbo);
    }

    if (app.ui) {
        ui_render(app.ui);
    }

    return TRUE;
}

static void on_resize(GtkGLArea *area, gint width, gint height, gpointer user_data) {
    (void)area; (void)user_data;
    glViewport(0, 0, width, height);
    app.camera.aspect = (float)width / (float)height;
    msaa_resize(&app, width, height);
    ui_update_display_metrics(GTK_WIDGET(area));
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data) {
    (void)user_data;

    gint64 now = gdk_frame_clock_get_frame_time(clock);
    if (app.last_time == 0) app.last_time = now;

    float dt = (float)(now - app.last_time) / 1000000.0f; /* microseconds to seconds */
    app.last_time = now;
    if (dt > 0.0f) {
        display.frame_rate = 1.0f / dt;
    }

    animator_update(&app.animator, dt);
    
    /* Update spring bones after animation */
    if (app.model_loaded) {
        gltf_update_transforms(&app.model);
        spring_bones_update(&app.model, dt);
        gltf_update_transforms(&app.model);
    }

    gtk_widget_queue_draw(widget);
    return G_SOURCE_CONTINUE;
}

static void setup_transparent_window(GtkWidget *window) {
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        printf("Compositing enabled\n");
    } else {
        fprintf(stderr, "WARNING: Compositor not detected\n");
    }
    gtk_widget_set_app_paintable(window, TRUE);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)user_data;
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget; (void)user_data;

    switch (event->keyval) {
        case GDK_KEY_Escape:
        case GDK_KEY_q:
            gtk_main_quit();
            return TRUE;
        case GDK_KEY_t:
            app.always_on_top = !app.always_on_top;
            gtk_window_set_keep_above(GTK_WINDOW(app.window), app.always_on_top);
            return TRUE;
        case GDK_KEY_c:
            renderer_toggle_shader(&app.renderer);
            return TRUE;
        case GDK_KEY_1:
            update_anim_correction_mode(0);
            return TRUE;
        case GDK_KEY_2:
            update_anim_correction_mode(1);
            return TRUE;
        case GDK_KEY_3:
            update_anim_correction_mode(2);
            return TRUE;
        case GDK_KEY_4:
            update_anim_correction_mode(3);
            return TRUE;
        case GDK_KEY_5:
            update_anim_correction_mode(4);
            return TRUE;
    }
    return FALSE;
}

static gdouble drag_start_x, drag_start_y;
static gboolean is_dragging = FALSE;

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)user_data;
    mouse.x = (int32_t)event->x;
    mouse.y = (int32_t)event->y;
    if (event->button == 1) {
        is_dragging = TRUE;
        mouse.button_l = 1;
        drag_start_x = event->x;
        drag_start_y = event->y;
        return TRUE;
    }
    if (event->button == 3) {
        mouse.button_r = 1;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)user_data;
    if (event->button == 1) {
        is_dragging = FALSE;
        mouse.button_l = 0;
        return TRUE;
    }
    if (event->button == 3) {
        mouse.button_r = 0;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget; (void)user_data;
    mouse.x = (int32_t)event->x;
    mouse.y = (int32_t)event->y;
    if (is_dragging) {
        app.camera.azimuth += (float)(event->x - drag_start_x) * 0.01f;
        app.camera.polar += (float)(event->y - drag_start_y) * 0.01f;
        if (app.camera.polar < 0.1f) app.camera.polar = 0.1f;
        if (app.camera.polar > 3.0f) app.camera.polar = 3.0f;
        drag_start_x = event->x;
        drag_start_y = event->y;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    (void)widget; (void)user_data;
    if (event->direction == GDK_SCROLL_UP) {
        app.camera.distance *= 0.9f;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        app.camera.distance *= 1.1f;
    }
    if (app.camera.distance < 0.5f) app.camera.distance = 0.5f;
    if (app.camera.distance > 10.0f) app.camera.distance = 10.0f;
    /* Bounds are camera-dependent, scale cached values proportionally */
    update_window_for_zoom();
    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* Initialize IPC server */
    if (!ipc_init(&app.ipc, "/tmp/vrm-overlay.sock", on_ipc_command, NULL)) {
        fprintf(stderr, "Failed to start IPC server\n");
    }

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "VRM Overlay");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 400, 600);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);

    setup_transparent_window(app.window);

    app.gl_area = gtk_gl_area_new();
    gtk_gl_area_set_required_version(GTK_GL_AREA(app.gl_area), 3, 3);
    gtk_gl_area_set_has_alpha(GTK_GL_AREA(app.gl_area), TRUE);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(app.gl_area), TRUE);

    gtk_container_add(GTK_CONTAINER(app.window), app.gl_area);

    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app.window, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    g_signal_connect(app.gl_area, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(app.gl_area, "render", G_CALLBACK(on_render), NULL);
    g_signal_connect(app.gl_area, "resize", G_CALLBACK(on_resize), NULL);

    gtk_widget_add_events(app.gl_area,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
    g_signal_connect(app.gl_area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(app.gl_area, "button-release-event", G_CALLBACK(on_button_release), NULL);
    g_signal_connect(app.gl_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(app.gl_area, "scroll-event", G_CALLBACK(on_scroll), NULL);

    gtk_widget_add_tick_callback(app.gl_area, on_tick, NULL, NULL);

    app.visible = TRUE;
    gtk_widget_show_all(app.window);

    printf("VRM Overlay started. Press 'q' to quit, 't' for always-on-top\n");
    printf("Press 'c' to toggle cel shading\n");
    printf("IPC socket: /tmp/vrm-overlay.sock\n");

    gtk_main();

    gltf_unload(&app.model);
    renderer_cleanup(&app.renderer);
    ipc_cleanup(&app.ipc);

    return 0;
}
