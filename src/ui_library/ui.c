// #define MEMORY_DEBUG 
// #define DEBUG_CONTENT_REGION	
// #define DEBUG_ALIGNMENT
// #define DEBUG_BOUNDS
// #define LOG_ELEMENTS
// #define DEBUG_DELETING
// #define DEBUG_NODES
// #define WRITE_TEXTURES
// #define DEBUG_MENUS

// BUG LIST
// need to create an interface for rendering and events.
// use pbm file format for image writing etc
// make this lib thread safe?
// need to control access to global states and static vars
#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
// #define _POSIX_C_SOURCE
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <epoxy/gl.h>
#define UI_EXTERNS
#include "ui.h"
#include "import.h"
//#include "main.h"
// todo remove this dependancy
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef TRUETYPE_SUPPORT
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#endif

#ifdef MEMORY_DEBUG
#define STB_LEAKCHECK_IMPLEMENTATION
#include "./stb_leakcheck.h"
#endif

// float GEOM_UV = 3/255.0; // hack so texture zoordinate hits white on the text texture
float GEOM_UV = 1.0f/(float)FONT_TEXTURE_W; // hack so texture zoordinate hits white on the text texture
#define LINE_ARC_VERTS(x) 6 * x 
#define CIRCLE_SEG_VERTS(x) 3 * x
#define RECTANGLE_VERTS 6
#define RECT_BORDERED_VERTS 30
#define RRECT_BORDERED_VERTS (CIRCLE_SEG_VERTS(SEGMENTS) + LINE_ARC_VERTS(SEGMENTS) + 18)
#define TRIANGLE_VERTS 3 
#define VALUE_VERTS RECTANGLE_VERTS * 32
#define HEXAGON_VERTS CIRCLE_SEG_VERTS(6)
#define LIST_BOX_VERTS (RRECT_BORDERED_VERTS + RECTANGLE_VERTS + TRIANGLE_VERTS)
#define LIST_BOX_CONTENT_VERTS (RECTANGLE_VERTS + RECT_BORDERED_VERTS)
// #define LIST_BOX_DYNAMIC_OFFSET (RRECT_BORDERED_VERTS + RECTANGLE_VERTS + TRIANGLE_VERTS)

#define PI 3.141592653
#define RED_BYTE(w) ((uint8_t) ((w) & 0xff))
#define GREEN_BYTE(w) ((uint8_t) ((w) >> 8))
#define BLUE_BYTE(w) ((uint8_t) ((w) >> 16))
#define ALPHA_BYTE(w) ((uint8_t) ((w) >> 24))
#define DEG_TO_RAD(ang) (ang * PI / 180.0)
#define RAD_TO_DEG(ang) (ang * 180.0 / PI)
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define CLAMP(x, upper, lower) (MIN(upper, MAX(x, lower)))
#define UI_LERP(b,a,t) ((a) + (t) * (float) ((b)-(a)))

#define ROUND(a) (int) (a+0.5)
#define FILE_BUF_SIZE 65536
#define FILE_FORMAT '1'
char file_version[8] = {'u', 'i', 'd', 'a', 't', 'a', FILE_FORMAT, '\0'};

// UI action flags 
enum {	LEFT_HELD = UI_FLAG(0),
		MIDDLE_HELD = UI_FLAG(1)
};
// Colour picker action states
enum {	
		ALPHA	 = UI_FLAG(29),
		SV_BOX   = UI_FLAG(30),
		HUE_RING = UI_FLAG(31),
};
enum {	GEOM_MODIFIED = UI_FLAG(0),
		TEXT_MODIFIED = UI_FLAG(1),
		USER_MODIFIED = UI_FLAG(2),
		GEOM_SIZE_MODIFIED = UI_FLAG(3),
};

// collision detection for moving windows optional
// very basic physics, make windows throwable/ use a gravity direction to spawn and dynamically place elements
// tabbed and grouped elements should inherit their parents flags
// functions to match/constrain dimensions relative to other elements
// add menu bar element ideally can take all other e as sub elements

// can add some icons etc to this aswell for a toolbar element/ pie menu
// button highlight fades after deactivating
// add function to let user set parent id
// make ui-> buffer growable
// text input
// add frame var for ele, and lerp for animations
// rolling waveform
// bezier curves splines
// Lissajous patterns
// could create an ordered list of ui positions in x and y space to give much faster collision tests

typedef struct {
    float x;
    float y;
} v2;

typedef struct {
	int x;
	int y;
	int width;
	int height;
	int16_t xoffset;
	int16_t yoffset;
	int16_t xadvance;
} glyph_t;

typedef struct {
	glyph_t *glyphs;
	uint16_t num_glyphs;
	uint16_t size;
	int16_t ascent;
	int16_t descent;
	int16_t line_gap;
	int16_t line_height;
	int16_t base_height;
	int16_t texture_height;
	int16_t texture_width;
} font_t;

struct ui_interface_t {
	font_t font[MAX_FONTS];
	uint32_t uid_source;
	uint32_t vao[2];
	uint32_t vbo[2];
	uint32_t shader;
	uint32_t uniform;
	uint32_t texture_id;
	uint32_t user_texture_id;
	uint32_t num_verts;
	uint32_t num_dynamic_verts;
	uint32_t num_text;
 	uint32_t active_elements;
 	uint32_t selected_element;
 	uint32_t modified;
	uint32_t active_group;
	uint32_t active_container;
	uint32_t current_align;
	uint32_t active_font;
	uint32_t action_flags;
	ui_theme_t theme;
	ui_element_t *elements;
	ui_vertex_t *buffer;					// This buffer is only updated when necessary
	ui_vertex_t *dynamic_buffer;			// This buffer is cleared every frame
	int (*action)(ui_interface_t *ui, uint32_t id);
};


typedef struct {
	uint32_t next;
	uint32_t size;
	uint8_t data[];
} ui_buffer_t;

// font_t font[MAX_FONTS];

static int ui_line_strip_create(ui_vertex_t *buf, v2 *p, uint16_t num_points, uint32_t col, float width);
static int ui_line_strip_create_aa(ui_vertex_t *buf, v2 *p, uint16_t num_points, uint32_t col, float width);
static int ui_rect_bordered_create(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col, uint32_t border_col, uint32_t border_size);
static void ui_rect_bordered_set_pos(ui_vertex_t *buf, float x, float y);
static inline int ui_rect_bordered_get_width(ui_vertex_t *b);
static inline int ui_rect_bordered_get_height(ui_vertex_t *b);
static inline int ui_rect_bordered_get_x(ui_vertex_t *b);
static inline int ui_rect_bordered_get_y(ui_vertex_t *b);
static int ui_rectangle_create(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col);
static inline int ui_rectangle_get_width(ui_vertex_t *b);
static inline int ui_rectangle_get_height(ui_vertex_t *b);
static inline int ui_rectangle_get_x(ui_vertex_t *b);
static inline int ui_rectangle_get_y(ui_vertex_t *b);
static int ui_rrect_bordered_gradient(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col1, uint32_t col2, uint32_t border_col, float border_size);
static int ui_rrect_bordered(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col, uint32_t border_col, float border_size);
static int ui_circle_seg_create(ui_vertex_t *buf, float x, float y, float start_angle, float end_angle, float mag, int segments, uint32_t col);
static inline int ui_hexagon_create(ui_vertex_t *buf, float x, float y, float radius, uint32_t col);
static int ui_line_arc_create(ui_line_t *buf, float x, float y, float start_angle, float end_angle, float mag, uint32_t col, uint16_t seg);
static inline void ui_make_quad(ui_vertex_t *buf, v2 vert1, v2 vert2, v2 vert3, v2 vert4);
static inline int ui_make_triangle(ui_vertex_t *buf, v2 vert1, v2 vert2, v2 vert3, uint32_t col);
static int ui_line_arc_create_wide(ui_vertex_t *buf, float x, float y, float start_angle, float end_angle, float mag, uint32_t col, uint16_t seg, float width);
static int ui_lines_create(ui_vertex_t *buf, ui_line_t *lines, uint32_t count, uint32_t width);

static inline void ui_colour_verts(ui_vertex_t *buf, uint32_t count, uint32_t colour);
static int ui_text_create(ui_vertex_t *v, const char *str, font_t *f, float x, float y, float w, float h, uint32_t col, uint16_t align);

static void ui_rect_create(ui_interface_t *ui, uint16_t id, ui_rect_t *r);
static void ui_button_create(ui_interface_t *ui, uint16_t id, ui_button_t *data);
static void ui_button_col_swap(ui_interface_t *ui, uint16_t id, uint16_t state);
static void ui_element_dectivate(ui_interface_t *ui, uint16_t id);
static void ui_element_activate(ui_interface_t *ui, uint16_t id);
static void ui_checkbox_create(ui_interface_t *ui, uint16_t id, ui_checkbox_t *c);
static void ui_checkbox_col_swap(ui_interface_t *ui, uint16_t id);
static void ui_label_create(ui_interface_t *ui, uint16_t id, ui_label_t *t);
static void ui_slider_create(ui_interface_t *ui, uint16_t id, ui_slider_t *s);
static void ui_set_slider_value2(ui_interface_t *ui, uint16_t id, float value);
static void ui_set_slider_value(ui_interface_t *ui, uint16_t id);
static void ui_menu_create(ui_interface_t *ui, uint16_t id, ui_menu_t *t);
static void ui_menu_update(ui_interface_t *ui, uint16_t id);
static void ui_menu_highlight(ui_interface_t *ui, uint16_t id, uint16_t item);
static void ui_menu_unhighlight(ui_interface_t *ui, uint16_t id);
static void ui_menu_toggle(ui_interface_t *ui, uint16_t id);
static void ui_menu_actions(ui_interface_t *ui, uint16_t id);
static void ui_group_header_create(ui_interface_t *ui, uint16_t id, ui_header_t *h);
static void ui_group_header_finalise(ui_interface_t *ui);
static void ui_group_header_toggle(ui_interface_t *ui, uint16_t id);
static inline int32_t ui_group_get_header_id(ui_interface_t *ui, uint16_t id);
static void ui_align_position(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_create(ui_interface_t *ui, uint16_t id, ui_colour_picker_t *cp);
static void ui_colour_picker_recolour(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_actions(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_update(ui_interface_t *ui, uint16_t id, int32_t offset);
static void ui_colour_picker_set_alpha_mark(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_set_alpha(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_set_preset(ui_interface_t *ui, uint16_t id, uint16_t preset);
static void ui_colour_picker_colour_preset(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_set_sv_mark(ui_interface_t *ui, uint16_t id);
static void ui_colour_picker_colour_alpha(ui_interface_t *ui, uint16_t id);
static void ui_parameter_create(ui_interface_t *ui, uint16_t id, ui_parameter_t *p);
static void ui_parameter_actions(ui_interface_t *ui, uint16_t id);
static void ui_parameter_set(ui_interface_t *ui, uint16_t id, int16_t dy);
static void ui_tab_header_create(ui_interface_t *ui, uint16_t id, ui_tab_header_t *th);
static void ui_tabs_finalise(ui_interface_t *ui, uint16_t id);
static void ui_tab_switch(ui_interface_t *ui, ui_element_t *e);
static void ui_tab_actions(ui_interface_t *ui, uint16_t id);
static void ui_finalise_container(ui_interface_t *ui);
static void ui_parameter_str_create(ui_interface_t *ui, uint16_t id, ui_parameter_str_t *p);
static void ui_parameter_str_actions(ui_interface_t *ui, uint16_t id);
static void ui_position_element_internal(ui_interface_t *ui, uint16_t id, int32_t x, int32_t y);
static void ui_seven_segment_display_create(ui_interface_t *ui, uint16_t id, ui_segment_display_t *s);
static int ui_make_seven_segment(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col);
static void ui_seven_segment_display_char(ui_vertex_t *buf, uint32_t col_on, uint32_t col_off, char c);
static inline void ui_light_segment(ui_vertex_t *buf, int start_vertex, uint32_t col);
static void ui_node_create(ui_interface_t *ui, uint16_t id, ui_node_t *n);
static void ui_node_link_create(ui_interface_t *ui, uint16_t id, ui_node_link_t *l);
static void ui_node_link_update(ui_interface_t *ui, uint32_t id);
static void ui_node_link_finalise(ui_interface_t *ui, uint32_t id);
static void ui_delete_link(ui_interface_t *ui, uint32_t id);
static int ui_draw_node_link(ui_interface_t *ui, ui_vertex_t *buf, ui_node_link_t *l);
static void ui_node_move(ui_interface_t *ui, uint32_t id, int offset_x, int offset_y);
static void ui_print_all_links(ui_interface_t *ui);
static int ui_pie_menu_activate(ui_interface_t *ui, uint32_t id);
static void ui_pie_menu_create(ui_interface_t *ui, uint32_t id, ui_pie_menu_t *p);
static int ui_pie_menu_finalise(ui_interface_t *ui, uint32_t id);
static void ui_pie_menu_select(ui_interface_t *ui, uint32_t id);
static int ui_pie_menu_animate(ui_interface_t *ui, uint32_t id);
static int ui_menu_open(ui_interface_t *ui, uint32_t id);
static void ui_menu_finalise(ui_interface_t *ui, uint32_t id);
static void ui_menu_label_create(ui_interface_t *ui, uint32_t id, ui_label_t *l);
static void ui_menu_label_highlight(ui_interface_t *ui, uint32_t id);
static void ui_menu_label_unhighlight(ui_interface_t *ui, uint32_t id);
static int ui_menu_monitor(ui_interface_t *ui, uint32_t id);
static int ui_menu_get_toggle_status(ui_interface_t *ui, uint32_t id);
static int ui_list_box_rrect_bordered(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col_l, uint32_t col_r, uint32_t border_col, float border_size);
static void ui_list_box_create(ui_interface_t *ui, uint32_t id, ui_list_box_t *l);
static int ui_list_box_contents(ui_interface_t *ui, uint32_t id);
static int ui_list_box_open(ui_interface_t *ui, uint32_t id);
static int ui_list_box_update(ui_interface_t *ui, uint32_t id);

static void ui_shape_button_create(ui_interface_t *ui, uint16_t id, ui_shape_button_t *c);
static void ui_matrix_create(ui_interface_t *ui, uint16_t id, ui_matrix_t *m);
// Returns the state (off/on) of the button and swaps the relevant vertex colours
static void ui_matrix_swap_colour(ui_vertex_t *buf, ui_matrix_t *m, uint32_t button_id);
static void ui_matrix_actions(ui_interface_t *ui, uint16_t id);
// todo get all functions dependant on opengl or sdl listed here 
// and separate them into an "implementation" section
static void ui_init_glbuffer(ui_interface_t *ui);
int ui_load_shaders(const char *vert_src, const char *frag_src);

static void ui_update_buffer(ui_interface_t *ui);
static void ui_update_dynamic_buffer(ui_interface_t *ui);
static void ui_actions(ui_interface_t *ui);

static void ui_set_alpha(ui_vertex_t *buf, uint32_t count, uint32_t alpha);
static uint32_t ui_rgb_to_hsv(uint32_t rgba);
static uint32_t ui_hsv_to_rgb(uint32_t hsv);
static float ui_get_hue(uint32_t rgba);
static float ui_get_value(uint32_t rgba);
static float ui_get_saturation(uint32_t rgba);
static void ui_get_content_size(ui_element_t *e, int *w, int *h, int tag);
static int ui_get_row_height(ui_interface_t *ui, ui_element_t *e);
static void ui_reflow(ui_interface_t *ui, int *bounds);

static void *ui_buffer_init(uint32_t size);
static void ui_buffer_reserve(void *buffer, uint32_t bytes);
static void ui_buffer_free(void *buffer);
static ui_buffer_t *ui_buffer_get_header(void *buffer);
static void ui_buffer_print(void *ptr);
static void ui_buffer_reset(void *buffer);

static void ui_serialise_interface(ui_interface_t *ui, ui_buffer_t *buffer);
static void ui_serialise_button(ui_buffer_t *buffer, ui_button_t *b);
static void ui_serialise_slider(ui_buffer_t *buffer, ui_slider_t *s);
static void ui_serialise_dial(ui_buffer_t *buffer, ui_dial_t *d);
static void ui_serialise_element(ui_element_t *e, ui_buffer_t *buffer);
static void ui_deserialise_dial(ui_buffer_t *buffer, ui_dial_t *d);
static void ui_deserialise_slider(ui_buffer_t *buffer, ui_slider_t *s);
static void ui_deserialise_button(ui_buffer_t *buffer, ui_button_t *b);
static uint32_t ui_unpack_element(ui_buffer_t *buffer, void *data);

static inline void ui_pack_float(ui_buffer_t *buffer, float f);
static inline void ui_pack_uint32_vector(ui_buffer_t *buffer, uint32_t *u, uint32_t count);
static inline void ui_pack_uint32(ui_buffer_t *buffer, uint32_t u);
static inline void ui_pack_uint16(ui_buffer_t *buffer, uint16_t u);
static inline void ui_pack_string(ui_buffer_t *buffer, char *s);
static inline void ui_unpack_uint32(ui_buffer_t *buffer, uint32_t *data);
static inline void ui_unpack_uint32_vector(ui_buffer_t *buffer, uint32_t *data, uint32_t count);
static inline void ui_unpack_uint16(ui_buffer_t *buffer, uint16_t *data);
static inline void ui_unpack_float(ui_buffer_t *buffer, float *data);
static inline char *ui_unpack_string(ui_buffer_t *buffer);
static char *ui_strdup(char *str);
static inline int ui_strlen(char *str);
static void ui_strpad(char *dst, char *str, int desired_length);
static int ui_str_has_ascenders(char *str);
static int ui_str_has_descenders(char *str);
static void ui_string_size_pixels(const char *str, font_t *f, int *width, int *height);
static void ui_free_string(ui_interface_t *ui, ui_element_t *e);
static inline uint32_t ui_string_vert_count(char *str);
static void ui_val_to_str(int value, char *buf, uint16_t frac_digits);
static void ui_format_value(char *str, float start_val, float end_val, float step, float val);
static void ui_strcat(char *dest, char *src, int dest_len);

static inline v2 ui_normalise_v2(v2 v);
static inline v2 ui_scale_v2(v2 v, float mag);
static inline v2 ui_perp_v2(v2 v);
static inline v2 ui_add_v2(v2 a, v2 b);
static inline v2 ui_sub_v2(v2 a, v2 b);
static int ui_point_in_triangle(v2 a, v2 b, v2 c, v2 p);
static inline float ui_dot_v2(v2 a, v2 b);
static inline float ui_mag_squared_v2(v2 a);
static v2 ui_2line_intersection(v2 p1, v2 p2, v2 p3, v2 p4);
static inline uint32_t ui_elemtent_intersection(int32_t *a, int32_t *b);
static float ui_lerp_log(float start, float end, float t);

static int ui_still_selected(ui_interface_t *ui, uint16_t id);
static void ui_update_active_element(ui_interface_t *ui, uint16_t id);

static void ui_render_waveform_to_element(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count);
// pix_per_quad is the quality of the render, 1 == 1 quad per pixel width for max accuracy
static void ui_render_wavetable_to_element(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count, uint32_t pix_per_quad);
static void ui_render_chart_to_element(ui_interface_t *ui, uint16_t id, float *points, uint32_t count);
static void ui_render_data_linear(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count);
static void ui_render_waveform_linear(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count);
static void ui_render_data_bars(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count);
static void ui_dial_create(ui_interface_t *ui, uint16_t id, ui_dial_t* data);

static void ui_dial_update(ui_interface_t *ui, uint16_t id, ui_dial_t* d);
static void ui_dial_set_value(ui_interface_t *ui, uint16_t id, float dy);

static void ui_get_element_type_as_string(ui_interface_t *ui, uint16_t id, char *type_as_str);
static int ui_find_array_index(ui_interface_t *ui, uint32_t uuid);

static inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a){
    return ((uint32_t)a << 24) | (b << 16) | (g << 8) | r;
}

int32_t ui_element_init(ui_interface_t *ui, void *data, uint16_t count, uint16_t type){

	int (*post_fn)(ui_interface_t *ui, uint32_t id) = NULL;
	int32_t id = ui->active_elements;
	if (id == MAX_UI_ELEMENTS - 1){
		log("ERROR: Not enough free element slots increase \"#define MAX_UI_ELEMENTS\"");
		return EXIT_FAILURE;
	}

	ui_element_t *e = &ui->elements[id];
	e->type = type;
	e->uuid = ui->uid_source++;
	e->array_index = id; // todo use this to simplfy many functions
	e->num_verts = 0;
	e->usage = UI_STATIC_CONTENT;
	e->flags = 0;
	e->font_id = ui->active_font;
	e->action = NULL;
	e->align = ui->current_align;
	/// added !active
	if (ui->active_group && !ui->active_container){
		e->group_tag = ui->active_group;
		// todo could use active parent here much simpler?
		int parent_id;
		if ((e->type != UI_GROUP_END) && (e->type != UI_CONTAINER_END) && (e->type != UI_TABS_END)){
			// ui_log_elements(ui);
			parent_id = ui_group_get_header_id(ui, id);
			e->parent = parent_id;
		}
		// todo i think the movable flag should only be set if 
		// parent container is movable
		// if (ui->elements[parent_id].flags & UI_MOVABLE){
			e->flags |= UI_MOVABLE;
		// }
	}
	if (ui->active_container){
		e->parent = ui->active_container;
		e->flags |= UI_CONTAINER_MEMBER;
		e->group_tag = ui->active_group;	// added
	}

	e->x = 0;
	e->y = 0;
	
	switch (type){

		case UI_RECT:
			ui_rect_create(ui, id, (ui_rect_t *)data);
			break;
		case UI_DIAL:
		case UI_DIAL_HIDE_VAL:
			ui_dial_create(ui, id, (ui_dial_t *)data);
			break;
		case UI_LABEL:
			ui_label_create(ui, id, (ui_label_t *)data);
			break;
		case UI_MENU_LABEL:
			ui_menu_label_create(ui, id, (ui_label_t *)data);
			break;
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
		case UI_BUTTON_RETAIN_STATE:
		case UI_BUTTON_ROUND_RETAIN_STATE:
			ui_button_create(ui, id, (ui_button_t *)data);
			break;
		case UI_SHAPE_BUTTON:
			ui_shape_button_create(ui, id, (ui_shape_button_t *)data);
			break;
		case UI_MATRIX:
			ui_matrix_create(ui, id, (ui_matrix_t *)data);
			break;
		case UI_CHECKBOX:
			ui_checkbox_create(ui, id, (ui_checkbox_t *)data);
			break;
		case UI_SLIDER_V:
		case UI_SLIDER_H:
		case UI_SLIDER_H_HIDE_VAL:
		case UI_SLIDER_V_HIDE_VAL:
			ui_slider_create(ui, id, (ui_slider_t *)data);
			break;
		case UI_MENU:
			ui_menu_create(ui, id, (ui_menu_t *)data);
			break;
		case UI_GROUP_HEADER:
		case UI_GROUP_NO_HEADER:
			ui_group_header_create(ui, id, (ui_header_t *)data);
			break;
		case UI_COLOUR_PICKER:
			ui_colour_picker_create(ui, id, (ui_colour_picker_t *)data);
			break;
		case UI_PARAMETER:
			ui_parameter_create(ui, id, (ui_parameter_t *)data);
			break;
		case UI_PARAMETER_STR:
			ui_parameter_str_create(ui, id, (ui_parameter_str_t *)data);
			break;
		case UI_SEGMENT_DISPLAY:
			ui_seven_segment_display_create(ui, id, (ui_segment_display_t *)data);
			break;
		case UI_TAB_HEADER:
			ui_tab_header_create(ui, id, (ui_tab_header_t *)data);
			break;
		case UI_NODE:
			ui_node_create(ui, id, (ui_node_t *)data);
			break;
		case UI_NODE_LINK:
			ui_node_link_create(ui, id, (ui_node_link_t *)data);
			break;
		case UI_PIE_MENU:
			ui_pie_menu_create(ui, id, (ui_pie_menu_t *)data);
			post_fn = ui_pie_menu_finalise;
			break;
		case UI_LIST_BOX:
			ui_list_box_create(ui, id, (ui_list_box_t *)data);
			post_fn = ui_list_box_contents;
			break;
		case UI_LIST_BOX_CONTENT:
			break;
		case UI_GROUP_END:
			// todo this could contain the bounds of the group it delimits
			{
			e->group_tag = 0;
			e->flags |= UI_HIDDEN;
			e->parent = -1;
			
			int id = ui->active_elements - 1;
			// get the id of the groups parent
			id = ui_group_get_header_id(ui, id);
			ui_element_t *h = &ui->elements[id];
			// needed for alignments
			e->bounds[0] = h->bounds[0];
			e->bounds[1] = h->bounds[1];
			e->bounds[2] = h->bounds[2];
			e->bounds[3] = h->bounds[3];
			e->content_region[0] = h->content_region[0];
			e->content_region[1] = h->content_region[1];
			e->content_region[2] = h->content_region[2];
			e->content_region[3] = h->content_region[3];
			}
			break;
		case UI_TABS_END:
			{
			e->group_tag = 0;
			e->flags |= UI_HIDDEN;
			e->parent = -1;
			
			int id = ui->active_elements - 1;
			// get the id of the groups parent
			//id = ui_group_get_header_id(ui, id);
			ui_element_t *h = &ui->elements[id];
			while (h->type != UI_TAB_HEADER){
				--h;
			}
			e->x = h->x;
			e->y = h->y;
			//printf("finalising tabs\n");
			// needed for alignments
			e->bounds[0] = h->bounds[0];
			e->bounds[1] = h->bounds[1];
			e->bounds[2] = h->bounds[2];
			e->bounds[3] = h->bounds[3];
			e->content_region[0] = h->content_region[0];
			e->content_region[1] = h->content_region[1];
			e->content_region[2] = h->content_region[2];
			e->content_region[3] = h->content_region[3];
			}
			break;
		case UI_CONTAINER_END:
			{
			e->flags |= UI_HIDDEN;
			e->parent = -1;
			int id = ui->elements[ui->active_elements - 1].parent;
			ui_element_t *h = &ui->elements[id];
			e->align = h->align;
			e->bounds[0] = h->bounds[0];
			e->bounds[1] = h->bounds[1];
			e->bounds[2] = h->bounds[2];
			e->bounds[3] = h->bounds[3];
			e->content_region[0] = h->content_region[0];
			e->content_region[1] = h->content_region[1];
			e->content_region[2] = h->content_region[2];
			e->content_region[3] = h->content_region[3];
			}
			break;
		default:
			log("ERROR: Unknown element type");
	}

	if (!(e->x || e->y)){
		ui_align_position(ui, id);
	}
	ui->modified = TEXT_MODIFIED | GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	ui->active_elements += 1;
	if (post_fn){
		int ret_id = post_fn(ui, id);
		return ret_id;
	}
	else {
		return e->uuid;
	}
}

void ui_set_align(ui_interface_t *ui, uint32_t align){

	ui->current_align = align;
}

void ui_set_align_to_element(ui_interface_t *ui, uint32_t align){

	align = ui_find_array_index(ui, align);
	// storing the id in upper 16 bits of align
	ui->current_align = (ui->current_align | 0xFFFF0000) & (0x0000FFFF | (align << 16));
}

void ui_align_position(ui_interface_t *ui, uint16_t id){
	
	ui_element_t *e = &ui->elements[id];
	ui_element_t *target = e - 1;
	int pad = ui->theme.padding_size;
	int x = 0;
	int y = 0;
	// todo finish this func
	// add constraints add screen edge this will be important for lists at right side of screen
	// need some form of screen/ui dimensions in ui struct
	// todo function to align to specified element
	// split this function into align in group, align in tab, align to element, fixed position
	if (id == 0){
		return;
	}

	if (e->type == UI_GROUP_END || e->type == UI_CONTAINER_END || e->type == UI_TABS_END){
		return;
	}
	if (e->type == UI_LIST_BOX_CONTENT){
		x = target->bounds[0];
		y = target->bounds[1];
		goto pos;
	}
	// ignore the hidden list box contents when aligning elements
	if (target->type == UI_LIST_BOX_CONTENT){
		target -= 1;
	}

	ui_element_t *b = ui->elements;
	uint32_t align = e->align;

	// standalone element
	if (e->group_tag == 0 && !ui->active_container){
		#ifdef DEBUG_ALIGNMENT
		log("-----Standalone element-----");
		#endif
		if (align == UI_BELOW){
			x = target->bounds[0];
			y = target->bounds[3] + pad;
		}
		else if (align == UI_RIGHT){
			x = target->bounds[2] + pad * 2;
			y = target->bounds[1];
		}
		else if (align == UI_LEFT){
			int size = e->bounds[2] - e->bounds[0];
			x = target->bounds[0] - pad - size;
			y = target->bounds[1];
		}
		// might be good to have above_centered, below_centered etc
		else if (align == UI_ABOVE){
			int size = e->bounds[3] - e->bounds[1];
			x = target->bounds[0];
			y = target->bounds[1] - pad - size;
		}
		goto pos;
	}

	// element is a root header or tab
	if (e->parent == id){
		#ifdef DEBUG_ALIGNMENT
		log("-----root element-----");
		#endif
		if (align & UI_BELOW){
			if (target->type == UI_GROUP_END || target->type == UI_TABS_END){
				// element above is another group
				// so skip the group end spacer element
				//y = b[(e - 2)->parent].bounds[3] + pad;
				//x = b[(e - 2)->parent].bounds[0];
				x = target->bounds[0];
				y = target->bounds[3] + pad;
			}
			else {
				x = target->bounds[0];
				y = target->bounds[3] + pad;
			}
		}
		else {
			// explicitly positioned header
			x = e->x;
			y = e->y;
		}
		goto pos;
	}
	// align to specified element
	// looks to be using the top half of bits to store an element id
	// todo maybe remove seems complicated!
	if ((align >> 16)){
		// has this been tested anywhere??? todo
		// align to specified element
		int tid = align >> 16;
		ui_element_t *target_id = &ui->elements[tid];
		if (align & UI_BELOW){
			x = target_id->bounds[0];
			y = target_id->bounds[3] + pad;
		}
		else if (align & UI_RIGHT){
			x = target_id->bounds[2] + pad * 2;
			y = target_id->bounds[1];
		}
		else if (align & UI_LEFT){
			int size = target_id->bounds[2] - target_id->bounds[0];
			x = target_id->bounds[0] - pad - size;
			y = target_id->bounds[1];
		}
		// might be good to have above_centered, below_centered etc
		else if (align & UI_ABOVE){
			int size = target_id->bounds[3] - target_id->bounds[1];
			x = target_id->bounds[0];
			y = target_id->bounds[1] - pad - size;
		}
		goto pos;
	}

	// elements are being placed within another element
	if (ui->active_container){
		#ifdef DEBUG_ALIGNMENT
		log("-----inside element-----");
		#endif
		if (ui->active_elements - 1 == ui->active_container){
			// first element in container
			x = target->content_region[0] + pad;
			y = target->content_region[1] + pad;
		}
		else if (align & UI_RIGHT){
			x = target->bounds[2] + pad * 2;
			y = target->bounds[1];
		}
		else if (align & UI_BELOW_LAST){
			x = target->bounds[0];
			y = target->bounds[3] + pad;
		}
		else if (align & UI_BELOW){
			x = ui->elements[ui->active_container].bounds[0] + pad;
			y = target->bounds[3] + pad;
		}
		else if (align & UI_BELOW_LAST_CENTERED){
			// prev x + 1/2 difference between element widths
			x = target->bounds[0] + ((target->bounds[2] - target->bounds[0]) - (e->bounds[2] - e->bounds[0])) / 2;
			y = target->bounds[3] + pad;
		}
		goto pos;
	}

	// Element is a member of a group or tab
	// -------------------------------------------
	int parent_id = ui_group_get_header_id(ui, id);
	int parent_type = ui->elements[parent_id].type;
	if (parent_type == UI_TAB_HEADER){
		// first element in a new tab
		if (e->group_tag != target->group_tag){
			x = ui->elements[parent_id].content_region[0] + pad;
			y = ui->elements[parent_id].content_region[3] + 2 * pad;
			#ifdef DEBUG_ALIGNMENT
			log("-----new tab type-----");
			#endif
			goto pos;
		}

	}
	uint32_t prev_type = target->type;
	if (prev_type == UI_GROUP_HEADER){
		// First item in a group
		#ifdef DEBUG_ALIGNMENT
		log("----first in group type-----");
		#endif
		e->align = UI_BELOW;
		x = target->content_region[0] + pad;
		y = target->content_region[3] + pad;
	}
	else if (prev_type == UI_TAB_HEADER){
		// First item in a tab
		#ifdef DEBUG_ALIGNMENT
		log("-----First item in a tab-----");
		#endif
		e->align = UI_BELOW;
		x = target->content_region[0] + pad;
		y = target->content_region[3] + 2 * pad;
	}
	else if (align & UI_BELOW){
		// Below the previous elements row
		#ifdef DEBUG_ALIGNMENT
		log("-----align below-----");
		#endif
		int row_height = 0;
		row_height = ui_get_row_height(ui, target);
		x = ui->elements[e->parent].bounds[0] + pad;
		// First element after a nested group
		if (e->type != UI_GROUP_HEADER && e->group_tag != target->group_tag){
			y = b[target->parent].bounds[3] + pad;
		}
		else {
			y = target->bounds[1] + row_height + pad;
		}
	}
	else if (align & UI_RIGHT){
		#ifdef DEBUG_ALIGNMENT
		log("-----align right-----");
		#endif
		switch (e->type){
			case UI_RECT:
				x = target->bounds[2] + pad;
				y = target->bounds[1];
				break;
			default:
				x = target->bounds[2] + pad * 2;
				y = target->bounds[1];
				break;
		}
	}
	#ifdef DEBUG_ALIGNMENT
	log("-----no match-----");
	#endif
	pos:
	{
	ui_position_element_internal(ui, id, x, y);
	}
}

static int ui_get_row_height(ui_interface_t *ui, ui_element_t *e){

	int height = 0;
	int row_height = 0;

	do {
		height = e->bounds[3] - e->bounds[1];
		row_height = height > row_height ? height : row_height;
		if (e->type == UI_CONTAINER_END){
			--e;
			while (e->flags & UI_CONTAINER_MEMBER){
				// active container
				--e;
			}
		}
		//log("> row height = %d", height);
	}

	while ((e--)->align & (UI_RIGHT | UI_LEFT));
	
	return row_height;
}

void ui_force_update(ui_interface_t *ui){

	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

// external function for uids
int32_t ui_get_element_id(ui_interface_t *ui, int32_t x, int32_t y){
	
	int32_t *bounds;
	ui_element_t *e = ui->elements;

	for (uint32_t i = 0; i < ui->active_elements; ++i, ++e){
		bounds = e->bounds;
		if (e->flags & UI_HIDDEN){
			// ++e;
			continue;
		}
		if (x > bounds[0] && x < bounds[2]){
			if (y > bounds[1] && y < bounds[3]){
				if (e->type == UI_GROUP_HEADER){
					if (y > e->content_region[3]){
						continue;
					}
				}
				else if (e->type == UI_TAB_HEADER){
					if (y > e->content_region[3]){
						continue;
					}
				}
				log("INFO: Selected element %d", i);
				return e->uuid;
			}
		}
	}
	return -1;
}

// External function not for array indices
int ui_element_delete(ui_interface_t *ui, uint32_t uid){
	
	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid UUID in ui_element_delete uid = %d\n", uid);
		return 0;
	}
	ui_element_t *e = &ui->elements[id];
	int count = 0;	// the number of elements to be removed

	if (e->parent == -1 && e->group_tag == 0){
		// Standalone elemnt with no dependants
		count = 1;
		#ifdef DEBUG_DELETING
			log("delete lone element\n");
		#endif
	}
	else if (e->type == UI_TAB_HEADER){
		// remove until the next UI_TABS_END with a lower tag
		#ifdef DEBUG_DELETING
			log("delete tab group\n");
		#endif
		uint32_t i = 1;
		count = 1;
		while ((e + i)->type != UI_TABS_END && (e + i)->group_tag >= e->group_tag){
			#ifdef DEBUG_DELETING
				printf("type = %d, count = %d, tag = %d\n", (e+i)->type, count, (e+i)->group_tag);
			#endif
			if (i > ui->active_elements - id - 2){
				// if we get here we are at the end of the array
				log("ERROR: Something went wring while trying to delete element %d\n", id);
			}
			++count;
			++i;
		}
		++count;	// include the stop tabs element
	}
	else if (e->type == UI_GROUP_HEADER){
		// delete all subnsequent elements which have a greater or equal group tag
		// Keep track of how many elements we are removing
		#ifdef DEBUG_DELETING
			log("delete group\n");
		#endif
		// nested groups do not have a group end item!
		// need to use the group tag.
		ui_element_t *f = e;
		while (f->group_tag >= e->group_tag){
			++count;
			++f;
		}
		// remove the group end element if it belonged to this group
		if (f->type == UI_GROUP_END && (e->group_tag - f->group_tag == 1)){
			++count;
		}
	}
	else if ((e + 1)->parent == id){
		// In this situation if the element is not a tab header
		// then it must? be a container.
		// iterate until we find a UI_CONTAINER_END
		#ifdef DEBUG_DELETING
			log("delete container\n");
		#endif
		int i = 0;
		if ((e + i)->type != UI_CONTAINER_END){
			log("ERROR: Something went wring while trying to delete element %d\n", id);
			return 0;
		}
		count = 2;	// include the container end element
	}
	else if (e->type == UI_CONTAINER_END || e->type == UI_TABS_END || e->type == UI_GROUP_END){
		// this should never happen
		log("ERROR: Something went wring while trying to delete element %d\n", id);
		return 0;
	}
	else {
		// element must be a simple member of a group or tab
		#ifdef DEBUG_DELETING
			printf("element contained in a group or tabs\n");
		#endif
		count = 1;
		// this is where we might want to reflow a group header or tabs
	}

	for (int i = 0; i < count; ++i){
		ui_free_string(ui, e + i);
		ui_buffer_free((e + i)->index);
	}

	// compute the number of elements after the element being deleted
	int num_to_move = ui->active_elements - id - count;
	memmove(e, e + count, sizeof(ui_element_t) * num_to_move);
	ui->active_elements -= count;

	#ifdef DEBUG_DELETING
		log("deleting %d elements\n", count);
	#endif
	// Fix each parent of the elements which were moved
	// If parent id > lowest elent removed, parent_id -= number deleted
	for (uint32_t i = id; i < ui->active_elements; ++i){
		if (ui->elements[i].parent > id){
			ui->elements[i].parent -= count;
		}
		ui->elements[i].array_index = i;
	}
	// should we attemp a reflow of the ui here? todo
	// what to do if a deleted element had alignment dependancies?
	#ifdef DEBUG_DELETING
		ui_log_elements(ui);
	#endif

	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
	return count;
}

void ui_set_callback(ui_interface_t *ui, uint32_t uid, void(*function)(int)){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_set_callback\n");
		return;
	}
	ui->elements[id].action = function;
}

void ui_set_element_usage(ui_interface_t *ui, uint32_t uid, uint32_t usage){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_set_callback\n");
		return;
	}
	ui->elements[id].usage = usage;
}
// external function for uuids
void ui_add_content_to_element(ui_interface_t *ui, uint32_t uid, void* data, uint32_t count, uint16_t type){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_add_content_to_element\n");
		return;
	}
	// also add ui_vertex_data as enum option here
	
	switch (type){
		case UI_WAVETABLE_DISP:
			ui_render_wavetable_to_element(ui, id, (float *)data, count, 3);
			break;
		case UI_WAVEFORM_DISP:
			ui_render_waveform_to_element(ui, id, (float *)data, count);
			break;
		case UI_WAVEFORM_LINEAR:
			ui_render_waveform_linear(ui, id, (float *)data, count);
			break;
		case UI_IMAGE_DISP:
			// todo
			break;
		case UI_TEXT:
			// ui_render_text(ui, id, char **lines, num_lines);
			break;
		case UI_LINE_CHART:
			ui_render_data_linear(ui, id, (float *)data, count);
			break;
		case UI_SPECTROGRAM:
			// todo ??
			break;
		case UI_FILLED_CHART:
			ui_render_data_bars(ui, id, (float *)data, count);
			break;
		default:
			printf("ERROR: Unknown case in ui_add_content\n");
			break;
	}
}

// External function not for array indices
void ui_set_element_flags(ui_interface_t *ui, uint32_t uid, uint16_t flags){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_set_element_flags\n");
		return;
	}
	ui->elements[id].flags |= flags;
}

// External function not for array indices
void ui_unset_element_flags(ui_interface_t *ui, uint32_t uid, uint16_t flags){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_unset_element_flags\n");
		return;
	}
	ui->elements[id].flags ^= flags;
}

