#include <math.h>
#include <stdio.h>

#include "debug_ui.h"
#include "animator.h"
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
    SPRING_PARAM_CLOTH_MODE,
    SPRING_PARAM_CLOTH_STIFFNESS,
    SPRING_PARAM_CLOTH_DRAG,
    SPRING_PARAM_CLOTH_GRAVITY,
    SPRING_PARAM_CLOTH_TAPER,
    SPRING_PARAM_COLLISION_DEBUG,
    SPRING_PARAM_MAX_ELEMENT
} SpringParamAction;

typedef enum {
    ANIM_PARAM_BLEND_TIME = 0,
    ANIM_PARAM_GROUP_ROOT,
    ANIM_PARAM_GROUP_SPINE,
    ANIM_PARAM_GROUP_HEAD,
    ANIM_PARAM_GROUP_ARMS,
    ANIM_PARAM_GROUP_HANDS,
    ANIM_PARAM_GROUP_LEGS,
    ANIM_PARAM_GROUP_FEET,
    ANIM_PARAM_GROUP_FACE,
    ANIM_PARAM_GROUP_OTHER,
    ANIM_PARAM_MAX_ELEMENT
} AnimParamAction;

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
static int32_t anim_menu_id = -1;
static uint32_t anim_param_ids[ANIM_PARAM_MAX_ELEMENT] = {0};
static int spring_close_guard = 0;
static int anim_close_guard = 0;
static int ui_close_request = 0;
static int ui_force_close_request = 0;
static int spring_menu_close_pending = 0;
static int anim_menu_close_pending = 0;

extern float g_spring_stiffness;
extern float g_spring_drag;
extern float g_spring_gravity;
extern int g_spring_cloth_mode;
extern float g_spring_cloth_stiffness;
extern float g_spring_cloth_drag;
extern float g_spring_cloth_gravity;
extern float g_spring_cloth_taper;
extern int g_spring_debug_collisions;

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
    int guard = spring_close_guard || anim_close_guard;
    spring_close_guard = 0;
    anim_close_guard = 0;
    return guard;
}

int debug_ui_consume_close_request(void) {
    int request = ui_close_request;
    ui_close_request = 0;
    return request;
}

int debug_ui_consume_force_close_request(void) {
    int request = ui_force_close_request;
    ui_force_close_request = 0;
    return request;
}

int debug_ui_has_open_panels(void) {
    return spring_menu_id >= 0 || anim_menu_id >= 0;
}

int debug_ui_get_panel_ids(int32_t *ids, int max_ids) {
    int count = 0;
    if (!ids || max_ids <= 0) return 0;
    if (spring_menu_id >= 0 && count < max_ids) {
        ids[count++] = spring_menu_id;
    }
    if (anim_menu_id >= 0 && count < max_ids) {
        ids[count++] = anim_menu_id;
    }
    return count;
}

void debug_ui_prepare_for_close(void) {
    if (spring_menu_close_pending) {
        spring_menu_close_pending = 0;
        if (debug_ui && spring_menu_id >= 0) {
            ui_element_delete(debug_ui, (uint32_t)spring_menu_id);
            spring_menu_id = -1;
        }
        spring_menu_id = -1;
        for (int i = 0; i < SPRING_PARAM_MAX_ELEMENT; i++) {
            spring_param_ids[i] = 0;
        }
    }
    if (anim_menu_close_pending) {
        anim_menu_close_pending = 0;
        if (debug_ui && anim_menu_id >= 0) {
            ui_element_delete(debug_ui, (uint32_t)anim_menu_id);
            anim_menu_id = -1;
        }
        //anim_menu_id = -1;
        for (int i = 0; i < ANIM_PARAM_MAX_ELEMENT; i++) {
            anim_param_ids[i] = 0;
        }
    }
}

