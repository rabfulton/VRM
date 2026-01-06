#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <stdint.h>

typedef struct ui_interface_t ui_interface_t;

void debug_ui_set_interface(ui_interface_t *ui);
void debug_ui_set_spring_reset(void (*fn)(void));
void debug_ui_set_spring_save(void (*fn)(void));
int debug_ui_consume_close_guard(void);
int debug_ui_consume_close_request(void);
void debug_ui_prepare_for_close(void);
void debug_ui_reset_menus(void);
void debug_ui_sync_spring_params(void);
int32_t debug_ui_build_pie_menu(ui_interface_t *ui, int32_t pie_menu_id, float radius);

#endif