// External function to be used with uids
void ui_start_container(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_start_container\n");
		return;
	}

	assert(ui->elements[id].type == UI_RECT || ui->elements[id].type == UI_ROUND_RECT);
	ui->active_container = id;
}

void ui_stop_container(ui_interface_t *ui){

	if (!ui->active_container){
		log("ERROR: Extraneous call to ui_stop_container. No active container to stop\n");
		return;
	}
	ui_finalise_container(ui);
	ui->active_container = 0;
	ui_element_init(ui, NULL, 0, UI_CONTAINER_END);
	// finalise
}

static void ui_finalise_container(ui_interface_t *ui){

	int cont_id = ui->active_container;
	ui_element_t *e = &ui->elements[cont_id];
	int pad = ui->theme.padding_size;

	int w = 0;
	int h = 0;
	int prev_w = e->data.r.w;
	int prev_h = e->data.r.h;
	int resize = 0;

	ui_get_content_size(e, &w, &h, 0);

	if (w != prev_w){
		w += pad;
		resize = 1;
	}
	if (w != prev_h){
		h += pad;
		resize = 1;
	}
	if (resize){
		if (e->type == UI_RECT){
			ui_rect_t r = {e->x, e->y, w, h, e->data.r.border_width};
			ui_buffer_free(e->index);
			e->num_verts = 0;
			ui_rect_create(ui, cont_id, &r);
		}
		else if ( e->type == UI_ROUND_RECT){
			// todo this and an invisible container with no padding
		}
	}

	//todo switch type here
	// ie for round rect subtract border size
	e->content_region[0] = 0;
	e->content_region[1] = 0;
	e->content_region[2] = 0;
	e->content_region[3] = 0;
}

uint32_t ui_start_tabs(ui_interface_t *ui, ui_tab_header_t *t){

	ui->active_group += 1;
	ui_set_align(ui, UI_BELOW);
	return ui_element_init(ui, t, 0, UI_TAB_HEADER);
}

void ui_next_tab(ui_interface_t *ui){

	ui->active_group += 1;
	ui_set_align(ui, UI_BELOW);
}

void ui_stop_tabs(ui_interface_t *ui){

	int id = ui->active_elements - 1;
	// get the tab header id
	ui_element_t *e = &ui->elements[id];
	while (e->type != UI_TAB_HEADER){
		--e;
	}
	//id = ui_group_get_header_id(ui, id);
	id = e->array_index;
	//printf("final id = %d\n", id);
	ui_tabs_finalise(ui, id);

	// reset the group tag to the parent group
	if (!ui->active_group){
		log("ERROR: No active Tab Group");
		exit(EXIT_FAILURE);
	}
	ui->active_group = e->group_tag - 1;
	if (ui->active_group == 0){
		ui_element_init(ui, NULL, 0, UI_TABS_END);
	}
}

static void ui_rect_create(ui_interface_t *ui, uint16_t id, ui_rect_t *r){

	ui_element_t *e = &ui->elements[id];
	e->index = ui_buffer_init(RECTANGLE_VERTS * sizeof(ui_vertex_t));
	uint32_t col;
	if (r->colour){
		col = r->colour;
	}
	else {
		// col = 0xFF051515;
		col = ui->theme.default_rect_col;
	}
	e->x = r->x;
	e->y = r->y;

	for (int i = 0; i < RECTANGLE_VERTS; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	e->num_verts =
		ui_rectangle_create(e->index, r->x, r->y, r->w, r->h, col);
	e->bounds[0] = r->x;
	e->bounds[1] = r->y;
	e->bounds[2] = e->bounds[0] + r->w;
	e->bounds[3] = e->bounds[1] + r->h;
	e->content_region[0] = e->bounds[0];
	e->content_region[1] = e->bounds[1];
	e->content_region[2] = e->bounds[2];
	e->content_region[3] = e->bounds[3];
}

static void ui_tab_header_create(ui_interface_t *ui, uint16_t id, ui_tab_header_t *th){

	ui_element_t *e = &ui->elements[id];
	e->index = ui_buffer_init(512 * sizeof(ui_vertex_t));
	int pad = ui->theme.padding_size;
	int line_height = ui->font[e->font_id].line_height;

	for (int i = 0; i < 512; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	e->data.th = *th;
	e->data.th.titles = malloc(sizeof(char *) * th->no_of_tabs);

	for (int i = 0; i < th->no_of_tabs; ++i){
		e->data.th.titles[i] = ui_strdup(th->titles[i]);
	}

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->bounds[0] + th->w;
	e->bounds[3] = e->bounds[1] + line_height + pad;
	e->content_region[0] = e->bounds[0];
	e->content_region[1] = e->bounds[1];
	e->content_region[2] = e->bounds[2];
	e->content_region[3] = e->bounds[3];
}

#define TAB_VERTS 9
#define TABS_OFFSET 6
static void ui_tabs_finalise(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	assert(e->type == UI_TAB_HEADER);
	ui_tab_header_t *th = &e->data.th;
	int tag = e->group_tag;
	int x, y;
	int widest = 0;
	int tallest = 0;
	int pad = ui->theme.padding_size;
	uint32_t tb = ui->theme.tab_header_col;
	uint32_t ts = ui->theme.tab_selected_col;
	uint32_t td = ui->theme.tab_deselected_col;
	uint32_t text_col = ui->theme.text_col;
	uint32_t tab_col;
	int line_height = ui->font[e->font_id].line_height;
	int header_height = line_height + pad;

	for (int i = 0; i < th->no_of_tabs; ++i){
		ui_get_content_size(e, &x, &y, tag);
		widest = x > widest ? x : widest;
		tallest = y > tallest ? y : tallest;
	}
	widest += pad;
	tallest -= header_height;
	
	uint32_t width = th->w > widest ? th->w : widest;

	// create rectangle for header 
	e->num_verts =
		ui_rectangle_create(e->index, e->x, e->y, width, header_height, tb);
	
	// create tabs
	float t_width = (float)(width - pad) / th->no_of_tabs;
	float t_height = line_height;
	float tri_width = 0.1 * t_width;
	float box_width = t_width - tri_width;
	v2 a, b, c;
	a.x = e->x;				a.y = e->y + header_height;
	b.x = a.x + tri_width;	b.y = a.y;
	c.x = b.x;				c.y = b.y - t_height;

	for (uint32_t i = 0; i < th->no_of_tabs; ++i){
		tab_col = i == th->active_tab - 1 ? ts : td;
		e->num_verts +=
			ui_make_triangle(e->index + e->num_verts, a, b, c, tab_col);
		e->num_verts +=
			ui_rectangle_create(e->index + e->num_verts, c.x, c.y, box_width, t_height, tab_col);
		a.x += t_width;
		b.x += t_width;
		c.x += t_width;
	}
	// create a rectangle to provide padding
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, e->x, e->y + header_height, width, pad, ts);

	// add the tab titles
	float sx = e->x + tri_width;
	float sy = e->y + pad;

	for (int i = 0; i < th->no_of_tabs; ++i){
		e->num_verts +=
		ui_text_create(e->index + e->num_verts, th->titles[i], &ui->font[e->font_id], sx, sy, box_width, line_height, text_col, UI_CENTERED_X | UI_CENTERED_Y);
		sx += t_width;
	}

	// content_box
	float cy = e->y + header_height + pad;
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, e->x, cy, width, tallest, tb);
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + width;
	e->bounds[3] = e->y + tallest + header_height + pad;
	e->content_region[0] = e->bounds[0];
	e->content_region[1] = e->bounds[1];
	e->content_region[2] = e->bounds[2];
	e->content_region[3] = e->bounds[1] + header_height;
	ui_tab_switch(ui, e);
}

static void ui_tab_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	assert(e->type == UI_TAB_HEADER);
	ui_tab_header_t *th = &e->data.th;

	int width = e->bounds[2] - e->bounds[0];
	int tab_width = width / th->no_of_tabs;
	int active_tab = 1 + (MOUSE_X - e->x) / tab_width;

	th->active_tab = active_tab;
	log("tab active = %d", active_tab);
	ui_tab_switch(ui, e);
}

static void ui_tab_switch(ui_interface_t *ui, ui_element_t *e){

	ui_tab_header_t *th = &e->data.th;

	int tag = e->group_tag;
	int active_tab = tag + th->active_tab - 1;

	// hide elements in other tabs
	ui_element_t *p = e;
	++p;
	while (p->group_tag >= tag){
		if (p->group_tag != active_tab){
			p->flags |= UI_HIDDEN;
		}
		else {
			p->flags &= ~UI_HIDDEN;
		}
		++p;
	}
	uint32_t ts = ui->theme.tab_selected_col;
	uint32_t td = ui->theme.tab_deselected_col;

	ui_vertex_t *buf = e->index;
	buf += TABS_OFFSET;
	
	// Recolour header verts
	for (int i = 0; i < th->no_of_tabs; ++i){
		if (i == active_tab - 1){
			(buf++)->colour = ts;
			(buf++)->colour = ts;
			(buf++)->colour = ts;

			(buf++)->colour = ts;
			(buf++)->colour = ts;
			(buf++)->colour = ts;
			(buf++)->colour = ts;
			(buf++)->colour = ts;
			(buf++)->colour = ts;
		}
		else {
			(buf++)->colour = td;
			(buf++)->colour = td;
			(buf++)->colour = td;

			(buf++)->colour = td;
			(buf++)->colour = td;
			(buf++)->colour = td;
			(buf++)->colour = td;
			(buf++)->colour = td;
			(buf++)->colour = td;
		}
	}
	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

#define COLOUR_PICKER_VERTS		1908
#define HUE_WHEEL_SEGMENTS		256

#define SVBOX_VERTS				12
#define ALPHA_BOX_BACK_VERTS	12
#define ALPHA_BOX_VERTS			(30 + ALPHA_BOX_BACK_VERTS)
#define HUE_VERTS				HUE_WHEEL_SEGMENTS * 6
#define PRESET_BACK_VERTS		12
#define PRESETS_VERTS			(8 * 30 + PRESET_BACK_VERTS)
#define ALPHA_MARK_VERTS		30
#define SV_MARK_VERTS			30

#define SVBOX_VERT_OFFSET		0
#define ALPHA_BOX_OFFSET		(SVBOX_VERT_OFFSET + SVBOX_VERTS)
#define HUE_VERT_OFFSET			(ALPHA_BOX_OFFSET + ALPHA_BOX_VERTS)
#define PRESETS_VERT_OFFSET		(HUE_VERT_OFFSET + HUE_VERTS) 
#define ALPHA_MARK_VERT_OFFSET	(PRESETS_VERT_OFFSET + PRESETS_VERTS)
#define SVBOX_MARK_OFFSET		(ALPHA_MARK_VERT_OFFSET + ALPHA_MARK_VERTS)

static void ui_colour_picker_create(ui_interface_t *ui, uint16_t id, ui_colour_picker_t *cp){

	ui_element_t *e = &ui->elements[id];
	e->index = ui_buffer_init(COLOUR_PICKER_VERTS * sizeof(ui_vertex_t));
	e->data.cp = *cp;
	e->data.cp.active_preset = 0;
	for (int i = 0; i < 8; ++i){
		e->data.cp.presets[i] = cp->presets[i];
		e->data.cp.hue[i] = ROUND(255 * ui_get_hue(cp->presets[i]));
	}
	for (int i = 0; i < COLOUR_PICKER_VERTS; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	uint32_t border_col = 0xFF000000;
	uint32_t marker_col = 0xFFFFFFFF;
	uint32_t border_width = 1;
	int line_width = cp->size / 16;
	float size = cp->size;
	float ring_size = size / 2.0f - line_width / 2.0f;
	int x = e->x + cp->size / 2.0f;
	int y = e->y + cp->size / 2.0f;
	
	// Draw inner rectangle for saturation and lightness
	v2 a, b;
	float radius = ring_size - line_width;
	a.x = radius * cosf(DEG_TO_RAD(225)) + x;
	a.y = radius * sinf(DEG_TO_RAD(225)) + y;
	b.x = radius * cosf(DEG_TO_RAD(45)) + x;
	b.y = radius * sinf(DEG_TO_RAD(45)) + y;
	float sl_size = b.x - a.x;
	e->num_verts += 
	ui_rectangle_create(e->index + e->num_verts, a.x, a.y, sl_size, sl_size, 0);
	e->num_verts += 
	ui_rectangle_create(e->index + e->num_verts, a.x, a.y, sl_size, sl_size, 0);

	ui_buffer_print(e->index);
	// Draw transparency box
	float alpha_x = e->x + size + line_width;
	float alpha_y = e->y;
	// background to reveal transparancy
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, alpha_x, alpha_y, line_width, size, 0xFF888888);
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, alpha_x + line_width, alpha_y, line_width, size, 0xFFFFFFFF);

	e->num_verts += 
	ui_rect_bordered_create(e->index + e->num_verts, alpha_x, alpha_y, 2 * line_width, size, 0, border_col, border_width);
	
	// Draw outer ring with 256 hue's
	e->num_verts +=
		ui_line_arc_create_wide(e->index + e->num_verts, x, y, -PI, PI, ring_size, 0, HUE_WHEEL_SEGMENTS, line_width);

	// draw presets boxes
	float box_x = alpha_x + 3 * line_width;
	float box_y = e->y;
	uint32_t preset_col;
	// background to reveal transparancy
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, box_x, box_y, 1.5f * line_width, size, 0xFF888888);
	e->num_verts +=
		ui_rectangle_create(e->index + e->num_verts, box_x + 1.5f * line_width, box_y, 1.5f * line_width, size, 0xFFFFFFFF);

	for (int i = 0; i < 8; ++i){
		if (i == e->data.cp.active_preset){
			preset_col = 0xFFFFFFFF;
		}
		else {
			preset_col = border_col;
		}
		e->num_verts += 
		ui_rect_bordered_create(e->index + e->num_verts, box_x, box_y, 3 * line_width, size / 8.0f, cp->presets[i], preset_col, border_width);
		box_y += size / 8.0f;
	}
	ui_buffer_print(e->index);

	// Draw alpha selection marker
	int alpha_mark_w = line_width << 1;
	int alpha_mark_h = line_width >> 1;
	alpha_mark_h = alpha_mark_h < 3 ? 3 : alpha_mark_h;
	int alpha_mark_x = e->x + cp->size + line_width;
	int alpha_mark_y = e->y + (size / 255.0f) * ALPHA_BYTE(cp->presets[0]) - alpha_mark_h / 2.0f;

	e->num_verts +=
	ui_rect_bordered_create(e->index + e->num_verts, alpha_mark_x, alpha_mark_y, alpha_mark_w, alpha_mark_h, border_col, marker_col, border_width); 

	// Draw marker for location in saturation / lightness box
	int slx = a.x + sl_size * ui_get_saturation(cp->presets[cp->active_preset]);
	int sly = a.y + sl_size * (1 - ui_get_value(cp->presets[cp->active_preset]));
	int sl_mark_size = alpha_mark_h;

	e->num_verts +=
	ui_rect_bordered_create(e->index + e->num_verts, slx, sly, sl_mark_size, sl_mark_size, border_col, marker_col, 1); 

	// Draw marker for current hue on hue ring
	v2 m1, m2, m3;
	int outline = 2;
	m1.x = x;
	m1.y = y - ring_size - outline;
	m2.x = x + (line_width >> 1) + outline;
	m2.y = y - ring_size + line_width * 2 + outline;
	m3.x = x - (line_width >> 1) - outline;
	m3.y = m2.y;
	// todo replace this with make trangle bordered or something better
	e->num_verts +=
		ui_make_triangle(e->index + e->num_verts, m1, m2, m3, 0xFF000000);
	m1.y += outline; m2.x -= outline; m2.y -= outline; m3.x += outline; m3.y -= outline;
	e->num_verts +=
		ui_make_triangle(e->index + e->num_verts, m1, m2, m3, 0xFFFFFFFF);

	// Set Bounds
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + size + line_width * 7;
	e->bounds[3] = e->y + size;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + size + line_width * 7;
	e->content_region[3] = e->y + size;
	ui_colour_picker_recolour(ui, id);
	assert(e->num_verts == COLOUR_PICKER_VERTS);
}

static void ui_colour_picker_set_sv_mark(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	uint32_t rgba = e->data.cp.presets[e->data.cp.active_preset];;

	int mark_size = ui_rectangle_get_width(e->index + SVBOX_MARK_OFFSET) / 2;
	int box_x = -mark_size + ui_rectangle_get_x(e->index + SVBOX_VERT_OFFSET);
	int box_y = -mark_size + ui_rectangle_get_y(e->index + SVBOX_VERT_OFFSET);
	float size = ui_rectangle_get_width(e->index + SVBOX_VERT_OFFSET); 

	float sat = ui_get_saturation(rgba);
	float val = ui_get_value(rgba);

	int x = box_x + size * sat;
	int y = box_y + size * (1.0f - val);
	x = sat == 0 ? MOUSE_X : x;
	x = x < box_x ? box_x : x;
	x = x > (box_x + size) ? (box_x + size) : x;
	y = y > (box_y + size) ? (box_y + size) : y;
	ui_rect_bordered_set_pos(e->index + SVBOX_MARK_OFFSET, x, y);
}

static void ui_colour_picker_set_sv(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;

	// sat = x-axis ; val = y-axis inverted
	int box_x = ui_rectangle_get_x(e->index + SVBOX_VERT_OFFSET);
	int box_y = ui_rectangle_get_y(e->index + SVBOX_VERT_OFFSET);
	int offset_x = MOUSE_X - box_x;
	int offset_y = MOUSE_Y - box_y;
	float size = ui_rectangle_get_width(e->index + SVBOX_VERT_OFFSET); 

	offset_x = offset_x > 255 ? 255 : offset_x;
	offset_y = offset_y > 255 ? 255 : offset_y;
	offset_x = offset_x < 0 ? 0 : offset_x;
	offset_y = offset_y < 0 ? 0 : offset_y;
	
	int new_sat = 255.0f * offset_x / (float)size;
	int new_val = 255.0f * (1.0f - (float)(offset_y / size));
	new_sat = new_sat > 255 ? 255 : new_sat;
	new_val = new_val < 0 ? 0 : new_val;

	uint32_t *rgb = &cp->presets[cp->active_preset];
	uint32_t hsv = RGBA(cp->hue[cp->active_preset], new_sat, new_val, ALPHA_BYTE(*rgb));
	*rgb = ui_hsv_to_rgb(hsv);
}

static void ui_colour_picker_set_alpha_mark(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;
	int alpha_mark_h = ui_rect_bordered_get_height(e->index + ALPHA_MARK_VERT_OFFSET);
	int alpha = ALPHA_BYTE(cp->presets[cp->active_preset]);
	int alpha_mark_x = ui_rect_bordered_get_x(e->index + ALPHA_MARK_VERT_OFFSET);
	int alpha_mark_y = e->y + (cp->size / 255.0f) * alpha - alpha_mark_h / 2.0f;

	ui_rect_bordered_set_pos(e->index + ALPHA_MARK_VERT_OFFSET, alpha_mark_x, alpha_mark_y); 
}

static void ui_colour_picker_set_alpha(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;

	int offset = MOUSE_Y - e->y;
	offset = offset > 255 ? 255 : offset;
	offset = offset < 0 ? 0 : offset;
	int alpha = 255 * offset / cp->size;

	alpha = alpha > 255 ? 255 : alpha;
	alpha = alpha < 0 ? 0 : alpha;

	alpha = alpha << 24;
	cp->presets[cp->active_preset] = (cp->presets[cp->active_preset] & 0x00FFFFFF) | alpha;
}

static void ui_colour_picker_colour_preset(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;
	
	ui_vertex_t *buf = e->index;
	buf += 30 * cp->active_preset + PRESETS_VERT_OFFSET + PRESET_BACK_VERTS;
	(buf++)->colour = cp->presets[cp->active_preset];
	(buf++)->colour = cp->presets[cp->active_preset];
	(buf++)->colour = cp->presets[cp->active_preset];
	(buf++)->colour = cp->presets[cp->active_preset];
	(buf++)->colour = cp->presets[cp->active_preset];
	(buf++)->colour = cp->presets[cp->active_preset];
}

static void ui_colour_picker_set_preset(ui_interface_t *ui, uint16_t id, uint16_t preset){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;
	
	int start_preset = 30 * cp->active_preset + PRESETS_VERT_OFFSET + PRESET_BACK_VERTS + 6;
	ui_vertex_t *buf = e->index + start_preset;

	// Remove highlight from old border
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;

	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;

	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;

	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;

	start_preset = 30 * preset + PRESETS_VERT_OFFSET + PRESET_BACK_VERTS + 6;
	buf = e->index + start_preset;
	// Update border of the active preset
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;

	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;

	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;

	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;

	cp->active_preset = preset;
}

static void ui_colour_picker_colour_alpha(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf = e->index + ALPHA_BOX_OFFSET + ALPHA_BOX_BACK_VERTS;
	ui_colour_picker_t *cp = &e->data.cp;
	uint32_t rgba = cp->presets[cp->active_preset];
	(buf++)->colour = rgba & 0x00FFFFFF;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba & 0x00FFFFFF;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba & 0x00FFFFFF;
}

static void ui_colour_picker_recolour(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;
	ui_vertex_t *buf = e->index;
	uint32_t rgba = cp->presets[cp->active_preset];
	uint8_t hue = cp->hue[cp->active_preset];
	uint32_t saturated_rgba = ui_hsv_to_rgb(0xFFFFFF00 | hue);
	// central SV box
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = saturated_rgba;
	(buf++)->colour = 0xFFFFFFFF;
	(buf++)->colour = saturated_rgba;
	(buf++)->colour = saturated_rgba;
	(buf++)->colour = 0;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0;
	(buf++)->colour = 0xFF000000;
	(buf++)->colour = 0;
	// alpha bar
	buf += ALPHA_BOX_BACK_VERTS;
	(buf++)->colour = rgba & 0x00FFFFFF;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba & 0x00FFFFFF;
	(buf++)->colour = rgba | 0xFF000000;
	(buf++)->colour = rgba & 0x00FFFFFF;
	// Hue Ring
	buf = e->index + HUE_VERT_OFFSET;
	uint32_t current_col;
	uint8_t col_idx = cp->hue[cp->active_preset] - 64;

	for (int i = 0; i < HUE_WHEEL_SEGMENTS; ++i){
		// todo make this a lookup table
		current_col = ui_hsv_to_rgb(RGBA(col_idx++, 255, 255, 255));
		(buf++)->colour = current_col;
		(buf++)->colour = current_col;
		(buf++)->colour = current_col;
		(buf++)->colour = current_col;
		(buf++)->colour = current_col;
		(buf++)->colour = current_col;
	}
	// Recolour preset box
	buf = e->index + PRESETS_VERT_OFFSET + PRESET_BACK_VERTS + 30 * cp->active_preset;
	(buf++)->colour = rgba;
	(buf++)->colour = rgba;
	(buf++)->colour = rgba;
	(buf++)->colour = rgba;
	(buf++)->colour = rgba;
	(buf++)->colour = rgba; 
}

static void ui_colour_picker_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;
	int line_width = cp->size / 16;
	float size = cp->size;
	cp->active_component = 0;

	// section of colour picker containg the colour wheel
	if (MOUSE_X < e->x + size){
		float ring_size = size / 2.0f - line_width / 2.0f;
		v2 ring_center;
		ring_center.x = e->x + cp->size / 2.0f;
		ring_center.y = e->y + cp->size / 2.0f;
		v2 mouse_pos = {MOUSE_X, MOUSE_Y};
		v2 distance = ui_sub_v2(ring_center, mouse_pos);
		float magnitude = sqrtf(distance.x * distance.x + distance.y * distance.y);
		// single instance click on hue ring
		if (magnitude > (ring_size - line_width * 0.5) && magnitude < (ring_size + line_width * 0.5)){
			cp->active_component |= HUE_RING;
		}	   
		// single instance saturation-value box click
		else {
			int size = ui_rectangle_get_width(e->index + SVBOX_VERT_OFFSET);
			int box_left = ui_rectangle_get_x(e->index + SVBOX_VERT_OFFSET);
			int box_right = box_left + size;
			if (MOUSE_X > box_left && MOUSE_X < box_right){
				int box_top = ui_rectangle_get_y(e->index + SVBOX_VERT_OFFSET);
				int box_bottom = box_top + size;;
				if (MOUSE_Y > box_top && MOUSE_Y < box_bottom){
					cp->active_component |= SV_BOX;
					ui_colour_picker_set_sv(ui, id);
					ui_colour_picker_set_sv_mark(ui, id);
					ui_colour_picker_colour_preset(ui, id);
					ui_colour_picker_colour_alpha(ui, id);
					ui->modified |= GEOM_MODIFIED;
				}
			}
		}
	}
	// single instance alpha bar click
	else if (MOUSE_X < e->x + size + line_width * 3){
		// no need to check y as as bar is full size of element
		if (MOUSE_X > e->x + size + line_width){
				cp->active_component |= ALPHA;
				ui_colour_picker_set_alpha(ui, id);
				ui_colour_picker_set_alpha_mark(ui, id);
				ui_colour_picker_colour_preset(ui, id);
				ui->modified |= GEOM_MODIFIED;
		}
	}
	// single instance pallete click
	else if (MOUSE_X < e->x + size + line_width * 7){
		if (MOUSE_X > e->x + size + line_width * 4){
			int selected_preset = (MOUSE_Y - e->y) / (size / 8);
			ui_colour_picker_set_preset(ui, id, selected_preset);
			ui_colour_picker_set_alpha_mark(ui, id);
			ui_colour_picker_set_sv_mark(ui, id);
			ui_colour_picker_recolour(ui, id);
			ui->modified |= GEOM_MODIFIED;
		}
	}
}

static void ui_colour_picker_update(ui_interface_t *ui, uint16_t id, int32_t offset){

	ui_element_t *e = &ui->elements[id];
	ui_colour_picker_t *cp = &e->data.cp;

	if (cp->active_component & HUE_RING){
		// update the current hue and selected colour
		uint32_t hsv = ui_rgb_to_hsv(cp->presets[cp->active_preset]);
		uint8_t new_hue = offset + cp->hue[cp->active_preset];
		cp->hue[cp->active_preset] = new_hue;
		hsv = (hsv & 0xFFFFFF00) | new_hue;
		cp->presets[cp->active_preset] = ui_hsv_to_rgb(hsv);
		ui_colour_picker_recolour(ui, id);
		ui->modified |= GEOM_MODIFIED;
	}
	else if (cp->active_component & SV_BOX){
		// Updating the Saturation / Lightness box
		ui_colour_picker_set_sv(ui, id);
		ui_colour_picker_set_sv_mark(ui, id);
		ui_colour_picker_colour_preset(ui, id);
		ui_colour_picker_colour_alpha(ui, id);
		ui->modified |= GEOM_MODIFIED;
	}
	else if (cp->active_component & ALPHA){
		// Updating the Alpha slider
		ui_colour_picker_set_alpha(ui, id);
		ui_colour_picker_set_alpha_mark(ui, id);
		ui_colour_picker_colour_preset(ui, id);
		ui->modified |= GEOM_MODIFIED;
	}
}

static float ui_get_hue(uint32_t rgba){

	float hue;
	float r = RED_BYTE(rgba) / 255.0f;
   	float g = GREEN_BYTE(rgba) / 255.0f;	
	float b = BLUE_BYTE(rgba) / 255.0f;

	if(r < g){
		if(r < b){
			if(g < b){
				hue = (4.0f - (g - r) / (b - r)) / 6.0f;
			}
			else{
				hue = (2.0f + (b - r) / (g - r)) / 6.0f;
			}
		}
		else{
			hue = (2.0f - (r - b) / (g - b)) / 6.0f;
		}
	}
	else{
		if(r < b){
			hue = (4.0f + (r - g) / (b - g)) / 6.0f;
		}
		else{
			if(g < b){
				hue = (6.0f - (b - g) / (r - g)) / 6.0f;
			}
			else if(r == g && r == b){
				hue = 0.0f;
			}
			else{
				hue = (0.0f + (g - b) / (r - b)) / 6.0f;
			}
		}
	}
	return hue; 
}

static float ui_get_saturation(uint32_t rgba){

	float sat;
	float r = RED_BYTE(rgba) / 255.0f;
   	float g = GREEN_BYTE(rgba) / 255.0f;	
	float b = BLUE_BYTE(rgba) / 255.0f;

	if(r < g){
		if(r < b){
			if(g < b){
				sat = 1.0f - r / b;
			}
			else{
				sat = 1.0f - r / g;
			}
		}
		else{
			sat = 1.0f - b / g;
		}
	}
	else{
		if(r < b){
			sat = 1.0f - g / b;
		}
		else{
			if(g < b){
				sat = 1.0f - g / r;
			}
			else if(r == g && r == b){
				sat = 0.0f;
			}
			else{
				sat = 1.0f - b / r;
			}
		}
	}
	return sat;
}

static float ui_get_value(uint32_t rgba){

	return MAX(MAX(RED_BYTE(rgba), GREEN_BYTE(rgba)), BLUE_BYTE(rgba)) / 255.0f; 
}

static uint32_t ui_rgb_to_hsv(uint32_t rgba){

	float hue, sat, val;
	float r = RED_BYTE(rgba) / 255.0f;
   	float g = GREEN_BYTE(rgba) / 255.0f;	
	float b = BLUE_BYTE(rgba) / 255.0f;

	if(r < g){
		if(r < b){
			if(g < b){
				hue = (4.0f - (g - r) / (b - r)) / 6.0f;
				sat = 1.0f - r / b;
				val = b;
			}
			else{
				hue = (2.0f + (b - r) / (g - r)) / 6.0f;
				sat = 1.0f - r / g;
				val = g;
			}
		}
		else{
			hue = (2.0f - (r - b) / (g - b)) / 6.0f;
			sat = 1.0f - b / g;
			val = g;
		}
	}
	else{
		if(r < b){
			hue = (4.0f + (r - g) / (b - g)) / 6.0f;
			sat = 1.0f - g / b;
			val = b;
		}
		else{
			if(g < b){
				hue = (6.0f - (b - g) / (r - g)) / 6.0f;
				sat = 1.0f - g / r;
				val = r;
			}
			else if(r == g && r == b){
				hue = 0.0f;
				sat = 0.0f;
				val = r;
			}
			else{
				hue = (0.0f + (g - b) / (r - b)) / 6.0f;
				sat = 1.0f - b / r;
				val = r;
			}
		}
	}
	hue = ROUND(hue * 255);
	sat = ROUND(sat * 255);
	val = ROUND(val * 255);
	return RGBA(hue, sat, val, ALPHA_BYTE(rgba));
}

static uint32_t ui_hsv_to_rgb(uint32_t hsv){

	float red, green, blue;
	float h = RED_BYTE(hsv) / 255.0f;
   	float s = GREEN_BYTE(hsv) / 255.0f;
	float v = BLUE_BYTE(hsv) / 255.0f;

	if(h > 1 || h < 0.0f)
		h = h - (float)((int)h);
	if(s < 0.0f)
		s = 0.0f;
	if(s > 1.0f)
		s = 1.0f;
	if(v < 0.0f)
		v = 0.0f;
	if(v > 1.0f)
		v = 1.0f;

	s = 1.0f - s;
	h *= 6.0f;
	s *= v;
	switch((uint32_t)h){
		case 0 :
			red = v;
			green = s + (0.0f + h) * (v - s);
			blue = s;
		break;
		case 1 :
			red = s + (2.0f - h) * (v - s);
			green = v;
			blue = s;
		break;
		case 2 :
			red = s;
			green = v;
			blue = s + (h - 2.0f) * (v - s);
		break;
		case 3 :
			red = s;
			green = s + (4.0f - h) * (v - s);
			blue = v;
		break;
		case 4 :
			red = s + (h - 4.0f) * (v - s);
			green = s;
			blue = v;
		break;
		default :
			red = v;
			green = s;
			blue = s + (6.0f - h) * (v - s);
		break;
	}
	red = ROUND(red * 255);
	green = ROUND(green * 255);
	blue = ROUND(blue * 255);
	return RGBA(red, green, blue, ALPHA_BYTE(hsv));
}

// types wanted todo
// top_bar_full_width
// bottom_bar_full_width
// verticle
// in container
// int header
// floating
int ui_menu_start(ui_interface_t* ui, void *data, uint32_t type){

	static int active_menu = 0;
	if (ui == NULL){
		active_menu = 0;
		return 0;
	}
	uint32_t uuid;
	ui_element_t *e;
	uint32_t menubar_col = ui->theme.menubar_col;
	uint32_t pad = ui->theme.menubar_padding;
	int pos_x = 0;
	int pos_y = 0;
	// int pad = ui->theme.menubar_padding;
	// Find menu root if we are adding more items to a menu
	if (active_menu){
		e = &ui->elements[ui->active_elements - 1];
		while (1){
			if ((e->type == UI_MENU_ROOT) || (e->type == UI_MENU_ROOT_RECT)){
				break;
			}
			if (e->array_index == -1){
				log("ERROR: Menu root object not found\n");
				ui_log_elements(ui);
				exit(EXIT_FAILURE);
			}
			--e;
		}
		pos_x = e->bounds[2];
		pos_y = e->bounds[1];
		// Finalise the previous menus group header object
		uint32_t gh_bg_col = ui->theme.group_background_col;

		if (e->type == UI_MENU_ROOT){
			ui->theme.group_background_col = menubar_col;
			ui_group_stop(ui);
			ui->theme.group_background_col = gh_bg_col;
		}
	}
	// Determine if we are continuing a menu or starting a new one
	if (!active_menu){
		active_menu = 1;
	}
	int height = ui->font[ui->active_font].line_height + pad;
	if (type != UI_MENU_LABEL){
		// draw a background rectangle
		ui_rect_t rect_data = {.x = pos_x, .y = pos_y, .w = 12, .h = height, .border_width = 0, .colour = menubar_col};
		uuid = ui_element_init(ui, &rect_data, 0, UI_RECT);
		e = &ui->elements[ui_find_array_index(ui, uuid)];
		e->type = UI_MENU_ROOT_RECT;
	}
	// Todo need special trick for deleting these 
	switch (type){
		case UI_LABEL:
		case UI_MENU_LABEL:
			uuid = ui_element_init(ui, data, 0, UI_MENU_LABEL);
			e = &ui->elements[ui_find_array_index(ui, uuid)];
			e->type = UI_MENU_ROOT;
			e->flags |= UI_ACTION_ON_PRESS;
			break;
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
			break;
		case UI_LIST_BOX:
			{
			int list_h, list_w;
			uuid = ui_element_init(ui, data, 0, UI_LIST_BOX);
			list_h = ui_get_element_height(ui, uuid);
			list_w = ui_get_element_width(ui, uuid);
			// resize the rectangle
			ui_rectangle_create(e->index, pos_x, pos_y, list_w + 2 * pad, height, menubar_col);
			pos_y += (height - list_h) / 2;
			pos_x += pad;
			e = &ui->elements[ui_find_array_index(ui, uuid)];
			}
			break;
	default:
		log("ERROR: Unsupported object type for menu root\n");
		exit(EXIT_FAILURE);
	}
	ui_position_element_internal(ui, e->array_index, pos_x, pos_y);
	// todo this only works for pull down menus, support pullup menus!

	if (e->type == UI_MENU_ROOT){
		ui_set_align(ui, 0);
		ui_header_t header = {.w = 0, .h = 0, .state = 0, .title = NULL};
		ui->active_group += 1;
		int group_uuid = ui_element_init(ui, &header, 0, UI_GROUP_NO_HEADER);

		ui_position_element(ui, group_uuid, e->bounds[0], e->bounds[3]);
		ui_set_align(ui, UI_BELOW);
	}

	return uuid;
}

static int ui_menu_get_toggle_status(ui_interface_t *ui, uint32_t id){

	ui_element_t *e = &ui->elements[id + 1];
	assert(e->type == UI_GROUP_HEADER);
	return e->data.h.state;
}

static void ui_menu_label_create(ui_interface_t *ui, uint32_t id, ui_label_t *l){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t text_col;
	uint32_t bg_col = ui->theme.menubar_col;
	uint32_t pad = ui->theme.menubar_padding;
	if (l->colour == 0){
		text_col = ui->theme.text_col;
	}
	else {
		text_col = l->colour;
	}
	int w, h;

	uint32_t vert_count = ui_string_vert_count(l->str);
	vert_count += RECTANGLE_VERTS;
	e->index = ui_buffer_init(vert_count * sizeof(ui_vertex_t));

	ui_string_size_pixels(l->str, &ui->font[e->font_id], &w, &h);
	if (!l->w){
		l->w = w;
	}
	if (!l->h){
		l->h = h;
	}
	int width = w + 4 * pad;
	int height = ui->font[e->font_id].line_height + pad;
	e->num_verts = ui_rectangle_create(e->index, e->x, e->y, width, height, bg_col);
	e->num_verts +=
	ui_text_create(e->index + e->num_verts, l->str, &ui->font[e->font_id], e->x, e->y, width, height, text_col, UI_CENTERED_X | UI_CENTERED_Y);

	e->data.l = *l;
	e->data.l.str = ui_strdup(l->str);

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + width;
	e->bounds[3] = e->y + height;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + width;
	e->content_region[3] = e->y + height;
	assert(e->num_verts == vert_count);
}

static void ui_menu_label_highlight(ui_interface_t *ui, uint32_t id){

	ui_vertex_t *p = ui->elements[id].index;
	p++->colour = ui->theme.menu_hovered_col;
	p++->colour = ui->theme.menu_hovered_col;
	p++->colour = ui->theme.menu_hovered_col;
	p++->colour = ui->theme.menu_hovered_col;
	p++->colour = ui->theme.menu_hovered_col;
	p++->colour = ui->theme.menu_hovered_col;
	ui->modified |= GEOM_MODIFIED;
}

static void ui_menu_label_unhighlight(ui_interface_t *ui, uint32_t id){

	ui_vertex_t *p = ui->elements[id].index;
	p++->colour = ui->theme.menubar_col;
	p++->colour = ui->theme.menubar_col;
	p++->colour = ui->theme.menubar_col;
	p++->colour = ui->theme.menubar_col;
	p++->colour = ui->theme.menubar_col;
	p++->colour = ui->theme.menubar_col;
	ui->modified |= GEOM_MODIFIED;
}


