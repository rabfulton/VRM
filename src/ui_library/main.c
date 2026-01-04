#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#define CGLM_ALL_UNALIGNED
#include <cglm/cglm.h>

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "main.h"
//#include "import.h"
#include "ui.h"
#include <sys/stat.h>   // for file monitoring shaders

// todo implement this with inotify
//#define WATCH_SHADERS

int handle_events(void);
void handle_window_events(uint16_t e);
void player_keys(int key);
void init_controller(void);
void toggle_wireframe(void);
void update_keyboard_state(void);
void init_test_ui(void);
static void switch_gui(void);
int init_sdl(void);
int create_window(uint32_t width, uint32_t height, uint32_t flags, char* title);
void log_gl_stats();
void resize_window();
void refresh_shaders(void);
void switch_shader(const char *vert, const char *frag, int id);
void destroy_window(void);
void init_gl(void);

//void load_texture(char *str);

void free_gl_buffer(gl_buffer_t *id);
void gl_error();

//#define DRAW_ASYNC

SDL_Window* app_window;
SDL_GLContext ogl_ctx;

display_t display;
void *test_ui = NULL;
void *current_gui = NULL;
mouse_t mouse;
controller_t game_keys;

// If shader files are modified reload them
void watch_shaders(){

	static uint8_t count = 0;
	static time_t old_time_vert, old_time_frag;
	struct stat file_stat;
	int flag = 0;
	if(count != 0) return;

    stat("./shaders/ui_frag.glsl", &file_stat);
	if (file_stat.st_mtime > old_time_frag){
		flag = 1;
	}
	old_time_frag = file_stat.st_mtime;

    stat("./shaders/ui_vert.glsl", &file_stat);
	if (file_stat.st_mtime > old_time_vert){
		flag = 1;
	}

	if (flag){
		ui_refresh_shaders(current_gui);
	}
	old_time_vert = file_stat.st_mtime;
}

int main(void) {
    
    init_sdl();     // Create a window, start up required sdl components
    init_gl();      // Start up opengl
    init_test_ui(); // Initialize our gui components

	current_gui = test_ui; // We can have multiple gui's and switch between them!
    int run = 1;
    
    while (run){
        
#ifdef WATCH_SHADERS
		watch_shaders();
#endif
        run = handle_events();
        
		ui_render(current_gui);
		SDL_GL_SwapWindow(app_window);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    }
	
    destroy_window();
}