void debug_ui_reset_menus(void) {
    spring_menu_close_pending = 0;
    anim_menu_close_pending = 0;
    ui_close_request = 0;
    ui_force_close_request = 0;
    spring_close_guard = 0;
    anim_close_guard = 0;
    if (debug_ui && spring_menu_id >= 0) {
        ui_element_delete(debug_ui, (uint32_t)spring_menu_id);
        spring_menu_id = -1;
    }
    spring_menu_id = -1;
    for (int i = 0; i < SPRING_PARAM_MAX_ELEMENT; i++) {
        spring_param_ids[i] = 0;
    }
    if (debug_ui && anim_menu_id >= 0) {
        ui_element_delete(debug_ui, (uint32_t)anim_menu_id);
        anim_menu_id = -1;
    }
    anim_menu_id = -1;
    for (int i = 0; i < ANIM_PARAM_MAX_ELEMENT; i++) {
        anim_param_ids[i] = 0;
    }
}

void debug_ui_sync_spring_params(void) {
    if (!debug_ui || spring_menu_id < 0) return;
    if (spring_param_ids[SPRING_PARAM_STIFFNESS]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_STIFFNESS], g_spring_stiffness);
    }
    if (spring_param_ids[SPRING_PARAM_DRAG]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_DRAG], g_spring_drag);
    }
    if (spring_param_ids[SPRING_PARAM_GRAVITY]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_GRAVITY], g_spring_gravity);
    }
    if (spring_param_ids[SPRING_PARAM_CLOTH_MODE]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_MODE], (float)g_spring_cloth_mode);
    }
    if (spring_param_ids[SPRING_PARAM_CLOTH_STIFFNESS]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_STIFFNESS], g_spring_cloth_stiffness);
    }
    if (spring_param_ids[SPRING_PARAM_CLOTH_DRAG]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_DRAG], g_spring_cloth_drag);
    }
    if (spring_param_ids[SPRING_PARAM_CLOTH_GRAVITY]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_GRAVITY], g_spring_cloth_gravity);
    }
    if (spring_param_ids[SPRING_PARAM_CLOTH_TAPER]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_TAPER], g_spring_cloth_taper);
    }
    if (spring_param_ids[SPRING_PARAM_COLLISION_DEBUG]) {
        ui_write_parameter(debug_ui, spring_param_ids[SPRING_PARAM_COLLISION_DEBUG], (float)g_spring_debug_collisions);
    }
}

void debug_ui_sync_anim_params(void) {
    if (!debug_ui || anim_menu_id < 0) return;
    if (anim_param_ids[ANIM_PARAM_BLEND_TIME]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_BLEND_TIME], g_anim_blend_time_default);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_ROOT]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ROOT], g_anim_blend_group_factors[BLEND_GROUP_ROOT]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_SPINE]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_SPINE], g_anim_blend_group_factors[BLEND_GROUP_SPINE]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_HEAD]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HEAD], g_anim_blend_group_factors[BLEND_GROUP_HEAD]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_ARMS]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ARMS], g_anim_blend_group_factors[BLEND_GROUP_ARMS]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_HANDS]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HANDS], g_anim_blend_group_factors[BLEND_GROUP_HANDS]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_LEGS]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_LEGS], g_anim_blend_group_factors[BLEND_GROUP_LEGS]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_FEET]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FEET], g_anim_blend_group_factors[BLEND_GROUP_FEET]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_FACE]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FACE], g_anim_blend_group_factors[BLEND_GROUP_FACE]);
    }
    if (anim_param_ids[ANIM_PARAM_GROUP_OTHER]) {
        ui_write_parameter(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_OTHER], g_anim_blend_group_factors[BLEND_GROUP_OTHER]);
    }
}

static void springs_menu(void);
static void animation_menu(void);

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