void ui_menu_stop(ui_interface_t *ui){

	uint32_t id = ui->active_elements - 1;
	uint32_t gh_bg_col = ui->theme.group_background_col;

	if (ui->active_group){
		printf("active group\n");
		ui->theme.group_background_col = ui->theme.menubar_col;
		ui_group_stop(ui);
		ui->theme.group_background_col = gh_bg_col;
	}
	ui_menu_finalise(ui, id);
	ui_menu_start(NULL, NULL, 0);
}

static void ui_menu_finalise(ui_interface_t *ui, uint32_t id){

}

static int ui_menu_open(ui_interface_t *ui, uint32_t id){

	ui_group_header_toggle(ui, id + 1);
	// Check if other menus are open and if so toggle them
	ui_element_t *e = ui->elements;
	for (uint32_t i = 0; i < ui->active_elements; ++i, ++e){
		if (e->type == UI_MENU_ROOT && e->array_index != id){
			if (ui_menu_get_toggle_status(ui, i)){
				ui_group_header_toggle(ui, i + 1);
			}
		}
	}
	ui->action = ui_menu_monitor;
	ui->action_flags |= LEFT_HELD;
	return 1;
}

static int ui_menu_monitor(ui_interface_t *ui, uint32_t id){

	ui_element_t *e = ui->elements;
	int menu_id = -1;
	// Wait for mouse up
	if (!MOUSE_L && (ui->action_flags & LEFT_HELD)){
		ui->action_flags = 0;
		return 0;
	}
	// find the open menu
	for (uint32_t i = 0; i < ui->active_elements; ++i, ++e){
		if (e->type == UI_MENU_ROOT){
			if (ui_menu_get_toggle_status(ui, i)){
				menu_id = (e + 2)->uuid;		// Fist element in the menu
				break;
			}
		}
	}
	// Perhaps the client has closed the menu...
	if (menu_id < 0){
		ui->action = NULL;
		return 1;
	}
	// group_header object
	++e;
	// check for any mouse clicks outside of the active menu
	if (MOUSE_L || MOUSE_R){
		if (MOUSE_X < e->bounds[0] || MOUSE_X > e->bounds[2] || MOUSE_Y < (e - 1)->bounds[1] || MOUSE_Y > e->bounds[3]){
			ui_menu_close(ui, menu_id);
			ui_element_dectivate(ui, menu_id - 2);
			return 0;
		}
	}

	// Allow other items to be processed
	return 1;
}

void ui_menu_close(ui_interface_t *ui, uint32_t uuid){

	int header_id = ui_group_get_header_id(ui, ui_find_array_index(ui, uuid));
	ui_group_header_toggle(ui, header_id);
	ui->action = NULL;
}

#define GROUP_HEADER_VERTS 15
static void ui_group_header_create(ui_interface_t *ui, uint16_t id, ui_header_t *h){

	ui_element_t *e = &ui->elements[id];

	if (!h->w){
		int width, height;
		ui_string_size_pixels(h->title, &ui->font[e->font_id], &width, &height);
		h->w = width + ui->theme.padding_size * 2 + height;
	}

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	if (e->type != UI_GROUP_NO_HEADER){
		e->data.h = *h;
		e->data.h.title = ui_strdup(h->title);
		e->bounds[2] = e->bounds[0] + h->w;
		e->bounds[3] = e->bounds[1] + ui->font[e->font_id].line_height;
	}
	else {
		e->data.h = *h;
		e->data.h.title = NULL;
		e->bounds[2] = e->bounds[0];
		e->bounds[3] = e->bounds[1];
	}
	e->type = UI_GROUP_HEADER;

	e->content_region[0] = e->bounds[0];
	e->content_region[1] = e->bounds[1];
	e->content_region[2] = e->bounds[2];
	e->content_region[3] = e->bounds[3];
}

static void ui_group_header_finalise(ui_interface_t *ui){
	
	uint32_t id = ui->active_elements - 1;
	id = ui_group_get_header_id(ui, id);
	ui_element_t *e = &ui->elements[id];

	uint32_t tag = e->group_tag;
	uint32_t widest_element = 0;
	uint32_t content_height = 0;
	uint32_t border_size = ui->theme.padding_size;
	uint32_t text_col = ui->theme.text_col;
	// uint32_t header_height = ui->font[e->font_id].line_height;
	uint32_t header_height = e->bounds[3] - e->bounds[1];

	int x, y;
	ui_get_content_size(e, &x, &y, tag);
	content_height = y + border_size;
	widest_element = x + border_size;

	int num_verts;
	if (header_height){
		// Regular group header
		num_verts = ui_string_vert_count(e->data.h.title) + GROUP_HEADER_VERTS;
	}
	else {
		// GROUP_NO_HEADER
		num_verts = 6;
	}
	e->index = ui_buffer_init(num_verts * sizeof(ui_vertex_t));
	// box for title
	if (header_height){
		e->num_verts += 
			ui_rectangle_create(e->index + e->num_verts, e->x, e->y, widest_element, header_height, ui->theme.group_header_col);

		// triangle for roll/unroll
		v2 a, b, c;
		float tri_pad = header_height / 4.0f;
		a.x = e->x + widest_element - tri_pad;		
		a.y = e->y + tri_pad;
		b.x = e->x + widest_element - header_height + tri_pad;
		b.y = a.y;
		c.x = e->x + widest_element - header_height / 2.0f;
		c.y = e->y + header_height - tri_pad;
		
		e->num_verts +=
		ui_make_triangle(e->index + e->num_verts, a, b, c, text_col);
	}

	// add the header title
	ui_header_t *h = &e->data.h;
	float sx = e->x + 2 * ui->theme.padding_size;
	e->num_verts +=
	ui_text_create(e->index + e->num_verts, h->title, &ui->font[e->font_id], sx, e->y, h->w, header_height, text_col, UI_CENTERED_Y | UI_LEFT);

	// add verts for content box
	e->data.h.h = content_height;
	e->data.h.w = widest_element;

	content_height -= header_height;
	e->num_verts += ui_rectangle_create(&e->index[e->num_verts], e->x, e->y + header_height, 
			widest_element,	content_height, ui->theme.group_background_col);

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + widest_element;
	e->bounds[3] = e->y + content_height + header_height;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + widest_element;
	e->content_region[3] = e->y + header_height;

	assert(e->num_verts == num_verts);
	if (!e->data.h.state){
		e->data.h.state = 1;
		ui_group_header_toggle(ui, id);
	}
}

static void ui_get_content_size(ui_element_t *e, int *w, int *h, int tag){

	int lowest_x = e->x;
	int lowest_y = e->y;
	// assert(abs(e->x) < 1200);
	// assert(abs(e->y) < 1200);
	int highest_x = 0;
	int highest_y = 0;

	if (e->type == UI_GROUP_HEADER || e->type == UI_TAB_HEADER){
		while (e->group_tag >= tag){

			if (e->flags & UI_HIDDEN){
				++e;
				continue;
			}
			lowest_x = e->bounds[0] < lowest_x ? e->bounds[0] : lowest_x;
			lowest_y = e->bounds[1] < lowest_y ? e->bounds[1] : lowest_y;
			
			highest_x = e->bounds[2] > highest_x ? e->bounds[2] : highest_x;
			highest_y = e->bounds[3] > highest_y ? e->bounds[3] : highest_y;
			++e;
		}
	}
	else {
		++e;
		int container_id = e->parent;
		while (e->parent == container_id){

			if (e->flags & UI_HIDDEN){
				++e;
				continue;
			}
			lowest_x = e->bounds[0] < lowest_x ? e->bounds[0] : lowest_x;
			lowest_y = e->bounds[1] < lowest_y ? e->bounds[1] : lowest_y;
			
			highest_x = e->bounds[2] > highest_x ? e->bounds[2] : highest_x;
			highest_y = e->bounds[3] > highest_y ? e->bounds[3] : highest_y;
			++e;
		}
	}
	*w = highest_x - lowest_x;
	*h = highest_y - lowest_y;
	// assert(*w < 1200);
	// assert(*h < 1200);
}

static void ui_group_header_toggle(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	int32_t tag = e->group_tag;
	int *bounds = e->bounds;

	bounds[2] = e->content_region[2];
	bounds[3] = e->content_region[3];

	// collapse tree
	if (e->data.h.state){
		e->data.h.state = 0;
		e->num_verts -= 6;
		++e;
		while (e->group_tag >= tag){
			e->flags |= UI_HIDDEN;
			++e;
		}
	}
	// open tree
	else {
		e->data.h.state = 1;
		e->num_verts += 6;
		bounds[2] = e->x + e->data.h.w;
		bounds[3] = e->y + e->data.h.h;
		++e;
		while (e->group_tag >= tag){
			// nested groups items
			if (e->group_tag != tag && e->type != UI_GROUP_HEADER){
				if (ui->elements[e->parent].data.h.state){
					// tab within tree
					if (e->type == UI_TAB_HEADER){
						e->flags &= ~UI_HIDDEN;
						ui_tab_switch(ui, e);
						int tabs_tag = e->group_tag;
						while (e->group_tag == tabs_tag){ ++e;}
					}
					else {
						e->flags &= ~UI_HIDDEN;
					}
				}	
				++e;
				continue;
			}
			else if (e->type == UI_TAB_HEADER){
				e->flags &= ~UI_HIDDEN;
				ui_tab_switch(ui, e);
				int tabs_tag = e->group_tag;
				while (e->group_tag == tabs_tag){ ++e;}
			}
			e->flags &= ~UI_HIDDEN;
			++e;
		}
	}
	ui_reflow(ui, bounds);
	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
	return;
}

static void ui_reflow(ui_interface_t *ui, int *bounds){

	ui_element_t *e = ui->elements;
	int y = bounds[1];
	int x1 = bounds[0];
	int x2 = bounds[2];

	for (uint32_t i = 0; i < ui->active_elements; ++i){
		// Visible and moveable 
		if ((e->flags & UI_MOVABLE) && !(e->flags & UI_HIDDEN)){
			if (e->bounds[1] > y){
				if (!(e->bounds[0] > x2 || e->bounds[2] < x1)){
					//log("REFLOW %d, ALIGN = %d", i, e->align);
					//log("before e->bounds[0] = %d", e->bounds[0]);
					ui_align_position(ui, i);
					//log("after e->bounds[0] = %d", e->bounds[0]);
				}
			}
		}
		++e;
	}
}

static inline int32_t ui_group_get_header_id(ui_interface_t *ui, uint16_t id){
	
	ui_element_t *e = &ui->elements[id];
	int i = 1;
	
	// handle nested groups
	if (e->type == UI_GROUP_HEADER || e->type == UI_TAB_HEADER || e->type == UI_GROUP_NO_HEADER){
		if (ui->active_group == 1){
			return id;
		}
		else {
			return (e - 1)->parent;
		}
	}
	// tabs active
	
	// prevents illegal access to element -1!
	//if ((e - 1)->parent != -1 && ui->elements[(e - 1)->parent].type == UI_TAB_HEADER){
	// if container end was last element in previous tab group,
	// if ((e - 1)->parent == -1){
	// 	while ((e - i)-> type == UI_TAB_HEADER){
	// 	++i;
	// }

	// prev parent is tab header
	if (ui->elements[(e - 1)->parent].type == UI_TAB_HEADER){
		return (e - 1)->parent;
	}
	// case previous element is in a tab header, but does not ref that header as it is in a container
	// first element in a new tab
	if (ui->elements[id].group_tag == (ui->elements[id - 1].group_tag + 1)){
		while (ui->elements[id - i].type != UI_TAB_HEADER){
			// get the parent of the container
			++i;
		}
		return (e - i)->array_index;

	}

	// if ((e - 1)->parent == -1 && ui->active_group > 1){
	
	// 	while ((e - i)-> type != UI_TAB_HEADER){
	// 		++i;
	// 	}

	// 	return id - i;
	// }
	// skip container end
	// if (ui->elements[(e - 1)->parent].type == UI_TAB_HEADER){
	// 	--e;
	// }
	// todo not properly handleing active containers here

	// group header active
	// scan backwards through elements and find the correct parent
	while ((e - i)->group_tag != e->group_tag 
			|| !((e - i)->type == UI_GROUP_HEADER 
				|| (e - i)-> type == UI_TAB_HEADER)){
		++i;
	}
	return id - i;
}

int32_t ui_group_start(ui_interface_t *ui, ui_header_t *u){

	ui->active_group += 1;
	return ui_element_init(ui, u, 0, UI_GROUP_HEADER);
}

void ui_group_stop(ui_interface_t *ui){
	
	ui_group_header_finalise(ui);
	ui->active_group -= 1;
	if (ui->active_group == 0){
		ui_element_init(ui, NULL, 0, UI_GROUP_END);
	}
	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

static void ui_label_create(ui_interface_t *ui, uint16_t id, ui_label_t *l){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t text_col;
	if (l->colour == 0){
		text_col = ui->theme.text_col;
	}
	else {
		text_col = l->colour;
	}
	int w, h;

	int text_verts = ui_string_vert_count(l->str);
	e->index = ui_buffer_init(text_verts * sizeof(ui_vertex_t));

	ui_string_size_pixels(l->str, &ui->font[e->font_id], &w, &h);
	if (!l->w){
		l->w = w;
	}
	if (!l->h){
		l->h = h;
	}
	e->num_verts =
	ui_text_create(e->index, l->str, &ui->font[e->font_id], e->x, e->y, l->w, l->h, text_col, l->align);

	
	e->data.l = *l;
	e->data.l.str = ui_strdup(l->str);

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + l->w;
	e->bounds[3] = e->y + l->h;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + l->w;
	e->content_region[3] = e->y + l->h;
	assert(e->num_verts == text_verts);
}

static void ui_node_create(ui_interface_t *ui, uint16_t id, ui_node_t *n){

	ui_element_t *e = &ui->elements[id];
	float line_height = ui->font[e->font_id].line_height;
	uint32_t text_col = ui->theme.text_col;
	uint32_t pad = ui->theme.padding_size;
	uint32_t title_rect_col = ui->theme.node_head_col;
	uint32_t content_rect_col = ui->theme.node_content_col;
	uint32_t input_col = ui->theme.node_in_connect_col;
	uint32_t output_col = ui->theme.node_out_connect_col;

	uint32_t vert_count = 2 * RECTANGLE_VERTS + n->inputs * TRIANGLE_VERTS + n->outputs * TRIANGLE_VERTS;
	uint32_t pad_w, pad_h;
	ui_string_size_pixels("TTT", &ui->font[e->font_id], &pad_w, &pad_h);
	// Calculate the height of the node
	float height = MAX(n->inputs, n->outputs) + 2;
	height *= line_height;
	// Calculate the width of the node
	int title_w, title_h;
	ui_string_size_pixels(n->title, &ui->font[e->font_id], &title_w, &title_h);
	vert_count += ui_string_vert_count(n->title);

	int w, h;
	int max_w = 0;
	int max_w_in = 0;
	int max_w_out = 0;
	for (int i = 0; i < n->inputs; ++i){
		ui_string_size_pixels(n->input_labels[i], &ui->font[e->font_id], &w, &h);
		vert_count += ui_string_vert_count(n->input_labels[i]);
		max_w_in = max_w_in > w ? max_w_in : w;
	}
	for (int i = 0; i < n->outputs; ++i){
		ui_string_size_pixels(n->output_labels[i], &ui->font[e->font_id], &w, &h);
		vert_count += ui_string_vert_count(n->output_labels[i]);
		max_w_out = max_w_out > w ? max_w_out : w;
	}
	max_w = max_w_in + max_w_out + pad_w;
	max_w = MAX(max_w, title_w + pad_w);

	e->index = ui_buffer_init(vert_count * sizeof(ui_vertex_t));
	ui_vertex_t *buf = e->index;
	// Draw rectangle for title
	buf += ui_rectangle_create(buf, e->x, e->y, max_w, line_height, title_rect_col);
	// Draw rextangle to contain inputs and outputs
	buf += ui_rectangle_create(buf, e->x, e->y + line_height, max_w, height - line_height, content_rect_col);
	// Draw termination points
	v2 p1, p2, p3;
	float edge_size = line_height * 0.6666667f;
	float y_start = 2.0f * line_height - 0.333333f * line_height;
	float tri_height = sqrtf(edge_size * edge_size * 0.75f);
	p1.x = e->x;
	p1.y = e->y + y_start;
	p2.x = e->x - tri_height;
	p2.y = e->y + 2.0f * line_height;
	p3.x = e->x;
	p3.y = e->y + y_start + edge_size;

	for (int i = 0; i < n->inputs; ++i){
		buf += ui_make_triangle(buf, p1, p2, p3, input_col);
		p1.y += line_height;
		p2.y += line_height;
		p3.y += line_height;
	}
	p1.x = e->x + max_w;
	p1.y = e->y + y_start;
	p2.x = e->x + max_w + tri_height;
	p2.y = e->y + 2.0f * line_height;
	p3.x = e->x + max_w;
	p3.y = e->y + y_start + edge_size;
	for (int i = 0; i < n->outputs; ++i){
		buf += ui_make_triangle(buf, p1, p2, p3, output_col);
		p1.y += line_height;
		p2.y += line_height;
		p3.y += line_height;
	}

	// Create the text vertices
	buf += ui_text_create(buf, n->title, &ui->font[e->font_id], e->x, e->y, max_w, line_height, text_col, UI_CENTERED_X);
	
	y_start = e->x + line_height * 1.5f;
	for (int i = 0; i < n->inputs; ++i){
		buf += ui_text_create(buf, n->input_labels[i], &ui->font[e->font_id], e->x + pad, y_start + line_height * i, max_w, line_height, text_col, UI_LEFT);
	}
	for (int i = 0; i < n->outputs; ++i){
		buf += ui_text_create(buf, n->output_labels[i], &ui->font[e->font_id], e->x, y_start + line_height * i, max_w - pad, line_height, text_col, UI_RIGHT);
	}
	// Setup element boundries
	e->content_region[0] = e->x - tri_height;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + max_w + tri_height;
	e->content_region[3] = e->y + height;
	
	e->bounds[0] = e->x - tri_height;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + max_w + tri_height;
	e->bounds[3] = e->y + height;

	// copy data to element
	e->data.node.title = ui_strdup(n->title);
	e->data.node.inputs = n->inputs;
	e->data.node.outputs = n->outputs;
	e->data.node.input_labels = calloc(n->inputs, sizeof(char *));
	for (int i = 0; i < n->inputs; ++i){
		e->data.node.input_labels[i] = ui_strdup(n->input_labels[i]);
	}
	e->data.node.output_labels = calloc(n->outputs, sizeof(char *));
	for (int i = 0; i < n->outputs; ++i){
		e->data.node.output_labels[i] = ui_strdup(n->output_labels[i]);
	}
	e->data.node.in_link_ids = calloc(n->inputs, sizeof(*e->data.node.in_link_ids));
	for (int i = 0; i < n->inputs; ++i){
		e->data.node.in_link_ids[i] = -1;
	}
	e->data.node.out_link_ids = calloc(n->outputs, sizeof(*e->data.node.out_link_ids));
	for (int i = 0; i < n->outputs; ++i){
		e->data.node.out_link_ids[i] = -1;
	}

	e->num_verts = vert_count;
	assert(vert_count == (buf - e->index));
}

// todo we need to delete these links if the parent node is removed
// probably with a special case in ui_element_delete
static void ui_node_link_create(ui_interface_t *ui, uint16_t id, ui_node_link_t *l){

	ui_element_t *e = &ui->elements[id];
	int out_id;
	int in_id;

	if (l->in_node_id != -1){
		in_id = ui_find_array_index(ui, l->in_node_id);
		ui_element_t *dst = &ui->elements[in_id];
		dst->data.node.in_link_ids[l->in_id] = e->uuid;
	}
	if (l->out_node_id != -1){
		out_id = ui_find_array_index(ui, l->out_node_id);
		ui_element_t *src = &ui->elements[out_id];
		src->data.node.out_link_ids[l->out_id] = e->uuid;
	}

	e->data.link = *l;
	e->data.link.state = NOT_CONNECTED;
	// todo have options for different link style
	e->index = ui_buffer_init(sizeof(ui_vertex_t) * RECTANGLE_VERTS);
	e->num_verts = ui_draw_node_link(ui, e->index, &e->data.link);

	e->content_region[0] = -1;
	e->content_region[1] = -1;
	e->content_region[2] = -1;
	e->content_region[3] = -1;
	
	e->bounds[0] = -1;
	e->bounds[1] = -1;
	e->bounds[2] = -1;
	e->bounds[3] = -1;
	#ifdef DEBUG_NODES
	printf("NEW: uuid = %d : input_node = %d connected at %d : output_node = %d connected at %d\n", 
		e->uuid, e->data.link.in_node_id, e->data.link.in_id, e->data.link.out_node_id, e->data.link.out_id);
	#endif
}

static int ui_node_left_click_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_node_t *n = &e->data.node;
	float line_height = ui->font[e->font_id].line_height;
	// Increased size of click target here
	float edge_size = line_height * 0.6666667f;
	float y_start = 2.0f * line_height - 0.5f * line_height;//0.333333f * line_height;
	float tri_height = sqrtf(edge_size * edge_size * 0.75f);
	float tri_height_clickable = sqrtf(line_height * line_height *0.75f);
	v2 p1, p2, p3, mouse_vec;
	mouse_vec.x = MOUSE_X;
	mouse_vec.y = MOUSE_Y;

	// ---------------------------------------------------------
	// 				Output side of the node
	// ---------------------------------------------------------
	if (mouse_vec.x > e->content_region[2] - tri_height){
		p1.x = e->bounds[2] - tri_height;
		p1.y = e->y + y_start;
		p2.x = p1.x + tri_height_clickable;
		p2.y = e->y + y_start + 0.5f * line_height;
		p3.x = e->bounds[2] - tri_height;
		p3.y = e->y + y_start + line_height;
		for (int i = 0; i < n->outputs; ++i){
			// test each output
			if (ui_point_in_triangle(p1, p2, p3, mouse_vec)){
				int link_id;
				// check that a link does not already exist on this output
				if (n->out_link_ids[i] == -1){
					ui_node_link_t l;
					l.in_node_id = -1;
					l.out_node_id = e->uuid;
					l.in_id = -1;
					l.out_id = i;
					link_id = ui_element_init(ui, &l, 0, UI_NODE_LINK);
				}
				else {
					link_id = n->out_link_ids[i];
					n->out_link_ids[i] = -1;
					int array_id = ui_find_array_index(ui, link_id);
					ui_node_link_t *l = &ui->elements[array_id].data.link;
					// Inform user that link is disconnecting
					l->state = DISCONNECTING;
					if (e->action != NULL){
						e->action(link_id);
					}
					l->out_id = -1;
					l->out_node_id = -1;
					// ui_print_all_links(ui);
				}
				// Hand over control to the link
				return ui_find_array_index(ui, link_id);
			}
			p1.y += line_height;
			p2.y += line_height;
			p3.y += line_height;
		}
	}
	// ---------------------------------------------------------
	// 				Inputs side of the node
	// ---------------------------------------------------------
	else if (mouse_vec.x < e->x){
		p1.x = e->x;
		p1.y = e->y + y_start;
		p2.x = e->x - tri_height_clickable;
		p2.y = e->y + y_start + 0.5f * line_height;
		p3.x = e->x;
		p3.y = e->y + y_start + line_height;
		for (int i = 0; i < n->inputs; ++i){
			// test each input
			if (ui_point_in_triangle(p1, p2, p3, mouse_vec)){
				int link_id;
				if (n->in_link_ids[i] == -1){
					// New link is being created
					ui_node_link_t l;
					l.in_node_id = e->uuid;
					l.out_node_id = -1;
					l.in_id = i;
					l.out_id = -1;
					link_id = ui_element_init(ui, &l, 0, UI_NODE_LINK);
				}
				else {
					// Link is being disconnected
					link_id = n->in_link_ids[i];
					n->in_link_ids[i] = -1;
					int array_id = ui_find_array_index(ui, link_id);
					ui_node_link_t *l = &ui->elements[array_id].data.link;
					// Inform user that link is disconnecting
					l->state = DISCONNECTING;
					if (e->action != NULL){
						e->action(link_id);
					}
					l->in_id = -1;
					l->in_node_id = -1;
					// ui_print_all_links(ui);
				}
				// Hand over control to the link
				return ui_find_array_index(ui, link_id);
			}
			p1.y += line_height;
			p2.y += line_height;
			p3.y += line_height;
		}
	}
	// The main body of the node
	else {
		// move the node
		// ui_position_element(ui, id, MOUSE_X, MOUSE_Y);
	}
	return id;
}

static void ui_node_move(ui_interface_t *ui, uint32_t id, int offset_x, int offset_y){

	// takes uuid!
	ui_move_element(ui, ui->elements[id].uuid, offset_x, offset_y);
	ui_node_t *n = &ui->elements[id].data.node;

	for (int i = 0; i < n->inputs; ++i){
		if (n->in_link_ids[i] > -1){
			ui_node_link_update(ui, ui_find_array_index(ui, n->in_link_ids[i]));
		}
	}
	for (int i = 0; i < n->outputs; ++i){
		if (n->out_link_ids[i] > -1){
			// update node_link_update to not exist, use fptr and detect mouse up!
			ui_node_link_update(ui, ui_find_array_index(ui,  n->out_link_ids[i]));
		}
	}
}

static void ui_node_link_update(ui_interface_t *ui, uint32_t id){

	// Draw a wire from the starting node to the mouse pointer
	ui_node_link_t *l = &ui->elements[id].data.link;
	ui_element_t *e = &ui->elements[id];
	// printf("NEW: uuid = %d : input_node = %d connected at %d : output_node = %d connected at %d\n", 
	// 	e->uuid, l->in_node_id, l->in_id, l->out_node_id, l->out_id);
	if (l->in_node_id < 0 && l->out_node_id < 0){
		// node link is completed and we are awaiting the user to release the mouse button
		return;
	}
	e->num_verts = ui_draw_node_link(ui, e->index, l);
}

static int ui_draw_node_link(ui_interface_t *ui, ui_vertex_t *buf, ui_node_link_t *l){

	ui_element_t *e;
	uint32_t col = ui->theme.node_link_col;
	v2 p[2];
	int in_id = ui_find_array_index(ui, l->in_node_id);
	int out_id = ui_find_array_index(ui, l->out_node_id);
	// Find the active node
	if (l->in_node_id != -1){
		e = &ui->elements[in_id];
		float line_height = ui->font[e->font_id].line_height;
		p[0].x = e->bounds[0];
		p[0].y = e->bounds[1] + line_height + line_height * (l->in_id + 1);
	}
	else {
		p[0].x = MOUSE_X;
		p[0].y = MOUSE_Y;
	}
	if (l->out_node_id != -1){
		e = &ui->elements[out_id];
		float line_height = ui->font[e->font_id].line_height;
		p[1].x = e->bounds[2];
		p[1].y = e->bounds[1] + line_height + line_height * (l->out_id + 1);
	}
	else {
		p[1].x = MOUSE_X;
		p[1].y = MOUSE_Y;
	}

	ui->modified |= GEOM_MODIFIED;
	#ifdef DEBUG_NODES
	assert(p[0].x >= 0);
	assert(p[0].x < UI_DISPLAY_WIDTH);
	assert(p[1].x >= 0);
	assert(p[1].x < UI_DISPLAY_WIDTH);
	assert(p[0].y >= 0);
	assert(p[0].y < UI_DISPLAY_HEIGHT);
	assert(p[1].y >= 0);
	assert(p[1].y < UI_DISPLAY_HEIGHT);
	#endif
	return ui_line_strip_create(buf, p, 2, col, ui->theme.line_width);
}

static void ui_node_link_finalise(ui_interface_t *ui, uint32_t id){

	int link_id = ui->elements[id].uuid;
	ui_node_link_t *l = &ui->elements[id].data.link;
	v2 mouse_vec, p1, p2, p3;
	mouse_vec.x = MOUSE_X;
	mouse_vec.y = MOUSE_Y;
	int32_t *region;
	int selected_node = -1;
	int success = 0;
	// find if we got a valid click on a node
	for (int i = 0; i < ui->active_elements; ++i){
		if (ui->elements[i].type == UI_NODE){
			// if (ui->elements[i].flags & UI_HIDDEN){
			// 	continue;
			// }
			region = ui->elements[i].content_region;
			if (mouse_vec.x >= region[0] && mouse_vec.x <= region[2]){
				if (mouse_vec.y >= region[1] && mouse_vec.y <= region[3]){
					selected_node = i;
					break;
				}
			}
		}
	}

	do {
		if (selected_node != -1){
			// check if an input or output was selected
			ui_element_t *e = &ui->elements[selected_node];
			ui_node_t *n = &e->data.node;
			float line_height = ui->font[e->font_id].line_height;
			float edge_size = line_height * 0.6666667f;
			float y_start = 2.0f * line_height - 0.5f * line_height;
			float tri_height = sqrtf(edge_size * edge_size * 0.75f);
			float tri_height_clickable = sqrtf(line_height * line_height *0.75f);
			// Output side of the node
			if (mouse_vec.x > e->content_region[2] - tri_height){
				if (l->in_node_id == -1){
					// User trying to connect an output to an output
					break;
				}
				p1.x = e->bounds[2] - tri_height;
				p1.y = e->y + y_start;
				p2.x = p1.x + tri_height_clickable;
				p2.y = e->y + y_start + 0.5f * line_height;
				p3.x = e->bounds[2] - tri_height;
				p3.y = e->y + y_start + line_height;
				for (int i = 0; i < n->outputs; ++i){
					// test each output
					if (ui_point_in_triangle(p1, p2, p3, mouse_vec)){
						l->out_node_id = e->uuid;
						l->out_id = i;
						e->data.node.out_link_ids[i] = link_id;
						success = 1;
						break;
					}
					p1.y += line_height;
					p2.y += line_height;
					p3.y += line_height;
				}
			}
			// Inputs side of the node
			else if (mouse_vec.x < e->x){
				if (l->out_node_id == -1){
					// User trying to connect outputs together
					break;
				}
				p1.x = e->x;
				p1.y = e->y + y_start;
				p2.x = e->x - tri_height_clickable;
				p2.y = e->y + y_start + 0.5f * line_height;
				p3.x = e->x;
				p3.y = e->y + y_start + line_height;
				for (int i = 0; i < n->inputs; ++i){
					// test each input
					if (ui_point_in_triangle(p1, p2, p3, mouse_vec)){
						l->in_node_id = e->uuid;
						l->in_id = i;
						e->data.node.in_link_ids[i] = link_id;
						success = 1;
						break;
					}
					p1.y += line_height;
					p2.y += line_height;
					p3.y += line_height;
				}
			}
		}
	} while (0);
	// otherwise delete the link
	if (!success){
		#ifdef DEBUG_NODES
		printf("ERROR: failed to click an appropriate node connection, cancel link\n");
		#endif
		// generating a magic negative value to inform event loop that this link should be deleted
		// Note: we need to be able to recover the correct value when the link is deleted
		l->state = NOT_CONNECTED;
		l->in_node_id -= MAX_UI_ELEMENTS;
	}
	else {
		ui_element_t *e = &ui->elements[id];
		e->num_verts = ui_draw_node_link(ui, e->index, l);
		// Callback with the link id to the nodes callback function
		l->state = CONNECTED;
		if (ui->elements[selected_node].action != NULL){
			ui->elements[selected_node].action(ui->elements[e->uuid].uuid);
		}
	}
	#ifdef DEBUG_NODES
	ui_print_all_links(ui);
	#endif
}

static void ui_delete_link(ui_interface_t *ui, uint32_t id){

	#ifdef DEBUG_NODES
	ui_log_elements(ui);
	printf("INFO: Deleting Link : ID = %d UUID = %d\n", id, ui->elements[id].uuid);
	#endif

	ui_node_link_t *l = &ui->elements[id].data.link;
	int array_id;
	if (l->in_node_id >= 0){
		array_id = ui_find_array_index(ui, l->in_node_id);
		ui->elements[array_id].data.node.in_link_ids[l->in_id] = -1;
	}
	if (l->out_node_id >= 0){
		array_id = ui_find_array_index(ui, l->out_node_id);
		ui->elements[array_id].data.node.out_link_ids[l->out_id] = -1;
	}
	ui_element_delete(ui, ui->elements[id].uuid);
}

static void ui_print_all_links(ui_interface_t *ui){

	ui_element_t *e = ui->elements;
	for (int i = 0; i < ui->active_elements; ++i){
		if (e->type == UI_NODE_LINK){
			printf("LINK: %2d, uuid = %2d : input_node = %2d connected at %2d : output_node = %2d connected at %2d\n", 
				i, e->uuid, e->data.link.in_node_id, e->data.link.in_id, e->data.link.out_node_id, e->data.link.out_id);
		}
		else if (e->type == UI_NODE){
			printf("NODE: %2d, uuid = %2d Node inputs are: ", i, e->uuid);
			for (int y = 0; y < e->data.node.inputs; ++y){
				printf("| %3d ", e->data.node.in_link_ids[y]);
			}
			printf("\n");
			printf("                   Node outputs are: ");
			for (int y = 0; y < e->data.node.outputs; ++y){
				printf("| %3d ", e->data.node.out_link_ids[y]);
			}
			printf("\n");
		}
		++e;
	}
}

ui_node_link_t ui_get_link_state(ui_interface_t *ui, uint32_t uid){

	return ui->elements[ui_find_array_index(ui, uid)].data.link;
}

static int ui_point_in_triangle(v2 a, v2 b, v2 c, v2 p){

	float u, v;
	//// Compute vectors        
	v2 vec0 = ui_sub_v2(c, a);
	v2 vec1 = ui_sub_v2(b, a);
	v2 vec2 = ui_sub_v2(p, a);

	// Compute dot products
	float dot00 = ui_dot_v2(vec0, vec0);
	float dot01 = ui_dot_v2(vec0, vec1);
	float dot02 = ui_dot_v2(vec0, vec2);
	float dot11 = ui_dot_v2(vec1, vec1);
	float dot12 = ui_dot_v2(vec1, vec2);

	// Compute barycentric coordinates
	float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
	u = (dot11 * dot02 - dot01 * dot12) * invDenom;
	v = (dot00 * dot12 - dot01 * dot02) * invDenom;

	// Check if point is in triangle
	return (u >= 0) && (v >= 0) && ((u + v) < 1);
}

