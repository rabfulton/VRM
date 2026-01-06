#ifndef _UI_H_
#define _UI_H_
#include <bits/stdint-intn.h>
#include <bits/stdint-uintn.h>
#define MAX_UI_ELEMENTS 512
// used for waveform display must be large enough for 2 * displayed width in pixels
#define INITIAL_VERTEX_COUNT 400000
#define INITIAL_STREAM_VERTEX_COUNT 40000
#define MAX_FONTS 4
#define FONT_TEXTURE_W 1024	
#define FONT_TEXTURE_H 256
#define SEGMENTS 64
#define DIAL_SEGMENTS 48
#define VALUE_STR_WIDTH 32	// buffer size for digits as strings for dials and sliders
// todo add this optimisation to other line striip functions. ~10x reduction in vertices
#define COLINEAR_OPTIMISATION	// used in the line strip functions to reduce vertex counts

// use these defines to map functions or variables 
// to allow the UI code to query the mouse position
// todo better define this
// typedef struct mouse_t mouse_t;
// extern struct mouse_t mouse;
typedef struct {
    float *vertices;
    float *data;  //todo make each entity use single data buffer
    uint32_t *indices;
    uint32_t no_of_indices;
    uint32_t no_of_vertices;
    unsigned int vbo; 
    unsigned int vao;
    unsigned int ebo;
} gl_buffer_t;
extern gl_buffer_t gl_buffer_default;

typedef struct { 
    int width;
    int height;
    float h_dpi;
    float v_dpi;
    float d_dpi;
    float frame_rate;
} display_t;
// extern display_t display;

typedef struct {    // todo maybe part of controller type
    int32_t x;
    int32_t y;
    int32_t x_rel;
    int32_t y_rel;
    uint16_t button_l;
    uint16_t button_r;
    uint16_t button_m;
} mouse_t;

extern display_t display;
extern mouse_t mouse;

#ifdef UI_EXTERNS
#define MOUSE_X mouse.x
#define MOUSE_Y mouse.y
#define MOUSE_L mouse.button_l
#define MOUSE_R mouse.button_r
#define MOUSE_M mouse.button_m
#define UI_DISPLAY_WIDTH (display.width)
#define UI_DISPLAY_HEIGHT display.height
#define UI_FRAME_RATE display.frame_rate
#endif

#define UI_FLAG(x) (1 << (x))
#define log(...) printf(__VA_ARGS__); printf(" : %s : %d\n", __FILE__, __LINE__)
#define TRUETYPE_SUPPORT
// const char *labels[] = {UI_BUTTON = "add button", UI_CHECKBOX = "add checkbox", ...
typedef struct {
	// Could add a pallette array here and each entry 
	// could be an array index into the pallette
	// for say a 16c pallette you could store 8 colours in a 32bit int
	// enough for an element
	// could also make colours that make sense to be matched at every nth entry and only look up the first 16 entries
	// for a reduced pallette
	uint32_t border_size;
	uint32_t line_width;
	uint32_t dial_line_thickness;
	uint32_t padding_size;
	uint32_t text_col;
	uint32_t line_col;
	uint32_t group_header_col;
	uint32_t group_background_col;
	uint32_t tab_header_col;
	uint32_t tab_selected_col;
	uint32_t tab_deselected_col;
	uint32_t button_hovered_col;
	uint32_t button_border_size;
	uint32_t button_border_col;
	uint32_t button_col;
	uint32_t menu_border_col;
	uint32_t menu_background_col;
	uint32_t menu_title_col;
	uint32_t menu_hovered_col;
	uint32_t menu_selected_col;
	uint32_t group_padding_left;
	uint32_t group_padding_top;
	uint32_t checkbox_selected_col;
	uint32_t checkbox_deselected_col;
	uint32_t rect_border_col_top;
	uint32_t rect_border_col_bot;
	uint32_t rect_col_top;
	uint32_t rect_col_bot;
	uint32_t content_col_top;
	uint32_t content_col_bot;
	uint32_t wave_col_top;
	uint32_t wave_col_bot;
	uint32_t chart_col_1;
	uint32_t chart_col_2;
	uint32_t dial_col_1;
	uint32_t dial_col_2;
	uint32_t slider_col_1;
	uint32_t slider_col_2;
	uint32_t slider_border_size;
	uint32_t background_col;
	uint32_t border_col;
	uint32_t matrix_background_col;
	uint32_t matrix_button_col;
	uint32_t matrix_button_col2;
	uint32_t matrix_hex_padding;
	uint32_t matrix_square_padding;
	uint32_t matrix_circle_padding;
	uint32_t segment_display_background_col;
	uint32_t segment_display_off_col;
	uint32_t segment_display_on_col;
	uint32_t parameter_line_width;
	uint32_t parameter_pad_size;
	uint32_t default_rect_col;
	uint32_t node_head_col;
	uint32_t node_content_col;
	uint32_t node_in_connect_col;
	uint32_t node_out_connect_col;
	uint32_t node_link_col;
	uint32_t menubar_col;
	uint32_t menubar_padding;
	uint32_t list_box_border_size;
	uint32_t list_box_background_col;
	uint32_t list_box_border_col;
} ui_theme_t;