static void on_spring_cloth_mode(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_cloth_mode = v >= 0.5f ? 1 : 0;
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_cloth_stiffness(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_cloth_stiffness = clampf(v, 0.0f, 1.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_cloth_drag(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_cloth_drag = clampf(v, 0.0f, 3.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_cloth_gravity(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_cloth_gravity = clampf(v, 0.0f, 2.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_cloth_taper(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_cloth_taper = clampf(v, 0.0f, 1.0f);
    if (spring_reset_fn) spring_reset_fn();
}

static void on_spring_collision_debug(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_spring_debug_collisions = v >= 0.5f ? 1 : 0;
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

static void on_anim_blend_time(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_time_default = clampf(v, 0.05f, 3.0f);
}

static void on_anim_group_root(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_ROOT] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_spine(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_SPINE] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_head(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_HEAD] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_arms(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_ARMS] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_hands(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_HANDS] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_legs(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_LEGS] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_feet(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_FEET] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_face(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_FACE] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_group_other(int id) {
    if (!debug_ui) return;
    float v = ui_read_parameter(debug_ui, (uint32_t)id);
    g_anim_blend_group_factors[BLEND_GROUP_OTHER] = clampf(v, 0.1f, 1.0f);
}

static void on_anim_menu_close(int id) {
    (void)id;
    printf("UI debug: animation menu close pressed (menu_id=%d)\n", anim_menu_id);
    anim_menu_close_pending = 1;
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
            ui_force_close_request = 1;
            break;
        case UI_PIE_STATS:
            printf("UI pie selection: stats\n");
            break;
        case UI_PIE_LOAD_MODEL:
            printf("UI pie selection: load Model\n");
            break;
        case UI_PIE_ANIMATION:
            printf("UI pie selection: Animation\n");
            animation_menu();
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
    ui_set_element_flags(debug_ui, (uint32_t)spring_menu_id, UI_MOVABLE);
    printf("UI debug: springs menu opened (menu_id=%d)\n", spring_menu_id);

    ui_position_element(debug_ui, (uint32_t)spring_menu_id, 50, 50);
    ui_set_align(debug_ui, UI_BELOW);

    ui_parameter_t stiffness = {
        .w = 440.0f,
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
        .w = 440.0f,
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
        .w = 440.0f,
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

    ui_parameter_t cloth_mode = {
        .w = 440.0f,
        .value = (float)g_spring_cloth_mode,
        .start_val = 0.0f,
        .end_val = 1.0f,
        .step_size = 1.0f,
        .steps_per_pixel = 1.0f,
        .title = "Cloth Mode"
    };
    spring_param_ids[SPRING_PARAM_CLOTH_MODE] = (uint32_t)ui_element_init(debug_ui, &cloth_mode, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_MODE], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_MODE], on_spring_cloth_mode);

    ui_parameter_t cloth_stiffness = {
        .w = 440.0f,
        .value = g_spring_cloth_stiffness,
        .start_val = 0.0f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Cloth Stiffness"
    };
    spring_param_ids[SPRING_PARAM_CLOTH_STIFFNESS] = (uint32_t)ui_element_init(debug_ui, &cloth_stiffness, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_STIFFNESS], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_STIFFNESS], on_spring_cloth_stiffness);

    ui_parameter_t cloth_drag = {
        .w = 440.0f,
        .value = g_spring_cloth_drag,
        .start_val = 0.0f,
        .end_val = 3.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Cloth Drag"
    };
    spring_param_ids[SPRING_PARAM_CLOTH_DRAG] = (uint32_t)ui_element_init(debug_ui, &cloth_drag, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_DRAG], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_DRAG], on_spring_cloth_drag);

    ui_parameter_t cloth_gravity = {
        .w = 440.0f,
        .value = g_spring_cloth_gravity,
        .start_val = 0.0f,
        .end_val = 2.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Cloth Gravity"
    };
    spring_param_ids[SPRING_PARAM_CLOTH_GRAVITY] = (uint32_t)ui_element_init(debug_ui, &cloth_gravity, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_GRAVITY], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_GRAVITY], on_spring_cloth_gravity);

    ui_parameter_t cloth_taper = {
        .w = 440.0f,
        .value = g_spring_cloth_taper,
        .start_val = 0.0f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Cloth Taper"
    };
    spring_param_ids[SPRING_PARAM_CLOTH_TAPER] = (uint32_t)ui_element_init(debug_ui, &cloth_taper, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_TAPER], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_CLOTH_TAPER], on_spring_cloth_taper);

    ui_parameter_t collision_debug = {
        .w = 440.0f,
        .value = (float)g_spring_debug_collisions,
        .start_val = 0.0f,
        .end_val = 1.0f,
        .step_size = 1.0f,
        .steps_per_pixel = 1.0f,
        .title = "Collision Debug"
    };
    spring_param_ids[SPRING_PARAM_COLLISION_DEBUG] = (uint32_t)ui_element_init(debug_ui, &collision_debug, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, spring_param_ids[SPRING_PARAM_COLLISION_DEBUG], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, spring_param_ids[SPRING_PARAM_COLLISION_DEBUG], on_spring_collision_debug);


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