#define CHECKBOX_VERTS 12
static void ui_checkbox_create(ui_interface_t *ui, uint16_t id, ui_checkbox_t *c){

	ui_element_t *e = &ui->elements[id];
	e->data.c = *c;
	e->data.c.str = ui_strdup(c->str);

	int text_verts = ui_string_vert_count(c->str);
	e->index = ui_buffer_init((CHECKBOX_VERTS + text_verts) * sizeof(ui_vertex_t));
	for (int i = 0; i < CHECKBOX_VERTS; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	ui_vertex_t *buf = e->index;

	uint32_t col1;
	uint32_t text_col = ui->theme.text_col;
	uint32_t col2 = ui->theme.checkbox_deselected_col;

	if (c->state){
		col1 = ui->theme.checkbox_selected_col;
	}
	else {
		col1 = ui->theme.checkbox_deselected_col;
	}
	uint32_t i = 0;
	v2 box_center, box_tl, box_tr, box_bl, box_br;
	float box_size = ui->font[e->font_id].line_height;

	box_center.x = e->x + box_size / 2.0f;
	box_center.y = e->y + box_size / 2.0f;
	box_tl.x = e->x;
	box_tl.y = e->y;
	box_tr.x = box_tl.x + box_size;
	box_tr.y = box_tl.y;
	box_bl.x = e->x;
	box_bl.y = e->y + box_size;
	box_br.x = e->x + box_size;
	box_br.y = e->y + box_size;

	buf[i].x = box_center.x;		buf[i].y = box_center.y;
	buf[i++].colour = col1;
	buf[i].x = box_tl.x;			buf[i].y = box_tl.y;
	buf[i++].colour = col2;
	buf[i].x = box_bl.x;			buf[i].y = box_bl.y;
	buf[i++].colour = col2;
	
	buf[i].x = box_center.x;		buf[i].y = box_center.y;
	buf[i++].colour = col1;
	buf[i].x = box_bl.x;			buf[i].y = box_bl.y;
	buf[i++].colour = col2;
	buf[i].x = box_br.x;			buf[i].y = box_br.y;
	buf[i++].colour = col2;

	buf[i].x = box_center.x;		buf[i].y = box_center.y;
	buf[i++].colour = col1;
	buf[i].x = box_br.x;			buf[i].y = box_br.y;
	buf[i++].colour = col2;
	buf[i].x = box_tr.x;			buf[i].y = box_tr.y;
	buf[i++].colour = col2;

	buf[i].x = box_center.x;		buf[i].y = box_center.y;
	buf[i++].colour = col1;
	buf[i].x = box_tr.x;			buf[i].y = box_tr.y;
	buf[i++].colour = col2;
	buf[i].x = box_tl.x;			buf[i].y = box_tl.y;
	buf[i++].colour = col2;
	e->num_verts += i;

	int w, h;
	ui_string_size_pixels(c->str, &ui->font[e->font_id], &w, &h);

	e->content_region[0] = box_tl.x;
	e->content_region[1] = box_tl.y;
	e->content_region[2] = box_br.x;
	e->content_region[3] = box_br.y;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + w + box_size + 2 * ui->theme.padding_size;
	e->bounds[3] = e->y + box_size;

	float start_text = e->x + box_size +  2 * ui->theme.padding_size;
	float width = e->bounds[2] - start_text;
	e->num_verts +=
	ui_text_create(e->index + e->num_verts, c->str, &ui->font[e->font_id], start_text, e->y, width, box_size, text_col, UI_CENTERED_Y);
	assert(e->num_verts == text_verts + CHECKBOX_VERTS);
}

static void ui_checkbox_col_swap(ui_interface_t *ui, uint16_t id){
	
	uint32_t col1 = ui->theme.checkbox_selected_col;
	uint32_t col2 = ui->theme.checkbox_deselected_col;

	ui_vertex_t *buf = ui->elements[id].index;
	uint8_t *state = &ui->elements[id].data.c.state;
	*state ^= 1;

	if (*state){
		for (int i = 0; i < 12; ++i){
			if ((i % 3)){
				buf[i].colour = col2;
			}
			else {
				buf[i].colour = col1;
			}
		}
	}
	else {
		for (int i = 0; i < 12; ++i){
			if ((i % 3)){
				buf[i].colour = col2;
			}
			else {
				buf[i].colour = col2;
			}
		}
	}
	ui->modified |= GEOM_MODIFIED;
}

// todo we need to account for descendes when sizing this box
static void ui_list_box_create(ui_interface_t *ui, uint32_t id, ui_list_box_t *l){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t text_col = ui->theme.text_col;
	int line_height = ui->font[e->font_id].line_height;
	uint32_t col_l = ui->theme.list_box_background_col;
	uint32_t col_r = ui->theme.list_box_border_col;
	uint32_t border_col = ui->theme.list_box_border_col;
	uint32_t border_size = ui->theme.list_box_border_size;
	// uint32_t pad = ui->theme.menubar_padding;

	e->data.lb.selected_item = l->selected_item;
	e->data.lb.count = l->count;
	e->data.lb.strings = malloc(sizeof(char *) * l->count);
	int text_verts = 0;
	int w = 0;
	int h = 0;
	int width = 0;
	int longest = 0;
	int len;
	for (int i = 0; i < l->count; ++i){
		ui_string_size_pixels(l->strings[i], &ui->font[e->font_id], &w, &h);
		len = ui_strlen(l->strings[i]);
		longest = longest > len ? longest : len;
		width = width > w ? width : w;
	}
	// Pad the string with spaces so that we have a stable number of verts
	char *str = malloc(longest + 1);
	for (int i = 0; i < l->count; ++i){
		ui_strpad(str, l->strings[i], longest);
		e->data.lb.strings[i] = ui_strdup(str);
	}
	text_verts = ui_string_vert_count(str);
	free(str);
	width += line_height * 2 + line_height / 2;
	h += 2 * border_size;

	e->index = ui_buffer_init((LIST_BOX_VERTS + text_verts) * sizeof(ui_vertex_t));
	e->num_verts += 
		ui_list_box_rrect_bordered(e->index + e->num_verts, e->x, e->y, width, line_height + 2 * border_size, col_l, col_r, border_col, border_size);

	// up/down triangle
	v2 a1, a2, a3;
	int tri_padding = line_height / 4;
	a1.x = e->x + width - border_size - line_height + tri_padding;
	a1.y = e->y + border_size + tri_padding;
	a2.x = e->x + width - border_size - tri_padding;
	a2.y = a1.y;
	a3.x = e->x + width - border_size - line_height / 2.0f;
	a3.y = e->y + h - border_size - tri_padding;

	e->num_verts +=
		ui_make_triangle(e->index + e->num_verts, a1, a2, a3, col_l);
	// text for the option 0
	int sx = e->x + line_height / 2;
	e->num_verts +=
		ui_text_create(e->index + e->num_verts, e->data.lb.strings[0], &ui->font[e->font_id], sx, e->y, width, line_height + 2 * border_size, text_col, UI_LEFT | UI_CENTERED_Y);
	// // Rectangle for contents list
	// int content_height =  (l->count - 1) * line_height + 2 * pad + 2 * border_size;
	// e->num_verts +=
	// 	ui_rect_bordered_create(e->index + e->num_verts, e->x, e->y + h + pad, width, content_height, col_l, border_col, border_size);
	// // Text for the remainder of the options
	// int sy = e->y + h + 2 * pad + border_size - line_height;
	// for (int i = 1; i < l->count; ++i){
	// 	e->num_verts +=
	// 	ui_text_create(e->index + e->num_verts, l->strings[i], &ui->font[e->font_id], sx, sy + i * line_height, width, h, text_col, UI_LEFT | UI_CENTERED_Y);
	// }
	// printf("num listboxverts = %d %d\n", e->num_verts, LIST_BOX_VERTS + text_verts);
	// assert(e->num_verts == text_verts + LIST_BOX_VERTS);

	e->content_region[0] = e->x + width - 1.5f * line_height;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + width;
	e->content_region[3] = e->y + h;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + width;
	e->bounds[3] = e->y + h;
}

#define LIST_BOX_HIGHLIGHT_OFFSET RECT_BORDERED_VERTS
static int ui_list_box_contents(ui_interface_t *ui, uint32_t id){

	ui_list_box_t *l = &ui->elements[id].data.lb;
	uint32_t text_col = ui->theme.text_col;
	uint32_t col_l = ui->theme.list_box_background_col;
	// uint32_t col_r = ui->theme.list_box_border_col;
	uint32_t border_col = ui->theme.list_box_border_col;
	uint32_t border_size = ui->theme.list_box_border_size;
	uint32_t pad = ui->theme.menubar_padding;
	uint32_t text_verts = 0;
	int h = ui->elements[id].bounds[3] - ui->elements[id].bounds[1];
	int width = ui->elements[id].bounds[2] - ui->elements[id].bounds[0];

	// uint32_t current_align = ui->current_align;
	// ui_set_align(ui, 0);
	ui_element_t *e = &ui->elements[id];
	int uuid = ui_element_init(ui, NULL, 0, UI_LIST_BOX_CONTENT);
	ui_position_element(ui, uuid, e->x, e->y);
	// ui_set_align(ui, current_align);

	for (int i = 1; i < l->count; ++i){
		text_verts += ui_string_vert_count(l->strings[i]);
	}
	int array_id = ui_find_array_index(ui, uuid);
	e = &ui->elements[array_id];

	int line_height = ui->font[e->font_id].line_height;
	int sx = e->x + line_height / 2;
	// e->parent = id;
	e->flags |= UI_HIDDEN;
	e->index = ui_buffer_init((LIST_BOX_CONTENT_VERTS + text_verts + RECTANGLE_VERTS * (l->count - 1)) * sizeof(ui_vertex_t));
	// Rectangle for contents list
	int content_height = (l->count - 1) * line_height + 2 * pad + 2 * border_size;
	e->num_verts =
		ui_rect_bordered_create(e->index, e->x, e->y + h + pad, width, content_height, col_l, border_col, border_size);
	// Rectangle for highlights
	int xpos = e->x + line_height / 4;
	int ypos = e->y + pad + border_size + h + pad;
	for (int i = 1; i < l->count; ++i){
		e->num_verts += ui_rectangle_create(e->index + e->num_verts, xpos, ypos, width - line_height / 2, line_height, 0x00);
		ypos += line_height;
	}
	// Text for the options
	int sy = e->y + h + 2 * pad + border_size - line_height;
	for (int i = 1; i < l->count; ++i){
		e->num_verts +=
		ui_text_create(e->index + e->num_verts, l->strings[i], &ui->font[e->font_id], sx, sy + i * line_height, width, h, text_col, UI_LEFT | UI_CENTERED_Y);
	}

	e->content_region[0] = xpos;
	e->content_region[1] = ypos - (l->count - 1) * line_height;
	e->content_region[2] = e->x + width - line_height / 2;
	e->content_region[3] = ypos;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + width;
	e->bounds[3] = e->y + h + pad + content_height;

	return ui->elements[id].uuid;
}

static int ui_list_box_open(ui_interface_t *ui, uint32_t id){

	// ui->elements[id + 1].flags &= ~UI_HIDDEN;
	// ui_list_box_t *lb = &ui->elements[id].data.lb;
	// uint32_t size = 0;

	// for (int i = 0; i < lb->count; ++i){
	// 	size += ui_string_vert_count(lb->strings[i]);
	// }
	float ft = UI_FRAME_RATE / 3.0f;
	ui_element_t *e = &ui->elements[id + 1];
	uint32_t size = e->num_verts;
	uint32_t alpha = 0;
	ui_buffer_reserve(&ui->dynamic_buffer, size * sizeof(ui_vertex_t));
	ui_vertex_t *src = e->index;
	ui_vertex_t *dst = ui->dynamic_buffer + ui->num_dynamic_verts;
	if (e->frame <= ft){
		alpha = (int)(UI_LERP(255, 0, e->frame / ft));
		ui_set_alpha(e->index, size, alpha);
		e->frame += 1;
	}
	src = e->index;
	memcpy(dst, src, size * sizeof(ui_vertex_t));
	ui->num_dynamic_verts += size;

	// Wait for mouse up
	if (!MOUSE_L && alpha == 0xFF){
		ui->action = ui_list_box_update;
		ui->action_flags &= !LEFT_HELD;
		e->frame = 0;
	}
	return 0;
}

static void ui_set_alpha(ui_vertex_t *buf, uint32_t count, uint32_t alpha){

	for (int i = 0; i < count; ++i, ++buf){
		buf->colour = buf->colour & 0x00FFFFFF;
		buf->colour = buf->colour | (alpha << 24);
	}
}

static int ui_list_box_update(ui_interface_t *ui, uint32_t id){

	uint32_t col = ui->theme.menu_hovered_col;
	ui_element_t *e = &ui->elements[id];
	ui_list_box_t *lb = &e->data.lb;
	int selected_item = 0;
	int line_height = ui->font[e->font_id].line_height;
	uint32_t border_size = ui->theme.list_box_border_size;
	int h = line_height + 2 * border_size;

	// mouse up event
	if (!MOUSE_L && (ui->action_flags & LEFT_HELD)){
		ui->action = NULL;
		ui->action_flags &= !LEFT_HELD;
		ui->selected_element = MAX_UI_ELEMENTS;
		if (lb->selected_item){
			int sx = e->x + line_height / 2;
			int width = e->content_region[2] - e->content_region[0];
			uint32_t text_col = ui->theme.text_col;
			// int text_verts = 
				ui_text_create(e->index + LIST_BOX_VERTS, lb->strings[lb->selected_item], &ui->font[e->font_id], sx, e->y, width, h, text_col, UI_LEFT | UI_CENTERED_Y);
			// e->num_verts = LIST_BOX_VERTS + text_verts;
			ui->modified = GEOM_MODIFIED;
		}
		return 0;
	}
	// Advance the pointer to the contents box;
	++e;
	// Reset highlight to invisible
	ui_colour_verts(e->index + LIST_BOX_HIGHLIGHT_OFFSET, (lb->count - 1) * RECTANGLE_VERTS, 0x00000000);
	// highlitgh the hovered item
	if (MOUSE_X > e->content_region[0] && MOUSE_X < e->content_region[2]){
		if (MOUSE_Y > e->content_region[1] && MOUSE_Y < e->content_region[3]){
			selected_item = MOUSE_Y - e->content_region[1];
			selected_item /= line_height;
			ui_colour_verts(e->index + LIST_BOX_HIGHLIGHT_OFFSET + selected_item * RECTANGLE_VERTS, RECTANGLE_VERTS, col);
			++selected_item;
		}
	}
	if (MOUSE_L){
		ui->action_flags |= LEFT_HELD;
		if (selected_item){
			lb->selected_item = selected_item;
		}
		return 0;
	}
	uint32_t size = e->num_verts;
	ui_buffer_reserve(&ui->dynamic_buffer, size * sizeof(ui_vertex_t));
	ui_vertex_t *src = e->index;
	ui_vertex_t *dst = ui->dynamic_buffer + ui->num_dynamic_verts;
	src = e->index;
	memcpy(dst, src, size * sizeof(ui_vertex_t));
	ui->num_dynamic_verts += size;
	return 0;
}
// Parameter everts is the geometry vertices excluding any texture supporting verts for text
#define PARAMETER_VERTS (RECT_BORDERED_VERTS + 6)
static void ui_parameter_create(ui_interface_t *ui, uint16_t id, ui_parameter_t *p){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t text_col = ui->theme.text_col;
	e->data.p = *p;
	e->data.p.title = ui_strdup(p->title);
	uint32_t center_col = ui->theme.background_col;
	uint32_t border_col = ui->theme.border_col;
	uint32_t border_size = ui->theme.parameter_pad_size;
	uint32_t border_line_width = ui->theme.parameter_line_width;
	uint32_t height = ui->font[e->font_id].line_height + border_size * 2;

	int text_verts = ui_string_vert_count(p->title);
	e->index = ui_buffer_init((PARAMETER_VERTS + text_verts) * sizeof(ui_vertex_t));
	// for (int i = 0; i < PARAMETER_VERTS; ++i){
	// 	e->index[i].u = GEOM_UV;
	// 	e->index[i].v = GEOM_UV;
	// }
	// containing rectangle
	e->num_verts = 
		ui_rect_bordered_create(e->index, e->x, e->y, p->w, height, center_col, border_col, border_line_width);
	// step up/down triangles
	v2 a1, a2, a3, b1, b2, b3;
	int line_height = ui->font[e->font_id].line_height;
	int tri_padding = line_height / 4;
	a1.x = e->x + p->w - border_size - line_height + tri_padding;
	a1.y = e->y + border_size + tri_padding;;
	a2.x = e->x + p->w - border_size - tri_padding;
	a2.y = a1.y;
	a3.x = e->x + p->w - border_size - line_height / 2.0f;
	a3.y = e->y + height - border_size - tri_padding;
	b1.x = e->x + p->w - border_size - line_height * 1.5;
	b1.y = a1.y;
	b2.x = e->x + p->w - border_size - line_height - tri_padding;
	b2.y = a3.y;
	b3.x = e->x + p->w - border_size - line_height * 2 + tri_padding;
	b3.y = a3.y;

	e->num_verts +=
	ui_make_triangle(e->index + e->num_verts, a1, a2, a3, border_col);
	e->num_verts +=
	ui_make_triangle(e->index + e->num_verts, b1, b2, b3, border_col);

	int text_width = p->w - 2 * height - 4 * ui->theme.border_size;
	int sx = e->x + 2 * ui->theme.padding_size;

	e->num_verts +=
	ui_text_create(e->index + e->num_verts, p->title, &ui->font[e->font_id], sx, e->y, text_width, height, text_col, UI_LEFT | UI_CENTERED_Y);

	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + p->w;
	e->bounds[3] = e->y + height;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + p->w;
	e->content_region[3] = e->y + height;

	assert(e->num_verts == PARAMETER_VERTS + text_verts);
	ui_buffer_reserve(&e->index, VALUE_VERTS * sizeof(ui_vertex_t));
}

static void ui_parameter_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_parameter_t *p = &e->data.p;
	int border_size = ui->theme.border_size;
	int height = e->bounds[3] - e->bounds[1];
	int location = e->bounds[2] - MOUSE_X - border_size;

	// click is in the button area
	if (location < 2 * height){
		if (location < height){
			p->value -= p->step_size;
			p->value = p->value < p->start_val ? p->start_val : p->value;
		}
		else {
			p->value += p->step_size;		
			p->value = p->value > p->end_val ? p->end_val : p->value;
		}
		ui->modified |= TEXT_MODIFIED;
	}
}

static void ui_parameter_set(ui_interface_t *ui, uint16_t id, int16_t dy){

	ui_parameter_t *p = &ui->elements[id].data.p;
	float delta = -dy;
	delta *= p->steps_per_pixel * p->step_size;
	p->value += delta;
	p->value = p->value < p->start_val ? p->start_val : p->value;
  	p->value = p->value > p->end_val ? p->end_val : p->value;

	ui->modified |= TEXT_MODIFIED;
}

#define RBUTTON_VERTS RRECT_BORDERED_VERTS
#define BUTTON_VERTS RECT_BORDERED_VERTS
#define RBUTTON_CENTRAL_VERT_CNT (6 + CIRCLE_SEG_VERTS(SEGMENTS))
#define BUTTON_CENTRAL_VERT_CNT 6
static void ui_button_create(ui_interface_t *ui, uint16_t id, ui_button_t *b){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t col = ui->theme.button_col;
	uint32_t border_col = ui->theme.button_border_col;
	uint32_t text_col = ui->theme.text_col;
	int bw = ui->theme.button_border_size;
	int pad = ui->theme.padding_size;

	//b->state = 0;
	int w, h;
	int text_verts = ui_string_vert_count(b->str);
	ui_string_size_pixels(b->str, &ui->font[e->font_id], &w, &h);
	if (!b->w){
		b->w = w + bw * 2 + pad * 4;
	}
	if (!b->h){
		b->h = h + bw * 2 + pad;
	}

	if (e->type == UI_BUTTON || e->type == UI_BUTTON_RETAIN_STATE){
		e->index = ui_buffer_init((BUTTON_VERTS + text_verts) * sizeof(ui_vertex_t));
		for (int i = 0; i < BUTTON_VERTS; ++i){
			e->index[i].u = GEOM_UV;
			e->index[i].v = GEOM_UV;
		}
		e->num_verts = 
		ui_rect_bordered_create(e->index, e->x, e->y, b->w, b->h, col, border_col, bw);
	}
	else if (e->type == UI_BUTTON_ROUND || e->type == UI_BUTTON_ROUND_RETAIN_STATE){
		e->index = ui_buffer_init((RBUTTON_VERTS + text_verts) * sizeof(ui_vertex_t));
		for (int i = 0; i < RBUTTON_VERTS; ++i){
			e->index[i].u = GEOM_UV;
			e->index[i].v = GEOM_UV;
		}
		e->num_verts = 
		ui_rrect_bordered(e->index, e->x, e->y, b->w, b->h, col, border_col, bw);
	}
	e->num_verts +=
	ui_text_create(e->index + e->num_verts, b->str, &ui->font[e->font_id], e->x, e->y, b->w, b->h, text_col, UI_CENTERED_X | UI_CENTERED_Y);

	e->data.b = *b;
	e->data.b.str = ui_strdup(b->str);

	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + b->w;
	e->content_region[3] = e->y + b->h;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + b->w;
	e->bounds[3] = e->y + b->h;

	if (e->type == UI_BUTTON || e->type == UI_BUTTON_RETAIN_STATE){
		assert(e->num_verts == text_verts + BUTTON_VERTS);
	}
	else {
		assert(e->num_verts == text_verts + RBUTTON_VERTS);
	}

}

// todo custom click detection for circle type
// todo square and hexagon and outlined and textured versions of these

#define SHAPE_BUTTON_CIRCLE_VERTS CIRCLE_SEG_VERTS(SEGMENTS)
#define SHAPE_BUTTON_RECT_VERTS RECTANGLE_VERTS
#define SHAPE_BUTTON_HEX_VERTS HEXAGON_VERTS
static void ui_shape_button_create(ui_interface_t *ui, uint16_t id, ui_shape_button_t *c){
		
	ui_element_t *e = &ui->elements[id];
	uint32_t col;
	if (c->col_on || c->col_off){
		col = c->state ? c->col_on : c->col_off;
	}
	else {
		col = c->state ? ui->theme.button_hovered_col : ui->theme.button_col;
	}
	// uint32_t border_col = ui->theme.button_border_col;
	//int bw = ui->theme.button_border_size;
	// int pad = ui->theme.padding_size;
	int dimension = c->dimension;

	//b->state = 0;
	int segments = SEGMENTS;
	float x = (dimension / 2.0f);
	float y = (dimension / 2.0f);
	int vertex_count = 0;

	if (c->shape == CIRCLE){
		float radius = dimension / 2.0f;
		vertex_count = CIRCLE_SEG_VERTS(SEGMENTS);
		e->index = ui_buffer_init(vertex_count * sizeof(ui_vertex_t));
		e->num_verts = 
		 	ui_circle_seg_create(e->index, x, y, 0, 2.0f * PI, radius, segments, col);
	}
	else if (c->shape == SQUARE){
		vertex_count = RECTANGLE_VERTS;
		e->index = ui_buffer_init(vertex_count * sizeof(ui_vertex_t));
		e->num_verts = 
			ui_rectangle_create(e->index, e->x, e->y, dimension, dimension, col);
	}
	else if (c->shape == HEXAGON){
		float radius = dimension / 2.0f;
		vertex_count = HEXAGON_VERTS;
		e->index = ui_buffer_init(vertex_count * sizeof(ui_vertex_t));
		e->num_verts = 
			ui_hexagon_create(e->index, x, y, radius, col);
	}

	for (int i = 0; i < vertex_count; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	e->data.sb = *c;

	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + dimension;
	e->content_region[3] = e->y + dimension;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + dimension;
	e->bounds[3] = e->y + dimension;

	assert(e->num_verts == vertex_count);	
}

static void ui_shape_buttons_actions(ui_interface_t *ui, uint16_t id){


	// todo
}
//#define RBUTTON_CENTRAL_VERT_CNT (6 + CIRCLE_SEG_VERTS(SEGMENTS))
// todo option for square circular and kexagonal matrices
// maybe refactor out circle_button and hex_button as individual elements
static void ui_matrix_create(ui_interface_t *ui, uint16_t id, ui_matrix_t *m){
	
	ui_element_t *e = &ui->elements[id];
	uint32_t col;
	if (m->button.col_on || m->button.col_off){
		col = m->button.col_off;
	}
	else {
		col = ui->theme.matrix_button_col;
		// need to store these colours to later ascertain matrix state
		m->button.col_off = col;
		m->button.col_on = ui->theme.matrix_button_col2;
	}
	uint32_t background_col = ui->theme.matrix_background_col;
	// int bw = ui->theme.button_border_size;
	int pad = ui->theme.padding_size;
	float button_size = (m->button.dimension);
	uint32_t element_count = m->columns * m->rows;
	int columns = m->columns;
	int rows = m->rows;
	// width and height of the array
	float w;
	float h;
	int count = 0;
	int vert_count = 0;


	// ---------------------------------------------
	// 			Circular elements
	// ---------------------------------------------
	if (m->button.shape == CIRCLE){
		int circle_pad = ui->theme.matrix_circle_padding;
		w = columns * button_size + (columns - 1) * circle_pad + 2 * pad;
		h = rows * button_size + (rows - 1) * circle_pad + 2 * pad;
		float radius = m->button.dimension * 0.5f;
		// Location of the first button
		float x = e->x + radius + pad;
		float y = e->y + radius + pad;
		float start_x = x;
		vert_count = CIRCLE_SEG_VERTS(element_count * SEGMENTS) + RECTANGLE_VERTS;
		e->index = ui_buffer_init(vert_count * sizeof(ui_vertex_t));

		ui_vertex_t *p = e->index;
		count = ui_rectangle_create(p, e->x, e->y, w, h, background_col);
		p += RECTANGLE_VERTS;

		for (int i = 0; i < rows; ++i){
			for (int j = 0; j < columns; ++j){
				 count += ui_circle_seg_create(p, x, y, 0, 2 * PI, radius, SEGMENTS, col);
				 p += CIRCLE_SEG_VERTS(SEGMENTS);
				 x += button_size + circle_pad;
			}
			x = start_x;
			y += button_size + circle_pad;
		}
		e->num_verts = count;
	}
	// --------------------------------------------
	//			Hexagonal elments
	// --------------------------------------------
	else if (m->button.shape == HEXAGON){

		float radius = m->button.dimension * 0.5f;
		float hex_pad = ui->theme.matrix_hex_padding;
		// scale factor for x direction to move hex face down and to the right == 2*cos 30 == sqrt 3
		// for y direction it is 2*sin 30 == 1
		float sqrt3 = 1.732050807f;
		// Location of the first button
		float x = e->x + radius + pad;
		float y = e->y + sqrt3 * 0.5f *radius + pad;
		float start_x = x;
		float start_y = y;
		
		w = 1.5f * radius * columns + 0.5f * radius + hex_pad * 0.5f * sqrt3 * (columns - 1.0f) + 2 * pad;
		h = (rows + 0.5f) * sqrt3 * radius + rows * hex_pad + 2 * pad;

		vert_count = HEXAGON_VERTS * element_count + RECTANGLE_VERTS;
		e->index = ui_buffer_init(vert_count * sizeof(ui_vertex_t));

		ui_vertex_t *p = e->index;
		count = ui_rectangle_create(p, e->x, e->y, w, h, background_col);
		p += RECTANGLE_VERTS;

		float x_advance = 1.5f * radius + sqrt3 * 0.5f * hex_pad;
		float y_advance = sqrt3 * 0.5f * radius + 0.5f * hex_pad; 
		for (int i = 0; i < rows; ++i){
			for (int j = 0; j < columns; ++j){
				 count += ui_hexagon_create(p, x, y, radius, col);
				 p += HEXAGON_VERTS;
				 x += x_advance;
				 y = (j & 1) ? (y - y_advance) : (y + y_advance);
				 // x += 3.0f * radius + sqrt3 * 2.0f * hex_pad;
			}
			// y += y_advance * 2;
			y = start_y + sqrt3 * radius * (i + 1) + hex_pad * (i + 1);
			x = start_x;
		}
		e->num_verts = count;
	}
	// --------------------------------------------
	//			Square elments
	// --------------------------------------------
	else if (m->button.shape == SQUARE){
		int square_pad = ui->theme.matrix_square_padding;
		w = (columns - 1) * square_pad + columns * button_size + 2 * pad;
		h = (rows - 1) * square_pad + rows * button_size + 2 * pad;
		// Location of the first button
		float x = e->x + pad;
		float y = e->y + pad;
		float start_x = x;
		vert_count = element_count * RECTANGLE_VERTS + RECTANGLE_VERTS;
		e->index = ui_buffer_init(vert_count * sizeof(ui_vertex_t));

		ui_vertex_t *p = e->index;
		count = ui_rectangle_create(p, e->x, e->y, w, h, background_col);
		p += RECTANGLE_VERTS;

		for (int i = 0; i < rows; ++i){
			for (int j = 0; j < columns; ++j){
				 count += ui_rectangle_create(p, x, y, button_size, button_size, col);
				 p += RECTANGLE_VERTS;
				 x += button_size + square_pad;
			}
			x = start_x;
			y += button_size + square_pad;
		}
		e->num_verts = count;
	}
	else {
		printf("ERROR: Unknown shape type in ui_matrix_create\n");
		exit(EXIT_FAILURE);
	}
	// setting the texture coordinates
	for (int i = 0; i < vert_count; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}

	// int width = ROUND(w);
	e->content_region[0] = e->x + pad;
	e->content_region[1] = e->y + pad;
	e->content_region[2] = e->x + w - pad;
	e->content_region[3] = e->y + h - pad;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + w;
	e->bounds[3] = e->y + h;

	e->data.mat = *m;
	e->data.mat.states = NULL;
	e->data.mat.states = calloc(element_count, sizeof(uint8_t));
	assert(e->data.mat.states != NULL);
	if (m->states != NULL){
		for (int i = 0; i < element_count; ++i){
			e->data.mat.states[i] = m->states[i];
		}
	}
	assert(e->num_verts == vert_count);
}

static void ui_matrix_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf = e->index;
	ui_matrix_t *m = &e->data.mat;
	uint32_t *content_region = e->content_region;
	// Find which button we are over
	int width = content_region[2] - content_region[0];
	int height = content_region[3] - content_region[1];
	float x_step = 0;
	float y_step = 0;
	int button_id = 0;

	if (m->button.shape == CIRCLE){

		float radius = m->button.dimension * 0.5f;
		float button_size = ui->theme.matrix_circle_padding + m->button.dimension;
		// int pad = ui->theme.matrix_circle_padding;

		x_step = (float)width / m->columns;
		y_step = (float)height / m->rows;
		int x_id = ((MOUSE_X - content_region[0]) / x_step);
		int y_id = ((MOUSE_Y - content_region[1]) / y_step);

		// Get the center of the picked circle
		float button_x = button_size * x_id + radius + content_region[0];
		float button_y = button_size * y_id + radius + content_region[1];
		if (fabsf(button_x - MOUSE_X) < radius){
			if (fabsf(button_y - MOUSE_Y) < radius){
				button_id = x_id + m->columns * y_id;
				ui_matrix_swap_colour(buf, m, button_id);
				ui->modified |= GEOM_MODIFIED;
			}
		}
		// printf("clicked row %d, column %d id = %d\n", y_id, x_id, button_id);
		// printf("x= %f, y = %f mx = %d, my = %d\n", button_x, button_y, MOUSE_X,MOUSE_Y);
	}
	else if (m->button.shape == SQUARE){

		// should maybe add a pad here for accuracy
		x_step = (float)width / m->columns;
		y_step = (float)height / m->rows;
		int x_id = ((MOUSE_X - content_region[0]) / x_step);
		int y_id = ((MOUSE_Y - content_region[1]) / y_step);

		// Test xy id more accurately against the actual button shape
		int pad = ui->theme.matrix_square_padding;
		float button_size = m->button.dimension;
		// top left corner of button in screen coords
		float button_x = (button_size + pad) * x_id + content_region[0];
		float button_y = (button_size + pad) * y_id + content_region[1];
		if ((MOUSE_X - button_x) < button_size){
			if ((MOUSE_Y - button_y) < button_size){
				button_id = x_id + m->columns * y_id;
				ui_matrix_swap_colour(buf, m, button_id);
				ui->modified |= GEOM_MODIFIED;
			}
		}
	}
	else if (m->button.shape == HEXAGON){

		float sqrt3 = 1.732050807f;

		float x0 = e->x + ui->theme.padding_size;
		float y0 = e->y + ui->theme.padding_size;
		float hex_pad = ui->theme.matrix_hex_padding;

		float diameter = m->button.dimension;
		// todo can't figure why 2 is working here, should be root3/2, suspect this will break with different sized
		// grids and spacings.
		int x_advance = 1.5f * diameter + 2 * hex_pad;
		int y_advance = sqrt3 * 0.5f * diameter + hex_pad; 
	    int mpx = MOUSE_X - x0;
	    int mpy = MOUSE_Y - y0;
	    // I'm not going to pretend to know why the rest of this works.
	    // 
	    int hx = (mpx / x_advance) * 2;
	    int xMod = mpx % x_advance;
	    int hy = mpy / y_advance;
	    int yMod = mpy % y_advance;

	    if (yMod < y_advance / 2) {
	        if ((xMod * 2 + yMod) < (diameter / 2)) {
	            --hx;
	            --hy;
	        }
	        else if ((xMod * 2 - yMod) < (diameter * 3 / 2)) {
	            // do nothing
	        }
	        else {
	            ++hx;
	            --hy;
	        }
	    }
	    else {
	        if ((xMod * 2 - (yMod - diameter / 2)) < 0) {
	            --hx;
	        }
	        else if ((xMod * 2 + (yMod - diameter / 2)) < diameter * 2) {
	            // do nothing
	        }
	        else {
	            ++hx;
	        }
	    }

	    // ensure we are not clicking on any non existant hexagons at the boundries
		if (hx >= 0 && hx < m->columns){
			if (hy >= 0 && hy < m->rows){
				button_id = hx + m->columns * hy;
				ui_matrix_swap_colour(buf, m, button_id);
				ui->modified |= GEOM_MODIFIED;
			}
		}
	    // printf("x=%d, y=%d\n", hx, hy);
	}

	// printf("node %d : %d\n", x_id, y_id);
}

static void ui_matrix_swap_colour(ui_vertex_t *buf, ui_matrix_t *m, uint32_t button_id){

	uint32_t new_colour;

	if (m->states[button_id]){
		m->states[button_id] = 0;
		new_colour = m->button.col_off;
	}
	else {
		new_colour = m->button.col_on;
		m->states[button_id] = 1;  
	}
	if (m->button.shape == CIRCLE){
		buf += RECTANGLE_VERTS + button_id * CIRCLE_SEG_VERTS(SEGMENTS);
		for (int i = 0; i < CIRCLE_SEG_VERTS(SEGMENTS); ++i){
			buf[i].colour = new_colour;
		}
	}
	else if (m->button.shape == HEXAGON){
		buf += RECTANGLE_VERTS + button_id * HEXAGON_VERTS;
		for (int i = 0; i < HEXAGON_VERTS; ++i){
			buf[i].colour = new_colour;
		}
	}
	else if (m->button.shape == SQUARE){
		buf += RECTANGLE_VERTS + button_id * RECTANGLE_VERTS;
		for (int i = 0; i < RECTANGLE_VERTS; ++i){
			buf[i].colour = new_colour;
		}
	}
}

// External function for uids
void ui_matrix_set_button_colour(ui_interface_t *ui, uint32_t uid, int row, int column, uint32_t colour){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_matrix_set_button_colour\n");
		return;
	}
	ui_vertex_t *buf = ui->elements[id].index;
	ui_matrix_t *m = &ui->elements[id].data.mat;

	int button_id = column - 1 + (row - 1) * ui->elements[id].data.mat.columns;

	assert (ui->elements[id].data.mat.button.shape != HEXAGON);
	int count = 0;
	if (m->button.shape == CIRCLE){
		buf += RECTANGLE_VERTS + button_id * CIRCLE_SEG_VERTS(SEGMENTS);
		while (count++ < CIRCLE_SEG_VERTS(SEGMENTS)){
			(buf++)->colour = colour;
		}
	}
	else if (m->button.shape == HEXAGON){
		buf += RECTANGLE_VERTS + button_id * HEXAGON_VERTS;
		while (count++ < HEXAGON_VERTS){
			(buf++)->colour = colour;
		}
	}
	else if (m->button.shape == SQUARE){
		buf += RECTANGLE_VERTS + button_id * RECTANGLE_VERTS;
		while (count++ < RECTANGLE_VERTS){
			(buf++)->colour = colour;
		}
	}
	ui->modified |= GEOM_MODIFIED;
}

// External function for uids
void ui_matrix_set_states(ui_interface_t *ui, uint32_t uid, uint8_t *data){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_matrix_set_states\n");
		return;
	}
	// sets all of the states in a matrix
	ui_element_t *e = &ui->elements[id];
	ui_matrix_t *m = &e->data.mat;
	ui_vertex_t *buf = e->index;
	int button_count = m->columns * m->rows;
	buf += RECTANGLE_VERTS;
	int count;
	uint32_t col;
	int vertex_count = 0;
	switch(m->button.shape){
		case CIRCLE:
			vertex_count = CIRCLE_SEG_VERTS(SEGMENTS);
			break;
		case HEXAGON:
			vertex_count = HEXAGON_VERTS;
			break;
		case SQUARE:
			vertex_count = RECTANGLE_VERTS;
			break;
	}

	for (int i = 0; i < button_count; ++i){
		count = 0;
		col = data[i] ? m->button.col_on : m->button.col_off;
		while (count++ < vertex_count){
			(buf++)->colour = col;
		}
		m->states[i] = data[i];
	}
	ui->modified |= GEOM_MODIFIED;
}

// External function for uids
void ui_matrix_set_button(ui_interface_t *ui, uint32_t uid, int row, int column, int state){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_matrix_set_button\n");
		return;
	}
	ui_element_t *e = &ui->elements[id];
	ui_matrix_t *m = &e->data.mat;
	ui_vertex_t *buf = e->index;
	assert(row <= m->rows);
	assert(column <= m->columns);

	int vertex_count = 0;
	switch(m->button.shape){
		case CIRCLE:
			vertex_count = CIRCLE_SEG_VERTS(SEGMENTS);
			break;
		case HEXAGON:
			vertex_count = HEXAGON_VERTS;
			break;
		case SQUARE:
			vertex_count = RECTANGLE_VERTS;
			break;
	}
	buf += RECTANGLE_VERTS;
	int button_id = column - 1 + (row - 1) * m->columns;
	m->states[button_id] = state;
	buf += vertex_count * button_id;

	uint32_t col = state ? m->button.col_on : m->button.col_off;
	int count = 0;

	while (count++ < vertex_count){
		(buf++)->colour = col;
	}
	ui->modified |= GEOM_MODIFIED;
}

// External function for uids
void ui_matrix_read_state(ui_interface_t *ui, uint32_t uid, uint8_t *data){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_matrix_read_state\n");
		return;
	}
	ui_matrix_t *m = &ui->elements[id].data.mat;
	memcpy(data, m->states, m->rows * m->columns * sizeof(uint8_t));
}

static void ui_button_col_swap(ui_interface_t *ui, uint16_t id, uint16_t state){

	ui_vertex_t *buf = ui->elements[id].index;
	int type = ui->elements[id].type;
	uint32_t hcol = ui->theme.button_hovered_col;
	uint32_t col = ui->theme.button_col;

	if (type == UI_BUTTON_RETAIN_STATE || type == UI_BUTTON_ROUND_RETAIN_STATE){
		uint8_t *st = &ui->elements[id].data.b.state;
		*st = *st ? 0 : 1;
		state = *st;
	}

	if (state){
		if (type == UI_BUTTON || type == UI_BUTTON_RETAIN_STATE){
			for (int i = 0; i < BUTTON_CENTRAL_VERT_CNT; ++i){
				(buf++)->colour = hcol;
			}
		}
		else {
			// rounded button
			for (int i = 0; i < RBUTTON_CENTRAL_VERT_CNT; ++i){
				(buf++)->colour = hcol;
			}
		}
	}
	else {
		if (type == UI_BUTTON || type == UI_BUTTON_RETAIN_STATE){
			for (int i = 0; i < BUTTON_CENTRAL_VERT_CNT; ++i){
				(buf++)->colour = col;
			}
		}
		else {
			// rounded button
			for (int i = 0; i < RBUTTON_CENTRAL_VERT_CNT; ++i){
				(buf++)->colour = col;
			}
		}
	}
	if (ui->elements[id].usage != UI_DYNAMIC_CONTENT){
		ui->modified |= GEOM_MODIFIED;
	}
}

static void ui_pie_menu_create(ui_interface_t *ui, uint32_t id, ui_pie_menu_t *p){

	ui_element_t *e = &ui->elements[id];
	e->data.pie = *p;
	e->data.pie.elements = malloc(sizeof(element_data) * p->count);
	memcpy(e->data.pie.elements, p->elements, sizeof(element_data) * p->count);
	e->num_verts = 0;

	// use maxw/height to determine element seperation
	e->bounds[0] = 0;
	e->bounds[1] = 0;
	e->bounds[2] = 0;
	e->bounds[3] = 0;
	e->content_region[0] = 0;
	e->content_region[1] = 0;
	e->content_region[2] = 0;
	e->content_region[3] = 0;
}

static int ui_pie_menu_finalise(ui_interface_t *ui, uint32_t id){

	uint32_t e_uuid;
	ui_element_t *e = &ui->elements[id];
	ui_pie_menu_t *p = &e->data.pie;
	// int max_w = 0;

	ui_set_element_flags(ui, id, UI_HIDDEN);

	for (int i = 0; i < p->count; ++i){
		switch (p->type){
		case UI_BUTTON_ROUND:
		case UI_BUTTON_RETAIN_STATE:
		case UI_BUTTON_ROUND_RETAIN_STATE:
		case UI_BUTTON:
			e_uuid = ui_element_init(ui, &p->elements[i], 0, p->type);
			ui_set_element_flags(ui, e_uuid, UI_HIDDEN);
			e = &ui->elements[ui_find_array_index(ui, e_uuid)];
			e->parent = id;
			e->usage = UI_DYNAMIC_CONTENT;
			break;
		case UI_LABEL:
			break;
		default:
			printf("ERROR: Unsupported element type in pie menus\n");
			exit(1);
		}
	}

	return ui->elements[id].uuid;
}

static int ui_pie_menu_activate(ui_interface_t *ui, uint32_t id){

	// Unhide all child elements
	// use maxw/height to determine element seperation
	v2 pos;
	ui_element_t *e = &ui->elements[id];
	e->x = MOUSE_X;
	e->y = MOUSE_Y;
	ui_pie_menu_t *p = &e->data.pie;
	// float mag = p->radius;
	// float angle = 2.0f * PI / p->count;

	pos.x = MOUSE_X - (e->bounds[2] - e->bounds[0]) * 0.5f;
	pos.y = MOUSE_Y - (e->bounds[3] - e->bounds[1]) * 0.5f;
	for (int i = 0; i < p->count; ++i){
		++e;
		// pos.x = MOUSE_X + mag * cosf(angle * i);
		// pos.y = MOUSE_Y + mag * sinf(angle * i);
		// printf("angle = %f, x = %f y = %f\n", angle, pos.x, pos.y);
		e->frame = 0;
		ui_position_element(ui, e->uuid, pos.x, pos.y);
		// e->flags &= !UI_HIDDEN;
	}
	ui->action = ui_pie_menu_animate;
	return 0;
}

// Each fram calc overlap and serive from that the speed to move the button along its direction vector
// ui_pie_update  add a frame counter to elements
static void ui_pie_menu_select(ui_interface_t *ui, uint32_t id){

	ui_element_t *ep = &ui->elements[id];
	ui_element_t *e = ep + 1;
	ui_pie_menu_t *p = &ep->data.pie;

	for (int i = 0; i < p->count; ++i){
		// e->flags |= UI_HIDDEN;
		if (MOUSE_X > e->content_region[0] && MOUSE_X < e->content_region[2]){
			if (MOUSE_Y > e->content_region[1] && MOUSE_Y < e->content_region[3]){
				if (ep->action != NULL) ep->action(i);
				break;
			}
		}
		++e;
	}
}