void init_test_ui(){

    // Pass Null to create a new ui
	test_ui = ui_init(NULL);
	ui_load_font(test_ui, "./fonts/Inter-Regular.otf", 18);
	int current_id = 0;

    // Create a parameter box with text labels and up/down arrows
    // to switch between them. 
    // ------------------------------
    // |  parameter 2   (up)(down)  |
    // ------------------------------

	char *pstrings[] = {"parameter 1", "Parameter 2", "another Parameter"};
	ui_parameter_str_t ps = {
		.width = 0,           // Set to zero to have automatically determined width
		.selected = 1,        // Zero indexed number to set which string is currenty set
		.items = pstrings,    // Array of strings
		.no_of_items = 3,     // nimber of strings in the array
	};
	ui_element_init(test_ui, &ps, 0, UI_PARAMETER_STR);
	
    // Tabbed interface
	char *tab_titles[] = {"Tab 1", "Tab2", "Tab 3"};
	ui_tab_header_t th = {
		.w = 300,
		.h = 0,
		.active_tab = 1,
		.titles = tab_titles,
		.no_of_tabs = 3
	};
	current_id = ui_start_tabs(test_ui, &th);
	ui_position_element(test_ui, current_id, 50, 400);
	{
		ui_set_align(test_ui, UI_RIGHT);
		ui_button_t b = {.w = 0, .h = 0, .str = "Round"};
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_ROUND);
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_ROUND);
		ui_set_align(test_ui, UI_BELOW);
		ui_parameter_t v = {.w = 200, .value = 0.5, .start_val = 0, .end_val = 1.0, .step_size = 0.05, 
							.steps_per_pixel = 0.2, .title = "val: "};
		current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);
		current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);
		current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);
		current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);
		ui_next_tab(test_ui);
		current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);

	}
	ui_stop_tabs(test_ui);
	// Display a slider
	ui_slider_t s = {.length = 150, .width = 0, .value = 25, .start_val = 0, 
		.end_val = 100, .step_size = 1};

	current_id = ui_element_init(test_ui, &s, 0, UI_SLIDER_V);
	ui_position_element(test_ui, current_id, display.width - 50, 460);	
	current_id = ui_element_init(test_ui, &s, 0, UI_SLIDER_H);
	ui_position_element(test_ui, current_id, display.width - 550, 460);	
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE);
	
	// Display a collapsable menu
	char *items_str[] = {"item 1", "item 2", "item 3", "item 4"};
	ui_menu_t tree = {.title = "Menu", .items = items_str, .no_of_items = 4, .state = 1};
	current_id = ui_element_init(test_ui, &tree, 0, UI_MENU);
	ui_position_element(test_ui, current_id, display.width - 720, 460);	


	// colour picker
	ui_colour_picker_t cp = {.size = 180, 
		.active_preset = 0,
		.presets[0] = 0xff502fff,
		.presets[1] = 0xff550611,
		.presets[2] = 0xff556611,
		.presets[3] = 0xff6611ee,
		.presets[4] = 0xff006611,
		.presets[5] = 0xff550011,
		.presets[6] = 0xff556611,
		.presets[7] = 0xff0066ff,
	};
	current_id = ui_element_init(test_ui, &cp, 0, UI_COLOUR_PICKER);
	ui_position_element(test_ui, current_id, display.width - 333, 200);

	// display a dial
	ui_dial_t dial;
	dial.size = display.height * 0.05f;
	dial.value = 100.0f;
	dial.steps_per_pixel = display.v_dpi * 0.01f;
	dial.start_val = 0;
	dial.end_val = 200;
	dial.step_size = 1;
	dial.title = "";
	current_id = ui_element_init(test_ui, &dial, 0, UI_DIAL);
	ui_position_element(test_ui, current_id, display.width - 120, 460);	
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE);

	// a simple text label
	ui_label_t l = {.str = "Text Label"};
	current_id = ui_element_init(test_ui, &l, 1, UI_LABEL);
	ui_position_element(test_ui, current_id, display.width - 520, 150);	
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE);

	// display button
	ui_button_t b = {.w = 0, .h = 0, .str = "Press"};
	current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_RETAIN_STATE);
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE | UI_ACTION_ON_RELEASE | UI_ACTION_ON_PRESS);
	ui_position_element(test_ui, current_id, display.width - 300, 460);	
	//ui_set_callback(test_ui, 5, test_button_note_on);
	
	ui_header_t gheader = {.w = 0, .h = 0, .state = 1, .title = "Grouped Widgets"};
	current_id = ui_group_start(test_ui, &gheader);
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE);
	ui_position_element(test_ui, current_id, 50, 50);
	{
		ui_set_align(test_ui, UI_RIGHT);
		b.str = "Trying UP";
		b.w = 0;
		b.h = 0;
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_ROUND);
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_ROUND);
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON_ROUND);
		ui_set_align(test_ui, UI_BELOW);
		current_id = ui_element_init(test_ui, &dial, 0, UI_DIAL);
		ui_set_align(test_ui, UI_RIGHT);
		current_id = ui_element_init(test_ui, &dial, 0, UI_DIAL);
		current_id = ui_element_init(test_ui, &dial, 0, UI_DIAL);
		
		ui_set_align(test_ui, UI_BELOW);
		ui_header_t gheader2 = {.w = 0, .h = 0, .state = 1, .title = "More Widgets"};
		current_id = ui_group_start(test_ui, &gheader2);
		{
			current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON);
		}
		ui_group_stop(test_ui);

		ui_set_align(test_ui, UI_BELOW);
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON);
	}
	ui_group_stop(test_ui);

	ui_header_t gheader2 = {.w = 0, .h = 0, .state = 1, .title = "More Widgets"};
	current_id = ui_group_start(test_ui, &gheader2);
		current_id = ui_element_init(test_ui, &b, 0, UI_BUTTON);
	ui_group_stop(test_ui);

	// Display a checkbox
	ui_checkbox_t c = {.str = "Checkbox", .state = 0};
	current_id = ui_element_init(test_ui, &c, 0, UI_CHECKBOX);
	ui_position_element(test_ui, current_id, display.width - 300, 500);	
	ui_set_element_flags(test_ui, current_id, UI_MOVABLE);


	// create an editable value
	ui_parameter_t v = {.w = 200, .value = 0.5, .start_val = 0, .end_val = 1.0, .step_size = 0.05, 
						.steps_per_pixel = 0.2, .title = "val: "};
	current_id = ui_element_init(test_ui, &v, 0, UI_PARAMETER);
	ui_position_element(test_ui, current_id, display.width - 333, display.height - 100);
    ui_log_elements(test_ui);
}