typedef struct {
	float x; 
	float y; 
	float u; 
	float v; 
	uint32_t colour;
} ui_vertex_t;

typedef struct {
	float x; 
	float y; 
	uint32_t colour;
} ui_point_t;

typedef struct {
	float x1; 
	float y1; 
	uint32_t colour1;
	float x2; 
	float y2; 
	uint32_t colour2;
} ui_line_t;

typedef struct {
	float size;
	float value;
	float start_val;
	float end_val;
	float step_size;
	float steps_per_pixel;
	char *title;
} ui_dial_t;

typedef struct {
	float length;	// Total length of the slider object including any numbers
	float width;	// If set to zero the width will be determined by the font size line height
	float value;	// This is the current number stored by this slider
	float start_val;// The lowest value the slider can have
	float end_val;	// The highest value the slider can have
	float step_size;// Increments of the value between start and end values
} ui_slider_t;

typedef struct {
	// Set w and height to 0 zero for auto bounds
	char *str;
	float w;
	float h;
	uint32_t colour; // zero = dont care, use theme
	uint16_t align;
} ui_label_t;
/*
typedef struct {
	char *str;
	uint32_t colour; // zero = dont care, use theme
	uint16_t font_idx;
} ui_text_t;
*/
typedef struct {
	char *str;
	int w;
	int h;
	uint8_t state;
} ui_button_t;

enum button_shape {CIRCLE, SQUARE, HEXAGON};
typedef struct {
	enum button_shape shape;
	int32_t col_off;	// Set these to zero to use theme colours
	int32_t col_on;		// todo make theme entries for these
	int dimension;		// diameter for a circle otherwise height 
	uint8_t state;		// 0 = off, 1 = on
} ui_shape_button_t;
// creates a grid of button shapes which can be used as
// indicators, buttons or both.
typedef struct {
	int rows;
	int columns;
	ui_shape_button_t button;
	uint8_t *states;			// set to null for zero init
} ui_matrix_t;

typedef struct {
	int w;
	int h;
	int num_segments;
} ui_segment_display_t;

typedef struct {
	char *str;
	uint8_t state;
} ui_checkbox_t;

typedef struct {
	// need a text attributes struct(optional for user)
	char *title;
	char **items;
	uint8_t no_of_items;
	uint8_t state;
} ui_menu_t;

// typedef struct {
// 	uint32_t x;
// 	uint32_t y;
// 	uint32_t w;
// 	uint32_t h;
// } ui_tool_bar_t;
// typedef struct {
// 	char **titles;
// 	uint32_t count;
// 	uint32_t type;
// } ui_menu_bar;

typedef struct {
	// need a text attributes struct(optional for user)
	int width;
	int selected;
	char **items;
	uint8_t no_of_items;
	uint8_t state;
} ui_parameter_str_t;

typedef struct {
	uint32_t size;
	uint32_t presets[8];
	uint32_t active_component;
	uint32_t active_preset;
	// loss of precision here
	uint8_t hue[8];
} ui_colour_picker_t;