static int ui_pie_menu_animate(ui_interface_t *ui, uint32_t id){

	v2 pos;
	ui_element_t *e = &ui->elements[id];
	ui_pie_menu_t *p = &e->data.pie;
	++e;
	float mag = p->radius;
	int angle_inc = 360 / p->count;
	int start_angle = angle_inc + (90 - angle_inc);
	int angle;
	float ft = UI_FRAME_RATE/3.0f;
	if (MOUSE_L){
		ui->action_flags |= LEFT_HELD;
	}
	if (!MOUSE_L && (ui->action_flags & LEFT_HELD)){
		ui_pie_menu_select(ui, id);
		ui->action = NULL;
		ui->selected_element = MAX_UI_ELEMENTS;
		ui->action_flags = 0;
		return 0;
	}
	// ---------------------todo----------------------------------
	// todo switch for each supported type or just reserve the amount for the largest
	// ---------------------------------------------------------------------------------
	uint32_t size = p->count * sizeof(ui_vertex_t) * BUTTON_VERTS;
	ui_buffer_reserve(&ui->dynamic_buffer, size);
	ui_vertex_t *buf = ui->dynamic_buffer + ui->num_dynamic_verts;

	for (int i = 0; i < p->count; ++i){
		// todo make frame time refresh rate independant
		// set frame in activate and decrem3ent
		if (e->frame < ft){
			mag = ui_lerp_log(0, 1, e->frame / ft);
			mag *= p->radius;
			// printf("mag = %f\n", mag);
			angle = start_angle + angle_inc * i;
			// todo we want the original click location here not the current mousexy
			pos.x = MOUSE_X + mag * cosf(DEG_TO_RAD(angle));
			pos.y = MOUSE_Y - mag * sinf(DEG_TO_RAD(angle));
			if (angle == 90){
				pos.x -= (e->bounds[2] - e->bounds[0]) * 0.5f;
				pos.y -= e->bounds[3] - e->bounds[1];
			}
			else if (angle == 270){
				pos.x -= (e->bounds[2] - e->bounds[0]) * 0.5f;
			}
			else if ((angle > 90) && (angle < 270)){
				pos.x -= e->bounds[2] - e->bounds[0];
				pos.y -= (e->bounds[3] - e->bounds[1]) * 0.5f;
			}
			else {
				pos.y -= (e->bounds[3] - e->bounds[1]) * 0.5f;
			}
			// printf("angle = %d, x = %f y = %f\n", angle, pos.x, pos.y);
			ui_position_element(ui, e->uuid, pos.x, pos.y);
		}
		if (MOUSE_X > e->content_region[0] && MOUSE_X < e->content_region[2]){
			if (MOUSE_Y > e->content_region[1] && MOUSE_Y < e->content_region[3]){
				ui_button_col_swap(ui, e->array_index, 1);
			}
			else {
				ui_button_col_swap(ui, e->array_index, 0);
			}
		}
		else {
			ui_button_col_swap(ui, e->array_index, 0);
		}
		memcpy(buf, e->index, sizeof(ui_vertex_t) * e->num_verts);
		buf += e->num_verts;
		ui->num_dynamic_verts += e->num_verts;
		e->frame += 1;
		++e;
	}
	return 0;
}

static float ui_lerp_log(float start, float end, float t){

	float x = (1 - t) * start + t * end;
	return 1 - (1 - x) * (1 - x);
}

// todo use fixed width strings here
static void ui_parameter_str_create(ui_interface_t *ui, uint16_t id, ui_parameter_str_t *p){

	ui_element_t *e = &ui->elements[id];
	int pad = ui->theme.padding_size;
	// int border_size = ui->theme.parameter_pad_size;
	int border_size = ui->theme.parameter_line_width;
	int height = ui->font[e->font_id].line_height + border_size * 2;
	uint32_t center_col = ui->theme.background_col;
	uint32_t border_col = ui->theme.border_col;
	//uint32_t text_col = ui->theme.text_col;

	// count the verts required for text
	int width = p->width;
	int tw, th;
	//int text_verts = 0;
	for (int i = 0; i < p->no_of_items; ++i){
		ui_string_size_pixels(p->items[i], &ui->font[e->font_id], &tw, &th);
		width = width > tw ? width : tw;
		//text_verts += ui_string_vert_count(p->items[i]);
	}
	if (!p->width){
		// auto width
		p->width = width + 2 * height + 2 * pad;
	}

	e->index = ui_buffer_init((PARAMETER_VERTS) * sizeof(ui_vertex_t));
	// for (int i = 0; i < PARAMETER_VERTS; ++i){
	// 	e->index[i].u = GEOM_UV;
	// 	e->index[i].v = GEOM_UV;
	// }


	// containing rectangle
	e->num_verts = 
		ui_rect_bordered_create(e->index, e->x, e->y, p->width, height, center_col, border_col, border_size);
	// step up/down triangles
	v2 a1, a2, a3, b1, b2, b3;
	int line_height = ui->font[e->font_id].line_height;
	int tri_padding = line_height / 4;
	a1.x = e->x + p->width - border_size - line_height + tri_padding;
	a1.y = e->y + border_size + tri_padding;
	a2.x = e->x + p->width - border_size - tri_padding;
	a2.y = a1.y;
	a3.x = e->x + p->width - border_size - line_height / 2.0f;
	a3.y = e->y + height - border_size - tri_padding;
	b1.x = e->x + p->width - border_size - line_height * 1.5;
	b1.y = a1.y;
	b2.x = e->x + p->width - border_size - line_height - tri_padding;
	b2.y = a3.y;
	b3.x = e->x + p->width - border_size - line_height * 2 + tri_padding;
	b3.y = a3.y;

	e->num_verts +=
	ui_make_triangle(e->index + e->num_verts, a1, a2, a3, border_col);
	e->num_verts +=
	ui_make_triangle(e->index + e->num_verts, b1, b2, b3, border_col);


	e->data.ps = *p;
	e->data.ps.items = malloc(sizeof(char *) * p->no_of_items);

	for (int i = 0; i < p->no_of_items; ++i){
		e->data.ps.items[i] = ui_strdup(p->items[i]);
	}


	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + p->width;
	e->bounds[3] = e->y + height;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + p->width;
	e->content_region[3] = e->y + height;

	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

static void ui_parameter_str_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	ui_parameter_str_t *p = &e->data.ps;
	int border_size = ui->theme.border_size;
	int height = e->bounds[3] - e->bounds[1];
	int location = e->bounds[2] - MOUSE_X - border_size;

	// click is in the button area
	if (location < 2 * height){
		if (location < height){
			p->selected = p->selected - 1;
			if (p->selected == -1){
				p->selected = p->no_of_items - 1;
			}
		}
		else {
			p->selected = (p->selected + 1) % p->no_of_items;
		}
		ui->modified |= TEXT_MODIFIED;
	}
}

#define MENU_CONTENTS_OFFSET 12
static void ui_menu_create(ui_interface_t *ui, uint16_t id, ui_menu_t *t){

	ui_element_t *e = &ui->elements[id];
	int h = ui->font[e->font_id].line_height;
	int adv_y = h;
	int y_pos = e->y;
	int pad = ui->theme.padding_size;
	uint32_t back_col = ui->theme.menu_background_col;
	uint32_t text_col = ui->theme.text_col;
	int variance = 0;
	int tw, th;
	int text_verts = 0;
	int title_verts = 0;

	title_verts = ui_string_vert_count(t->title);

	// find the widest string in items
	ui_string_size_pixels(t->title, &ui->font[e->font_id], &tw, &th);

	int width = tw;
	for (int i = 0; i < t->no_of_items; ++i){
		ui_string_size_pixels(t->items[i], &ui->font[e->font_id], &tw, &th);
		variance += abs(tw - width);
		width = width > tw ? width : tw;
		text_verts += ui_string_vert_count(t->items[i]);
	}
	width += pad * 4;
	// add a bit of space to the right of the text
	// if menu will appear too cramped
	width = variance < (width >> 2) ? width + (width >> 1) : width;

	int total_verts = title_verts + text_verts + t->no_of_items * 6 + 18;
	e->index = ui_buffer_init(total_verts * sizeof(ui_vertex_t));

	for (int i = 0; i < total_verts; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	// box for title
	e->num_verts = 
		ui_rectangle_create(e->index + e->num_verts, e->x, y_pos, width, adv_y, ui->theme.menu_title_col);
	y_pos += adv_y;

	// Title text
	e->num_verts +=
		ui_text_create(e->index + e->num_verts, t->title, &ui->font[e->font_id], e->x, e->y, width, h, text_col, UI_CENTERED_X | UI_CENTERED_Y);

	// top padding
	e->num_verts += 
		ui_rectangle_create(e->index + e->num_verts, e->x, y_pos, width, pad, back_col);
	y_pos += pad;

	e->data.m = *t;
	e->data.m.title = ui_strdup(t->title);
	e->data.m.items = malloc(sizeof(char *) * t->no_of_items);

	for (int i = 0; i < t->no_of_items; ++i){
		e->data.m.items[i] = ui_strdup(t->items[i]);
		// boxes for content
		e->num_verts += 
			ui_rectangle_create(e->index + e->num_verts, e->x, y_pos, width, adv_y, back_col);
		y_pos += adv_y;
	}
	// Bottom padding
	e->num_verts += 
		ui_rectangle_create(e->index + e->num_verts, e->x, y_pos, width, pad, back_col);

	pad *= 2;
	if (!t->state){
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + adv_y;
		e->content_region[0] = e->x;
		e->content_region[1] = e->y;
		e->content_region[2] = e->x + width;
		e->content_region[3] = e->y + adv_y;
	}
	else {
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + pad + adv_y * (t->no_of_items + 1);
		e->content_region[0] = e->x;
		e->content_region[1] = e->y;
		e->content_region[2] = e->x + width;
		e->content_region[3] = e->y + pad + adv_y * (t->no_of_items + 1);
	}
	// add the text
	float sx = e->x + 2 * ui->theme.padding_size;
	float sy = e->y + h + ui->theme.padding_size;
	

	for (int i = 0; i < t->no_of_items; ++i){
		e->num_verts +=
		ui_text_create(e->index + e->num_verts, t->items[i], &ui->font[e->font_id], sx, sy, width, h, text_col, UI_LEFT | UI_CENTERED_Y);
		sy += h;
	}
	// menu is open
	assert(e->num_verts == total_verts);
	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

static void ui_menu_actions(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	int selected_item = 0;
	int h = ui->font[e->font_id].line_height;
	int pad = ui->theme.padding_size;

	if (MOUSE_Y - e->y < pad + h){
		//ui_menu_toggle(ui, id);
	}
	else {
		selected_item = (MOUSE_Y - e->y - h - pad) / h;
		selected_item++;
		if (e->action){
			e->action(e->uuid);
		}
	}
	ui_menu_toggle(ui, id);
}

static void ui_menu_toggle(ui_interface_t *ui, uint16_t id){

	ui_element_t *e = &ui->elements[id];
	int h = ui->font[e->font_id].line_height;
	ui_menu_t *t = &ui->elements[id].data.m;
	int pad = ui->theme.padding_size;
	int width = e->bounds[2] - e->bounds[0];
	int text_verts = 0;

	log("state = %d\n", t->state);
	if (t->state){
		// close menu
		text_verts = ui_string_vert_count(t->title);
		e->num_verts = text_verts + MENU_CONTENTS_OFFSET - 6;
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + h;
		e->content_region[0] = e->x;
		e->content_region[1] = e->y;
		e->content_region[2] = e->x + width;
		e->content_region[3] = e->y + h;
		ui_menu_unhighlight(ui, id);
		t->state = 0;
	}
	else {
		// opem menu
		for (int i = 0; i < t->no_of_items; ++i){
			text_verts += ui_string_vert_count(t->items[i]);
			text_verts += 6;
		}
		text_verts += ui_string_vert_count(t->title);
		e->num_verts = MENU_CONTENTS_OFFSET	+ 6 + text_verts;
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + pad + h * (t->no_of_items + 1);
		e->content_region[0] = e->x;
		e->content_region[1] = e->y;
		e->content_region[2] = e->x + width;
		e->content_region[3] = e->y + pad + h * (t->no_of_items + 1);
		t->state = t->no_of_items + 1;
	}
	ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED | TEXT_MODIFIED;
}

// Keep track of which item the mouse is hovering over
// and trigger highlight of expected item
static void ui_menu_update(ui_interface_t *ui, uint16_t id){

	ui_menu_t *m = &ui->elements[id].data.m;
	ui_element_t *e = &ui->elements[id];
	int pad = ui->theme.padding_size;
	int selected_item;

	float line_height = ui->font[e->font_id].line_height;
	
	if (MOUSE_Y - e->y < pad + line_height){
		selected_item = 0;
	}
	else {
		selected_item = (MOUSE_Y - e->y - line_height - pad) / line_height;
		selected_item++;
	}

	if (selected_item > m->no_of_items){
		return;
	}
	//swap colour of highlighted item
	if (selected_item != m->state){
		if (selected_item){
			ui_menu_highlight(ui, id, selected_item);
			m->state = selected_item;
		}
		else {
			ui_menu_unhighlight(ui, id);
			//t->state = t->no_of_items + 1;
		}
	}
}

static void ui_menu_highlight(ui_interface_t *ui, uint16_t id, uint16_t item){
	
	ui_menu_t *t = &ui->elements[id].data.m;
	ui_vertex_t *bp;
	int text_verts = ui_string_vert_count(t->title);
	--item;

	if (t->state != t->no_of_items + 1){

		bp = ui->elements[id].index + (t->state - 1) * 6 + MENU_CONTENTS_OFFSET;
		bp += text_verts;

		bp++->colour = ui->theme.menu_background_col;
		bp++->colour = ui->theme.menu_background_col;
		bp++->colour = ui->theme.menu_background_col;
		bp++->colour = ui->theme.menu_background_col;
		bp++->colour = ui->theme.menu_background_col;
		bp++->colour = ui->theme.menu_background_col;
	}
	
	bp = ui->elements[id].index + item * 6 + MENU_CONTENTS_OFFSET;
	bp += text_verts;

	bp++->colour = ui->theme.menu_hovered_col;
	bp++->colour = ui->theme.menu_hovered_col;
	bp++->colour = ui->theme.menu_hovered_col;
	bp++->colour = ui->theme.menu_hovered_col;
	bp++->colour = ui->theme.menu_hovered_col;
	bp++->colour = ui->theme.menu_hovered_col;
	ui->modified |= GEOM_MODIFIED;
}

static void ui_menu_unhighlight(ui_interface_t *ui, uint16_t id){
	
	ui_menu_t *t = &ui->elements[id].data.m;
	int text_verts = ui_string_vert_count(t->title);

	if (t->state == 0 || t->state > t->no_of_items){
		return;
	}
	ui_vertex_t *bp = ui->elements[id].index + (t->state - 1) * 6 + MENU_CONTENTS_OFFSET;
	bp += text_verts;

	bp++->colour = ui->theme.menu_background_col;
	bp++->colour = ui->theme.menu_background_col;
	bp++->colour = ui->theme.menu_background_col;
	bp++->colour = ui->theme.menu_background_col;
	bp++->colour = ui->theme.menu_background_col;
	bp++->colour = ui->theme.menu_background_col;

	ui->modified |= GEOM_MODIFIED;
}

#define BOTTOM_SEGMENT_OFFSET 0
#define MIDDLE_SEGMENT_OFFSET 12
#define TOP_SEGMENT_OFFSET 24
#define LOWER_LEFT_SEGMENT_OFFSET 36
#define LOWER_RIGHT_SEGMENT_OFFSET 48
#define UPPER_LEFT_SEGMENT_OFFSET 60
#define UPPER_RIGHT_SEGMENT_OFFSET 72
#define SEGMENT_VERTEX_COUNT 12
static int ui_make_seven_segment(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col){

	// segment thickness
	float st = w * 0.2f;
	// segment spacing gap
	float sp = h * 0.05f;
	if ( sp < 1){
		sp = 1;
	}

	int i = 0;
	// -----------------------------------------------------------
	// Bottom Segment
	//------------------------------------------------------------
	// left tri
	buf[i].x = x; 			buf[i].y = y + h;				buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h; 				buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h - st; 			buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x + st, y + h - st, w - 2 * st, st, col);
	// int ui_rectangle_create(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col){
	// right hand tri
	buf[i].x = x + w - st; 		buf[i].y = y + h;				buf[i++].colour = col;
	buf[i].x = x + w;			buf[i].y = y + h; 				buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + h - st; 			buf[i++].colour = col;
	// -----------------------------------------------------------
	// Middle Segment
	//------------------------------------------------------------
	// left tri
	buf[i].x = x; 			buf[i].y = y + h / 2.0f;				buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h / 2.0f + st / 2.0f; 	buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h / 2.0f - st / 2.0f; 	buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x + st, y + h / 2.0f - st / 2.0f, w - 2 * st, st, col);
	// right tri
	buf[i].x = x + w; 			buf[i].y = y + h / 2.0f;				buf[i++].colour = col; 		
	buf[i].x = x + w - st;		buf[i].y = y + h / 2.0f - st / 2.0f;	buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + h / 2.0f + st / 2.0f;	buf[i++].colour = col; 
	// -----------------------------------------------------------
	// Top Segment
	// -----------------------------------------------------------
	// left tri
	buf[i].x = x; 			buf[i].y = y;		buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + st; 	buf[i++].colour = col;
	buf[i].x = x + st; 		buf[i].y = y; 		buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x + st, y, w - 2 * st, st, col);
	// right tri
	buf[i].x = x + w - st; 		buf[i].y = y + st;		buf[i++].colour = col;
	buf[i].x = x + w;			buf[i].y = y; 			buf[i++].colour = col;
	buf[i].x = x + w - st; 		buf[i].y = y; 			buf[i++].colour = col;

	// Make the spacing between the center segments look more even
	float sp2 = sp * 0.707f;
	// -----------------------------------------------------------
	// Lower Left Segment
	// -----------------------------------------------------------
	// bot tri
	buf[i].x = x; 			buf[i].y = y + h - sp;				buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h - st - sp; 		buf[i++].colour = col;
	buf[i].x = x;		buf[i].y = y + h - st - sp; 			buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x, y + h / 2.0f + sp2 + st / 2.0f, st, h / 2.0f - sp - sp2 - st * 1.5f, col);
	// top tri
	buf[i].x = x; 			buf[i].y = y + h / 2.0f + sp2;				buf[i++].colour = col;
	buf[i].x = x;		buf[i].y = y + h / 2.0f + st / 2.0f + sp2; 		buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h / 2.0f + st / 2.0f + sp2; 	buf[i++].colour = col;
	// -----------------------------------------------------------
	// Lower right Segment
	// -----------------------------------------------------------
	// bot tri
	buf[i].x = x + w; 			buf[i].y = y + h - sp;				buf[i++].colour = col;
	buf[i].x = x + w;		buf[i].y = y + h - st - sp; 			buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + h - st - sp; 		buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x + w - st, y + h / 2.0f + sp2 + st / 2.0f, st, h / 2.0f - sp2 - sp - st * 1.5f, col);
	// top tri
	buf[i].x = x + w; 			buf[i].y = y + h / 2.0f + sp2;				buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + h / 2.0f + st / 2.0f + sp2; 	buf[i++].colour = col;
	buf[i].x = x + w;		buf[i].y = y + h / 2.0f + st / 2.0f + sp2; 		buf[i++].colour = col;
	// -----------------------------------------------------------
	// Upper Left Segment
	// -----------------------------------------------------------
	// bot tri
	buf[i].x = x; 			buf[i].y = y + h / 2.0f - sp2;				buf[i++].colour = col;
	buf[i].x = x + st;		buf[i].y = y + h / 2.0f - st / 2.0f - sp2; 	buf[i++].colour = col;
	buf[i].x = x;		buf[i].y = y + h / 2.0f - st / 2.0f - sp2; 		buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x, y + sp + st, st, h / 2.0f - sp2 - sp - st * 1.5f, col);
	// top tri
	buf[i].x = x; 			buf[i].y = y + sp;		buf[i++].colour = col;
	buf[i].x = x;		buf[i].y = y + st + sp; 	buf[i++].colour = col;
	buf[i].x = x + st; 		buf[i].y = y + st + sp; 		buf[i++].colour = col;
	// -----------------------------------------------------------
	// Upper Right Segment
	// -----------------------------------------------------------
	// bot tri
	buf[i].x = x + w; 			buf[i].y = y + h / 2.0f - sp2;				buf[i++].colour = col;
	buf[i].x = x + w;		buf[i].y = y + h / 2.0f - st / 2.0f - sp2; 	buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + h / 2.0f - st / 2.0f - sp2; 		buf[i++].colour = col;
	// center two triangles	
	i += ui_rectangle_create(&buf[i], x + w - st, y + sp + st, st, h / 2.0f - sp2 - sp - st * 1.5f, col);
	// top tri
	buf[i].x = x + w; 			buf[i].y = y + sp;		buf[i++].colour = col;
	buf[i].x = x + w - st;		buf[i].y = y + st + sp; 	buf[i++].colour = col;
	buf[i].x = x + w; 		buf[i].y = y + st + sp; 		buf[i++].colour = col;


	return i;

}

#define SEVEN_SEG_DISPLAY_VERTS (12 * 7)
static void ui_seven_segment_display_create(ui_interface_t *ui, uint16_t id, ui_segment_display_t *s){

	ui_element_t *e = &ui->elements[id];
	uint32_t background_col = ui->theme.segment_display_background_col;
	uint32_t off_col = ui->theme.segment_display_off_col;
	int t_pad = ui->theme.padding_size;
	float pad = s->w * 0.25f;

	e->index = ui_buffer_init((RECTANGLE_VERTS + SEVEN_SEG_DISPLAY_VERTS * s->num_segments) * sizeof(ui_vertex_t));
	e->data.seg = *s;

	int x = e->x + t_pad;
	int y = e->y + t_pad;
	int width = s->w * s->num_segments + (s->num_segments - 1) * pad + 2 * t_pad;
	ui_vertex_t *buf = e->index;
	e->num_verts += ui_rectangle_create(buf, e->x, e->y, width, s->h, background_col);
	buf += RECTANGLE_VERTS;

	for (int i = 0; i < s->num_segments; ++i){
		e->num_verts += ui_make_seven_segment(buf, x, y, s->w, s->h - 2 * t_pad, off_col);
		buf += SEVEN_SEG_DISPLAY_VERTS;
		x += s->w + pad;
	}

	for (int i = 0; i < RECTANGLE_VERTS + SEVEN_SEG_DISPLAY_VERTS * s->num_segments; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	e->content_region[0] = e->x + t_pad;
	e->content_region[1] = e->y + t_pad;
	e->content_region[2] = e->x + width - t_pad;
	e->content_region[3] = e->y + s->h - t_pad;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	e->bounds[2] = e->x + width;
	e->bounds[3] = e->y + s->h;
}

static inline void ui_light_segment(ui_vertex_t *buf, int start_vertex, uint32_t col){

	for (int i = start_vertex; i < start_vertex + SEGMENT_VERTEX_COUNT; ++i){
		buf[i].colour = col;
	}
}

// External function for uids
void ui_write_segment_display(ui_interface_t *ui, uint32_t uid, char *c, int count){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_segment_display\n");
		return;
	}
	assert(count <= ui->elements[id].data.seg.num_segments);
	uint32_t col_on = ui->theme.segment_display_on_col;
	uint32_t col_off = ui->theme.segment_display_off_col;
	ui_vertex_t *buf = ui->elements[id].index + RECTANGLE_VERTS;
	while (count--){
		ui_seven_segment_display_char(buf, col_on, col_off, *(c++));
		buf += SEVEN_SEG_DISPLAY_VERTS;
	}
}

static void ui_seven_segment_display_char(ui_vertex_t *buf, uint32_t col_on, uint32_t col_off, char c){

	// todo minus a->f
	switch (c){
	case '\0':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_off);
		break;
	case '-':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_off);
		break;
	case '0':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '1':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '2':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '3':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '4':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '5':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_off);
		break;
	case '6':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_off);
		break;
	case '7':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '8':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	case '9':
		ui_light_segment(buf, BOTTOM_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, MIDDLE_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, TOP_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, LOWER_LEFT_SEGMENT_OFFSET, col_off);
		ui_light_segment(buf, LOWER_RIGHT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_LEFT_SEGMENT_OFFSET, col_on);
		ui_light_segment(buf, UPPER_RIGHT_SEGMENT_OFFSET, col_on);
		break;
	default:
		printf("ERROR: Unrecognised character :%c, in ui_seven_segment_display_char()\n", c);
		break;

	}
}

#define DIAL_VERTS (DIAL_SEGMENTS * 6 + 3)
static void ui_dial_create(ui_interface_t *ui, uint16_t id, ui_dial_t* d){

	assert(d->value <= d->end_val);
	assert(d->value >= d->start_val);
	ui_element_t *e = &ui->elements[id];
	e->data.d = *d;
	e->data.d.title = ui_strdup(d->title);
	int	text_verts = ui_string_vert_count(d->title);
	e->index = ui_buffer_init((DIAL_VERTS + text_verts) * sizeof(ui_vertex_t));
	for (int i = 0; i < DIAL_VERTS; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}

	uint32_t text_col = ui->theme.text_col;
	uint32_t col;

	float value = (d->value - d->start_val) / (d->end_val - d->start_val);
	float line_width = ui->theme.dial_line_thickness;
	float line_height = ui->font[e->font_id].line_height;
	float mag = d->size / 2.0f - line_width / 2.0f;
	float x = e->x + mag + line_width / 2.0f;
	float y = e->y + mag + line_width / 2.0f;

	int arc_length = 270;
	int segments = DIAL_SEGMENTS;
	int segments_arc_1 = value * segments;
	int segments_arc_2 = segments - segments_arc_1; 
	float radius = d->size / 2.0f;
	float text_offset = radius + 0.70712f * radius;
	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + d->size;
	e->content_region[3] = e->y + d->size;
	
	e->bounds[0] = e->x;
	e->bounds[1] = e->y;
	if (e->type == UI_DIAL){
		e->bounds[2] = e->x + d->size;
		e->bounds[3] = e->y + text_offset + line_height;
	}
	else if (e->type == UI_DIAL_HIDE_VAL){
		e->bounds[2] = e->x + d->size;
		e->bounds[3] = e->y + text_offset;
	}
	
	// convert range from 0->1 to 135->405 degrees
	value = value * arc_length + 135;

	col = ui->theme.dial_col_1;

	ui_vertex_t *buf = e->index;

	v2 line1, perp, a, b, c, origin;
	origin.x = x;
	origin.y = y;
	c.x = mag * cosf(DEG_TO_RAD(value)) + x;
	c.y = mag * sinf(DEG_TO_RAD(value)) + y;
	// perpendicular vector
	line1 = ui_sub_v2(c, origin);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, line_width * 0.5f);
	// first point starts:
	a = ui_add_v2(origin, perp);
	b = ui_sub_v2(origin, perp);
	
	uint32_t idx = 0;	
	buf[idx].x = a.x; buf[idx].y = a.y; buf[idx++].colour = col;
	buf[idx].x = b.x; buf[idx].y = b.y; buf[idx++].colour = col;
	buf[idx].x = c.x; buf[idx].y = c.y; buf[idx++].colour = col;
	ui->elements[id].num_verts +=3;

	if (segments_arc_1){	
		e->num_verts += 
		ui_line_arc_create_wide(e->index + e->num_verts, x, y, DEG_TO_RAD(135), DEG_TO_RAD(value), mag, col, segments_arc_1, line_width);
	}

	if (segments_arc_2){	
		col = ui->theme.dial_col_2;
		e->num_verts += 
		ui_line_arc_create_wide(e->index + e->num_verts, x, y, DEG_TO_RAD(value), DEG_TO_RAD(405), mag, col, segments_arc_2, line_width);
	}
	if (*d->title){
		float radius = d->size / 2.0f;
		float y = radius + 0.70712f * radius;
		float starty = 0;
		if (e->type == UI_DIAL){
			starty = e->y + y + line_height;
		}
		else if (e->type == UI_DIAL_HIDE_VAL){
			starty = e->y + y;
		}
		int width, height;
		ui_string_size_pixels(d->title, &ui->font[e->font_id], &width, &height);
		width = width > d->size ? width : d->size;
		e->bounds[2] = e->x + width;
		e->bounds[3] += line_height;

		e->num_verts +=
		ui_text_create(e->index + e->num_verts, d->title, &ui->font[e->font_id], e->x, starty, d->size, line_height, text_col, UI_CENTERED_X);
	}
	assert(DIAL_VERTS + text_verts == e->num_verts);
	if (e->type == UI_DIAL){
		ui_buffer_reserve(&e->index, VALUE_VERTS * sizeof(ui_vertex_t));
	}
}

static void ui_dial_update(ui_interface_t *ui, uint16_t id, ui_dial_t* d){

	float value = (d->value - d->start_val) / (d->end_val - d->start_val);
	float line_thickness = ui->theme.dial_line_thickness;
	float mag = d->size / 2.0f - line_thickness / 2.0f;
	float x = ui->elements[id].x + mag + line_thickness / 2.0f;
	float y = ui->elements[id].y + mag + line_thickness / 2.0f;

	const int arc_length = 270;
	int i = 0;
	uint32_t col = ui->theme.dial_col_1;
	uint16_t segments = DIAL_SEGMENTS;
	int segments_arc_1 = ROUND(value * (float)segments);
	int segments_arc_2 = segments - segments_arc_1;
	// convert range from 0->1 to 135->405 degrees
	value = ROUND(value * arc_length + 135);
	ui_vertex_t *buf = ui->elements[id].index;
	
	v2 line1, perp, a, b, c, origin;
	origin.x = x;
	origin.y = y;

	c.x = mag * cosf(DEG_TO_RAD(value)) + x;
	c.y = mag * sinf(DEG_TO_RAD(value)) + y;
	// perpendicular vector
	line1 = ui_sub_v2(c, origin);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, line_thickness * 0.5f);
	// first point starts:
	a = ui_add_v2(origin, perp);
	b = ui_sub_v2(origin, perp);
	
	buf[i].x = a.x; buf[i].y = a.y; buf[i++].colour = col;
	buf[i].x = b.x; buf[i].y = b.y; buf[i++].colour = col;
	buf[i].x = c.x; buf[i].y = c.y; buf[i++].colour = col;

	int count = 3;
	
	if (segments_arc_1){
		count += segments_arc_1 * 6;
		col = ui->theme.dial_col_1;
		while (i < count){
			buf[i++].colour = col;
		}
	}

	if (segments_arc_2){	
		col = ui->theme.dial_col_2;
		count = count + segments_arc_2 * 6;
		while (i < count){
			buf[i++].colour = col;
		}
	}
	ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
}

static void ui_dial_set_value(ui_interface_t *ui, uint16_t id, float dy){

	ui_dial_t *d = &ui->elements[id].data.d;
	float delta = -dy * d->steps_per_pixel * d->step_size;
	d->value += delta;
	d->value = d->value < d->start_val ? d->start_val : d->value;
  	d->value = d->value > d->end_val ? d->end_val : d->value;
	ui_dial_update(ui, id, d);
}

// custom for auidio mixer revise
// todo make proper level display element that also
// buffers its state and can smooth the display
// instant reaction to spikes with gradual fall of to lower levels
// sets the slider level in the gui according to external value input
static void ui_set_slider_value2(ui_interface_t *ui, uint16_t id, float value){

	ui_slider_t *s = &ui->elements[id].data.s;
	ui_element_t *e = &ui->elements[id];
	uint32_t bs = ui->theme.slider_border_size; 
	float scale = s->length / (s->end_val - s->start_val);
	int new_length;

	if (e->type == UI_SLIDER_V || e->type == UI_SLIDER_V_HIDE_VAL){
		
		value = value < s->start_val ? s->start_val : value;
		value = value > s->end_val ? s->end_val : value;
		value = scale * value;
		s->value = value;

		int bottom = e->y + s->length - bs;
		value = bottom - value;

		ui_vertex_t *buf = e->index + RECT_BORDERED_VERTS + 1;
		buf++->y = value;
		buf->y = value;
		buf += 2;
		buf->y = value;
	}
	else if (e->type == UI_SLIDER_H || e->type == UI_SLIDER_H_HIDE_VAL){
		value = scale * (float)(MOUSE_X - e->x);
		value = value < s->start_val ? s->start_val : value;
		value = value > s->end_val ? s->end_val : value;
		s->value = value;
		int left = e->x + bs;
		int right = e->x + s->length - bs;

		new_length = MOUSE_X;
		new_length = new_length < left ? left : new_length;
		new_length = new_length > right ? right : new_length;
		ui_vertex_t *buf = e->index + RECT_BORDERED_VERTS + 2;
		buf++->x = new_length;
		buf++;
		buf++->x = new_length;
		buf++->x = new_length;
	}

	ui->modified |= GEOM_MODIFIED;
}

// Sets the slider level in the gui according to mouse input
static void ui_set_slider_value(ui_interface_t *ui, uint16_t id){

	ui_slider_t *s = &ui->elements[id].data.s;
	ui_element_t *e = &ui->elements[id];
	uint32_t bs = ui->theme.slider_border_size; 

	float scale = (s->end_val - s->start_val) / s->length;
	float value;
	int new_length;

	if (e->type == UI_SLIDER_V || e->type == UI_SLIDER_V_HIDE_VAL){
		value = scale * (float)(MOUSE_Y - e->y);
		value = s->end_val - value;
		value = value < s->start_val ? s->start_val : value;
		value = value > s->end_val ? s->end_val : value;
		s->value = value;

		int bottom = e->y + s->length - bs;
		int top = e->y + bs;

		new_length = MOUSE_Y;
		new_length = new_length < top ? top : new_length;
		new_length = new_length > bottom ? bottom : new_length;

		ui_vertex_t *buf = e->index + RECT_BORDERED_VERTS + 1;
		buf++->y = new_length;
		buf->y = new_length;
		buf += 2;
		buf->y = new_length;
	}
	else if (e->type == UI_SLIDER_H || e->type == UI_SLIDER_H_HIDE_VAL){
		value = scale * (float)(MOUSE_X - e->x);
		value = value < s->start_val ? s->start_val : value;
		value = value > s->end_val ? s->end_val : value;
		s->value = value;
		int left = e->x + bs;
		int right = e->x + s->length - bs;

		new_length = MOUSE_X;
		new_length = new_length < left ? left : new_length;
		new_length = new_length > right ? right : new_length;
		ui_vertex_t *buf = e->index + RECT_BORDERED_VERTS + 2;
		buf++->x = new_length;
		buf++;
		buf++->x = new_length;
		buf++->x = new_length;
	}

	ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
}

#define SLIDER_H_VERTS (RECT_BORDERED_VERTS + RECTANGLE_VERTS)
#define SLIDER_V_VERTS SLIDER_H_VERTS
static void ui_slider_create(ui_interface_t *ui, uint16_t id, ui_slider_t *s){

	ui_element_t *e = &ui->elements[id];
	uint32_t col = ui->theme.slider_col_2;
	uint32_t border_col = ui->theme.border_col;
	uint32_t border_size = ui->theme.slider_border_size; 
	int line_height = ui->font[e->font_id].line_height;
	int width = 0;
	int height = 0;
	int x, y, w, h;
	
	e->data.s = *s;
	int slider_vert_size = SLIDER_H_VERTS * sizeof(ui_vertex_t);
	e->index = ui_buffer_init(slider_vert_size);

	for (int i = 0; i < SLIDER_H_VERTS; ++i){
		e->index[i].u = GEOM_UV;
		e->index[i].v = GEOM_UV;
	}
	if (!s->width){
		s->width = ui->font[e->font_id].line_height;
	}

	// (input - min) / (max - min)
	float scale = (s->value - s->start_val) / (s->end_val - s->start_val);
	if (e->type == UI_SLIDER_V || e->type == UI_SLIDER_V_HIDE_VAL){
		width = s->width;
		height = s->length;
		x = e->x + border_size;
		y = e->y + height - border_size;
		h = (height - 2 * border_size) * (-scale);
		w = width - 2 * border_size;
	}
	else {
		width = s->length;
		height = s->width;
		// height = height < line_height ? line_height : height;
		x = e->x + border_size;
		y = e->y + border_size;
		h = height - 2 * border_size;
		w = (width - 2 * border_size) * scale;
	}
	e->num_verts += 
		ui_rect_bordered_create(e->index , e->x, e->y, width, height, col, border_col, border_size);

	col = ui->theme.slider_col_1;
	
	e->num_verts += 
		ui_rectangle_create(e->index + e->num_verts, x, y, w, h, col);

	e->content_region[0] = e->x;
	e->content_region[1] = e->y;
	e->content_region[2] = e->x + width;
	e->content_region[3] = e->y + height;
	
	if (e->type == UI_SLIDER_V){
	// todo get precision here to calculate proper bounds
		e->bounds[0] = e->x - width;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + 2 * width;
		e->bounds[3] = e->y + height + line_height;
	}
	else if (e->type == UI_SLIDER_H || e->type == UI_SLIDER_H_HIDE_VAL){
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + height;
	}
	else if (e->type == UI_SLIDER_V_HIDE_VAL){
		e->bounds[0] = e->x;
		e->bounds[1] = e->y;
		e->bounds[2] = e->x + width;
		e->bounds[3] = e->y + height;
	}
	assert(e->num_verts == SLIDER_H_VERTS);
	if (e->type == UI_SLIDER_H || e->type == UI_SLIDER_V){
		ui_buffer_reserve(&e->index, VALUE_VERTS * sizeof(ui_vertex_t));
	}
}
// -----------------------------------------------------
// ######## functions to pass data back to user ########
// -----------------------------------------------------
// External function for uids

int ui_get_element_width(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid ui_read_button\n");
		return -1;
	}

	return ui->elements[id].bounds[2] - ui->elements[id].bounds[0];
}

int ui_get_element_height(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid ui_read_button\n");
		return -1;
	}

	return ui->elements[id].bounds[3] - ui->elements[id].bounds[1];
}

int ui_get_element_position(ui_interface_t *ui, uint32_t uid, int *x, int *y){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid ui_read_button\n");
		return -1;
	}
	*x = ui->elements[id].x;
	*y = ui->elements[id].y;
	return 0;
}

int ui_read_button(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid ui_read_button\n");
		return -1;
	}
	return ui->elements[id].data.b.state;
}

// External function for uids
void ui_write_button(ui_interface_t *ui, uint32_t uid, int val){

	// this is really ony for retained state button
	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_button\n");
		return;
	}
	ui->elements[id].data.b.state = !val;
	ui_button_col_swap(ui, id, val);
}

float ui_read_slider(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_slider\n");
		return -1;
	}
	return ui->elements[id].data.s.value;
}
void ui_write_slider(ui_interface_t *ui, uint32_t uid, float value){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_slider\n");
		return;
	}
	// todo this is temp for synth.c
	ui_set_slider_value2(ui, id, value);
}

float ui_read_dial(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_dial\n");
		return -1;
	}
	return ui->elements[id].data.d.value;
}

void ui_write_dial(ui_interface_t *ui, uint32_t uid, float value){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_dial\n");
		return;
	}
	ui->elements[id].data.d.value = value;
	ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
}

float ui_read_parameter(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_parameter\n");
		return -1;
	}
	return ui->elements[id].data.p.value;
}

void ui_write_parameter(ui_interface_t *ui, uint32_t uid, float value){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_parameter\n");
		return;
	}
	ui->elements[id].data.p.value = value;
	ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
}

int ui_read_checkbox(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_checkbox\n");
		return -1;
	}
	return ui->elements[id].data.c.state;
}

void ui_write_checkbox(ui_interface_t *ui, uint32_t uid, int state){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_checkbox\n");
		return;
	}
	int current_state = ui->elements[id].data.c.state;
	if (state != current_state){
		ui_checkbox_col_swap(ui, id);
	}
}

int ui_read_menu(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_menu\n");
		return 0;
	}
	return ui->elements[id].data.m.state;
}