int init_sdl(){

    if (SDL_Init(SDL_INIT_VIDEO) < 0){
        SDL_Log("ERROR: SDL init error! sdl: %s\n", SDL_GetError());
        SDL_Quit();
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    
    // Get desktop screen resolution
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(0, &mode);

    uint32_t flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_ALLOW_HIGHDPI |SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    // Create a window 80% of the size of the desktop resolution
    if (create_window(mode.w * 0.8, mode.h * 0.8, flags, "ui test")){
        SDL_Log("ERROR: Could not create window\n");
        SDL_Quit();
    }
    // Telling opengl where its drawable area
    glViewport(0, 0, display.width, display.height);
    if (glewInit() != GLEW_OK){
        SDL_Log("ERROR: Could not init GLEW\n");
        SDL_Quit();
    }
    if (!glewIsSupported("GL_VERSION_3_0")) {
        SDL_Log("OpenGL 3.0 not available\n");
        SDL_Quit();
    }  
    log_gl_stats();

    SDL_GL_SetSwapInterval(1);  
    return 0;
}

void init_gl(){

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST); 
}

int create_window(uint32_t width, uint32_t height, uint32_t flags, char* title){

    app_window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
    
    if (app_window == NULL){
        SDL_Log("ERROR: Could not create window! sdl:%s\n", SDL_GetError());
        SDL_Quit();
    }
    ogl_ctx = SDL_GL_CreateContext(app_window);
    if (ogl_ctx == NULL){
        SDL_Log("ERROR: OpenGL context could not be created. SDL Error: %s\n", SDL_GetError());
        SDL_Quit();
    }
    // TODO dpi query seems to be broken, getting very high values.
    if (SDL_GetDisplayDPI(0, &display.d_dpi, &display.h_dpi, &display.v_dpi)){
        SDL_Log("ERROR: unable to read dpi. SDL Error: %s\n", SDL_GetError());
        SDL_Quit();
    }
    printf("vert dpi = %f\n", display.v_dpi);
    printf("vert hpi = %f\n", display.h_dpi);
    display.v_dpi = 144.0f;
    display.h_dpi = 144.0f;
    display.d_dpi = 144.0f;
    //
    SDL_GL_GetDrawableSize(app_window, &display.width, &display.height);
    return 0;
}

void resize_window(){
    
    SDL_GL_GetDrawableSize(app_window, &display.width, &display.height);
    glViewport(0, 0, display.width, display.height);

	ui_render(test_ui);
	SDL_GL_SwapWindow(app_window);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}

void destroy_window(){
    
 	ui_close(test_ui);
    SDL_GL_DeleteContext(ogl_ctx);
    ogl_ctx = NULL;
    SDL_DestroyWindow(app_window);
    app_window = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    //SDL_Quit();
}


int load_shaders(const char *vert_src, const char *frag_src){

    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vert_src, NULL);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        SDL_Log("ERROR:VERTEX:COMPILATION_FAILED - %s\n", infoLog);
    }
    // fragment shader
    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &frag_src, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        SDL_Log("ERROR:FRAGMENT:COMPILATION_FAILED - %s\n", infoLog);
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
        SDL_Log("ERROR:SHADER:LINKING_FAILED - %s\n", infoLog);
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

