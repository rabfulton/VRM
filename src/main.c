/*
 * VRM Desktop Overlay Viewer - Main Entry Point
 */

#include <gtk/gtk.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>

#include "renderer.h"
#include "camera.h"
#include "gltf_loader.h"
#include "animator.h"
#include "ipc.h"
#include "../deps/cjson/cJSON.h"

typedef struct {
    GtkWidget *window;
    GtkWidget *gl_area;
    Renderer renderer;
    OrbitCamera camera;
    GltfModel model;
    Animator animator;
    IpcServer ipc;
    gboolean visible;
    gboolean always_on_top;
    gboolean model_loaded;
    gint64 last_time;
} VrmApp;

static VrmApp app = {0};

static void update_anim_correction_mode(int mode) {
    /* VRMA/poses.glb in this repo require a global Y-180 correction.
       Keep the hotkeys for testing, but default is rotY 180. */
    app.model.anim_correction_mode = mode;
    if (mode == 0) {
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
        printf("Anim correction: auto\n");
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

    camera_init(&app.camera);
    app.camera.distance = 3.0f;
    app.camera.target[1] = 0.8f;  /* Look at chest height */
    app.camera.polar = 1.57f;     /* Look from front */
    app.camera.azimuth = 3.14f;   /* Face the model */

    animator_init(&app.animator);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data) {
    (void)area; (void)context; (void)user_data;

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_update(&app.camera);
    renderer_draw(&app.renderer, &app.camera);

    return TRUE;
}

static void on_resize(GtkGLArea *area, gint width, gint height, gpointer user_data) {
    (void)area; (void)user_data;
    glViewport(0, 0, width, height);
    app.camera.aspect = (float)width / (float)height;
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data) {
    (void)user_data;

    gint64 now = gdk_frame_clock_get_frame_time(clock);
    if (app.last_time == 0) app.last_time = now;

    float dt = (float)(now - app.last_time) / 1000000.0f; /* microseconds to seconds */
    app.last_time = now;

    animator_update(&app.animator, dt);

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
    if (event->button == 1) {
        is_dragging = TRUE;
        drag_start_x = event->x;
        drag_start_y = event->y;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)user_data;
    if (event->button == 1) {
        is_dragging = FALSE;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget; (void)user_data;
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
    printf("IPC socket: /tmp/vrm-overlay.sock\n");

    gtk_main();

    gltf_unload(&app.model);
    renderer_cleanup(&app.renderer);
    ipc_cleanup(&app.ipc);

    return 0;
}