uint32_t ui_read_colour_picker(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_colour_picker\n");
		return 0;
	}
	ui_colour_picker_t *cp = &ui->elements[id].data.cp;
	return cp->presets[cp->active_preset];
}

void ui_write_colour_picker(ui_interface_t *ui, uint32_t uid, uint32_t col){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_write_colour_picker\n");
		return;
	}
	ui_colour_picker_t *cp = &ui->elements[id].data.cp;
	cp->presets[cp->active_preset] = col;
	cp->hue[cp->active_preset] = ROUND(255 * ui_get_hue(col));
}

union element_data ui_read_element(ui_interface_t *ui, uint32_t uid){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uid in ui_read_element\n");
		exit(EXIT_FAILURE);
	}
	return ui->elements[id].data;
}

// ######## Functions to create primitives ########
static inline int ui_rectangle_get_width(ui_vertex_t *b){ return (b[2].x - b[0].x);}
static inline int ui_rectangle_get_height(ui_vertex_t *b){ return (b[2].y - b[0].y);}
static inline int ui_rectangle_get_x(ui_vertex_t *b){ return (b[0].x);}
static inline int ui_rectangle_get_y(ui_vertex_t *b){ return (b[0].y);}

static int ui_rectangle_create(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col){

	uint32_t idx = 0;

	buf[idx].x = x; 	buf[idx].y = y; 	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x; 	buf[idx].y = y + h; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w; buf[idx].y = y + h; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	buf[idx].x = x; 	buf[idx].y = y; 	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w; buf[idx].y = y + h; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w; buf[idx].y = y; 	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	return idx;
}

// Deprecated function still used occassionally for debugging normals etc
static int ui_lines_create(ui_vertex_t *buf, ui_line_t *lines, uint32_t count, uint32_t width){

	uint32_t idx = 0;

	for (int i = 0; i < count; ++i){
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x2; buf[idx].y = lines->y2; buf[idx++].colour = lines->colour2;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x1; buf[idx].y = lines->y1; buf[idx++].colour = lines->colour1;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x1 + width; buf[idx].y = lines->y1; buf[idx++].colour = lines->colour1;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x2; buf[idx].y = lines->y2; buf[idx++].colour = lines->colour2;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x1 + width; buf[idx].y = lines->y1; buf[idx++].colour = lines->colour1;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = lines->x2 + width; buf[idx].y = lines->y2; buf[idx++].colour = lines->colour2;
		++lines;
	}

	return idx;
}

static void ui_rect_bordered_set_pos(ui_vertex_t *buf, float x, float y){

	float w = ui_rect_bordered_get_width(buf);
	float h = ui_rect_bordered_get_height(buf);
	float border_size = buf[0].x - buf[6].x;

	// central area
	buf->x = x + border_size, (buf++)->y = y + border_size; 
	buf->x = x + border_size, (buf++)->y = y + h - border_size; 
	buf->x = x + w - border_size, (buf++)->y = y + h - border_size; 

	buf->x = x + border_size, (buf++)->y = y + border_size; 
	buf->x = x + w - border_size, (buf++)->y = y + h - border_size; 
	buf->x = x + w - border_size, (buf++)->y = y + border_size; 

	// top border
	buf->x = x, (buf++)->y = y; 
	buf->x = x, (buf++)->y = y + border_size; 
	buf->x = x + w, (buf++)->y = y + border_size; 

	buf->x = x, (buf++)->y = y; 
	buf->x = x + w, (buf++)->y = y + border_size; 
	buf->x = x + w, (buf++)->y = y; 

	// left  border
	buf->x = x; (buf++)->y = y + border_size; 
	buf->x = x; (buf++)->y = y + h - border_size; 
	buf->x = x + border_size; (buf++)->y = y + h - border_size; 

	buf->x = x; (buf++)->y = y + border_size; 
	buf->x = x + border_size; (buf++)->y = y + h - border_size; 
	buf->x = x + border_size; (buf++)->y = y + border_size; 

	// Right border
	buf->x = x + w - border_size; (buf++)->y = y + border_size; 
	buf->x = x + w - border_size; (buf++)->y = y + h - border_size; 
	buf->x = x + w; (buf++)->y = y + h - border_size; 

	buf->x = x + w - border_size; (buf++)->y = y + border_size; 
	buf->x = x + w; (buf++)->y = y + h - border_size; 
	buf->x = x + w; (buf++)->y = y + border_size; 

	// bottom border
	buf->x = x; (buf++)->y = y + h - border_size;
	buf->x = x; (buf++)->y = y + h; 
	buf->x = x + w; (buf++)->y = y + h; 
	
	buf->x = x; (buf++)->y = y + h - border_size; 
	buf->x = x + w; (buf++)->y = y + h; 
	buf->x = x + w; (buf++)->y = y + h - border_size; 
}

static inline int ui_rect_bordered_get_width(ui_vertex_t *b){ return (b[28].x - b[6].x);}
static inline int ui_rect_bordered_get_height(ui_vertex_t *b){ return (b[28].y - b[6].y);}
static inline int ui_rect_bordered_get_x(ui_vertex_t *b){ return (b[6].x);}
static inline int ui_rect_bordered_get_y(ui_vertex_t *b){ return (b[6].y);}

static int ui_rect_bordered_create(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col, uint32_t border_col, uint32_t border_size){

	int idx = 0;

	// Ensure a border of at least 1 pixel
	//if (border_size < 1) border_size = 1;


	// central area
	buf[idx].x = x + border_size,		buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + border_size,		buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w - border_size,	buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	buf[idx].x = x + border_size,		buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w - border_size,	buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = x + w - border_size,	buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	// top border
	buf[idx].x = x,			buf[idx].y = y;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x,			buf[idx].y = y + border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w,		buf[idx].y = y + border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	buf[idx].x = x,			buf[idx].y = y;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w,		buf[idx].y = y + border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w,		buf[idx].y = y;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	// left  border
	buf[idx].x = x;					buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x;					buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + border_size;	buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	buf[idx].x = x;					buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + border_size;	buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + border_size;	buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	// Right border
	buf[idx].x = x + w - border_size;	buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w - border_size;	buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w;					buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	buf[idx].x = x + w - border_size;	buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w;					buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w;					buf[idx].y = y + border_size;		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	// bottom border
	buf[idx].x = x;		buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x;		buf[idx].y = y + h;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w; buf[idx].y = y + h;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	
	buf[idx].x = x;		buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w; buf[idx].y = y + h;					buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;
	buf[idx].x = x + w; buf[idx].y = y + h - border_size;	buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = border_col;

	return idx;
}

static int ui_rrect_bordered(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col, uint32_t border_col, float border_size){
	
	uint32_t i = 0;
	float hh = h / 2.0f;
	float hb = border_size / 2.0f;

	// central area
	buf[i].x = x + hh;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;
	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;
	
	buf[i].x = x + hh;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col;

	int seg = SEGMENTS / 2;
	// todo fix all arc creating funcs so they work like you would expect
	i += ui_circle_seg_create(&buf[i], x + hh, y + hh, 0.5f * PI, 1.5f * PI, hh - border_size, seg, col);
	i += ui_circle_seg_create(&buf[i], x + w - hh, y + hh, 1.5f * PI, 2.5f * PI, hh - border_size, seg, col);

	// top border/rectangle
	buf[i].x = x + hh;		buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + hh;		buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	buf[i].x = x + hh;		buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	// bottom border/rectangle
	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + hh;		buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	
	i +=
	ui_line_arc_create_wide(&buf[i], x + hh, y + hh, 0.5f * PI, 1.5f * PI , hh - hb, border_col, seg, border_size);
	i +=
	ui_line_arc_create_wide(&buf[i], x + w - hh, y + hh, 1.5f * PI, 2.5f * PI, hh - hb, border_col, seg, border_size);

	return i;
}

static int ui_list_box_rrect_bordered(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col_l, uint32_t col_r, uint32_t border_col, float border_size){
	
	uint32_t i = 0;
	float hh = h / 2.0f;
	float hb = border_size / 2.0f;
	float sp = w - hh - hh;

	// central area left
	buf[i].x = x + hh;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	buf[i].x = x + sp;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	
	buf[i].x = x + hh;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	buf[i].x = x + sp;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	buf[i].x = x + sp;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_l;
	// central area right
	buf[i].x = x + sp;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;
	buf[i].x = x + sp;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;
	
	buf[i].x = x + sp;		buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;		 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = col_r;

	int seg = SEGMENTS / 2;
	// todo fix all arc creating funcs so they work like you would expect
	i += ui_circle_seg_create(&buf[i], x + hh, y + hh, 0.5f * PI, 1.5f * PI, hh - border_size, seg, col_l);
	i += ui_circle_seg_create(&buf[i], x + w - hh, y + hh, 1.5f * PI, 2.5f * PI, hh - border_size, seg, col_r);

	// top border/rectangle
	buf[i].x = x + hh;		buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + hh;		buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	buf[i].x = x + hh;		buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	// bottom border/rectangle
	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + hh;		buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;

	buf[i].x = x + hh;		buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h;				 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	buf[i].x = x + w - hh;	buf[i].y = y + h - border_size;	 buf[i].u = GEOM_UV; buf[i].v = GEOM_UV; buf[i++].colour = border_col;
	
	i +=
	ui_line_arc_create_wide(&buf[i], x + hh, y + hh, 0.5f * PI, 1.5f * PI , hh - hb, border_col, seg, border_size);
	i +=
	ui_line_arc_create_wide(&buf[i], x + w - hh, y + hh, 1.5f * PI, 2.5f * PI, hh - hb, border_col, seg, border_size);

	return i;
}
static int ui_rrect_bordered_gradient(ui_vertex_t *buf, float x, float y, float w, float h, uint32_t col1, uint32_t col2, uint32_t border_col, float border_size){
	
	uint32_t i = 0;

	w /= 2.0f;
	h /= 2.0f;
	// central area
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h - border_size;
	
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h - border_size;
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = 1.0f * h - border_size;

	// left rectangle
	buf[i].x = -1.0f * w; 					buf[i++].y = -1.0f * h + border_size;	
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;
	buf[i].x = -1.0f * w;					buf[i++].y = 1.0f * h - border_size;
	
	buf[i].x = -1.0f * w;					buf[i++].y = 1.0f * h - border_size;
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = 1.0f * h - border_size;

	// right rectangle
	buf[i].x = 1.0f * w; 					buf[i++].y = 1.0f * h - border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w; 					buf[i++].y = -1.0f * h + border_size; 

	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w; 					buf[i++].y = 1.0f * h - border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h - border_size; 

	// top rectangle
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = 1.0f * h - border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h - border_size;
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = 1.0f * h; 

	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = 1.0f * h; 
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h - border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = 1.0f * h;

	// bottom rectangle
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h; 
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h;
	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;

	buf[i].x = -1.0f * w + border_size; 	buf[i++].y = -1.0f * h + border_size;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h;
	buf[i].x = 1.0f * w - border_size;		buf[i++].y = -1.0f * h + border_size; 
	
	int seg = SEGMENTS / 4;
	i += ui_circle_seg_create(&buf[i], w - border_size, h - border_size, 0, PI * 0.5f, border_size, seg, border_col);
	i += ui_circle_seg_create(&buf[i], -w + border_size, h - border_size, PI * 0.5f, PI, border_size, seg, border_col);
	i += ui_circle_seg_create(&buf[i], -w + border_size, -h + border_size, PI, PI * 1.5f, border_size, seg, border_col);
	i += ui_circle_seg_create(&buf[i], w - border_size, -h + border_size, PI * 1.5f, PI * 2.0f, border_size, seg, border_col);

	for (int v = 0; v < i; ++v){
		if (v > 5){

		}
		else{
			if (buf[v].y < 0){
				buf[v].colour = col1;
			}
			else {
				buf[v].colour = col2;
			}
		}
		
		buf[v].x += (w + x);
		buf[v].y += (h + y);
	}
	return i;
}

static inline int ui_hexagon_create(ui_vertex_t *buf, float x, float y, float radius, uint32_t col){

	return ui_circle_seg_create(buf, x, y, 0, 2 * PI, radius, 6, col);
}

static int ui_circle_seg_create(ui_vertex_t *buf, float x, float y, float start_angle, float end_angle, float mag, int segments, uint32_t col){

	float angle;
	float ang_inc = (end_angle - start_angle) / segments;
	int i = 0;

	for (int s = 0; s < segments; ++s){
		buf[i].x = x;
		buf[i].y = y;
		buf[i].u = GEOM_UV;
		buf[i].v = GEOM_UV;
		buf[i++].colour = col;
		angle = start_angle + s * ang_inc;
		buf[i].x = mag * cosf(angle) + x; 
		buf[i].y = mag * sinf(angle) + y;
		buf[i].u = GEOM_UV;
		buf[i].v = GEOM_UV;
		buf[i++].colour = col;
		angle = start_angle + (s + 1) * ang_inc;
		buf[i].x = mag * cosf(angle) + x; 
		buf[i].y = mag * sinf(angle) + y;
		buf[i].u = GEOM_UV;
		buf[i].v = GEOM_UV;
		buf[i++].colour = col;
	}
	return i;
}
/*
static int ui_line_arc_create(ui_line_t *buf, float x, float y, float start_angle, float end_angle, float mag, uint32_t col, uint16_t seg){

	float angle;
	float ang_inc = (end_angle - start_angle) / seg;
	int i = 0;

	for (int s = 0; s < seg; ++s){
		angle = start_angle + s * ang_inc;
		buf[i].x1 = mag * cosf(angle) + x;
		buf[i].y1 = mag * sinf(angle) + y;
		buf[i].colour1 = col;
		angle = start_angle + (s + 1) * ang_inc;
		buf[i].x2 = mag * cosf(angle) + x; 
		buf[i].y2 = mag * sinf(angle) + y;
		buf[i++].colour2 = col;
	}
	// todo this must be fixed
	//ui_buffer_lines(ui, id, buf, i);
	return i;
}
*/
static int ui_line_arc_create_wide(ui_vertex_t *buf, float x, float y, float start_angle, float end_angle, float mag, uint32_t col, uint16_t seg, float width){

	uint32_t verts = 0;
	
	v2 points[seg + 1];
	float angle;
	float ang_inc = (end_angle - start_angle) / (float)seg;
	uint16_t i = 0;

	for (int s = 0; s <= seg; ++s){
		angle = start_angle + s * ang_inc;
		points[i].x = mag * cosf(angle) + x;
		points[i].y = mag * sinf(angle) + y;
		++i;
	}
	verts += ui_line_strip_create(buf ,points, i, col, width);
	return verts;
}

static int ui_line_strip_create_aa(ui_vertex_t *buf, v2 *p, uint16_t num_points, uint32_t col, float width){

	// x--------------y
	// a--------------b
	// |       		  |
	// c--------------d
	// z--------------w
	// acb bcd ayx aby czd zwd
	uint32_t fade_col = col & 0x00FFFFFF;
	uint32_t idx = 0;
	// todo find a way to handle this var
	float fade_factor = 16.0f;

	v2 line1, line2, perp, normal, a, b, c, d, x, y, w , z, prev_b, prev_d, prev_y, prev_w;
	// perpendicular vector
	line1 = ui_sub_v2(p[1], p[0]);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, width * 0.5f);
	// first point starts:
	a = ui_add_v2(p[0], perp);	
	b = ui_add_v2(p[1], perp);	
	c = ui_sub_v2(p[0], perp);	
	d = ui_sub_v2(p[1], perp);	

	perp = ui_scale_v2(perp, fade_factor);
	x = ui_add_v2(p[0], perp);
	y = ui_add_v2(p[1], perp);
	z = ui_sub_v2(p[0], perp);
	w = ui_sub_v2(p[1], perp);	

	// get intersection of normal and line edges
	if (2 != num_points){
		// get normal direction of two lines = mitre line
		line2 = ui_sub_v2(p[1], p[2]);
		line1 = ui_normalise_v2(line1);
		line2 = ui_normalise_v2(line2);
		normal = ui_add_v2(line1, line2);
		// normal = ui_normalise_v2(normal);
		b = ui_2line_intersection(a, b, p[1], ui_add_v2(p[1], normal));
		y = ui_2line_intersection(x, y, p[1], ui_add_v2(p[1], normal));
		d = ui_2line_intersection(c, d, p[1], ui_add_v2(p[1], normal));
		w = ui_2line_intersection(z, w, p[1], ui_add_v2(p[1], normal));
	}
	prev_b = b;
	prev_d = d;
	prev_y = y;
	prev_w = w;
	// push triangles to vertex buffer
	// acb bcd ayx aby czd zwd
	buf[idx].x = a.x; buf[idx].y = a.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = c.x; buf[idx].y = c.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = b.x; buf[idx].y = b.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	buf[idx].x = b.x; buf[idx].y = b.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = c.x; buf[idx].y = c.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = d.x; buf[idx].y = d.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	buf[idx].x = a.x; buf[idx].y = a.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = y.x; buf[idx].y = y.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;
	buf[idx].x = x.x; buf[idx].y = x.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;

	buf[idx].x = a.x; buf[idx].y = a.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = b.x; buf[idx].y = b.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = y.x; buf[idx].y = y.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;

	buf[idx].x = c.x; buf[idx].y = c.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;
	buf[idx].x = z.x; buf[idx].y = z.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;
	buf[idx].x = d.x; buf[idx].y = d.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	buf[idx].x = z.x; buf[idx].y = z.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;
	buf[idx].x = w.x; buf[idx].y = w.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = fade_col;
	buf[idx].x = d.x; buf[idx].y = d.y; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV; buf[idx++].colour = col;

	for (int i = 1; i < num_points - 1; ++i){
		// perpendicular vector
		line1 = ui_sub_v2(p[i + 1], p[i]);
		perp = ui_perp_v2(line1);
		perp = ui_scale_v2(perp, width * 0.5f);
		
		// setup points on rectangle that define line edges ab, cd
		b = ui_add_v2(p[i + 1], perp);	
		d = ui_sub_v2(p[i + 1], perp);
		perp = ui_scale_v2(perp, fade_factor);
		y = ui_add_v2(p[i + 1], perp);
		w = ui_sub_v2(p[i + 1], perp);
		a = prev_b;
		c = prev_d;
		x = prev_y;
		z = prev_w;
		// get intersection of normal and line edges
		if (i + 2 != num_points){
			// normal is not the normal here!
			// get normal direction of two lines = mitre line
			line2 = ui_sub_v2(p[i + 1], p[i + 2]);
			line1 = ui_normalise_v2(line1);
			line2 = ui_normalise_v2(line2);
			normal = ui_add_v2(line1, line2);
				
			b = ui_2line_intersection(a, b, p[i + 1], ui_add_v2(p[i + 1], normal));
			d = ui_2line_intersection(c, d, p[i + 1], ui_add_v2(p[i + 1], normal));
			y = ui_2line_intersection(x, y, p[i + 1], ui_add_v2(p[i + 1], normal));
			w = ui_2line_intersection(z, w, p[i + 1], ui_add_v2(p[i + 1], normal));
		}
		prev_b = b;
		prev_d = d;
		prev_y = y;
		prev_w = w;
		// push triangles to vertex buffer
		// acb bcd ayx aby czd zwd

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = a.x; buf[idx].y = a.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = c.x; buf[idx].y = c.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = b.x; buf[idx].y = b.y; buf[idx++].colour = col;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = b.x; buf[idx].y = b.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = c.x; buf[idx].y = c.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = d.x; buf[idx].y = d.y; buf[idx++].colour = col;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = a.x; buf[idx].y = a.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = y.x; buf[idx].y = y.y; buf[idx++].colour = fade_col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = x.x; buf[idx].y = x.y; buf[idx++].colour = fade_col;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = a.x; buf[idx].y = a.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = b.x; buf[idx].y = b.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = y.x; buf[idx].y = y.y; buf[idx++].colour = fade_col;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = c.x; buf[idx].y = c.y; buf[idx++].colour = col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = z.x; buf[idx].y = z.y; buf[idx++].colour = fade_col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = d.x; buf[idx].y = d.y; buf[idx++].colour = col;

		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = z.x; buf[idx].y = z.y; buf[idx++].colour = fade_col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = w.x; buf[idx].y = w.y; buf[idx++].colour = fade_col;
		buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = d.x; buf[idx].y = d.y; buf[idx++].colour = col;
	}

	return idx;
}

// todo can save a lot of computation here by testing if points are colinear
static int ui_line_strip_create(ui_vertex_t *buf, v2 *p, uint16_t num_points, uint32_t col, float width){

	// a--------b
	// |        |
	// c--------d
	
	uint32_t idx = 0;	

	v2 line1, line2, perp, normal, a, b, c, d, prev_b, prev_d;
	// perpendicular vector
	line1 = ui_sub_v2(p[1], p[0]);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, width * 0.5f);
	// first point starts:
	a = ui_add_v2(p[0], perp);
	b = ui_add_v2(p[1], perp);

	c = ui_sub_v2(p[0], perp);
	d = ui_sub_v2(p[1], perp);
	// get intersection of normal and line edges
	if (2 != num_points){
		// get normal direction of two lines = mitre line
		line2 = ui_sub_v2(p[1], p[2]);
		line1 = ui_normalise_v2(line1);
		line2 = ui_normalise_v2(line2);
		normal = ui_add_v2(line1, line2);
		// normal = ui_normalise_v2(normal);
		b = ui_2line_intersection(a, b, p[1], ui_add_v2(p[1], normal));
		d = ui_2line_intersection(c, d, p[1], ui_add_v2(p[1], normal));
	}
	prev_b = b;
	prev_d = d;
	// push triangles to vertex buffer
	// acb bcd
	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = a.x; buf[idx++].y = a.y;
	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = c.x; buf[idx++].y = c.y;
	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = b.x; buf[idx++].y = b.y;

	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = b.x; buf[idx++].y = b.y;
	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = c.x; buf[idx++].y = c.y;
	buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
	buf[idx].x = d.x; buf[idx++].y = d.y;

	for (int i = 1; i < num_points - 1; ++i){
		// perpendicular vector
		line1 = ui_sub_v2(p[i + 1], p[i]);
		perp = ui_perp_v2(line1);
		perp = ui_scale_v2(perp, width * 0.5f);
		
		// setup points on rectangle that define line edges ab, cd
		a = prev_b;
		b = ui_add_v2(p[i + 1], perp);
		c = prev_d;
		d = ui_sub_v2(p[i + 1], perp);
		// get intersection of normal and line edges
		if (i + 2 != num_points){
			// get normal direction of two lines = mitre line
			line2 = ui_sub_v2(p[i + 1], p[i + 2]);
			line1 = ui_normalise_v2(line1);
			line2 = ui_normalise_v2(line2);
			normal = ui_add_v2(line1, line2);
			// normal = ui_normalise_v2(normal);
			b = ui_2line_intersection(a, b, p[i + 1], ui_add_v2(p[i + 1], normal));
			d = ui_2line_intersection(c, d, p[i + 1], ui_add_v2(p[i + 1], normal));
			// todo more efficient detection of parrallel lines
		}
		prev_b = b;
		prev_d = d;
		// push triangles to vertex buffer
		// acb bcd
		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = a.x; buf[idx++].y = a.y;
		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = c.x; buf[idx++].y = c.y;
		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = b.x; buf[idx++].y = b.y;

		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = b.x; buf[idx++].y = b.y;
		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = c.x; buf[idx++].y = c.y;
		buf[idx].colour = col; buf[idx].u = GEOM_UV; buf[idx].v = GEOM_UV;
		buf[idx].x = d.x; buf[idx++].y = d.y;
	}
	
	return idx;
}

// todo make a version of this for rounded corners
// ui line strip bevelled
static int ui_line_strip_bevelled_create(ui_vertex_t *buf, v2 *p, uint16_t num_points, uint32_t col, float width){

	// a--------b
	// |        |
	// c--------d
	uint32_t count = 0;	

	v2 line1, line2, perp, normal, a, b, c, d;
	// Get the perpendicular vector so we can give the line width
	line1 = ui_sub_v2(p[1], p[0]);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, width * 0.5f);

	// first point starts:
	a = ui_add_v2(p[0], perp);
	c = ui_sub_v2(p[0], perp);

	for (int i = 0; i < num_points - 2; ++i){
		#ifdef COLINEAR_OPTIMISATION
		// Are points coliniear? Early exit here seems very effective
		float g1 = (p[i+1].y - p[i].y) * (p[i+2].x - p[i+1].x);
		float g2 = (p[i+2].y - p[i+1].y) * (p[i+1].x - p[i].x);
		if (fabsf(g2-g1) < 0.01f){
			continue;
		}
		#endif
		// if ((p[i+1].y - p[i].y) * (p[i+2].x - p[i+1].x) == (p[i+2].y - p[i+1].y) * (p[i+1].x - p[i].x)){
		// 	// printf("found colinear : %d\n", i);
		// 	continue;
		// }
		// perpendicular vector
		line1 = ui_sub_v2(p[i + 1], p[i]);
		perp = ui_perp_v2(line1);
		perp = ui_scale_v2(perp, width * 0.5f);
		
		// setup points on rectangle that define line edges ab, cd
		b = ui_add_v2(p[i + 1], perp);
		d = ui_sub_v2(p[i + 1], perp);
		// get intersection of normal and line edges
		// get normal direction of two lines = mitre line
		line2 = ui_sub_v2(p[i + 1], p[i + 2]);
		line1 = ui_normalise_v2(line1);
		line2 = ui_normalise_v2(line2);
		normal = ui_add_v2(line1, line2);
		// normal = ui_normalise_v2(normal);
		if (ui_mag_squared_v2(ui_sub_v2(d, p[i + 2])) < ui_mag_squared_v2(ui_sub_v2(b, p[i + 2]))){
			// d is the inside point
			// thereis a possibility that the points are colinear and our subsequent
			// triangle has zero thickness, is this an issue?
			d = ui_2line_intersection(c, d, p[i + 1], ui_add_v2(p[i + 1], normal));

			buf += ui_make_triangle(buf, a, c, b, col);
			buf += ui_make_triangle(buf, b, c, d, col);

			// perpendicular vector
			line1 = ui_sub_v2(p[i + 2], p[i + 1]);
			perp = ui_perp_v2(line1);
			perp = ui_scale_v2(perp, width * 0.5f);
		
			c = d;
			a = ui_add_v2(p[i + 1], perp);
			// construct an additional triangle for the gap between the two
			// rectagulated lines using points b, d and p[2]s' a
			buf += ui_make_triangle(buf, b, d, a, col);
			count += 9;
		}
		else {
			// b is the inside point
			b = ui_2line_intersection(a, b, p[i + 1], ui_add_v2(p[i + 1], normal));
			// construct an additional triangle for the gap between the two
			// rectagulated lines using points b, d and p[2]s' c

			buf += ui_make_triangle(buf, a, c, b, col);
			buf += ui_make_triangle(buf, b, c, d, col);

			// perpendicular vector
			line1 = ui_sub_v2(p[i + 2], p[i + 1]);
			perp = ui_perp_v2(line1);
			perp = ui_scale_v2(perp, width * 0.5f);
		
			c = ui_sub_v2(p[i + 1], perp);
			a = b;
			buf += ui_make_triangle(buf, d, c, b, col);
			count += 9;
		}
	}
	line1 = ui_sub_v2(p[num_points - 1], p[num_points - 2]);
	perp = ui_perp_v2(line1);
	perp = ui_scale_v2(perp, width * 0.5f);
	
	b = ui_add_v2(p[num_points - 1], perp);
	d = ui_sub_v2(p[num_points - 1], perp);
	buf += ui_make_triangle(buf, a, c, b, col);
	buf += ui_make_triangle(buf, b, c, d, col);
	count += 6;
	
	return count;
}


static inline uint32_t ui_elemtent_intersection(int32_t *a, int32_t *b){
	// currently not used
	return !(b[0] > a[3] || b[3] < a[0] || b[1] < a[3] || b[3] > a[1]);
}

static inline void ui_colour_verts(ui_vertex_t *buf, uint32_t count, uint32_t colour){

	while (count--){
		(buf++)->colour = colour;
	}
}


static inline int ui_make_triangle(ui_vertex_t *buf, v2 vert1, v2 vert2, v2 vert3, uint32_t col){

	buf[0].x = vert1.x;
	buf[0].y = vert1.y;
	buf[0].u = GEOM_UV;
	buf[0].v = GEOM_UV;
	buf[0].colour = col;
	buf[1].x = vert2.x;
	buf[1].y = vert2.y;
	buf[1].u = GEOM_UV;
	buf[1].v = GEOM_UV;
	buf[1].colour = col;
	buf[2].x = vert3.x;
	buf[2].y = vert3.y;
	buf[2].u = GEOM_UV;
	buf[2].v = GEOM_UV;
	buf[2].colour = col;
	return 3;
}

static inline void ui_make_quad(ui_vertex_t *buf, v2 vert1, v2 vert2, v2 vert3, v2 vert4){

	// 1-----4
	// |     |
	// 2-----3
	buf[0].x = vert1.x;
	buf[0].y = vert1.y;
	buf[0].u = GEOM_UV;
	buf[0].v = GEOM_UV;
	buf[1].x = vert2.x;
	buf[1].y = vert2.y;
	buf[1].u = GEOM_UV;
	buf[1].v = GEOM_UV;
	buf[2].x = vert3.x;
	buf[2].y = vert3.y;
	buf[2].u = GEOM_UV;
	buf[2].v = GEOM_UV;

	buf[3].x = vert1.x;
	buf[3].y = vert1.y;
	buf[3].u = GEOM_UV;
	buf[3].v = GEOM_UV;
	buf[4].x = vert3.x;
	buf[4].y = vert3.y;
	buf[4].u = GEOM_UV;
	buf[4].v = GEOM_UV;
	buf[5].x = vert4.x;
	buf[5].y = vert4.y;
	buf[5].u = GEOM_UV;
	buf[5].v = GEOM_UV;
}

// ######## Functions for vector operations ########
static v2 ui_2line_intersection(v2 p1, v2 p2, v2 p3, v2 p4){

	// get form as Ax+By=C: A = y2-y1; B = x1-x2; C = Ax1+By1
	v2 res;
	float a, b, c;
	a = p2.y - p1.y;
	b = p1.x - p2.x;
	c = a * p1.x + b * p1.y;

	float a2, b2, c2;
	a2 = p4.y - p3.y;
	b2 = p3.x - p4.x;
	c2 = a2 * p3.x + b2 * p3.y;

	float det = a * b2 - a2 * b;
	if (det == 0){
		return p2;
		// todo could handle this case
		//log("ERROR: invalid lines in line strip, consecutive lines are parallel");
	}
	else{
		res.x = (b2 * c - b * c2) / det;
		res.y = (a * c2 - a2 * c) / det;
	}
	return res;
}

static inline v2 ui_add_v2(v2 a, v2 b){

	return (v2){a.x + b.x, a.y + b.y};
}

static inline float ui_dot_v2(v2 a, v2 b){

	return a.x * b.x + a.y * b.y;
}

static inline v2 ui_sub_v2(v2 a, v2 b){

	return (v2){a.x - b.x, a.y - b.y};
}

static inline float ui_mag_squared_v2(v2 a){
	return a.x * a.x + a.y * a.y;
}

static inline v2 ui_normalise_v2(v2 v){

	float mag = sqrtf(v.x * v.x + v.y * v.y);
	v.x /= mag;
	v.y /= mag;
	return v;
}

static inline v2 ui_scale_v2(v2 v, float mag){

	v.x *= mag;
	v.y *= mag;
	return v;
}

static inline v2 ui_perp_v2(v2 v){
	
	v2 res;
	v = ui_normalise_v2(v);
	res.x = -v.y;
	res.y = v.x;
	return res;
}

void ui_set_line_width(ui_interface_t *ui, float width){

	ui->theme.line_width = width;
}

// ######## Functions that modify elements ########

// Position an element at a specified coordinate
// used during the construction of a ui, does not interfere with alignment flags
void ui_position_element_internal(ui_interface_t *ui, uint16_t id, int32_t x, int32_t y){

	ui_element_t *e = &ui->elements[id];
	// printf("%d %d : %d %d\n",e->array_index, e->type, x, y);

	x = x - e->bounds[0];
	y = y - e->bounds[1];
	for (int i = 0; i < e->num_verts; ++i){
		e->index[i].x += x;
		e->index[i].y += y;
	}
	e->content_region[0] += x;
	e->content_region[1] += y;
	e->content_region[2] += x;
	e->content_region[3] += y;
	ui->elements[id].bounds[0] += x;
	ui->elements[id].bounds[1] += y;
	ui->elements[id].bounds[2] += x;
	ui->elements[id].bounds[3] += y;
	e->x += x;
	e->y += y;
	if (e->usage != UI_DYNAMIC_CONTENT){
		ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
	}
	if (e->type == UI_LIST_BOX){
		++e;
		if (e->type == UI_LIST_BOX_CONTENT){
			ui_position_element(ui, e->uuid, x + e->bounds[0], y + e->bounds[1]);
		}
	}
}

// Position an element at a specified coordinate
// External function to be used with uids
void ui_position_element(ui_interface_t *ui, uint32_t uid, int32_t x, int32_t y){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_position_element\n");
		return;
	}
	// todo could add to this function to allow positiong groups
	// in a similar manner to how move_element works
	ui_element_t *e = &ui->elements[id];
	// printf("%d %d : %d %d\n",e->array_index, e->type, x, y);
	x = x - e->bounds[0];
	y = y - e->bounds[1];

	for (int i = 0; i < e->num_verts; ++i){
		e->index[i].x += x;
		e->index[i].y += y;
	}
	e->content_region[0] += x;
	e->content_region[1] += y;
	e->content_region[2] += x;
	e->content_region[3] += y;
	ui->elements[id].bounds[0] += x;
	ui->elements[id].bounds[1] += y;
	ui->elements[id].bounds[2] += x;
	ui->elements[id].bounds[3] += y;
	e->x += x;
	e->y += y;
	e->align = 0;
	if (e->usage != UI_DYNAMIC_CONTENT){
		ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
	}
	if (e->type == UI_LIST_BOX){
		++e;
		if (e->type == UI_LIST_BOX_CONTENT){
			ui_position_element(ui, e->uuid, x + e->bounds[0], y + e->bounds[1]);
		}
	}
}

// Moves an element and any elements which are part of the same grouping
// x and y are relative displacements
// external function using uuid's not array indices!
void ui_move_element(ui_interface_t *ui, uint32_t uid, int32_t x, int32_t y){

	int id = ui_find_array_index(ui, uid);
	if (id == -1){
		log("ERROR: Invalid uuid in ui_move_elements: uid = %d\n", uid);
		return;
	}
	// todo we should check if element is flagged as movable
	ui_element_t *e = &ui->elements[id];

	// check parent type of selected object
	int32_t parent = e->parent;


	// Standalone element not part of any group
	if (parent == -1){
		for (int i = 0; i < e->num_verts; ++i){
			e->index[i].x += x;
			e->index[i].y += y;		
		}
		
		e->content_region[0] += x;
		e->content_region[1] += y;
		e->content_region[2] += x;
		e->content_region[3] += y;
		e->bounds[0] += x;
		e->bounds[1] += y;
		e->bounds[2] += x;
		e->bounds[3] += y;
		e->x += x;
		e->y += y;
	}
	else {
		// element is part of some kind of grouping
		e = &ui->elements[parent];
		// Find root element
		while (e->parent != e->array_index){
			parent = e->parent;
			e = &ui->elements[parent];
		}
		// move each element in the group
		// todo nested stuff will break here
		while(e->type != UI_GROUP_END && e->type != UI_TABS_END){
			for (int i = 0; i < e->num_verts; ++i){
				e->index[i].x += x;
				e->index[i].y += y;		
			}
			e->content_region[0] += x;
			e->content_region[1] += y;
			e->content_region[2] += x;
			e->content_region[3] += y;
			e->bounds[0] += x;
			e->bounds[1] += y;
			e->bounds[2] += x;
			e->bounds[3] += y;
			e->x += x;
			e->y += y;
			++e;
		}
	}
	
	ui->modified |= GEOM_MODIFIED | TEXT_MODIFIED;
}

// this works well for dynamic content
// Would be good to add waveform to texture function
// todo
static void ui_render_waveform_to_element(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count){

	// todo add padding
	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf;
	uint32_t size = sizeof(ui_vertex_t) * RECTANGLE_VERTS * 2 * (e->content_region[2] - e->content_region[0]);

	if (e->usage == UI_STATIC_CONTENT){
		size += RECTANGLE_VERTS;
		ui_buffer_reset(&e->index);
		ui_buffer_reserve(&e->index, size);
		e->num_verts = RECTANGLE_VERTS;
		buf = e->index + e->num_verts;
	}
	else {
		ui_buffer_reserve(&ui->dynamic_buffer, size);
		buf = ui->dynamic_buffer + ui->num_dynamic_verts;
	}

	uint32_t width = e->content_region[2] - e->content_region[0];
	// center of drawable region
	uint32_t max_height = (e->content_region[3] - e->content_region[1]) / 2.0f;
	// get number of samples per pixel
	float spp = count / (float)width;
	uint32_t c2 = ui->theme.wave_col_top;
	uint32_t c1 = c2;//ui->theme.wave_col_bot;
	
	float sample_buf;

	float start_y = e->content_region[3] - max_height;
	float start_x = e->content_region[0];

	float s_prev = 1.0f;

	for (int i = 0; i < width; ++i){

		sample_buf = samples[(int)(i * spp)];
		sample_buf = (sample_buf > 1.0f) ? 1.0f : sample_buf;
		sample_buf = (sample_buf < -1.0f) ? -1.0f : sample_buf;

		sample_buf = max_height * sample_buf;

		buf->x = i + start_x;
		buf->y = start_y - s_prev;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		buf->x = i + 1 + start_x;
		buf->y = start_y + sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		buf->x = i + start_x;
		buf->y = start_y + s_prev;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;

		buf->x = i + start_x;
		buf->y = start_y - s_prev;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		buf->x = i + 1 + start_x;
		buf->y = start_y - sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		buf->x = i + 1 + start_x;
		buf->y = start_y + sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		s_prev = sample_buf;
	}
	if (e->usage == UI_STATIC_CONTENT){
		e->num_verts += 6 * width;
		ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	}
	else {
		ui->num_dynamic_verts += 6 * width;
	}
	return;
}

static void ui_render_waveform_linear(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count){

	ui_element_t *e = &ui->elements[id];
	int pad = ui->theme.padding_size;
	int line_width = ui->theme.line_width;
	uint32_t width = e->content_region[2] - e->content_region[0];
	uint32_t height_offset = (e->content_region[3] - e->content_region[1]) / 2.0f;
	height_offset -= pad;
	uint32_t col = ui->theme.wave_col_top;
	
	float sample_buf;
	float start_y = e->content_region[3] - height_offset;
	float start_x = e->content_region[0];

	float quad_width = 2;
	int num_quads = width / quad_width;
	int spq = count / (float)num_quads;
	// if (spq > quad_width)
		// quad_width = width / (float)num_quads;
	// printf("spq = %i, count = %d numq = %d\n", spq, count, num_quads);
	v2 points[num_quads + 1];

	for (int i = 0; i <= num_quads; ++i){

		sample_buf = samples[(i) * spq];
		sample_buf = sample_buf > 1.0f ? 1.0f : sample_buf;
		sample_buf = sample_buf < -1.0f ? -1.0f : sample_buf;

		sample_buf = height_offset * sample_buf;
		points[i].x = i * quad_width + start_x;
		points[i].y = start_y + sample_buf;
	}

	uint32_t size = sizeof(ui_vertex_t) * (RECTANGLE_VERTS + 3) * (e->content_region[2] - e->content_region[0]);
	if (e->usage == UI_STATIC_CONTENT){
		size += RECTANGLE_VERTS;
		ui_buffer_reset(&e->index);
		ui_buffer_reserve(&e->index, size);
		e->num_verts = RECTANGLE_VERTS;
		e->num_verts +=
		ui_line_strip_bevelled_create(e->index + e->num_verts, points, num_quads + 1, col, line_width);
		ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	}
	else {
		ui_buffer_reserve(&ui->dynamic_buffer, size);
		ui->num_dynamic_verts +=
		ui_line_strip_bevelled_create(ui->dynamic_buffer + ui->num_dynamic_verts, points, num_quads + 1, col, line_width);
	}
}