// callback function returns an id of a link node
typedef struct {
	uint32_t inputs;
	uint32_t outputs;
	int32_t *in_link_ids;		
	int32_t *out_link_ids;		// element_ids for each active node_link
	char *title;
	char **input_labels;
	char **output_labels;
} ui_node_t;

enum link_state {NOT_CONNECTED, DISCONNECTING, CONNECTED};
typedef struct {
	enum link_state state;
	int32_t in_node_id;			// element_id for the node
	int32_t out_node_id;
	int32_t in_id;						// numerated input 0 based
	int32_t out_id;
} ui_node_link_t;

typedef struct {
	float length;
	float width;
	uint32_t style;
	uint32_t orientation;
} ui_meter_t;

typedef struct {
	float x;
	float y;
	float w;
	float h;
	float border_width;
	uint32_t colour;
} ui_rect_t;

typedef struct {
	int w;	// set to zero for automatic width
	int h;	// set to zero for autmatic height
	int state;
	char *title;
} ui_header_t;

typedef struct {
	uint32_t w;
	uint32_t h;
	uint32_t active_tab;
	char **titles;
	uint16_t no_of_tabs;
} ui_tab_header_t;

// Note that the digit format is determined from the start end and step values
// if step was 0.01, all values will have two digits after the decimal point
typedef struct {
	float w;
	float value;
	float start_val;
	float end_val;
	float step_size;
	float steps_per_pixel;
	char *title;
} ui_parameter_t;

// Callback is the element selected, zero indexed
//  query pie menus location to get the click point if required to position a new element
union element_data;
typedef union element_data element_data;
typedef struct {
	uint32_t type;
	uint32_t count;
	float radius;
	element_data *elements;
} ui_pie_menu_t;

typedef struct {
	uint32_t selected_item;
	uint32_t count;
	char **strings;
} ui_list_box_t;

union element_data {
	ui_header_t h;
	ui_button_t b;
	ui_checkbox_t c;
	ui_menu_t m;
	ui_label_t l;
	ui_slider_t s;
	ui_dial_t d;
	ui_rect_t r;
	ui_colour_picker_t cp;
	ui_parameter_t p;
	ui_parameter_str_t ps;
	ui_tab_header_t th;
	ui_shape_button_t sb;
	ui_matrix_t mat;
	ui_segment_display_t seg;
	ui_node_t node;
	ui_node_link_t link;
	ui_pie_menu_t pie;
	ui_meter_t meter;
	ui_list_box_t lb;
};

typedef struct {
	uint32_t type;							// Element type ie BUTTON_TEXTURED
	uint32_t array_index;				// Elements position in the element array
	uint32_t uuid;							// Unique indentifier
	uint32_t usage;							// Enum for static or dynamic drawing
	int32_t x;									// Position in pixels
	int32_t y;	
	uint32_t flags;
	uint32_t align;
	uint32_t frame;
	//uint32_t texture_id;
	uint32_t font_id;
	uint32_t num_verts;
	int32_t content_region[4];			// x, y, x2, y2
	int32_t bounds[4];							// x, y, x2, y2 
	int32_t group_tag;
	int32_t parent;
	union element_data data;
	ui_vertex_t *index;					// This elements vertices to draw 
	void (*action)(int id);			// Callback function to user
} ui_element_t;


static inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

