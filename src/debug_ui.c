#include <math.h>
#include <stdio.h>

#include "debug_ui.h"
#include "ui_library/ui.h"

typedef enum {
    UI_PIE_ADJUST_SPRINGS = 0,
    UI_PIE_ADJUST_AXIS,
    UI_PIE_CLOSE,
    UI_PIE_STATS,
    UI_PIE_LOAD_MODEL,
    UI_PIE_ANIMATION,
    UI_PIE_POSITION,
    UI_PIE_MAX_ELEMENT
} UiPieAction;

typedef enum {
    SPRING_PARAM_STIFFNESS = 0,
    SPRING_PARAM_DRAG,
    SPRING_PARAM_GRAVITY,
    SPRING_PARAM_MAX_ELEMENT
} SpringParamAction;

static const char *ui_pie_menu_labels[UI_PIE_MAX_ELEMENT] = {
    "Adjust Springs",
    "Adjust Axis",
    "Close",
    "stats",
    "load Model",
    "Animation",
    "Position"
};

static ui_interface_t *debug_ui = NULL;
static void (*spring_reset_fn)(void) = NULL;
static void (*spring_save_fn)(void) = NULL;
static int32_t spring_menu_id = -1;
static uint32_t spring_param_ids[SPRING_PARAM_MAX_ELEMENT] = {0};
static int spring_close_guard = 0;
static int ui_close_request = 0;
static int spring_menu_close_pending = 0;

extern float g_spring_stiffness;
extern float g_spring_drag;
extern float g_spring_gravity;

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void debug_ui_set_interface(ui_interface_t *ui) {
    debug_ui = ui;
}

void debug_ui_set_spring_reset(void (*fn)(void)) {
    spring_reset_fn = fn;
}

void debug_ui_set_spring_save(void (*fn)(void)) {
    spring_save_fn = fn;
}

int debug_ui_consume_close_guard(void) {
    int guard = spring_close_guard;
    spring_close_guard = 0;
    return guard;
}

int debug_ui_consume_close_request(void) {
    int request = ui_close_request;
    ui_close_request = 0;
    return request;
}

void debug_ui_prepare_for_close(void) {
    if (!spring_menu_close_pending) return;
    spring_menu_close_pending = 0;
    if (!debug_ui || spring_menu_id < 0) return;
    ui_element_delete(debug_ui, (uint32_t)spring_menu_id);
    spring_menu_id = -1;
    for (int i = 0; i < SPRING_PARAM_MAX_ELEMENT; i++) {
        spring_param_ids[i] = 0;
    }
}

static void springs_menu(void);