// Takes data in the range 0->1 and  renders a line to represent the data in the available space
// in the specified rectangle.
static void ui_render_data_linear(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count){

	ui_element_t *e = &ui->elements[id];
	int pad = ui->theme.padding_size;
	int line_width = ui->theme.line_width;
	uint32_t width = e->content_region[2] - e->content_region[0];
	uint32_t height_offset = (e->content_region[3] - e->content_region[1]) - pad;
	uint32_t col = ui->theme.wave_col_top;
	
	float sample_buf;
	float start_y = e->content_region[1] + height_offset;
	float start_x = e->content_region[0];

	float quad_width = 1;
	int num_quads;
	float spq;
	do {
		quad_width += 1;
		num_quads = width / quad_width;
		spq = count / (float)num_quads;
	} while (spq <= 2);
	// if (spq > quad_width)
		// quad_width = width / (float)num_quads;
	// printf("spq = %i, count = %d numq = %d\n", spq, count, num_quads);
	v2 points[num_quads + 1];
	uint32_t samples_processed = 0;

	for (int i = 0; i <= num_quads; ++i){

		// get the highest sample in the range
		sample_buf = 0;
		while (samples_processed < ((i + 1) * spq)){
			sample_buf = fabsf(*samples) > fabsf(sample_buf) ? *samples : sample_buf;
			++samples_processed;
			++samples;
		}

		sample_buf = sample_buf > 1.0f ? 1.0f : sample_buf;

		sample_buf = height_offset * sample_buf;
		points[i].x = i * quad_width + start_x;
		points[i].y = start_y - sample_buf;
	}

	uint32_t size = sizeof(ui_vertex_t) * (RECTANGLE_VERTS + 3) * (e->content_region[2] - e->content_region[0]);

	if (e->usage == UI_STATIC_CONTENT){
		size += RECTANGLE_VERTS;
		ui_buffer_reset(&e->index);
		ui_buffer_reserve(&e->index, size);
		e->num_verts = RECTANGLE_VERTS;
		e->num_verts +=
			ui_line_strip_bevelled_create(e->index + e->num_verts, points, num_quads + 1, col, line_width);
		ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	}
	else {
		ui_buffer_reserve(&ui->dynamic_buffer, size);
		ui->num_dynamic_verts +=
			ui_line_strip_bevelled_create(ui->dynamic_buffer + ui->num_dynamic_verts, points, num_quads + 1, col, line_width);
	}
}

static void ui_render_data_bars(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count){

	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf = e->index + e->num_verts;
	uint32_t size = sizeof(ui_vertex_t) * RECTANGLE_VERTS * (e->content_region[2] - e->content_region[0]);

	if (e->usage == UI_STATIC_CONTENT){
		size += RECTANGLE_VERTS;
		ui_buffer_reset(&e->index);
		ui_buffer_reserve(&e->index, size);
		e->num_verts = RECTANGLE_VERTS;
		buf = e->index + e->num_verts;
	}
	else {
		ui_buffer_reserve(&ui->dynamic_buffer, size);
		buf = ui->dynamic_buffer + ui->num_dynamic_verts;
	}

	int pad = ui->theme.padding_size;
	uint32_t c1 = ui->theme.wave_col_bot;
	uint32_t c2 = ui->theme.wave_col_top;

	uint32_t width = e->content_region[2] - e->content_region[0] - 2 * pad;
	uint32_t height_offset = (e->content_region[3] - e->content_region[1]) - pad;
	
	float sample_buf;
	float start_y = e->content_region[1] + height_offset;
	height_offset -= pad;
	float start_x = e->content_region[0] + pad;

	float s_prev = *samples;
	float quad_width = 1;
	int num_quads;
	float spq;
	do {
		quad_width += 1;
		num_quads = width / quad_width;
		spq = count / (float)num_quads;
	} while (spq <= 2);
	
	uint32_t samples_processed = 0;

	for (int i = 0; i <= num_quads; ++i){

		// get the highest sample in the range
		sample_buf = 0;
		while (samples_processed < ((i + 1) * spq)){
			sample_buf = fabsf(*samples) > fabsf(sample_buf) ? *samples : sample_buf;
			++samples_processed;
			++samples;
		}

		sample_buf = sample_buf > 1.0f ? 1.0f : sample_buf;

		sample_buf = height_offset * sample_buf;
		// lower left
		buf->x = i * quad_width + start_x;
		buf->y = start_y;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// upper right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y - sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		// upper left
		buf->x = i * quad_width + start_x;
		buf->y = start_y - s_prev;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;

		// lower left
		buf->x = i * quad_width + start_x;
		buf->y = start_y ;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// lower right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// upper right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y - sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		s_prev = sample_buf;
	}
	if (e->usage == UI_STATIC_CONTENT){
		e->num_verts += 6 * num_quads;
		ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	}
	else {
		ui->num_dynamic_verts += 6 * num_quads;
	}
}


// draws a rectangle for each sampled samples to fit pix per quad
static void ui_render_wavetable_to_element(ui_interface_t *ui, uint16_t id, float *samples, uint32_t count, uint32_t pix_per_quad){

	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf;
	uint32_t size = sizeof(ui_vertex_t) * RECTANGLE_VERTS * 1 * (e->content_region[2] - e->content_region[0]);
	if (e->usage == UI_STATIC_CONTENT){
		size += RECTANGLE_VERTS;
		ui_buffer_reset(&e->index);
		ui_buffer_reserve(&e->index, size);
		e->num_verts = RECTANGLE_VERTS;
		buf = e->index + e->num_verts;
	}
	else {
		ui_buffer_reserve(&ui->dynamic_buffer, size);
		buf = ui->dynamic_buffer + ui->num_dynamic_verts;
	}
	int pad = ui->theme.padding_size;
	uint32_t c1 = ui->theme.wave_col_bot;
	uint32_t c2 = ui->theme.wave_col_top;

	uint32_t width = e->content_region[2] - e->content_region[0] - 2 * pad;
	uint32_t height_offset = (e->content_region[3] - e->content_region[1]) / 2.0f;
	height_offset -= pad;
	float start_y = e->content_region[3] - height_offset;
	float start_x = e->content_region[0] + pad;
	
	float quad_width = 2;

	float sample_buf;
	float s_prev = *samples;
	int num_quads = width / quad_width;
	// get number of samples per quad
	int spq = count / (float)num_quads;
	// if (spq < quad_width){
	// 	printf("spq = %i, should upsample, count = %d\n", spq, count);
	// 	return;
	// }
	// printf("spq = %i, count = %d numq = %d\n", spq, count, num_quads);
	// recompute to include the fractional component
	quad_width = width / (float)num_quads;

	for (int i = 0; i < num_quads; ++i){

		sample_buf = samples[(i + 1) * spq];
		// get the highest sample in the range
		// while (samples_processed < ((i + 1) * spq)){
			// sample_buf = fabsf(*samples) > fabsf(sample_buf) ? *samples : sample_buf;
			// ++samples_processed;
			// ++samples;
		// }
		// sample_buf = *samples;

		// Clip to the wiewport boundries
		sample_buf = sample_buf > 1.0f ? 1.0f : sample_buf;
		sample_buf = sample_buf < -1.0f ? -1.0f : sample_buf;
		sample_buf = height_offset * sample_buf;

		// lower left
		buf->x = i * quad_width + start_x;
		buf->y = start_y;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// upper right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y + sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		// upper left
		buf->x = i * quad_width + start_x;
		buf->y = start_y + s_prev;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;

		// lower left
		buf->x = i * quad_width + start_x;
		buf->y = start_y ;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// lower right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c1;
		// upper right
		buf->x = i * quad_width + quad_width + start_x;
		buf->y = start_y + sample_buf;
		buf->u = GEOM_UV;
		buf->v = GEOM_UV;
		buf++->colour = c2;
		s_prev = sample_buf;
	}
	if (e->usage == UI_STATIC_CONTENT){
		e->num_verts += 6 * num_quads;
		ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	}
	else {
		ui->num_dynamic_verts += 6 * num_quads;
	}
	// ui->modified |= GEOM_MODIFIED | GEOM_SIZE_MODIFIED;
	return;
}

static void ui_render_chart_to_element(ui_interface_t *ui, uint16_t id, float *points, uint32_t count){

	// todo this function could be improved
	// better to get rid of make quad and colour as we go
	// also add padding
	ui_element_t *e = &ui->elements[id];
	ui_vertex_t *buf = ui->elements[id].index + e->num_verts;

	v2 va, vb, vc, vd;
	float width = e->content_region[2] - e->content_region[0];
	float height = e->content_region[3] - e->content_region[1];
	float base = e->content_region[3];
	float ppp = (float)width / (count - 1);

	for (int i = 0; i < count - 1; ++i){
		va.x = ppp * i + e->content_region[0]; 	
		va.y = base - points[i] * height;
		vb.x = ppp * i + e->content_region[0]; 	
		vb.y = base;
		vc.x = ppp * (i + 1) + e->content_region[0];
		vc.y = base;
		vd.x = ppp * (i + 1) + e->content_region[0];
		vd.y = base - points[i + 1] * height;	
		// buf[i++].y = 1.0f * y_size - border_size;
		ui_make_quad(&buf[i * 6], va, vb, vc, vd);
	}

	for (int i = 0; i < (count - 1) * 6; ++i){
		if (buf[i].y < (e->content_region[3] - height / 2)){
			buf[i].colour = ui->theme.content_col_top;
		}
		else{
			buf[i].colour = ui->theme.content_col_bot;
		}
	}
}

// ######## Init close update render #########
void *ui_init(char *path){

	if (path != NULL){
		// todo load ui specified by path
	};
	
	ui_interface_t *ui = calloc(1, sizeof(ui_interface_t));
	ui->elements = calloc(1, sizeof(ui_element_t) * MAX_UI_ELEMENTS);
	ui->dynamic_buffer = ui_buffer_init(INITIAL_STREAM_VERTEX_COUNT * sizeof(ui_vertex_t));
	// ui->dynamic_buffer = malloc(sizeof(ui_line_t) * SCRATCH_BUFFER);
	// todo this does not grow as of yet
	ui->buffer = ui_buffer_init(sizeof(ui_vertex_t) * INITIAL_VERTEX_COUNT);
	ui->active_font = 0; 

	ui->action = NULL;
	for (int i = 0; i < MAX_FONTS; ++i){
		ui->font[i].glyphs = NULL;
	}
	// todo text format for files
	ui->uid_source = 0;
	ui->active_elements = 0;
	ui->selected_element = MAX_UI_ELEMENTS; // never a valid element
	ui->active_group = 0;
	ui->active_container = 0;
	ui->current_align = 0;
	ui->num_verts = 0;
	ui->num_dynamic_verts = 0;
	ui->num_text = 0;
	ui->action_flags = 0;
	ui->action = NULL;
	ui->theme.line_width = 2;
	ui->theme.text_col = RGBA(255, 255, 255, 255);
	ui->theme.padding_size = 5;
	ui->theme.line_col = RGBA(15, 15, 15, 255);
	ui->theme.button_hovered_col = RGBA(155, 35, 10, 255);
	ui->theme.group_header_col = RGBA(55, 35, 10, 255);
	ui->theme.group_background_col = RGBA(35, 35, 35, 255);
	ui->theme.tab_header_col = RGBA(15, 15, 15, 155);
	ui->theme.tab_selected_col = RGBA(185, 105, 0, 255);
	ui->theme.tab_deselected_col = RGBA(95,55,0,255);
	ui->theme.button_border_size = 1;
	ui->theme.button_border_col = RGBA(205, 93, 0, 255);
	ui->theme.button_col = RGBA(15, 15, 15, 155);
	ui->theme.menu_background_col = RGBA(15, 15, 15, 155);
	ui->theme.menu_title_col = RGBA(15, 15, 15, 255);
	ui->theme.menu_border_col = RGBA(150, 15, 15, 155);
	ui->theme.menu_hovered_col = RGBA(205, 93, 0, 155);
	ui->theme.group_padding_left = 5;
	ui->theme.group_padding_top = 5;
	ui->theme.checkbox_selected_col = RGBA(225, 145, 0, 255);
	ui->theme.checkbox_deselected_col = RGBA(85, 5, 0, 255);
	ui->theme.border_size = 5;
	ui->theme.rect_col_top = RGBA(5, 5, 5, 255);
	ui->theme.rect_col_bot = RGBA(15, 15, 15, 155);
	ui->theme.text_col = RGBA(255, 255, 255, 225);
	ui->theme.content_col_top = RGBA(55, 0, 0, 255);
	ui->theme.content_col_bot = RGBA(33, 55, 65, 255);
	ui->theme.rect_border_col_top = RGBA(15, 15, 15, 155);
	ui->theme.rect_border_col_bot = RGBA(15, 15, 15, 155);
	ui->theme.wave_col_top = RGBA(255, 155, 45, 155);
	ui->theme.wave_col_bot = RGBA(55, 35, 10, 255);
	ui->theme.dial_col_1 = RGBA(255, 105, 0, 255);
	ui->theme.dial_col_2 = RGBA(85, 65, 40, 255);
	ui->theme.dial_line_thickness = 2;
	ui->theme.chart_col_1 = RGBA(255, 23, 0, 255);
	ui->theme.chart_col_2 = RGBA(55, 35, 10, 255);
	ui->theme.slider_col_1 = RGBA(205, 93, 10, 155);
	ui->theme.slider_col_2 = RGBA(55, 0, 0, 255);
	ui->theme.slider_border_size = 1;
	ui->theme.background_col = RGBA(15, 15, 15, 155);
	ui->theme.border_col = RGBA(205, 93, 0, 255);
	ui->theme.matrix_background_col = RGBA(35,35,35,255);
	ui->theme.matrix_button_col = RGBA(205, 93, 0, 100);
	ui->theme.matrix_button_col2 = RGBA(205, 93, 0, 255);
	ui->theme.matrix_hex_padding = 2;
	ui->theme.matrix_square_padding = 2;
	ui->theme.matrix_circle_padding = 4;
	ui->theme.segment_display_background_col = RGBA(15, 15, 15, 155);
	ui->theme.segment_display_off_col = RGBA(55, 35, 10, 255);
	ui->theme.segment_display_on_col = RGBA(255, 105, 0, 255);
	ui->theme.parameter_line_width = 1;
	ui->theme.parameter_pad_size = 2;
	ui->theme.default_rect_col = RGBA(25, 20, 20, 255);
	ui->theme.node_head_col = RGBA(20, 20, 20, 255);
	ui->theme.node_content_col = RGBA(70, 70, 70, 255);
	ui->theme.node_in_connect_col = RGBA(205, 93, 0, 155);
	ui->theme.node_out_connect_col = RGBA(205, 93, 0, 155);
	ui->theme.node_link_col = RGBA(225, 145, 0, 255);
	ui->theme.menubar_col = RGBA(15, 15, 15, 255);
	ui->theme.menubar_padding = 8;
	ui->theme.list_box_border_size = 1;
	ui->theme.list_box_background_col = RGBA(15, 15, 15, 255);
	ui->theme.list_box_border_col = RGBA(205, 93, 0, 255);

	for (int i = 0; i < MAX_UI_ELEMENTS; ++i){
		// todo perhaps not really needed
		ui->elements[i].index = NULL;
		ui->elements[i].array_index = MAX_UI_ELEMENTS;
		ui->elements[i].action = NULL;
		ui->elements[i].content_region[0] = -1;
		ui->elements[i].content_region[1] = -1;
		ui->elements[i].content_region[2] = -1;
		ui->elements[i].content_region[3] = -1;
		ui->elements[i].parent = -1;
	}

	// todo all opengl stuff should be sperated out
	static const GLchar *vert_src =
        "#version 330 core\n"
        "layout (location = 0) in vec2 pos;\n"
        "layout (location = 1) in vec2 texcoord;\n"
        "layout (location = 2) in vec4 color;\n"
        "uniform mat4 project;\n"
        "out vec4 Frag_Color;\n"
        "out vec4 position;\n"
        "out vec2 tcoord;\n"
        "void main(){\n"
        "Frag_Color = color;\n"
        "tcoord = texcoord.st;\n"
        "gl_Position = project * vec4(pos.xy, 0.0, 1.0);\n}\n";

    static const GLchar *frag_src =
        "#version 330 core\n"
        "out vec4 Out_Color;\n"
        "in vec4 Frag_Color;\n"
        "in vec4 position;\n"
        "in vec2 tcoord;\n"
        "uniform sampler2D texture_id;\n"
        //"uniform vec3 textColor;"
        "void main(){\n"
        "vec4 sampled = vec4(1.0, 1.0, 1.0, texture(texture_id, tcoord).r);"
        "Out_Color = Frag_Color * sampled;\n"
    	"}\n\0";

	ui->shader = ui_load_shaders(vert_src, frag_src);
	if (!ui->shader){
		log("ERROR:SHADER:LOADING_FAILED");
	}
	// Get a texture id for fonts
	glGenTextures(1, &ui->texture_id);
	// Get text id for user uploaded textures
	glGenTextures(1, &ui->user_texture_id);

	ui->uniform = glGetUniformLocation(ui->shader, "project");
	glGenVertexArrays(2, &ui->vao[0]);
    glGenBuffers(2, &ui->vbo[0]);
	ui_init_glbuffer(ui);
    
    // ui_load_bitmap_font("./fonts/kenvector-future-14.fnt", 0);
	// ui_load_font(ui, "./fonts/Solway-Bold.ttf", 18);
	// todo user specified fonts/paths
	// ui_load_font(ui, "./fonts/kenvector_future.ttf", 18);
	// ui_load_font(ui, "./fonts/kenvector_future.ttf", 14);
	// ui_load_font(ui, "./fonts/BeVietnamPro-Regular.ttf", 12);
	// ui_load_font(ui, "./fonts/icons.ttf", 32);
	//exit(EXIT_SUCCESS);
    return ui;
}

void ui_reset_interaction(ui_interface_t *ui){
	if (!ui) return;
	ui->selected_element = MAX_UI_ELEMENTS;
	ui->action = NULL;
	ui->action_flags = 0;
}

int ui_load_shaders(const char *vert_src, const char *frag_src){

    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vert_src, NULL);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        fprintf(stderr, "ERROR:VERTEX:COMPILATION_FAILED - %s\n", infoLog);
    }
    // fragment shader
    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &frag_src, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        fprintf(stderr, "ERROR:FRAGMENT:COMPILATION_FAILED - %s\n", infoLog);
    }
    // link shaders
    int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    // check for linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "ERROR:SHADER:LINKING_FAILED - %s\n", infoLog);
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

void ui_refresh_shaders(ui_interface_t *ui){

	// removed as this really should not be part of the library!
	// or should have the shader specified by the library user

	// log("INFO: Reloading Shaders from file");
	// glDeleteProgram(ui->shader);
 //    ui->shader = load_shaders_from_file("./shaders/ui_vert.glsl", "./shaders/ui_frag.glsl");
 //    ui->uniform = glGetUniformLocation(ui->shader, "project");
}

void ui_render(ui_interface_t *ui){

    if (ui == NULL) return;
    ui_actions(ui);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ui->texture_id);

    GLfloat ortho[4][4] = {
        {2.0f, 0.0f, 0.0f, 0.0f},
        {0.0f,-2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f,-1.0f, 0.0f},
        {-1.0f,1.0f, 0.0f, 1.0f},
    };

    ortho[0][0] /= display.width;
    ortho[1][1] /= display.height;
    // static int counter = 0;
    if (ui->modified){
    	// printf("dirty buffer %d\n", counter++);
        ui_update_buffer(ui);
        ui->modified = 0;
    }
    ui_update_dynamic_buffer(ui);
    //glViewport(0, 0, display.width, display.height);glUniform4f
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(ui->shader);

    glUniformMatrix4fv(ui->uniform, 1, GL_FALSE, &ortho[0][0]);

    // Draw the UI elements
    glBindVertexArray(ui->vao[0]);
    glDrawArrays(GL_TRIANGLES, 0,  ui->num_verts + ui->num_text);
    // Draw the Dynamic elements
    glBindVertexArray(ui->vao[1]);
    glDrawArrays(GL_TRIANGLES, 0, ui->num_dynamic_verts);

    // Cleanup
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    // reset the per frame vertex buffer
    ui_buffer_reset(&ui->dynamic_buffer);
    ui->num_dynamic_verts = 0;
}

void ui_close(ui_interface_t *ui){

	if (ui){ 
		log("INFO: UI is shuting down\n");
		for (int i = 0; i < MAX_FONTS; ++i){
			if (ui->font[i].glyphs != NULL){
				free(ui->font[i].glyphs);
			}
		}
		if (ui->buffer != NULL){
			ui_buffer_free(ui->buffer);
			ui->buffer = NULL;
		}
		if (ui->dynamic_buffer != NULL){
			ui_buffer_free(ui->dynamic_buffer);
			ui->dynamic_buffer = NULL;
		}
		for (int i = 0; i < ui->active_elements; ++i){
			ui_free_string(ui, &ui->elements[i]);
			if (ui->elements[i].index){
				ui_buffer_free(ui->elements[i].index);
				ui->elements[i].index = NULL;
			}
		}
		if (ui->elements != NULL){
			free(ui->elements);
			ui->elements = NULL;
		}
		glDeleteBuffers(2, &ui->vbo[0]);
		// is this vbo 1 used todo
		//glDeleteBuffers(1, &ui->vbo[1]);
		glDeleteVertexArrays(2, &ui->vao[0]);
		glDeleteProgram(ui->shader);
		glDeleteTextures(1, &ui->texture_id);
		glDeleteTextures(1, &ui->user_texture_id);
		free(ui);
		ui = NULL;
	}
}

static void ui_init_glbuffer(ui_interface_t *ui){

	// setup the static buffer
    glBindVertexArray(ui->vao[0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ui->texture_id);

    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[0]);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ui_vertex_t), NULL);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ui_vertex_t), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ui_vertex_t), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0); 
    glBindVertexArray(0);

    // setup the stream buffer
    glBindVertexArray(ui->vao[1]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ui->user_texture_id);

    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[1]);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ui_vertex_t), NULL);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ui_vertex_t), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ui_vertex_t), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0); 
    glBindVertexArray(0);
}

void ui_update_buffer(ui_interface_t *ui){

	ui_vertex_t *buffer = ui->buffer;
	if (ui->modified & (GEOM_MODIFIED | GEOM_SIZE_MODIFIED)){
		ui->num_verts = 0;
		for (int i = 0; i < ui->active_elements; ++i){
			ui_element_t *e = &ui->elements[i];
			if (!(e->flags & UI_HIDDEN)){
				if (e->index){	// ensure there are associated vetexes before calling memcpy
					memcpy(buffer, e->index, sizeof(ui_vertex_t) * e->num_verts);
				}
				buffer += e->num_verts;
				ui->num_verts += e->num_verts;
			}
		}
	}

#ifdef DEBUG_CONTENT_REGION
	for (int i = 0; i < ui->active_elements; ++i){
		if ((ui->elements[i].flags & UI_HIDDEN) && (ui->elements[i].type != UI_LIST_BOX_CONTENT)){
			continue;
		}
		int32_t *bounds = ui->elements[i].content_region;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;

		ui->num_verts += 
			ui_rectangle_create(&ui->buffer[ui->num_verts], bounds[0], bounds[1],
				bounds[2] - bounds[0], bounds[3] - bounds[1], RGBA(255, 0, 0, 55));
	}
#endif	
#ifdef DEBUG_BOUNDS
	for (int i = 0; i < ui->active_elements; ++i){
		if ((ui->elements[i].flags & UI_HIDDEN) && (ui->elements[i].type != UI_LIST_BOX_CONTENT)){
			continue;
		}
		int32_t *bounds = ui->elements[i].bounds;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		buffer->u = GEOM_UV;	(buffer++)->v = GEOM_UV;
		ui->num_verts += 
			ui_rectangle_create(ui->buffer + ui->num_verts, bounds[0], bounds[1],
				bounds[2] - bounds[0], bounds[3] - bounds[1], RGBA(255, 255, 255, 55));
	}
#endif	
	// todo we should keep each elements text verts with its geometry verts or at least within
	// another member of its structure, thais code is like this from before we unified text and geom 
	// to be one draw call

	if (ui->modified & (GEOM_SIZE_MODIFIED | TEXT_MODIFIED)){
		ui->num_text = 0;
		uint32_t text_col = ui->theme.text_col;
		for (int i = 0; i < ui->active_elements; ++i){

			ui_element_t *e = &ui->elements[i];
			
			if (e->flags & UI_HIDDEN){
				continue;
			}
			switch (ui->elements[i].type){
				
				case UI_DIAL:
				{
					ui_dial_t *d = &ui->elements[i].data.d;
					ui_vertex_t *buf = ui->buffer + ui->num_verts + ui->num_text;
					char str[VALUE_STR_WIDTH];
					ui_format_value(str, d->start_val, d->end_val, d->step_size, d->value);
					float line_height = ui->font[e->font_id].line_height;
					float radius = d->size / 2.0f;
					float y = radius + 0.70712f * radius;
					ui->num_text +=
					ui_text_create(buf, str, &ui->font[e->font_id], e->x, e->y + y, d->size, line_height, text_col, UI_CENTERED_X);
					break;
				}
				case UI_SLIDER_V:
				case UI_SLIDER_H:
				{
					ui_slider_t *s = &ui->elements[i].data.s;
					ui_vertex_t *buf = ui->buffer + ui->num_verts + ui->num_text;
					char str[VALUE_STR_WIDTH];
					int ty = e->bounds[3] - ui->font[e->font_id].line_height;
					int width = e->bounds[2] - e->bounds[0];
					ui_format_value(str, s->start_val, s->end_val, s->step_size, s->value);
					ui->num_text +=
					ui_text_create(buf, str, &ui->font[e->font_id], e->bounds[0], ty, width, ui->font[e->font_id].line_height, text_col, UI_CENTERED_X);
					break;
				}
				case UI_PARAMETER:
				{
					ui_parameter_t *p = &ui->elements[i].data.p;
					ui_vertex_t *buf = ui->buffer + ui->num_verts + ui->num_text;
					char str[VALUE_STR_WIDTH];
					// int x, h;
					// ui_string_size_pixels(p->title, 1, &x, &h);
					// x += e->x + 2 * ui->theme.padding_size;
					int height = e->content_region[3] - e->content_region[1];
					float text_width = p->w - 2 * height - 2 * ui->theme.border_size;

					ui_format_value(str, p->start_val, p->end_val, p->step_size, p->value);
					ui->num_text +=
					ui_text_create(buf, str, &ui->font[e->font_id], e->x, e->y, text_width, height, text_col, UI_RIGHT | UI_CENTERED_Y);
					break;
				}
				case UI_PARAMETER_STR:
				{
					ui_parameter_str_t *p = &ui->elements[i].data.ps;
					ui_vertex_t *buf = ui->buffer + ui->num_verts + ui->num_text;
					int pad = ui->theme.padding_size;
					int border_size = ui->theme.line_width;
					int height = ui->font[e->font_id].line_height + border_size * 2;
					//int x, y;
					//ui_string_size_pixels(p->items[p->selected], e->font_id, &x, &h);
					int text_width = p->width;
					int sx = e->x + pad;
					ui->num_text +=
					ui_text_create(buf, p->items[p->selected], &ui->font[e->font_id], sx, e->y, text_width, height, text_col, UI_CENTERED_Y);
					break;
				}
				default:
					break;
			}
		}
	}

	assert(ui->num_verts + ui->num_text < INITIAL_VERTEX_COUNT);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ui_vertex_t) * (ui->num_verts + ui->num_text), ui->buffer, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); 
}

static void ui_update_dynamic_buffer(ui_interface_t *ui){

    // ui->num_dynamic_verts = 0;
	// return;
    // glBindVertexArray(ui->vao[1]);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ui_vertex_t) * (ui->num_dynamic_verts), ui->dynamic_buffer, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); 
    // printf("count : %d\n", ui->num_dynamic_verts);
}


// ######## Buffer ########
// Buffers have a hidden header stored prior to their allocated data

// Allocates or resizes a buffer
// Pass the bytes wanted, the buffer already knows how big its current size is
static void ui_buffer_reserve(void *buffer, uint32_t bytes){

	// todo this void** stuff is undefined
	ui_buffer_t *buf = ui_buffer_get_header(*(void **)buffer);

	if (buf->next + bytes >= buf->size){

		ui_buffer_t *result = NULL;
		size_t new_size = buf->next + bytes + sizeof(ui_buffer_t);
		result = realloc(buf, new_size);
		if (result){
			buf = result;
			buf->size = new_size - sizeof(ui_buffer_t);
			*(void **)buffer = buf->data;
		}
		else {
			printf("ERROR: Buffer allocation failure\n");
			exit(EXIT_FAILURE);
		}
	}

	buf->next += bytes;
}

static void ui_buffer_reset(void *buffer){

	ui_buffer_t *buf = ui_buffer_get_header(*(void **)buffer);
	buf->next = 0;
}
// todo these buffer funcs should take an element size parameter
// and track the no of contained elements

static void *ui_buffer_init(uint32_t size){

	ui_buffer_t *buffer = malloc(size + sizeof(ui_buffer_t));

	if (buffer == NULL){
		log ("ERROR: Allocation failed in ui_buffer_init\n");
		exit (EXIT_FAILURE);
	}
	buffer->size = size;
	buffer->next = 0;

	return buffer->data;
}

static void ui_buffer_free(void *buffer){

	if(!buffer){
		return;
	}
	ui_buffer_t *buffer_head = ui_buffer_get_header(buffer);
	free(buffer_head);
}	

// Gets the true address of an allocation
static inline ui_buffer_t *ui_buffer_get_header(void *buffer){

	ui_buffer_t *buffer_head = (ui_buffer_t *)((uint8_t *)buffer - sizeof(ui_buffer_t));

	return buffer_head;
}

static void ui_buffer_print(void *ptr){

}
// ######## Functions for string manipulation ########

static inline uint32_t ui_string_vert_count(char *str){

	uint32_t count = 0;
	if (str == NULL){
		return 0;
	}

	while (*str++){
		count++;
	}
	return count * RECTANGLE_VERTS;
}

static void ui_string_size_pixels(const char *str, font_t *f, int *width, int *height){

	if(str == NULL){
		*width = 0;
		*height = 0;
		return;
	}
	glyph_t *g = f->glyphs;

	int length_in_pixels = g[(int)(*str)].xadvance - g[(int)(*str)].xoffset;
	int descenders = 0;
	
	while (*++str){	
		// todo: We probably don't want this	
		length_in_pixels += g[(int)(*str)].xadvance;
		if (*str == 'f' || *str == 'g' || *str == 'y' || *str == 'p' || *str == 'j'){
			descenders = 1;
		}
	}
	--str;
	// the last char
	g += *str;
	length_in_pixels += (g->width + g->xoffset - g->xadvance);

	*width = length_in_pixels;
	// todo: We probably don't want this
	// always, sometimes better to have consistent line height
	// which does not depend on text.
	if (!descenders){
		*height = f->line_height;
	}
	else {
		*height = f->line_height + f->descent;
	}
	// *height = f->line_height + f->descent;
	*height = f->line_height;
}

// str length without NULL byte!
static inline int ui_strlen(char *str){

	int len = 0;
	while (*str++) len++; 
	return len;
}

// pads a string with trailing spaces to be a desired length
static void ui_strpad(char *dst, char *str, int desired_length){

	int len = ui_strlen(str);
	while (len--){
		*dst++ = *str++;
		--desired_length;
	}
	while (desired_length--){
		*dst++ = ' ';
	}
	*dst = '\0';
}
// finds the null byte in a cstring and appends str
static void ui_strcat(char *dest, char *src, int dest_len){

	while (*dest++){ 
		--dest_len;
	}
	--dest;
	while (*src && --dest_len){
		*dest++ = *src++;
	}
	*dest = '\0';
}

static char *ui_strdup(char *str){
	
	if (str == NULL){
		return NULL;
	}
	int len = ui_strlen(str);
	char *dup = malloc(len + 1);
	char *ret = dup;

	while (*str){
		*dup++ = *str++;
	}
	*dup = '\0';

	return ret;
}

static int ui_str_has_descenders(char *str){

	while (*str++){
		if (*str == 'f' || *str == 'g' || *str == 'y' || *str == 'p' || *str == 'j'){
			return 1;
		}
	}
	return 0;
}

static int ui_str_has_ascenders(char *str){

	while (*str++){
		if (*str == 'b' || *str == 'd' || *str == 't' || *str == 'h' || *str == 'k' || *str == 'l' || *str == 't'){
			return 1;
		}
		// numbers and capitals
		else if (*str < 97 && *str > 47){
			return 1;
		}
	}
	return 0;
}
// frac_digits is the number of digits come after the decimal point
// value is the string of digits to be written
static void ui_val_to_str(int value, char *buf, uint16_t frac_digits){

    char tmp[32];
    char *p = tmp;
    int i = 0;
    unsigned value_abs;
	int sign = (value < 0);

	if (value == 0){
		*buf++ = '0';
		if (frac_digits){
			*buf++ = '.';
			while(frac_digits--){
				*buf++ = '0';
			}
		}
		*buf = '\0';
		return;
	}
	// get the absolute value
    if (value < 0)
        value_abs = -value;
    else
        value_abs = (unsigned)value;

    // write the digits into the string reversed
    while (value_abs || p == tmp){
        i = value_abs % 10;	// extract last digit
        value_abs /= 10;	// remove last digit
        *p++ = i + '0';		// write the digit into the string in ascii
    }
    // get the number of digits extracted
    int len = p - tmp;

    if (sign){
        *buf++ = '-';
        len++;
    }

    // Get value of "1" in our modified number
    // 1^frac_digits
	int unity = 1;
	for (int i = 0; i < frac_digits; ++i){
		unity = unity * 10;
	}
	//log("unity = %d, %d", unity, frac_digits*10);
	// Are we dealing with a floating point number?
	if (frac_digits){
		value = abs(value);
		// is that number less than 0?
		if ((value < unity)){
				*buf++ = '0';
				*buf++ = '.';
			unity /= 10;
			// Are there more leading digits to be had?
			while(value < unity){
				*buf++ = '0';
				unity /= 10;
			}
			// copy the digits
			while (p > tmp){
				*buf++ = *--p;
			}
		}

		while (p > tmp){
			// if num digits extracted == wanted digits, not including the sign
			if (len - sign == frac_digits){
				if (value < unity){
					*buf++ = '0';
				}
				*buf++ = '.';
			}
			--len;
			*buf++ = *--p;
		}
	}
	// else just reverse the string
	else {
		while (p > tmp){
			*buf++ = *--p;
		}
	}

    *buf = '\0';
}

// formats a value such that it is multiple of the step size and has a fixed width
// determined from the step size. This assume that the step size has a greater
// fractional component than the start and end values!!
static void ui_format_value(char *str, float start_val, float end_val, float step, float val){
	
	// todo verify this works in a variety of cases
	//float percentage = ((val - start_val))/(end_val - start_val);
	//int sign = (val < 0) ? -1 : 1;

	// Calculate the number of quantised steps from the start value
	int32_t num_steps = ROUND((val - start_val) / step);
	// Get the version of val quantised to the step value
	float disp_val = start_val + num_steps * step;
	//log("display = %f", disp_val);

	// calculate the number of digits after the floating point
	// required for the precision of the inputs
	int digits = 0;

	// remove sign bit
	float st = fabs(step);
	// get the fractional component
	st = st - (int)(st);
	float int_part;

	while (st > 0.00001){
		st = st * 10.0f;
		++digits;
		// float modff(float x, float *iptr)
		st = modff(st, &int_part);//st - (int)(st);
		st -= int_part;
		assert(digits < 4);
	}
	// premultiply the value until all required digits are before the decimal point
	// 5.5 with four digits require becomes 5500
	for (int i = 0; i < digits; ++i){
		disp_val *= 10;
	}
	//log("final display = %f, digits = %d", disp_val, digits);
	ui_val_to_str(disp_val, str, digits);
	//log("final display = %f, digits = %d, str=_%s_\n", disp_val, digits, str);
}

// ######## Functions related to file ops and i/o #######
static void ui_serialise_interface(ui_interface_t *ui, ui_buffer_t *buffer){

	ui_pack_string(buffer, file_version);
	ui_pack_uint32(buffer, ui->active_elements);

	// todo use reseve buffer function to check buffer size
	for (int i = 0; i < ui->active_elements; ++i){
		ui_serialise_element(&ui->elements[i], buffer);
	}
}

void ui_write_file(ui_interface_t *ui, char *path){

	ui_buffer_t *buffer = ui_buffer_get_header(ui_buffer_init(FILE_BUF_SIZE));
	
	ui_serialise_interface(ui, buffer);

	FILE *f = fopen(path, "w");
	fwrite(buffer->data, buffer->next, 1, f);

	fclose(f);
	free(buffer);
}

void *ui_read_file(char *path){

	ui_interface_t *ui = NULL; 
	ui_buffer_t *buffer = ui_buffer_get_header(ui_buffer_init(FILE_BUF_SIZE));

	char *v = NULL;
	FILE *f = fopen(path, "r");	
	buffer->size = fread(buffer->data, FILE_BUF_SIZE, 1, f);

	if (buffer->size == FILE_BUF_SIZE){
		log("ERROR: File too large, try increasing BUFSIZE\n");
		goto cleanup;
	}
	buffer->data[7] = '\0';
	v = ui_unpack_string(buffer);

	for (int i = 0; i < 8; ++i){
		if (v[i] != file_version[i]){
			log("ERROR: File not supported \"%s\"", v);
			goto cleanup;
		}
	}
	uint32_t element_count;
	ui_unpack_uint32(buffer, &element_count);
	if (element_count >= MAX_UI_ELEMENTS){
		log("ERROR: Too many elements in file, increase MAX_UI_ELEMENTS\n");
		goto cleanup;
	}

	ui = ui_init(NULL); 
	
	//////uint32_t type = 0;
	// todo not depend on 4096
	//uint8_t data[4096];

	for (int i = 0; i < element_count; ++i){
		////type = ui_unpack_element(buffer, data);
		// todo fix all this to work with unions
		//ui_element_init(ui, data, 0, type);
	}
	cleanup:
	free(buffer);
	free(v);
	fclose(f);
	return ui;
}

static uint32_t ui_unpack_element(ui_buffer_t *buffer, void *data){

	uint32_t element_type;
	ui_unpack_uint32(buffer, &element_type);

	// todo flags?
	switch (element_type){
		case UI_ROUND_RECT:
			break;
		case UI_LINESTRIP:
			break;
		case UI_LINESTRIP_AA:
			break;
		case UI_DIAL:
			ui_deserialise_dial(buffer, data);
			break;
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
			ui_deserialise_button(buffer, data);
			break;
		case UI_CHECKBOX:
			ui_deserialise_button(buffer, data);
			break;
		case UI_SLIDER_V:
		case UI_SLIDER_H:
			ui_deserialise_slider(buffer, data);
			break;
		default:
			log("ERROR: No deserialise function for element type %d\n", element_type);
	}
	return element_type;
}

