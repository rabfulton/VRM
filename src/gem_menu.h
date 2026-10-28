#ifndef GEM_MENU_H
#define GEM_MENU_H
#include <stdbool.h>
#include "../deps/cjson/cJSON.h"

#define GEM_ACTIONS 64
#define GEM_PAGES 16
#define GEM_ICONS 32
#define GEM_COMMANDS 64

typedef struct { char op; double p[6]; } GemCommand;
typedef struct { char name[24]; int count; GemCommand commands[GEM_COMMANDS]; } GemIcon;
typedef struct {
    char id[49], label[128], icon[24], target[25];
    int page;
    double x,y,radius;
    bool enabled,checked,close,hero,danger;
    bool tinted;
    double primary[3],accent[3],face[3];
} GemAction;
typedef struct { char id[32],group[25]; } GemPage;
typedef struct GemMenu {
    char context[49];
    int width,height,count,page_count,icon_count;
    double cx,cy;
    double primary[3],accent[3],face[3],shadow[3],ink[3];
    GemAction actions[GEM_ACTIONS];
    GemPage pages[GEM_PAGES];
    GemIcon icons[GEM_ICONS];
} GemMenu;

void gem_menu_default(GemMenu *menu);
bool gem_menu_parse(const cJSON *json, GemMenu *out);
/* -1 empty, -2 centre, -3 previous page, -4 next page, otherwise action index. */
int gem_menu_hit(const GemMenu *menu, int page, double x, double y);
int gem_menu_target(const GemMenu *menu, const char *group);
int gem_menu_sibling(const GemMenu *menu, int page, int direction);
#ifdef VRM_PLATFORM_WAYLAND
#include <cairo/cairo.h>
void gem_menu_draw(const GemMenu *menu, cairo_t *cr, int page, int hover, double opening);
#endif
#endif
