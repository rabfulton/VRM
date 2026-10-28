#include "gem_menu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    GemMenu menu;gem_menu_default(&menu);
    assert(menu.count==4 && menu.page_count==1);
    for(int i=0;i<menu.count;i++)assert(gem_menu_hit(&menu,0,menu.actions[i].x,menu.actions[i].y)==i);
    assert(gem_menu_hit(&menu,0,224,220)==-2);
    assert(gem_menu_hit(&menu,0,0,0)==-1);
    assert(gem_menu_sibling(&menu,0,0)==-1);
    assert(gem_menu_sibling(&menu,0,1)==-1);
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        char *buffer=calloc(65537,1);assert(buffer);
        size_t bytes=fread(buffer,1,65536,f);assert(bytes<65536);fclose(f);
        cJSON *json=cJSON_Parse(buffer);free(buffer);assert(json);
        assert(gem_menu_parse(json,&menu));
        for(int i=0;i<menu.count;i++)assert(gem_menu_hit(&menu,menu.actions[i].page,menu.actions[i].x,menu.actions[i].y)==i);
        for(int i=0;i<menu.page_count;i++)assert(gem_menu_target(&menu,menu.pages[i].group)>=0);
#ifdef VRM_PLATFORM_WAYLAND
        if(argc>2) {
            cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,448,472);
            cairo_t *cr=cairo_create(surface);gem_menu_draw(&menu,cr,0,-1,1);
            assert(cairo_status(cr)==CAIRO_STATUS_SUCCESS);
            assert(cairo_surface_write_to_png(surface,argv[2])==CAIRO_STATUS_SUCCESS);
            cairo_destroy(cr);cairo_surface_destroy(surface);
        }
#endif
        /* Reject updates transactionally, preserving the previous model. */
        cJSON *pages=cJSON_GetObjectItem(json,"pages"),*first=cJSON_GetArrayItem(pages,0);
        cJSON *items=cJSON_GetObjectItem(first,"items"),*item=cJSON_GetArrayItem(items,0);
        int previous=menu.count;
        cJSON_ReplaceItemInObject(item,"radius",cJSON_CreateNumber(9000));
        assert(!gem_menu_parse(json,&menu));assert(menu.count==previous);
        cJSON_ReplaceItemInObject(item,"radius",cJSON_CreateNumber(42));
        assert(gem_menu_parse(json,&menu));
        cJSON_AddItemToArray(items,cJSON_Duplicate(item,true));
        assert(!gem_menu_parse(json,&menu));assert(menu.count==previous);
        cJSON_Delete(json);
    }
    puts("Gem menu: bounded shared spec, action hits, pagination and atomic validation passed");
}