static void animation_menu(void) {
    if (!debug_ui) return;
    if (anim_menu_id >= 0) return;

    ui_header_t header = {.w = 0, .h = 0, .state = 1, .title = "Animation Blend"};
    anim_menu_id = ui_group_start(debug_ui, &header);
    ui_set_element_flags(debug_ui, (uint32_t)anim_menu_id, UI_MOVABLE);
    printf("UI debug: animation menu opened (menu_id=%d)\n", anim_menu_id);

    ui_position_element(debug_ui, (uint32_t)anim_menu_id, 50, 50);
    ui_set_align(debug_ui, UI_BELOW);

    ui_parameter_t blend_time = {
        .w = 440.0f,
        .value = g_anim_blend_time_default,
        .start_val = 0.05f,
        .end_val = 3.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Blend Time (sec)"
    };
    anim_param_ids[ANIM_PARAM_BLEND_TIME] = (uint32_t)ui_element_init(debug_ui, &blend_time, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_BLEND_TIME], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_BLEND_TIME], on_anim_blend_time);

    ui_parameter_t group_root = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_ROOT],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Root Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_ROOT] = (uint32_t)ui_element_init(debug_ui, &group_root, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ROOT], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ROOT], on_anim_group_root);

    ui_parameter_t group_spine = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_SPINE],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Spine Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_SPINE] = (uint32_t)ui_element_init(debug_ui, &group_spine, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_SPINE], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_SPINE], on_anim_group_spine);

    ui_parameter_t group_head = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_HEAD],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Head Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_HEAD] = (uint32_t)ui_element_init(debug_ui, &group_head, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HEAD], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HEAD], on_anim_group_head);

    ui_parameter_t group_arms = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_ARMS],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Arms Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_ARMS] = (uint32_t)ui_element_init(debug_ui, &group_arms, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ARMS], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_ARMS], on_anim_group_arms);

    ui_parameter_t group_hands = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_HANDS],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Hands Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_HANDS] = (uint32_t)ui_element_init(debug_ui, &group_hands, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HANDS], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_HANDS], on_anim_group_hands);

    ui_parameter_t group_legs = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_LEGS],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Legs Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_LEGS] = (uint32_t)ui_element_init(debug_ui, &group_legs, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_LEGS], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_LEGS], on_anim_group_legs);

    ui_parameter_t group_feet = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_FEET],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Feet Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_FEET] = (uint32_t)ui_element_init(debug_ui, &group_feet, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FEET], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FEET], on_anim_group_feet);

    ui_parameter_t group_face = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_FACE],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Face Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_FACE] = (uint32_t)ui_element_init(debug_ui, &group_face, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FACE], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_FACE], on_anim_group_face);

    ui_parameter_t group_other = {
        .w = 440.0f,
        .value = g_anim_blend_group_factors[BLEND_GROUP_OTHER],
        .start_val = 0.1f,
        .end_val = 1.0f,
        .step_size = 0.05f,
        .steps_per_pixel = 0.2f,
        .title = "Other Blend Factor"
    };
    anim_param_ids[ANIM_PARAM_GROUP_OTHER] = (uint32_t)ui_element_init(debug_ui, &group_other, 0, UI_PARAMETER);
    ui_set_element_flags(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_OTHER], UI_ACTION_ON_PRESS);
    ui_set_callback(debug_ui, anim_param_ids[ANIM_PARAM_GROUP_OTHER], on_anim_group_other);

    ui_button_t close_btn = {.str = "Close", .w = 0, .h = 0, .state = 0};
    uint32_t close_id = (uint32_t)ui_element_init(debug_ui, &close_btn, 0, UI_BUTTON_ROUND);
    ui_set_element_flags(debug_ui, close_id, UI_ACTION_ON_RELEASE);
    ui_set_callback(debug_ui, close_id, on_anim_menu_close);

    ui_group_stop(debug_ui);
    anim_close_guard = 1;
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