int load_shaders_from_file(const char *vert_src, const char *frag_src){

    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fshader = glCreateShader(GL_FRAGMENT_SHADER);
    int success;
    char* buffer;     
    char infoLog[512];

    SDL_RWops* vprog = SDL_RWFromFile(vert_src, "r");
    if (vprog == NULL){
        SDL_Log("ERROR:VERTEX SHADER NOT FOUND: %s\n", vert_src);
    }
    int64_t length = SDL_RWseek(vprog, 0, RW_SEEK_END);         // get size of file
    SDL_RWseek(vprog, 0, RW_SEEK_SET);                          // seek back to start of file
    buffer = malloc((length + 1) * sizeof(char));   
    SDL_RWread(vprog, buffer, length, 1);               
    buffer[length] = '\0';
    
    char const * p = buffer;
    glShaderSource(vshader, 1, &p, NULL);
    glCompileShader(vshader);

    glGetShaderiv(vshader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(vshader, 512, NULL, infoLog);
        SDL_Log("ERROR:VERTEX:COMPILATION_FAILED - %s\n", infoLog);
    }

    SDL_RWclose(vprog);
    free(buffer);

    // LOAD FRAGMENT SHADER
    SDL_RWops* fprog = SDL_RWFromFile(frag_src, "r");
    length = SDL_RWseek(fprog, 0, RW_SEEK_END);         // get size of file
    if (fprog == NULL){
        SDL_Log("ERROR:FRAGMENT SHADER NOT FOUND: %s\n", frag_src);
    }
    SDL_RWseek(fprog, 0, RW_SEEK_SET);                  // seek back to start of file
    buffer = malloc((length + 1) * sizeof(char));   
    SDL_RWread(fprog, buffer, length, 1);               // read file into buffer
    buffer[length] = '\0';
    
    p = buffer;
    glShaderSource(fshader, 1, &p, NULL);
    glCompileShader(fshader);

    glGetShaderiv(fshader, GL_COMPILE_STATUS, &success);
    if (!success){
        glGetShaderInfoLog(fshader, 512, NULL, infoLog);
        SDL_Log("ERROR:FRAGMENT:COMPILATION_FAILED - %s\n", infoLog);
    }
    
    SDL_RWclose(fprog);                                 // cleanup
    free(buffer);

    int program = glCreateProgram();                 // linking
    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        SDL_Log("ERROR:SHADER:LINKING_FAILED - %s\n", infoLog);
    }
    //glDeleteProgram(program);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    glDetachShader(program, vshader);
    glDetachShader(program, fshader);
    return program;
}

void log_gl_stats(){

    int i;
    int v[2];
    unsigned char s = 0;

    GLenum params[] = {
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        GL_MAX_CUBE_MAP_TEXTURE_SIZE,
        GL_MAX_DRAW_BUFFERS,
        GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,
        GL_MAX_TEXTURE_IMAGE_UNITS,
        GL_MAX_TEXTURE_SIZE,
        GL_MAX_VARYING_FLOATS,
        GL_MAX_VERTEX_ATTRIBS,
        GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
        GL_MAX_VERTEX_UNIFORM_COMPONENTS,
        GL_MAX_VIEWPORT_DIMS,
        GL_STEREO,
    };
    const char* names[] = {
        "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS",
        "GL_MAX_CUBE_MAP_TEXTURE_SIZE",
        "GL_MAX_DRAW_BUFFERS",
        "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS",
        "GL_MAX_TEXTURE_IMAGE_UNITS",
        "GL_MAX_TEXTURE_SIZE",
        "GL_MAX_VARYING_FLOATS",
        "GL_MAX_VERTEX_ATTRIBS",
        "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS",
        "GL_MAX_VERTEX_UNIFORM_COMPONENTS",
        "GL_MAX_VIEWPORT_DIMS",
        "GL_STEREO",
    };
    printf("%s\tVersion: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    for (i = 0; i < 10; i++) {
        int v = 0;
        glGetIntegerv(params[i], &v);
        printf("GPU: %s %i\n", names[i], v);
    }
    v[0] = v[1] = 0;
    glGetIntegerv(params[10], v);
    printf("GPU: %s %i %i\n", names[10], v[0], v[1]);
    glGetBooleanv (params[11], &s);
    printf("GPU: %s %i\n", names[11], (unsigned int)s);
}

void gl_error(){

    GLenum err = GL_NO_ERROR;
    while((err = glGetError()) != GL_NO_ERROR){
        switch (err){
            case GL_NO_ERROR:
            break;
            case GL_INVALID_ENUM:
            SDL_Log("opengl error invalid enum : %i", err); 
            break;            
            case GL_INVALID_VALUE:
            SDL_Log("opengl error invalid value : %i", err);
            break;
            case GL_INVALID_OPERATION:
            SDL_Log("opengl error invalid operation : %i", err);
            break;
            case GL_OUT_OF_MEMORY:
            SDL_Log("opengl error out of memory : %i", err);
            break;
            case GL_STACK_UNDERFLOW:
            SDL_Log("opengl error stack underflow : %i", err);
            break;
            case GL_STACK_OVERFLOW:
            SDL_Log("opengl error stack overflow : %i", err);
            break;
            default:
            SDL_Log ("opengl error type unknown : %i", err);
            break;
        }
    }
}