static void ui_serialise_element(ui_element_t *e, ui_buffer_t *buffer){
	// todo fibish these
	ui_pack_uint32(buffer, e->type);

	switch (e->type){
		case UI_ROUND_RECT:
			break;
		case UI_LINESTRIP:
			break;
		case UI_LINESTRIP_AA:
			break;
		case UI_DIAL:
			ui_serialise_dial(buffer, &e->data.d);
			break;
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
			ui_serialise_button(buffer, &e->data.b);
			break;
		case UI_CHECKBOX:
			ui_serialise_button(buffer, &e->data.b);
			break;
		case UI_SLIDER_V:
		case UI_SLIDER_H:
			ui_serialise_slider(buffer, &e->data.s);
			break;
		default:
			log("ERROR: No serialise function for element type %d\n", e->type);
	}
}

static void ui_serialise_slider(ui_buffer_t *buffer, ui_slider_t *s){

	ui_pack_float(buffer, s->length);
	ui_pack_float(buffer, s->width);
	ui_pack_float(buffer, s->value);
	ui_pack_float(buffer, s->start_val);
	ui_pack_float(buffer, s->end_val);
	ui_pack_float(buffer, s->step_size);
}

static void ui_deserialise_slider(ui_buffer_t *buffer, ui_slider_t *s){

	ui_unpack_float(buffer, &s->length);
	ui_unpack_float(buffer, &s->width);
	ui_unpack_float(buffer, &s->value);
	ui_unpack_float(buffer, &s->start_val);
	ui_unpack_float(buffer, &s->end_val);
	ui_unpack_float(buffer, &s->step_size);
}


static void ui_serialise_dial(ui_buffer_t *buffer, ui_dial_t *d){

	ui_pack_float(buffer, d->size);
	ui_pack_float(buffer, d->value);
	ui_pack_float(buffer, d->steps_per_pixel);
	ui_pack_float(buffer, d->start_val);
	ui_pack_float(buffer, d->end_val);
	ui_pack_float(buffer, d->step_size);
}

static void ui_deserialise_dial(ui_buffer_t *buffer, ui_dial_t *d){
	
	ui_unpack_float(buffer, &d->size);
	ui_unpack_float(buffer, &d->value);
	ui_unpack_float(buffer, &d->steps_per_pixel);
	ui_unpack_float(buffer, &d->start_val);
	ui_unpack_float(buffer, &d->end_val);
	ui_unpack_float(buffer, &d->step_size);
}


static void ui_serialise_button(ui_buffer_t *buffer, ui_button_t *b){

}

static void ui_deserialise_button(ui_buffer_t *buffer, ui_button_t *b){

}

static inline void ui_pack_float(ui_buffer_t *buffer, float f){

	uint8_t *b = &buffer->data[buffer->next];
	union {uint32_t as_int; float as_float;} u;
	u.as_float = f;
	*b++ = u.as_int;
	*b++ = u.as_int >> 8;
	*b++ = u.as_int >> 16;
	*b++ = u.as_int >> 24;
	buffer->next += 4;
}

static inline void ui_pack_uint32(ui_buffer_t *buffer, uint32_t u){

	uint8_t *b = &buffer->data[buffer->next];
	*b++ = u;
	*b++ = u >> 8;
	*b++ = u >> 16;
	*b++ = u >> 24;
	buffer->next += 4;
}

static inline void ui_pack_uint32_vector(ui_buffer_t *buffer, uint32_t *u, uint32_t count){

	uint8_t *b = &buffer->data[buffer->next];
	for (int i = 0; i < count; ++i){
		*b++ = u[i];
		*b++ = u[i] >> 8;
		*b++ = u[i] >> 16;
		*b++ = u[i] >> 24;
	}
	buffer->next += count * 4;
}

static inline void ui_pack_uint16(ui_buffer_t *buffer, uint16_t u){

	uint8_t *b = &buffer->data[buffer->next];
	*b++ = u;
	*b++ = u >> 8;
	buffer->next += 2;
}

static inline void ui_pack_string(ui_buffer_t *buffer, char *s){

	uint8_t *b = &buffer->data[buffer->next];
	uint32_t count = 0;

	while (s[count] != '\0'){
		b[count] = s[count];
		++count;
	}
	b[count++] = '\0';

	buffer->next += count;
}

static inline char *ui_unpack_string(ui_buffer_t *buffer){

	uint8_t *b = &buffer->data[buffer->next];
	uint32_t count = 0;
	while (b[count] != '\0'){
		++count;
	}
	char *str = malloc(count + 1);
	count = 0;

	while (b[count] != '\0'){
		str[count] = b[count];
		++count;
	}
	str[count++] = '\0';
	buffer->next += count;
	return str;
	// todo must free this how determine which side comes from
}

static inline void ui_unpack_uint32(ui_buffer_t *buffer, uint32_t *data){

	uint8_t *b = &buffer->data[buffer->next];
	*data = b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
	buffer->next += 4;
}

static inline void ui_unpack_uint32_vector(ui_buffer_t *buffer, uint32_t *data, uint32_t count){

	uint8_t *b = &buffer->data[buffer->next];
	for (int i = 0; i < count; ++i){
		data[i] = b[i * 4] | (b[i * 4 + 1]<<8) | (b[i * 4 + 2]<<16) | (b[i * 4 + 3]<<24);
	}
	buffer->next += count * 4;
}

static inline void ui_unpack_uint16(ui_buffer_t *buffer, uint16_t *data){

	uint8_t *b = &buffer->data[buffer->next];
	*data = b[0] | (b[1]<<8);
	buffer->next += 2;
}

static inline void ui_unpack_float(ui_buffer_t *buffer, float *data){

	uint8_t *b = &buffer->data[buffer->next];
	*((uint32_t*)data) = b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
	buffer->next += 4;
}

// todo include null checks before free, allow types to have empty or NULL strings
static void ui_free_string(ui_interface_t *ui, ui_element_t *e){

	switch (e->type){
		case UI_CHECKBOX:
			free(e->data.c.str);
			e->data.c.str = NULL;
			break;
		case UI_LABEL:
			free(e->data.l.str);
			e->data.l.str = NULL;
			break;
		case UI_DIAL:
		case UI_DIAL_HIDE_VAL:
			free(e->data.d.title);
			e->data.d.title = NULL;
			break;
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
		case UI_BUTTON_RETAIN_STATE:
		case UI_BUTTON_ROUND_RETAIN_STATE:
			free(e->data.b.str);
			e->data.b.str = NULL;
			break;
		case UI_MENU:
			free(e->data.m.title);
			for (int i = 0; i < e->data.m.no_of_items; ++i){
				free(e->data.m.items[i]);
				e->data.m.items[i] = NULL;
			}
			free(e->data.m.items);
			e->data.m.items = NULL;
			break;
		case UI_GROUP_HEADER:
			if (e->data.h.title){
				free(e->data.h.title);
				e->data.h.title = NULL;
			}
			break;
		case UI_PARAMETER:
			if (e->data.p.title){
				free(e->data.p.title);
				e->data.p.title = NULL;
			}
			break;
		case UI_TAB_HEADER:
			for (int i = 0; i < e->data.th.no_of_tabs; ++i){
				if (e->data.th.titles[i]){
					free(e->data.th.titles[i]);
					e->data.th.titles[i] = NULL;
				}
			}
			if (e->data.th.titles){
				free(e->data.th.titles);
				e->data.th.titles = NULL;
			}
			break;
		case UI_PARAMETER_STR:
			for (int i = 0; i < e->data.ps.no_of_items; ++i){
				free(e->data.ps.items[i]);
				e->data.ps.items[i] = NULL;
			}
			free(e->data.ps.items);
			e->data.ps.items = NULL;
			break;
		case UI_MATRIX:
			free(e->data.mat.states);
			e->data.mat.states = NULL;
			break;
		case UI_NODE:
			// todo free associated links
			for (int i = 0; i < e->data.node.inputs; ++i){
				free(e->data.node.input_labels[i]);
				e->data.node.input_labels[i] = NULL;
				if (e->data.node.in_link_ids[i] != -1){
					ui_delete_link(ui, ui_find_array_index(ui, e->data.node.in_link_ids[i]));
				}
				e->data.node.in_link_ids[i] = -1;
			}
			for (int i = 0; i < e->data.node.outputs; ++i){
				free(e->data.node.output_labels[i]);
				e->data.node.output_labels[i] = NULL;
				if (e->data.node.out_link_ids[i] != -1){
					ui_delete_link(ui, ui_find_array_index(ui, e->data.node.out_link_ids[i]));
				}
				e->data.node.out_link_ids[i] = -1;
			}
			free(e->data.node.title);
			free(e->data.node.input_labels);
			free(e->data.node.output_labels);
			free(e->data.node.in_link_ids);
			free(e->data.node.out_link_ids);
			break;
		case UI_PIE_MENU:
			free(e->data.pie.elements);
			break;
		case UI_MENU_ROOT:
			free(e->data.l.str);
			e->data.l.str = NULL;
			break;
		case UI_LIST_BOX:
			for (int i = 0; i < e->data.lb.count; ++i){
				free(e->data.lb.strings[i]);
				e->data.lb.strings[i] = NULL;
			}
			free(e->data.lb.strings);
			e->data.lb.strings = NULL;
			break;
		case UI_MENU_LABEL:
			free(e->data.l.str);
			e->data.l.str = NULL;
			break;

		// todo check nothing is malloced here and alter sliders as they should use malloc and store their recent text verts in index
		case UI_MENU_ROOT_RECT:
		case UI_LIST_BOX_CONTENT:
		case UI_NODE_LINK:
		case UI_SLIDER_V:
		case UI_SLIDER_V_HIDE_VAL:
		case UI_SLIDER_H: 
		case UI_SLIDER_H_HIDE_VAL:
		case UI_GROUP_END:
		case UI_TABS_END:
		case UI_CONTAINER_END:
		case UI_COLOUR_PICKER:
		case UI_SHAPE_BUTTON:
		case UI_ROUND_RECT:
		case UI_RECT:
		case UI_SEGMENT_DISPLAY:
			break;
		default:
			log("ERROR: Missing case in function ui_free_string type %d", e->type);
			break;
	}
}
//------------------------------------
// ######## User interaction ########
// -----------------------------------
// TODO:
// Implement each click type to append a function pointer ot an array/stack
// on receipt og that click event all registered functions are called with the click location.
// This allows simple handling of:
// 1. user has opened a menu, menu awaiting a close event.
// 2. user changes their mind and clicks on a dial elsewhere.
// We can now have the menu close easily and the dial activate easily in one frame.

void ui_actions(ui_interface_t *ui){

	int mx = MOUSE_X;
	int my = MOUSE_Y;
	int32_t *region;
	// check if an element is currently active
	if (ui->action){
		if (!ui->action(ui, ui->selected_element)){
			return;
		}
	}
	if (ui->selected_element != MAX_UI_ELEMENTS){
		if (ui_still_selected(ui, ui->selected_element)){
			// aim to remove this function
			ui_update_active_element(ui, ui->selected_element);
		}
		else {
			// element now going inactive
			ui_element_dectivate(ui, ui->selected_element);			
		}
	}
	else {
		for (int i = 0; i < ui->active_elements; ++i){
			
			if (ui->elements[i].flags & UI_HIDDEN){
				if (MOUSE_R && (ui->elements[i].type == UI_PIE_MENU)){
					// todo we need to have a right clickable property for all elements
					// so that menus can activate while elements are activated
					// only detecting clicks here when no other elements are beneath
					ui->selected_element = i;
					ui->action = ui_pie_menu_activate;
					return;
				}
				continue;
			}
			region = ui->elements[i].content_region;

			if (mx >= region[0] && mx <= region[2]){
				if (my >= region[1] && my <= region[3]){
					ui_element_activate(ui, i);					
					return;
				}
			}
		}
	}
}

static int ui_still_selected(ui_interface_t *ui, uint16_t id){
	
	if (MOUSE_M || MOUSE_L || MOUSE_R){
		return 1;
	}
	// Links are only active when they are being made
	else if (ui->elements[id].type == UI_NODE_LINK){
		// ui_node_link_t *l = &ui->elements[id].data.link;
		return 1;
	}
	else if(ui->elements[id].type == UI_PIE_MENU){
		return 1;
	}
	int32_t *region = ui->elements[id].content_region;
	if (MOUSE_X > region[0] && MOUSE_X < region[2]){
		if (MOUSE_Y > region[1] && MOUSE_Y < region[3]){
			return 1;
		}
	}
	return 0;
}

// todo could replace much or all of this function using a function pointer
static void ui_update_active_element(ui_interface_t *ui, uint16_t id){

	// todo not thread safe or multi ui safe!
	static int old_x = 0, old_y = 0;
	ui_element_t *e = &ui->elements[id];
	uint32_t flags = e->flags;
	int offset_x = 0, offset_y = 0;
	offset_y = MOUSE_Y - old_y;
	offset_x = MOUSE_X - old_x;

	// continuous events while mouse over
	switch (e->type){
		case UI_MENU:	// old deprecated menu type
			if (offset_x || offset_y)
				ui_menu_update(ui, id);
			break;
		case UI_NODE_LINK:
			if (offset_x || offset_y){
				ui_node_link_update(ui, id);
			}
			break;
		// case UI_PIE_MENU:
			// ui_pie_menu_animate(ui, id);
			// break;
		default:
			break;
	}
	// -------------------------------------------------------------------
	// Single instance button down events
	// -------------------------------------------------------------------
	if (MOUSE_L && !(ui->action_flags & LEFT_HELD)){

		switch (e->type){
			case UI_CHECKBOX:
				ui_checkbox_col_swap(ui, id);
				break;
			case UI_SLIDER_V:
			case UI_SLIDER_H:
			case UI_SLIDER_H_HIDE_VAL:
			case UI_SLIDER_V_HIDE_VAL:
				ui_set_slider_value(ui, id);
				break;
			case UI_DIAL:
				break;
			case UI_MENU:
				ui_menu_actions(ui, id);
				break;
			case UI_GROUP_HEADER:
				ui_group_header_toggle(ui, id);
				break;
			case UI_COLOUR_PICKER:
				ui_colour_picker_actions(ui, id);
				break;
			case UI_PARAMETER:
				ui_parameter_actions(ui, id);
				break;
			case UI_PARAMETER_STR:
				ui_parameter_str_actions(ui, id);
				break;
			case UI_TAB_HEADER:
				ui_tab_actions(ui, id);
				break;
			case UI_BUTTON_RETAIN_STATE:
			case UI_BUTTON_ROUND_RETAIN_STATE:
				ui_button_col_swap(ui, id, 0);
				break;
			case UI_MATRIX:
				ui_matrix_actions(ui, id);
				break;
			case UI_NODE:
				ui->selected_element = ui_node_left_click_actions(ui, id);
				break;
			case UI_NODE_LINK:
				ui_node_link_finalise(ui, id);
				break;
			case UI_MENU_ROOT:
				ui->action = ui_menu_open;
				break;
			case UI_LIST_BOX:
				ui->action = ui_list_box_open;
				break;
			// case UI_PIE_MENU:
			// 	ui_pie_menu_select(ui, id);
			// 	ui->selected_element = MAX_UI_ELEMENTS;
			// 	break;
			default:
				break;
		}
		// call user function to handle this event
		if (e->action != NULL && e->flags & UI_ACTION_ON_PRESS){
			e->action(e->uuid);
		}
		ui->action_flags |= LEFT_HELD;
	}
	if (MOUSE_M && !(ui->action_flags & MIDDLE_HELD)){
		ui->action_flags |= MIDDLE_HELD;
	}
	// -------------------------------------------------------------------
	// single instance mouse button released events
	// -------------------------------------------------------------------
	if (!MOUSE_L && (ui->action_flags & LEFT_HELD)){
		if (ui->elements[id].flags & UI_ACTION_ON_RELEASE){
			if (ui->elements[id].action != NULL){
				ui->elements[id].action(ui->elements[id].uuid);
			}
		}
		if (ui->elements[id].type == UI_NODE_LINK){
			ui_node_link_t *l = &ui->elements[id].data.link;
			// If both the input and output of the node have been connected
			// the link is complete and we release control
			if (l->in_node_id != -1 && l->out_node_id != -1){
				ui->selected_element = MAX_UI_ELEMENTS;
			}
			// lower value means we failed to make the limk
			if (l->in_node_id < -1){
				l->in_node_id += MAX_UI_ELEMENTS;
				// restore nodes original value before deleting
				ui_delete_link(ui, id);
				ui->selected_element = MAX_UI_ELEMENTS;
			}
		}
		// if (ui->elements[id].type == UI_MENU_ROOT){
		// 	ui->selected_element = MAX_UI_ELEMENTS;
		// }
		ui->action_flags &= !LEFT_HELD;
	}
	if (!MOUSE_M && (ui->action_flags & MIDDLE_HELD)){
		ui->action_flags &= !MIDDLE_HELD;
	}
	// -------------------------------------------------------------------
	// continuous button held events
	// -------------------------------------------------------------------
	if (MOUSE_M && (ui->action_flags & MIDDLE_HELD)){
		if (flags & UI_MOVABLE){
			ui_move_element(ui, id, offset_x, offset_y);
		}
	}
	if (MOUSE_L && (ui->action_flags & LEFT_HELD)){
		int32_t flags = ui->elements[id].flags;
		switch (ui->elements[id].type){
			case UI_DIAL:
			case UI_DIAL_HIDE_VAL:
				if (offset_y){
					if (ui->elements[id].action != NULL){
						if (flags & UI_ACTION_CONTINUOUS){
							e->action(ui->elements[id].uuid);
						}
					}
					ui_dial_set_value(ui, id, offset_y);
				}
				break;
			case UI_SLIDER_H:
			case UI_SLIDER_H_HIDE_VAL:
				if (offset_x){					
					if (e->action != NULL && flags & UI_ACTION_CONTINUOUS){
						e->action(e->uuid);
					}
					ui_set_slider_value(ui, id);
				}
				break;
			case UI_SLIDER_V:
			case UI_SLIDER_V_HIDE_VAL:
				if (offset_y){					
					if (e->action != NULL && flags & UI_ACTION_CONTINUOUS){
						e->action(e->uuid);
					}
					ui_set_slider_value(ui, id);
				}
				break;
			case UI_COLOUR_PICKER:
				if (offset_x || offset_y){					
					ui_colour_picker_update(ui, id, offset_y);
				}
				break;	
			case UI_PARAMETER:
				if (offset_x || offset_y){
					if (e->action != NULL && flags & UI_ACTION_ON_PRESS){
						e->action(e->uuid);
					}
					ui_parameter_set(ui, id, offset_y);
				}
				break;
			case UI_NODE:
				ui_node_move(ui, id, offset_x, offset_y);
				// todo need to move the links
				break;
			default:
				break;
		}
	}
	old_y = MOUSE_Y;
	old_x = MOUSE_X;
}

static void ui_element_dectivate(ui_interface_t *ui, uint16_t id){

	//char test[64];
	//ui_get_element_type_as_string(ui, id, test);
	//printf("deactivating %s\n", test);
	if (ui->elements[id].type == UI_BUTTON || 
			ui->elements[id].type == UI_BUTTON_ROUND){
		ui_button_col_swap(ui, id, 0);
	}
	else if (ui->elements[id].type == UI_MENU){
		ui_menu_unhighlight(ui, id);
	}
	else if (ui->elements[id].type == UI_MENU_LABEL){
		ui_menu_label_unhighlight(ui, id);
	}
	// handle case where mouse up occurs outside of object
	if (!MOUSE_L && (ui->action_flags & LEFT_HELD)){
		if (ui->elements[id].flags & UI_ACTION_ON_RELEASE){
			if (ui->elements[id].action != NULL){
				ui->elements[id].action(ui->elements[id].uuid);
			}
			
		}
	}
	ui->action_flags &= !LEFT_HELD;
	ui->action_flags &= !MIDDLE_HELD;
	ui->selected_element = MAX_UI_ELEMENTS;
}

static void ui_element_activate(ui_interface_t *ui, uint16_t id){

	ui->selected_element = id;
	ui_element_t *e = &ui->elements[id];
	//char test[64];
	//ui_get_element_type_as_string(ui, id, test);
	//printf("activating %s %d\n", test, ui->action_flags & LEFT_HELD);
	// single instance mouse over evemts
	switch (ui->elements[id].type){
		case UI_BUTTON:
		case UI_BUTTON_ROUND:
			if (ui->theme.button_hovered_col){
				ui_button_col_swap(ui, id, 1);
			}
			break;
		case UI_GROUP_HEADER:
			if ((MOUSE_Y - e->y) > ui->font[e->font_id].line_height){
				// not over title bar for group
				ui->selected_element = MAX_UI_ELEMENTS;
				return;
			}
			break;
		case UI_MENU_LABEL:
			ui_menu_label_highlight(ui, id);
			break;
		default:
			break;
	}
	ui_update_active_element(ui, id);
}
// ------------------------------------------
// ######## Fonts and Text Rendering ########
// ------------------------------------------
// void ui_load_bitmap_font(const char *str, uint16_t id){

// 	// todo alter this to load files created by load font func
// 	if (id > MAX_FONTS){
// 		log("ERROR: Invalid font ID, increase MAX_FONTS");
// 		return;
// 	}
// 	ui->font[id].glyphs = calloc(256, sizeof(glyph_t));

// 	FILE *fp;
// 	fp = fopen(str, "r");
	
// 	if (!fp){
// 		log("ERROR: File not found: %s", str);
// 		return;
// 	}

// 	char buf[256];

// 	fgets(buf, 256, fp);
// 	sscanf(buf, "info face=%*[^=]=%hu", &ui->font[id].size);

// 	fgets(buf, 256, fp);
// 	//sscanf(buf, "common lineHeight=%hu base=%hu scaleW=%hu", &font[id].line_height, &font[id].base_height, &font[id].texture_size);
// 	fgets(buf, 256, fp);
// 	char image_filename[64] = "./fonts/";
// 	sscanf(buf, "page id=0 file=\"%[^\"]", &image_filename[ui_strlen(image_filename)]);

// 	log("INFO: loading font - %s", image_filename);
// 	fgets(buf, 256, fp);
// 	sscanf(buf, "%*[^=]=%hu", &ui->font[id].num_glyphs);
	
// 	uint16_t code, x, y, w, h, xoff, yoff, adv; 
// 	glyph_t *g = ui->font[id].glyphs; 

// 	for (int i = 0; i < ui->font[id].num_glyphs; ++i){		
// 		fgets(buf, 256, fp);
// 		sscanf(buf, "%*[^=]=%hu %*[^=]=%hu %*[^=]=%hu %*[^=]=%hu %*[^=]=%hu %*[^=]=%hd %*[^=]=%hd %*[^=]=%hu", &code, &x, &y, &w, &h, &xoff, &yoff, &adv);
// 		g[code].x = x;
// 		g[code].y = y;
// 		g[code].width = w;
// 		g[code].height = h;
// 		g[code].xoffset = xoff;
// 		g[code].yoffset = yoff;
// 		g[code].xadvance = adv;
// 	}
// 	//load_texture(image_filename, &ui->texture_id, 0, GL_CLAMP_TO_EDGE);
// }

void ui_set_active_font(ui_interface_t *ui, uint16_t font_id){

	assert(font_id < MAX_FONTS);
	ui->active_font = font_id;
}

static int ui_next_pow2(int v) {
	int p = 1;
	while (p < v) p <<= 1;
	return p;
}

// Uses Sean Barretts STB library to Generate a buffer using a true type font
// https://github.com/nothings/stb/blob/master/stb_truetype.h
void ui_load_font(ui_interface_t *ui, const char *str, uint16_t size){

#ifdef TRUETYPE_SUPPORT
	FILE *fp;
	fp = fopen(str, "r");
	
	if (!fp){
		log("ERROR: File not found: %s", str);
		return;
	}
	int i, success = 0;
	for (i = 0; i < MAX_FONTS; ++i){
		if (ui->font[i].glyphs == NULL){
			ui->font[i].glyphs = calloc(256, sizeof(glyph_t));
			success = 1;
			break;
		}
	}
	if (!success){
		log("ERROR: No more free font slots, increase MAX_FONTS");
		return;
	}

	uint8_t *pixels;
	uint32_t buf_offset = 0;
	int width = 0;
	int height = 0;
	int current_rows = 0;
	float dpi = display.d_dpi > 0.0f ? display.d_dpi : 96.0f;
	float dpi_scale = dpi / 96.0f;
	int base_w = ui_next_pow2((int)ceilf((float)FONT_TEXTURE_W * dpi_scale));
	int base_h = ui_next_pow2((int)ceilf((float)FONT_TEXTURE_H * dpi_scale));
	if (base_w < FONT_TEXTURE_W) base_w = FONT_TEXTURE_W;
	if (base_h < FONT_TEXTURE_H) base_h = FONT_TEXTURE_H;
	int size_px = (int)ceilf((float)size * dpi_scale);
	if (size_px < 1) size_px = 1;
	GEOM_UV = 1.0f / (float)base_w;

	if (i > 0){
		// allocate a bigger buffer to accommodate the extra font
		pixels = malloc((i + 1) * base_w * base_h);
		int t_width, t_height;

		gpu_get_texture_size(ui->texture_id, &t_width, &t_height, 1);
		gpu_download_texture(ui->texture_id, pixels, GL_RED);
		gpu_delete_texture(ui->texture_id);

		uint64_t count = 0;
		int detected = 0;
		uint8_t *p = pixels + t_width * t_height - 1;

		// scan backwards to get first unused row
		while (detected == 0){
			detected += *(p--);
			count++;
		}

		buf_offset = t_height - count / base_w;
		++buf_offset;
		// buff_offset correspods to used rows + 1 here		
		current_rows = buf_offset;
		width = base_w;
		// edited dec 2022 changed max_fonts to i
		height = base_h * i - buf_offset;

		buf_offset *= base_w;

	}
	else {
		int waste = 6;
		width = base_w;
		height = base_h - waste;
		pixels = malloc(base_w * base_h);
		// wastefully add some lines for geometry, I think wee need 4 for msaa
		for ( int i = 0; i < base_w * waste; ++i){
			pixels[i] = 255;
		}
		buf_offset = waste * base_w;
		current_rows = waste;
	}

	// open the TTF
	fseek(fp, 0, SEEK_END);
	uint32_t file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *data = malloc(file_size);

	if (!fread(data, file_size, 1, fp)){
		log("ERROR: Failed to read TTF file %s", str);
		return;
	}
	fclose(fp);

	stbtt_bakedchar *chardata = malloc(256 * sizeof(stbtt_bakedchar));
	int rows_used = stbtt_BakeFontBitmap(data, 0, size_px, pixels + buf_offset, width, height, 32, 255 - 32, chardata);
	if (rows_used <= 0){
		// todo should handle this and add more space
		log("ERROR: Failed to load font not enough room in texture");
		return;
	}
	int texture_height = base_h + base_h * ((current_rows + rows_used) / base_h);

	glyph_t *glyphs = ui->font[i].glyphs + 32;
	stbtt_bakedchar *cd = chardata;

	for (int i = 32; i < 255; ++i){
		glyphs->x = cd->x0;
		glyphs->y = current_rows + cd->y0;
		glyphs->width = cd->x1 - cd->x0;
		glyphs->height = cd->y1 - cd->y0;
		glyphs->xoffset = cd->xoff;
		glyphs->yoffset = cd->yoff;
		glyphs->xadvance = cd->xadvance;
		//log("x: %d y: %d w: %d h: %d xoff: %d",
		//		glyphs->x, glyphs->y, glyphs->width, glyphs->height, glyphs->xoffset);
		++glyphs;
		++cd;
	}
	stbtt_fontinfo font_info;
	int ascent, descent, line_gap;
	stbtt_InitFont(&font_info, data, 0);
	float scale = stbtt_ScaleForPixelHeight(&font_info, size_px);
	stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
	int line_height = ROUND(scale * ((ascent - descent) + line_gap));

	ui->font[i].size = size_px;
	ui->font[i].line_gap = scale * line_gap;
	ui->font[i].ascent = ascent * scale;
	ui->font[i].descent = descent * scale;
	ui->font[i].line_height = line_height;
	ui->font[i].num_glyphs = 255 - 32;
	ui->font[i].base_height = line_height - scale * (line_gap - descent);
	ui->font[i].texture_width = base_w;
	for (; i >= 0; --i){
		ui->font[i].texture_height = texture_height;
	}
	//font[i].texture_size = FONT_TEXTURE_W;
	//log("line_height = %d scale = %f ascent = %d, base = %d", line_height, scale, ascent, font[0].base_height);
	#ifdef WRITE_TEXTURES
		char *tex_name = "out__.png";
		static int file_id = 0;
		tex_name[4] = '0' + file_id++;
		stbi_write_png("out.png", width, texture_height, 1, pixels, width);
	#endif
	load_texture_from_mem(pixels, width, texture_height, &(ui->texture_id), 0, GL_CLAMP_TO_EDGE, 1);
	free(data);
	free(pixels);
	free(chardata);
#endif
}

// Generate geometry to support character textures to render a string
// todo for buttons etc we need to account for ascenders and descenders
// str_has functions?
// consider moving to freetype_gl
static int ui_text_create(ui_vertex_t *v, const char *str, font_t *f, float x, float y, float w, float h, uint32_t col, uint16_t align){

	if (str == NULL) return 0;
	if (*str == '\0') return 0;

	glyph_t *g;
	int count = 0;
	int xoff, yoff;
	int adv = 0;

	float scale_u = 1.0f / (f->texture_width);
	float scale_v = 1.0f / (f->texture_height);

	float start_pos = x;
	int length_in_pixels;
	int line_height;

	ui_string_size_pixels(str, f, &length_in_pixels, &line_height);
	// const char *str_ptr = str;
	// int max_y = 0;
	// int min_y = 0;
	// while (*str_ptr++){
	// 	g = f->glyphs + (int)*str_ptr;
	// 	max_y = max_y > (g->height + g->yoffset) ? max_y : (g->height + g->yoffset);
	// 	min_y = min_y < g->yoffset ? min_y : g->yoffset;
	// }

	if (align & UI_CENTERED_X){	
		float box_center = x + w / 2.0f;
		start_pos = box_center - length_in_pixels / 2.0f;
	}
	if (align & UI_CENTERED_Y){
		float box_center_y = y + h / 2.0f;
		y = ROUND(box_center_y - f->line_height / 2.0f);
		// y = ROUND(box_center_y + (max_y - min_y) / 2.0f) - f->base_height;
		// v += ui_rectangle_create(v, x, y, 100, max_y - min_y, 0x55555555);
	}
	if (align & UI_LEFT){
		start_pos = x;
	}
	if (align & UI_RIGHT){
		start_pos = x + w - length_in_pixels;
	}

	// this greatly improves the display of small fonts
	start_pos = ROUND(start_pos);
	y = ROUND(y);

	y += f->base_height ;//+ f->line_gap;
	int gx, gy, gw, gh;

	while (*str){

		++count;
		g = f->glyphs + (int)*str;
		gx = g->x;
		gy = g->y;
		gw = g->width;
		gh = g->height;
		xoff = g->xoffset;
		yoff = g->yoffset;

		x = start_pos + adv;

		v->x = (x + xoff);
		v->y = (y + yoff);
		v->u = scale_u * gx;
		v->v = scale_v * gy;
		(v++)->colour = col;
		v->x = (x + xoff);
		v->y = (y + gh + yoff);
		v->u = scale_u * gx;
		v->v = scale_v * (gy + gh);
		(v++)->colour = col;
		v->x = (x + xoff + gw);
		v->y = (y + gh + yoff);
		v->u = scale_u * (gx + gw);
		v->v = scale_v * (gy + gh);
		(v++)->colour = col;
		v->x = (x + xoff);
		v->y = (y + yoff);
		v->u = scale_u * gx;
		v->v = scale_v * gy;
		(v++)->colour = col;
		v->x = (x + gw + xoff);
		v->y = (y + gh + yoff);
		v->u = scale_u * (gx + gw);
		v->v = scale_v * (gy + gh);
		(v++)->colour = col;
		v->x = (x + gw + xoff);
		v->y = (y + yoff);
		v->u = scale_u * (gx + gw);
		v->v = scale_v * gy;
		(v++)->colour = col;

		adv += g->xadvance;
		++str;
	}
	
	return count * 6;
}

void ui_get_element_type_as_string(ui_interface_t *ui, uint16_t id, char *type_as_str){

	type_as_str[0] = '\0';
	switch (ui->elements[id].type){
		case UI_ROUND_RECT:
			ui_strcat(type_as_str, "UI_ROUND_RECT", 64);
			break;
		case UI_RECT:
			ui_strcat(type_as_str, "UI_RECT", 64);
			break;
		case UI_LINESTRIP:
			ui_strcat(type_as_str, "UI_LINESTRIP", 64);
			break;
		case UI_LINESTRIP_AA:
			ui_strcat(type_as_str, "UI_LINESTRIP_AA", 64);
			break;
		case UI_PARAMETER:
			ui_strcat(type_as_str, "UI_PARAMETER", 64);
			break;
		case UI_PARAMETER_STR:
			ui_strcat(type_as_str, "UI_PARAMETER_STR", 64);
			break;
		case UI_DIAL:
			ui_strcat(type_as_str, "UI_DIAL", 64);
			break;
		case UI_DIAL_HIDE_VAL:
			ui_strcat(type_as_str, "UI_DIAL_HIDE_VAL", 64);
			break;
		case UI_BUTTON:
			ui_strcat(type_as_str, "UI_BUTTON", 64);
			break;
		case UI_BUTTON_RETAIN_STATE:
			ui_strcat(type_as_str, "UI_BUTTON_RETAIN_STATE", 64);
			break;
		case UI_BUTTON_ROUND:
			ui_strcat(type_as_str, "UI_BUTTON_ROUND", 64);
			break;
		case UI_BUTTON_ROUND_RETAIN_STATE:
			ui_strcat(type_as_str, "UI_BUTTON_ROUND_RETAIN_STATE", 64);
			break;
		case UI_CHECKBOX:
			ui_strcat(type_as_str, "UI_CHECKBOX", 64);
			break;
		case UI_SLIDER_V:
			ui_strcat(type_as_str, "UI_SLIDER_V", 64);
			break;
		case UI_SLIDER_V_HIDE_VAL:
			ui_strcat(type_as_str, "UI_SLIDER_V_HIDE_VAL", 64);
			break;
		case UI_SLIDER_H:
			ui_strcat(type_as_str, "UI_SLIDER_H", 64);
			break;
		case UI_SLIDER_H_HIDE_VAL:
			ui_strcat(type_as_str, "UI_SLIDER_H_HIDE_VAL", 64);
			break;
		case UI_LABEL:
			ui_strcat(type_as_str, "UI_LABEL", 64);
			break;
		case UI_MENU:
			ui_strcat(type_as_str, "UI_MENU", 64);
			break;
		case UI_GROUP_HEADER:
			ui_strcat(type_as_str, "UI_GROUP_HEADER", 64);
			break;
		case UI_TAB_HEADER:
			ui_strcat(type_as_str, "UI_TAB_HEADER", 64);
			break;
		case UI_COLOUR_PICKER:
			ui_strcat(type_as_str, "UI_COLOUR_PICKER", 64);
			break;
		case UI_GROUP_END:
			ui_strcat(type_as_str, "UI_GROUP_END", 64);
			break;
		case UI_TABS_END:
			ui_strcat(type_as_str, "UI_TABS_END", 64);
			break;
		case UI_CONTAINER_END:
			ui_strcat(type_as_str, "UI_CONTAINER_END", 64);
			break;
		case UI_USER:
			ui_strcat(type_as_str, "UI_USER", 64);
			break;
		case UI_MATRIX:
			ui_strcat(type_as_str, "UI_MATRIX", 64);
			break;
		case UI_SHAPE_BUTTON:
			ui_strcat(type_as_str, "UI_SHAPE_BUTTON", 64);
			break;
		case UI_SEGMENT_DISPLAY:
			ui_strcat(type_as_str, "UI_SEGMENT_DISPLAY", 64);
			break;
		case UI_NODE:
			ui_strcat(type_as_str, "UI_NODE", 64);
			break;
		case UI_NODE_LINK:
			ui_strcat(type_as_str, "UI_NODE_LINK", 64);
			break;
		case UI_PIE_MENU:
			ui_strcat(type_as_str, "UI_PIE_MENU", 64);
			break;
		case UI_MENU_ROOT:
			ui_strcat(type_as_str, "UI_MENU_ROOT", 64);
			break;
		case UI_MENU_LABEL:
			ui_strcat(type_as_str, "UI_MENU_LABEL", 64);
			break;
		case UI_MENU_ROOT_RECT:
			ui_strcat(type_as_str, "UI_MENU_ROOT_RECT", 64);
			break;
		case UI_LIST_BOX:
			ui_strcat(type_as_str, "UI_LIST_BOX", 64);
			break;
		case UI_LIST_BOX_CONTENT:
			ui_strcat(type_as_str, "UI_LIST_BOX_CONTENT", 64);
			break;
		default:
			log("ERROR: Unknown case type in ui_get_element_type_as_string");
	}
}

// #define TERM_NRM  "\x1B[0m"
// #define TERM_RED  "\x1B[31m"
// #define TERM_GRN  "\x1B[32m"
// #define TERM_YEL  "\x1B[33m"
// #define TERM_BLU  "\x1B[34m"
// #define TERM_MAG  "\x1B[35m"
// #define TERM_CYN  "\x1B[36m"
// #define TERM_WHT  "\x1B[37m"
void ui_log_elements(ui_interface_t *ui){

	char type_as_str[64];
	uint32_t count = 0;
	ui_element_t *e = ui->elements;
	char TERM_YEL[] = "\x1B[33m";
	char TERM_NRM[] = "\x1B[0m";
	char *colour_str = TERM_NRM;
	for (int i = 0; i < ui->active_elements; ++i){
		type_as_str[0] = '\0';
		ui_get_element_type_as_string(ui, i, type_as_str);
		count += e->num_verts;
		if (e->flags & UI_HIDDEN){
			colour_str = TERM_YEL;
		}
		else {
			colour_str = TERM_NRM;
		}
		printf("%s%2d:%2d%24s, Vertices = %4d, parent = %2d, tag = %2d bounds = %d %d\n", colour_str, i, e->uuid,
				type_as_str, e->num_verts, 
				e->parent, e->group_tag,
				e->bounds[2] - e->bounds[0],
				e->bounds[3] - e->bounds[1]);
		#ifdef DEBUG_CONTENT_REGION
		char TERM_GRN[] = "\x1B[32m";
		colour_str = TERM_GRN;
		printf("%sbounds = (x:%-4d y:%-4d) (x:%-4d y:%-4d) \tcontent = (x:%-4d y:%-4d) (x:%-4d y:%-4d)\n", colour_str,
				e->bounds[0], e->bounds[1],
				e->bounds[2], e->bounds[3],
				e->content_region[0], e->content_region[1],
				e->content_region[2], e->content_region[3]);
		#endif
		++e;
	}
	colour_str = TERM_NRM;
	printf("%sTotal Vertices = %d\n",colour_str, count);
}


static int ui_find_array_index(ui_interface_t *ui, uint32_t uuid){

	for (int i = 0; i < ui->active_elements; ++i){
		if (ui->elements[i].uuid == uuid){
			return i;
		}
	}
	return -1;
}

// open a directory for reading and return a list of directory entries containing its contents

