static void on_spring_stiffness(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_stiffness = clampf(v, 0.0f, 2.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_drag(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_drag = clampf(v, 0.0f, 2.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_gravity(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_gravity = clampf(v, 0.0f, 2.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_menu_save(int id) {
    (void)id;
    printf("UI debug: spring menu save pressed\n");
    if (spring_save_fn) spring_save_fn();
    spring_menu_close_pending = 1;
    ui_close_request = 1;
}

static void on_spring_menu_close(int id) {
    (void)id;
    printf("UI debug: spring menu close pressed (menu_id=%d)\n", spring_menu_id);
    spring_menu_close_pending = 1;
    ui_close_request = 1;
}

static void on_pie_select(int idx) {
    switch (idx) {
        case UI_PIE_ADJUST_SPRINGS:
            printf("UI pie selection: Adjust Springs\n");
            springs_menu();
            break;
        case UI_PIE_ADJUST_AXIS:
            printf("UI pie selection: Adjust Axis\n");
            break;
        case UI_PIE_CLOSE:
            printf("UI pie selection: Close\n");
            ui_close_request = 1;
            break;
        case UI_PIE_STATS:
            printf("UI pie selection: stats\n");
            break;
        case UI_PIE_LOAD_MODEL:
            printf("UI pie selection: load Model\n");
            break;
        case UI_PIE_ANIMATION:
            printf("UI pie selection: Animation\n");
            break;
        case UI_PIE_POSITION:
            printf("UI pie selection: Position\n");
            break;
        default:
            printf("UI pie selection: %d\n", idx);
            break;
    }
}

static void springs_menu(void) {
    if (!debug_ui) return;
    if (spring_menu_id >= 0) return;

    ui_header_t header = {.w = 0, .h = 0, .state = 1, .title = "Spring Constants"};
    spring_menu_id = ui_group_start(debug_ui, &header);
    printf("UI debug: springs menu opened (menu_id=%d)\n", spring_menu_id);

    ui_set_element_flags(debug_ui, (uint32_t)spring_menu_id, UI_MOVABLE);
    ui_position_element(debug_ui, (uint32_t)spring_menu_id, 50, 50);
    ui_set_align(debug_ui, UI_BELOW);

    ui_parameter_t stiffness = {
        .w = 220.0f,
        .value = g_spring_stiffness,
        .start_val = 0.0f,
        .end_val = 2.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Stiffness"
    };
    spring_param_ids[SPRING_PARAM_STIFFNESS] = (uint32_t)ui_element_init(debug_ui, &stiffness, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_STIFFNESS], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_STIFFNESS], on_spring_stiffness);

    ui_parameter_t drag = {
        .w = 220.0f,
        .value = g_spring_drag,
        .start_val = 0.0f,
        .end_val = 2.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Drag"
    };
    spring_param_ids[SPRING_PARAM_DRAG] = (uint32_t)ui_element_init(debug_ui, &drag, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_DRAG], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_DRAG], on_spring_drag);

    ui_parameter_t gravity = {
        .w = 220.0f,
        .value = g_spring_gravity,
        .start_val = 0.0f,
        .end_val = 2.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Gravity"
    };
    spring_param_ids[SPRING_PARAM_GRAVITY] = (uint32_t)ui_element_init(debug_ui, &gravity, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_GRAVITY], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_GRAVITY], on_spring_gravity);

    ui_button_t save_btn = {.str = "Save", .w = 0, .h = 0, .state = 0};
    uint32_t save_id = (uint32_t)ui_element_init(debug_ui, &save_btn, 0, UI_BUTTON_ROUND);
    ui_set_element_flags(debug_ui, save_id, UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, save_id, on_spring_menu_save);
    ui_set_align(debug_ui, UI_RIGHT);
    ui_button_t close_btn = {.str = "Close", .w = 0, .h = 0, .state = 0};
    uint32_t close_id = (uint32_t)ui_element_init(debug_ui, &close_btn, 0, UI_BUTTON_ROUND);
    ui_set_element_flags(debug_ui, close_id, UI_ACTION_ON_RELEASE);
    ui_set_callback(debug_ui, close_id, on_spring_menu_close);

    ui_group_stop(debug_ui);
    spring_close_guard = 1;
}


int32_t debug_ui_build_pie_menu(ui_interface_t *ui, int32_t pie_menu_id, float radius) {
    if (!ui) return pie_menu_id;
    if (pie_menu_id >= 0) return pie_menu_id;

    debug_ui = ui;
    // Setting width and height to 0 auto computes the correct sizes
    int button_w = 0, button_h = 0;

    element_data items[UI_PIE_MAX_ELEMENT];
    for (int i = 0; i < UI_PIE_MAX_ELEMENT; i++) {
        items[i].b = (ui_button_t){.str = (char *)ui_pie_menu_labels[i], .w = button_w, .h = button_h, .state = 0};
    }

    ui_pie_menu_t pie = { .type = UI_BUTTON_ROUND, .count = UI_PIE_MAX_ELEMENT, .radius = radius, .elements = items };
    pie_menu_id = ui_element_init(ui, &pie, 0, UI_PIE_MENU);
    ui_set_callback(ui, pie_menu_id, on_pie_select);
    printf("UI debug: pie menu created (id=%d)\n", pie_menu_id);
    return pie_menu_id;
}
