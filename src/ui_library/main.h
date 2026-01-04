#ifndef _MAIN_H_
#define _MAIN_H_

#define RESOLUTION 128
#define INV_RES 1.0f/RESOLUTION
int load_shaders(const char *, const char *);
int load_shaders_from_file(const char *vert_src, const char *frag_src);

typedef union {
    struct {
        float x;
        float y;
        float z;
    };
    struct {
        float r;
        float g;
        float b;
    };
    float v[3];
} v3;

typedef union {
    struct {
        float x;
        float y;
        float z;
        float w;
    };
    struct {
        float r;
        float g;
        float b;
        float a;
    };
    float v[4];
} v4;


enum {FORWARD, BACK, LEFT, RIGHT, T_UP, T_DOWN, T_LEFT, T_RIGHT, JUMP, NUM_BINDINGS};
//#define FORWARD (1 << 0) 
//#define BACK (1 << 1)
typedef struct {
    uint32_t state;                         // set bits for each bound control
    uint32_t bindings[NUM_BINDINGS];        // sdl scancodes for each key
} controller_t;


//extern display_t display;
//extern SDL_Window* app_window;
//extern mouse_t mouse;
#endif