struct ui_interface_t;
typedef struct ui_interface_t ui_interface_t; // this isn't necessary in C++
// UI Feature flags for elements
// use function ui_set_element_flags to override defaults
// or set additional capabilities
enum {      UI_MOVABLE = UI_FLAG(0), 
		     UI_NO_OVERLAP = UI_FLAG(1), 
		     UI_COLLIDABLE = UI_FLAG(2),
	           UI_HIDDEN = UI_FLAG(3), 
  UI_ACTION_ON_RELEASE = UI_FLAG(5),
    UI_ACTION_ON_PRESS = UI_FLAG(6),
   UI_CONTAINER_MEMBER = UI_FLAG(7),
  UI_ACTION_CONTINUOUS = UI_FLAG(8),		// set this flag when you want for example a dial to trigger a callback per frame while held.
};
// UI Containers and widget
// todo give enum a type
enum {	
	  UI_ROUND_RECT, 
		UI_RECT,
		UI_LINESTRIP, 
		UI_LINESTRIP_AA, 
		UI_PARAMETER, 
		UI_PARAMETER_STR,
		UI_SEGMENT_DISPLAY,
		UI_DIAL, 
		UI_DIAL_HIDE_VAL,
		UI_BUTTON, 
		UI_BUTTON_RETAIN_STATE,
		UI_BUTTON_ROUND,
		UI_BUTTON_ROUND_RETAIN_STATE,
		UI_SHAPE_BUTTON,
		UI_MATRIX,
		UI_CHECKBOX, 
		UI_SLIDER_V, 
		UI_SLIDER_V_HIDE_VAL,
		UI_SLIDER_H, 
		UI_SLIDER_H_HIDE_VAL,
		UI_LABEL, 
		UI_MENU, 
		UI_COLOUR_PICKER,
		UI_USER,		// uses UI_RECT for container
		UI_NODE,
		UI_NODE_LINK,
		UI_TYPE,
		UI_PIE_MENU,
		UI_LIST_BOX,
		UI_METER,
		// INTERNAL TYPES ONLY
		UI_MENU_ROOT, 					// Internal use only
		UI_MENU_ROOT_RECT, 					// Internal use only
		UI_MENU_LABEL,
		UI_GROUP_HEADER,
		UI_GROUP_NO_HEADER,
		UI_GROUP_END,	
		UI_CONTAINER_END,
		UI_TAB_HEADER,				// 20
		UI_TABS_END,
		UI_LIST_BOX_CONTENT,
};

// usage hint for add content to element functions
// Dynanic content persists only for one frame
enum {UI_STATIC_CONTENT, UI_DYNAMIC_CONTENT};

// Content types for ui_add_content_to_element
enum {UI_WAVEFORM_DISP, UI_WAVETABLE_DISP, UI_WAVEFORM_LINEAR, UI_IMAGE_DISP, UI_TEXT,
	  UI_POINT_CHART, UI_BAR_CHART, UI_LINE_CHART, UI_FILLED_CHART, UI_VERTICES, UI_SPECTROGRAM};
// Element and text alignments
enum{        UI_NOT_SET = UI_FLAG(1),
	        UI_CENTERED_X = UI_FLAG(2), 
	        UI_CENTERED_Y = UI_FLAG(3), 
	              UI_LEFT = UI_FLAG(4), 
	             UI_RIGHT = UI_FLAG(5),
	     	       UI_BELOW = UI_FLAG(6),
		           UI_ABOVE = UI_FLAG(7),
          UI_BELOW_LAST = UI_FLAG(8),
 UI_BELOW_LAST_CENTERED = UI_FLAG(9),
						// UI_AT_MOUSE = UI_FLAG(10),
};

void *ui_init(char *path);
void ui_render(ui_interface_t *ui);
void ui_reset_interaction(ui_interface_t *ui);


void ui_refresh_shaders(ui_interface_t *ui);
void ui_close(ui_interface_t *ui);
void ui_write_file(ui_interface_t *ui, char *path);
void *ui_read_file(char *path);

int32_t ui_element_init(ui_interface_t *ui, void *edata, uint16_t count, uint16_t type);

// pass the data type you want to use for the menu head. Currently UI_LABEL or UI_BUTTON
// After adding menu root object of supported type, add your menu items of any type.
// Further calls to menu start add a new menu header until a call is made to ui_menu_stop.
// You can close the menu by reacting to a callback from any of the contained objects id's.
// This allows for the menu to remain open to adjust sliders etc and to close on a button press.
int ui_menu_start(ui_interface_t* ui, void *data, uint32_t type);
void ui_menu_stop(ui_interface_t *ui);
void ui_menu_close(ui_interface_t *ui, uint32_t any_menu_item_uuid);


