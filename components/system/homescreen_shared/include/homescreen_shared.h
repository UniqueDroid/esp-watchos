#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lets the Settings app change the launcher's background color, even though
 * it doesn't own the Phone/Display object (only main.cpp does) - main.cpp
 * registers the launcher's main screen object once at boot, and any app can
 * then call homescreen_shared_set_bg_color() for an instant live change. */
void homescreen_shared_register_main_screen(lv_obj_t *scr);

/* Applies + persists the color to /data/etc/homescreen.json. */
void homescreen_shared_set_bg_color(uint32_t color);

/* Reads the saved color, or returns false if none was ever saved. */
bool homescreen_shared_get_saved_color(uint32_t *out_color);

/* Applies the saved color (if any) to the registered main screen - call once
 * at boot after registering. */
void homescreen_shared_apply_saved(void);

#ifdef __cplusplus
}
#endif