void toggle_wireframe(){

	static int wireframe = 0;

    if (wireframe){
        wireframe = 0;
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    else {
        wireframe = 1;
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
}

void init_controller(){

    //enum {FORWARD, BACK, LEFT, RIGHT, T_UP, T_DOWN, T_LEFT, T_RIGHT, JUMP, NUM_BINDINGS};
    //game_keys.bindings[FORWARD] = SDL_GetKeyFromName("SDLK_UP");
    game_keys.bindings[FORWARD] = SDL_GetScancodeFromName("UP");
    game_keys.bindings[BACK] = SDL_GetScancodeFromName("DOWN");
    game_keys.bindings[LEFT] = SDL_GetScancodeFromName("Z");
    game_keys.bindings[RIGHT] = SDL_GetScancodeFromName("X");
    game_keys.bindings[T_LEFT] = SDL_GetScancodeFromName("LEFT");
    game_keys.bindings[T_RIGHT] = SDL_GetScancodeFromName("RIGHT");
    game_keys.bindings[JUMP] = SDL_GetScancodeFromName("SPACE");
    game_keys.bindings[T_UP] = SDL_GetScancodeFromName("UNKNOWN");
    game_keys.bindings[T_DOWN] = SDL_GetScancodeFromName("UNKNOWN");
}

int handle_events(){

    SDL_Event e;
    int key_event_flag = 0;

#ifdef DRAW_ASYNC
	SDL_WaitEvent(NULL);
#endif
    while (SDL_PollEvent(&e) != 0){
        if (e.type == SDL_QUIT){
            return 0;
        }
        else if (e.type == SDL_KEYDOWN && e.key.repeat == 0){

            key_event_flag = 1;
            switch(e.key.keysym.sym){

        		case SDLK_w:
            		toggle_wireframe();
            		break;
        		case SDLK_ESCAPE:
            		return 0;
            		break;
				case SDLK_r:
					ui_refresh_shaders(test_ui);
					break;
    		}
        }
        else if (e.type == SDL_KEYUP){
            //player_keys(e.key.keysym.sym);
            key_event_flag = 1;
            switch(e.key.keysym.sym){
 
            }
        }
        else if (e.type == SDL_WINDOWEVENT){
            handle_window_events(e.window.event);
        }
        else if (e.type == SDL_MOUSEMOTION){
            SDL_GetMouseState(&mouse.x, &mouse.y);
            //SDL_GetRelativeMouseState(&mouse.x_rel, &mouse.y_rel); 
            mouse.x_rel = e.motion.xrel;
            mouse.y_rel = e.motion.yrel;
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN){
            if (e.button.button == SDL_BUTTON_LEFT){
            	mouse.button_l = 1;
            }
            else if (e.button.button == SDL_BUTTON_RIGHT){
			   	mouse.button_r = 1;
            	int id = ui_get_element_id(test_ui, mouse.x, mouse.y);
				if (id != -1){
				   	ui_element_delete(test_ui, id);
					ui_log_elements(test_ui);
				}
			}

            else if (e.button.button == SDL_BUTTON_MIDDLE) mouse.button_m = 1;
        }
        else if (e.type == SDL_MOUSEBUTTONUP){
            if (e.button.button == SDL_BUTTON_LEFT) mouse.button_l = 0;
            else if (e.button.button == SDL_BUTTON_RIGHT) mouse.button_r = 0;
            else if (e.button.button == SDL_BUTTON_MIDDLE) mouse.button_m = 0;
        }
    }

    if (key_event_flag){
        update_keyboard_state();
    }

    return 1;
}


void update_keyboard_state(){

    const Uint8 *kb_state = SDL_GetKeyboardState(NULL);
    controller_t *c = &game_keys;
    c->state = 0;
    for (int i = 0; i < NUM_BINDINGS; ++i){           
        if (kb_state[c->bindings[i]]){
            c->state |= 0x0001 << i;
        }
    }
}

void handle_window_events(uint16_t e){

    switch (e){
        case SDL_WINDOWEVENT_RESIZED:
            resize_window();
            break;        
        case SDL_WINDOWEVENT_MINIMIZED:
            SDL_Log("Window minimized");
            break;
        case SDL_WINDOWEVENT_RESTORED:
            SDL_Log("Window restored");
            break;
        default:
            break;
    }
}