int32_t ui_group_start(ui_interface_t *ui, ui_header_t *h);
void ui_group_stop(ui_interface_t *ui);
uint32_t ui_start_tabs(ui_interface_t *ui, ui_tab_header_t *t);
void ui_next_tab(ui_interface_t *ui);
void ui_stop_tabs(ui_interface_t *ui);
void ui_start_container(ui_interface_t *ui, uint32_t uid);
void ui_stop_container(ui_interface_t *ui);
void ui_set_align(ui_interface_t *ui, uint32_t align);
void ui_set_align_to_element(ui_interface_t *ui, uint32_t align);
void ui_set_element_usage(ui_interface_t *ui, uint32_t uid, uint32_t usage);
void ui_add_content_to_element(ui_interface_t *ui, uint32_t uid, void* data, uint32_t count, uint16_t type);
void ui_set_element_flags(ui_interface_t *ui, uint32_t uid, uint16_t flags);
void ui_unset_element_flags(ui_interface_t *ui, uint32_t uid, uint16_t flags);
void ui_set_line_width(ui_interface_t *ui, float width);
void ui_set_callback(ui_interface_t *ui, uint32_t uid, void (*function)(int id));
// returns -1 on error
int32_t ui_get_element_id(ui_interface_t *ui, int32_t x, int32_t y);
// Moves an element and any elements which are part of the same grouping
// x and y are relative displacements
void ui_move_element(ui_interface_t *ui, uint32_t uid, int32_t x, int32_t y);
// Explicitly position an element, warning removes alignment flag.
void ui_position_element(ui_interface_t *ui, uint32_t uid, int32_t x, int32_t y);
// Returns the number of elements deleted, deleting a header will delete its children
int ui_element_delete(ui_interface_t *ui, uint32_t uid);
void ui_force_update(ui_interface_t *ui);
void ui_set_active_font(ui_interface_t *ui, uint16_t font_id);
#ifdef TRUETYPE_SUPPORT
void ui_load_font(ui_interface_t *ui, const char *str, uint16_t size);
#endif
void ui_log_elements(ui_interface_t *ui);

void ui_load_bitmap_font(const char *str, uint16_t id);
int ui_read_button(ui_interface_t *ui, uint32_t uid);
void ui_write_button(ui_interface_t *ui, uint32_t uid, int val);
float ui_read_dial(ui_interface_t *ui, uint32_t uid);
void ui_write_dial(ui_interface_t *ui, uint32_t uid, float value);
float ui_read_slider(ui_interface_t *ui, uint32_t uid);
void ui_write_slider(ui_interface_t *ui, uint32_t uid, float value);
float ui_read_parameter(ui_interface_t *ui, uint32_t uid);
void ui_write_parameter(ui_interface_t *ui, uint32_t uid, float value);
int ui_read_checkbox(ui_interface_t *ui, uint32_t uid);
void ui_write_checkbox(ui_interface_t *ui, uint32_t uid, int state);
int ui_read_menu(ui_interface_t *ui, uint32_t uid);
uint32_t ui_read_colour_picker(ui_interface_t *ui, uint32_t uid);
void ui_write_colour_picker(ui_interface_t *ui, uint32_t uid, uint32_t col);
union element_data ui_read_element(ui_interface_t *ui, uint32_t uid);
void ui_matrix_set_states(ui_interface_t *ui, uint32_t uid, uint8_t *data);
void ui_matrix_set_button(ui_interface_t *ui, uint32_t uid, int row, int column, int state);
void ui_matrix_read_state(ui_interface_t *ui, uint32_t uid, uint8_t *data);
void ui_matrix_set_button_colour(ui_interface_t *ui, uint32_t uid, int row, int column, uint32_t colour);
ui_node_link_t ui_get_link_state(ui_interface_t *ui, uint32_t uid);
// null byte clears a segment
void ui_write_segment_display(ui_interface_t *ui, uint32_t uid, char *c, int count);

int ui_get_element_height(ui_interface_t *ui, uint32_t uid);
int ui_get_element_width(ui_interface_t *ui, uint32_t uid);
// returns 0 on success
int ui_get_element_position(ui_interface_t *ui, uint32_t uid, int *x, int *y);

#endif

#ifdef UI_SDL_IMPLEMETATION

#endif
